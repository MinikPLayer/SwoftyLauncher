use std::io::{Read, Seek, SeekFrom};
use std::path::Path;

use crate::erp::Resource;
use crate::{Error, Result, lz4};

const BC7: u32 = 0x46;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Codec {
    Bc1,
    Bc3,
    Bc5,
    Bc7,
}

impl Codec {
    pub fn of(format: u32) -> Option<Codec> {
        match format {
            52 | 54 => Some(Codec::Bc1),
            55 | 57 => Some(Codec::Bc3),
            65 => Some(Codec::Bc5),
            69 | BC7 => Some(Codec::Bc7),
            _ => None,
        }
    }

    pub fn block_bytes(self) -> usize {
        match self {
            Codec::Bc1 => 8,
            Codec::Bc3 | Codec::Bc5 | Codec::Bc7 => 16,
        }
    }

    pub fn level_bytes(self, width: u32, height: u32) -> usize {
        width.div_ceil(4) as usize * height.div_ceil(4) as usize * self.block_bytes()
    }
}

#[derive(Debug, Clone)]
pub struct Image {
    pub width: u32,
    pub height: u32,
    pub rgba: Vec<u8>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Header {
    pub format: u32,
    pub width: u32,
    pub height: u32,
    pub levels: u32,
    pub layers: u32,
}

impl Header {
    pub fn codec(&self) -> Option<Codec> {
        Codec::of(self.format)
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct SideLevel {
    pub compression: u8,
    pub offset: u64,
    pub packed: u64,
    pub size: u64,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SideFile {
    pub path: String,
    pub levels: Vec<SideLevel>,
}

const LZ4: u8 = 3;
const SIDE_LEVEL_LEN: usize = 26;
const MAX_LEVEL_BYTES: u64 = 1 << 28;

impl SideFile {
    pub fn parse(desc: &[u8]) -> Result<SideFile> {
        let len = *desc.first().ok_or(Error::Eof(0))? as usize;
        let path = desc.get(1..1 + len).ok_or(Error::Eof(desc.len()))?;
        let path = String::from_utf8_lossy(path)
            .trim_end_matches('\0')
            .replace('\\', "/");
        if path.is_empty()
            || path.starts_with('/')
            || path.contains(':')
            || path.split('/').any(|p| p == "..")
        {
            return Err(Error::Format(format!("bad mipmap file path {path:?}")));
        }
        let mut at = 1 + len + 1;
        let count = desc
            .get(at..at + 4)
            .map(|b| u32::from_le_bytes(b.try_into().expect("4 bytes")) as usize)
            .ok_or(Error::Eof(desc.len()))?;
        at += 4;
        let mut levels = Vec::with_capacity(count.min(32));
        for _ in 0..count {
            let entry = desc.get(at..at + SIDE_LEVEL_LEN).ok_or(Error::Eof(desc.len()))?;
            let u64_at = |o: usize| u64::from_le_bytes(entry[o..o + 8].try_into().expect("8 bytes"));
            levels.push(SideLevel {
                compression: entry[1],
                offset: u64_at(2),
                packed: u64_at(10),
                size: u64_at(18),
            });
            at += SIDE_LEVEL_LEN;
        }
        Ok(SideFile { path, levels })
    }

    pub fn read(&self, root: &Path, index: usize) -> Result<Vec<u8>> {
        let level = self
            .levels
            .get(index)
            .ok_or_else(|| Error::Format(format!("{}: no level {index}", self.path)))?;
        if level.packed > MAX_LEVEL_BYTES || level.size > MAX_LEVEL_BYTES {
            return Err(Error::Format(format!(
                "{}: level {index} is too large",
                self.path
            )));
        }
        let at = root.join(&self.path);
        let mut file = std::fs::File::open(&at).map_err(crate::io_at(&at))?;
        file.seek(SeekFrom::Start(level.offset))
            .map_err(crate::io_at(&at))?;
        let mut packed = vec![0; level.packed as usize];
        file.read_exact(&mut packed).map_err(crate::io_at(&at))?;
        let size = level.size as usize;
        match level.compression {
            0 if packed.len() == size => Ok(packed),
            LZ4 => lz4::decompress_block(&packed, size),
            c => Err(Error::Format(format!(
                "{}: level {index} has unsupported compression {c}",
                self.path
            ))),
        }
    }
}

pub fn header(surface: &Resource) -> Result<Header> {
    let head = surface
        .fragments
        .first()
        .ok_or_else(|| Error::Format(format!("{}: not a surface", surface.name)))?
        .data()?;
    let word = |at: usize| {
        head.get(at..at + 4)
            .map(|b| u32::from_le_bytes(b.try_into().expect("4 bytes")))
    };
    let (Some(format), Some(width), Some(height)) = (word(8), word(12), word(16)) else {
        return Err(Error::Format(format!("{}: short surface header", surface.name)));
    };
    Ok(Header {
        format,
        width,
        height,
        levels: word(24).unwrap_or(1).clamp(1, 16),
        layers: word(28).unwrap_or(1).max(1),
    })
}

pub fn side_file(surface: &Resource) -> Result<Option<SideFile>> {
    match surface.fragments.get(2) {
        Some(f) => SideFile::parse(&f.data()?).map(Some),
        None => Ok(None),
    }
}

pub fn decode_pixels(codec: Codec, width: u32, height: u32, data: &[u8]) -> Result<Vec<u8>> {
    let (bw, bh) = (width.div_ceil(4) as usize, height.div_ceil(4) as usize);
    let bytes = codec.block_bytes();
    if data.len() < bw * bh * bytes {
        return Err(Error::Format(format!(
            "{width}x{height} {codec:?} needs {} bytes, got {}",
            bw * bh * bytes,
            data.len()
        )));
    }
    let pitch = bw * 16;
    let mut padded = vec![0; pitch * bh * 4];
    let mut rg = [0u8; 32];
    for (i, block) in data.chunks_exact(bytes).take(bw * bh).enumerate() {
        let dst = &mut padded[i / bw * 4 * pitch + i % bw * 16..];
        match codec {
            Codec::Bc1 => bcdec_rs::bc1(block, dst, pitch),
            Codec::Bc3 => bcdec_rs::bc3(block, dst, pitch),
            Codec::Bc7 => bcdec_rs::bc7(block, dst, pitch),
            Codec::Bc5 => {
                bcdec_rs::bc5(block, &mut rg, 8, false);
                for y in 0..4 {
                    for x in 0..4 {
                        let s = y * 8 + x * 2;
                        dst[y * pitch + x * 4..][..4].copy_from_slice(&[rg[s], rg[s + 1], 0, 255]);
                    }
                }
            }
        }
    }
    let row = width as usize * 4;
    if row != pitch {
        for y in 1..height as usize {
            padded.copy_within(y * pitch..y * pitch + row, y * row);
        }
    }
    padded.truncate(row * height as usize);
    Ok(padded)
}

fn unsupported(surface: &Resource, h: &Header) -> Error {
    Error::Format(format!(
        "{}: unsupported surface format {:#x}",
        surface.name, h.format
    ))
}

fn pixels(surface: &Resource) -> Result<std::borrow::Cow<'_, [u8]>> {
    surface
        .fragments
        .get(1)
        .ok_or_else(|| Error::Format(format!("{}: not a surface", surface.name)))?
        .data_cow()
}

pub fn decode(surface: &Resource) -> Result<Image> {
    let h = header(surface)?;
    let codec = h.codec().ok_or_else(|| unsupported(surface, &h))?;
    let rgba = decode_pixels(codec, h.width, h.height, &pixels(surface)?)
        .map_err(|e| Error::Format(format!("{}: {e}", surface.name)))?;
    Ok(Image {
        width: h.width,
        height: h.height,
        rgba,
    })
}

pub fn decode_near(surface: &Resource, root: Option<&Path>, want: u32) -> Result<Image> {
    let h = header(surface)?;
    let codec = h.codec().ok_or_else(|| unsupported(surface, &h))?;
    let side = match root {
        Some(_) => side_file(surface)?,
        None => None,
    };
    let mut sizes: Vec<(u32, u32, Option<usize>)> = Vec::new();
    if let Some(side) = &side {
        let n = side.levels.len().min(8) as u32;
        for k in 0..n {
            let shift = n - k;
            sizes.push((h.width << shift, h.height << shift, Some(k as usize)));
        }
    }
    let (mut w, mut hh) = (h.width, h.height);
    for _ in 0..h.levels {
        sizes.push((w, hh, None));
        w = (w / 2).max(1);
        hh = (hh / 2).max(1);
    }
    let (width, height, level) = sizes
        .iter()
        .rev()
        .find(|(w, h, _)| (*w).max(*h) >= want)
        .or_else(|| sizes.first())
        .copied()
        .ok_or_else(|| Error::Format(format!("{}: no levels", surface.name)))?;
    let data: std::borrow::Cow<'_, [u8]>;
    let selected = match (level, &side, root) {
        (Some(k), Some(side), Some(root)) => {
            data = std::borrow::Cow::Owned(side.read(root, k)?);
            data.as_ref()
        }
        _ => {
            data = pixels(surface)?;
            let (mut w, mut hh, mut at) = (h.width, h.height, 0);
            while (w, hh) != (width, height) {
                at += codec.level_bytes(w, hh);
                w = (w / 2).max(1);
                hh = (hh / 2).max(1);
            }
            data.get(at..).ok_or(Error::Eof(at))?
        }
    };
    let rgba = decode_pixels(codec, width, height, selected)
        .map_err(|e| Error::Format(format!("{}: {e}", surface.name)))?;
    Ok(Image { width, height, rgba })
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::erp::Fragment;

    fn fragment(raw: Vec<u8>) -> Fragment {
        Fragment {
            name: *b"temp",
            offset: 0,
            size: raw.len() as u64,
            flags: 0,
            compression: 0,
            packed: raw.len() as u64,
            raw,
        }
    }

    fn surface_with(format: u32, width: u32, height: u32, levels: u32, data: Vec<u8>) -> Resource {
        let mut head = vec![0; 40];
        for (at, v) in [(8, format), (12, width), (16, height), (24, levels), (28, 1)] {
            head[at..at + 4].copy_from_slice(&v.to_le_bytes());
        }
        Resource {
            name: "flag.tif.image".into(),
            kind: [0; 16],
            unk: vec![0; 4],
            fragments: vec![fragment(head), fragment(data)],
            hash: [0; 16],
        }
    }

    fn surface(format: u32, width: u32, height: u32, data: Vec<u8>) -> Resource {
        surface_with(format, width, height, 1, data)
    }

    const BC1_RED: [u8; 8] = [0x00, 0xf8, 0x00, 0xf8, 0, 0, 0, 0];
    const BC1_BLUE: [u8; 8] = [0x1f, 0x00, 0x1f, 0x00, 0, 0, 0, 0];

    #[test]
    fn decodes_and_crops_bc7() {
        let block = vec![
            0x40, 0x20, 0x10, 0x02, 0xf9, 0xff, 0xff, 0xff, 0x01, 0, 0, 0, 0, 0, 0, 0,
        ];
        let image = decode(&surface(BC7, 3, 2, block)).unwrap();
        assert_eq!((image.width, image.height), (3, 2));
        assert_eq!(image.rgba, [0x81, 0x21, 0xff, 0xff].repeat(6));
    }

    #[test]
    fn decodes_bc1_blocks_in_place() {
        let image = decode(&surface(54, 8, 4, [BC1_RED, BC1_BLUE].concat())).unwrap();
        assert_eq!(&image.rgba[..4], &[255, 0, 0, 255]);
        assert_eq!(&image.rgba[16..20], &[0, 0, 255, 255]);
        assert_eq!(&image.rgba[image.rgba.len() - 4..], &[0, 0, 255, 255]);
    }

    #[test]
    fn decodes_bc3_alpha() {
        let mut block = vec![0x80, 0x80, 0, 0, 0, 0, 0, 0];
        block.extend(BC1_RED);
        let image = decode(&surface(57, 4, 4, block)).unwrap();
        assert!(image.rgba.chunks(4).all(|p| p == [255, 0, 0, 0x80]));
    }

    #[test]
    fn decodes_bc5_into_red_green() {
        let mut block = vec![200, 200, 0, 0, 0, 0, 0, 0];
        block.extend([40, 40, 0, 0, 0, 0, 0, 0]);
        let image = decode(&surface(65, 4, 4, block)).unwrap();
        assert!(image.rgba.chunks(4).all(|p| p == [200, 40, 0, 255]));
    }

    #[test]
    fn rejects_other_formats_and_short_data() {
        assert!(decode(&surface(0x47, 4, 4, vec![0; 16])).is_err());
        assert!(decode(&surface(54, 8, 8, vec![0; 16])).is_err());
    }

    #[test]
    fn picks_an_inner_level() {
        let data = [BC1_RED.repeat(4), BC1_BLUE.to_vec(), BC1_BLUE.to_vec()].concat();
        let s = surface_with(54, 8, 8, 3, data);
        let near = decode_near(&s, None, 4).unwrap();
        assert_eq!((near.width, near.height), (4, 4));
        assert_eq!(&near.rgba[..4], &[0, 0, 255, 255]);
        let big = decode_near(&s, None, 64).unwrap();
        assert_eq!((big.width, big.height), (8, 8));
        assert_eq!(&big.rgba[..4], &[255, 0, 0, 255]);
    }

    fn descriptor(path: &str, levels: &[(u8, u64, u64, u64)]) -> Vec<u8> {
        let mut d = vec![(path.len() + 1) as u8];
        d.extend(path.as_bytes());
        d.extend([0, 0]);
        d.extend((levels.len() as u32).to_le_bytes());
        for &(c, offset, packed, size) in levels {
            d.extend([0, c]);
            for v in [offset, packed, size] {
                d.extend(v.to_le_bytes());
            }
        }
        d
    }

    #[test]
    fn parses_the_side_file_descriptor() {
        let d = descriptor("a\\b.mipmaps", &[(3, 0, 10, 32), (0, 10, 8, 8)]);
        let side = SideFile::parse(&d).unwrap();
        assert_eq!(side.path, "a/b.mipmaps");
        assert_eq!(
            side.levels,
            [
                SideLevel {
                    compression: 3,
                    offset: 0,
                    packed: 10,
                    size: 32
                },
                SideLevel {
                    compression: 0,
                    offset: 10,
                    packed: 8,
                    size: 8
                },
            ]
        );
        assert!(SideFile::parse(&d[..d.len() - 1]).is_err());
        assert!(SideFile::parse(&descriptor("../x", &[])).is_err());
        assert!(SideFile::parse(&descriptor("/x", &[])).is_err());
    }

    #[test]
    fn reads_bigger_levels_from_the_side_file() {
        let dir = std::env::temp_dir().join(format!("swofty-side-{}", std::process::id()));
        std::fs::create_dir_all(&dir).unwrap();
        let mut packed = vec![0x8f];
        packed.extend(BC1_RED);
        packed.extend([8, 0, 5]);
        let mut raw = BC1_BLUE.repeat(4);
        raw.extend(&packed);
        std::fs::write(dir.join("t.mipmaps"), &raw).unwrap();
        let mut s = surface_with(54, 4, 4, 1, BC1_RED.to_vec());
        let levels = [(3, 32, packed.len() as u64, 128), (0, 0, 32, 32)];
        s.fragments.push(fragment(descriptor("t.mipmaps", &levels)));
        let biggest = decode_near(&s, Some(&dir), 16).unwrap_err();
        assert!(biggest.to_string().contains("lz4"), "{biggest}");
        let mid = decode_near(&s, Some(&dir), 8).unwrap();
        assert_eq!((mid.width, mid.height), (8, 8));
        assert!(mid.rgba.chunks(4).all(|p| p == [0, 0, 255, 255]));
        let small = decode_near(&s, None, 8).unwrap();
        assert_eq!((small.width, small.height), (4, 4));
        assert!(small.rgba.chunks(4).all(|p| p == [255, 0, 0, 255]));
        std::fs::remove_dir_all(&dir).ok();
    }

    #[test]
    fn decompresses_lz4_side_levels() {
        let dir = std::env::temp_dir().join(format!("swofty-side-lz4-{}", std::process::id()));
        std::fs::create_dir_all(&dir).unwrap();
        let mut packed = vec![0x8f];
        packed.extend(BC1_RED);
        packed.extend([8, 0, 5]);
        std::fs::write(dir.join("t.mipmaps"), &packed).unwrap();
        let mut s = surface_with(54, 4, 4, 1, BC1_BLUE.to_vec());
        s.fragments.push(fragment(descriptor(
            "t.mipmaps",
            &[(3, 0, packed.len() as u64, 32)],
        )));
        let big = decode_near(&s, Some(&dir), 8).unwrap();
        assert_eq!((big.width, big.height), (8, 8));
        assert!(big.rgba.chunks(4).all(|p| p == [255, 0, 0, 255]));
        std::fs::remove_dir_all(&dir).ok();
    }
}
