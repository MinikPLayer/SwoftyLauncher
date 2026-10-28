use std::collections::HashMap;

use crate::{Error, Result};

pub const SEED: u32 = 5381;
pub const MULT: u32 = 33;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Entry {
    pub key: String,
    pub value: String,
}

#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct Lng {
    pub entries: Vec<Entry>,
}

#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct Diff {
    pub changed: Vec<Entry>,
    pub added: Vec<Entry>,
}

impl Diff {
    pub fn is_empty(&self) -> bool {
        self.changed.is_empty() && self.added.is_empty()
    }

    pub fn len(&self) -> usize {
        self.changed.len() + self.added.len()
    }

    pub fn to_lng(&self) -> Lng {
        Lng {
            entries: self.changed.iter().chain(&self.added).cloned().collect(),
        }
    }
}

pub fn hash(key: &[u8], seed: u32, mult: u32) -> u32 {
    key.iter().fold(seed, |h, &c| {
        h.wrapping_mul(mult).wrapping_add(c as i8 as i32 as u32)
    })
}

pub fn is_lng(data: &[u8]) -> bool {
    data.starts_with(b"LNGT")
}

struct Chunks<'a> {
    data: &'a [u8],
    pos: usize,
}

impl<'a> Chunks<'a> {
    fn u32(&mut self) -> Result<u32> {
        let v = be32(self.data, self.pos)?;
        self.pos += 4;
        Ok(v)
    }

    fn chunk(&mut self, tag: &'static str) -> Result<&'a [u8]> {
        self.tag(tag)?;
        let size = self.u32()? as usize;
        self.body(tag, size)
    }

    fn chunk_counted(&mut self, tag: &'static str) -> Result<&'a [u8]> {
        self.tag(tag)?;
        let size = self.u32()? as usize;
        let count = self.u32()? as usize;
        if count.checked_mul(8) != Some(size) {
            return Err(Error::Format(format!(
                "{tag} is {size} bytes for {count} strings"
            )));
        }
        self.body(tag, size)
    }

    fn tag(&mut self, tag: &'static str) -> Result<()> {
        if self.data.get(self.pos..self.pos + 4) != Some(tag.as_bytes()) {
            return Err(Error::Magic {
                offset: self.pos,
                expected: tag,
            });
        }
        self.pos += 4;
        Ok(())
    }

    fn body(&mut self, tag: &'static str, size: usize) -> Result<&'a [u8]> {
        let end = self
            .pos
            .checked_add(size)
            .filter(|&e| e <= self.data.len())
            .ok_or_else(|| Error::Format(format!("{tag} size {size} runs past the end")))?;
        let body = &self.data[self.pos..end];
        self.pos = end;
        Ok(body)
    }
}

fn be32(data: &[u8], pos: usize) -> Result<u32> {
    data.get(pos..pos + 4)
        .map(|b| u32::from_be_bytes(b.try_into().expect("length checked")))
        .ok_or(Error::Eof(pos))
}

fn cstr<'a>(blob: &'a [u8], off: u32, what: &str) -> Result<&'a str> {
    let rest = blob
        .get(off as usize..)
        .ok_or_else(|| Error::Format(format!("{what} offset {off} is past its {} bytes", blob.len())))?;
    let end = rest
        .iter()
        .position(|&b| b == 0)
        .ok_or_else(|| Error::Format(format!("{what} string at {off} has no NUL")))?;
    std::str::from_utf8(&rest[..end])
        .map_err(|e| Error::Format(format!("{what} string at {off} is not UTF-8: {e}")))
}

impl Lng {
    pub fn parse(data: &[u8]) -> Result<Self> {
        Self::parse_with_warnings(data).map(|(lng, _)| lng)
    }

    pub fn parse_with_warnings(data: &[u8]) -> Result<(Self, Vec<String>)> {
        if !is_lng(data) {
            return Err(Error::Magic {
                offset: 0,
                expected: "LNGT",
            });
        }
        let total = be32(data, 4)? as usize;
        if total == data.len() {
            return Ok((Self::read(data)?, Vec::new()));
        }
        let size = format!("LNGT size {total} is not the file size {}", data.len());
        let lng = Self::read(data).map_err(|e| Error::Format(format!("{size}: {e}")))?;
        Ok((lng, vec![format!("{size}; its chunks end at the file size")]))
    }

    fn read(data: &[u8]) -> Result<Self> {
        let mut c = Chunks { data, pos: 8 };
        let hshs = c.chunk("HSHS")?;
        if hshs.len() != 12 {
            return Err(Error::Format(format!("HSHS is {} bytes, not 12", hshs.len())));
        }
        let buckets = be32(hshs, 0)? as usize;
        let hsht = c.chunk("HSHT")?;
        if hsht.len() != buckets * 8 {
            return Err(Error::Format(format!(
                "HSHT is {} bytes for {buckets} buckets",
                hsht.len()
            )));
        }
        let sida = c.chunk_counted("SIDA")?;
        let count = sida.len() / 8;
        let sidb = c.chunk("SIDB")?;
        let lngb = c.chunk("LNGB")?;
        if c.pos != data.len() {
            return Err(Error::Format(format!(
                "trailing bytes: parsed {} of {}",
                c.pos,
                data.len()
            )));
        }
        for b in 0..buckets {
            let first = be32(hsht, b * 8)? as usize;
            let n = be32(hsht, b * 8 + 4)? as usize;
            if first + n > count {
                return Err(Error::Format(format!(
                    "HSHT bucket {b} covers strings {first}..{} of {count}",
                    first + n
                )));
            }
        }
        let mut rows = (0..count)
            .map(|i| Ok((be32(sida, i * 8)?, be32(sida, i * 8 + 4)?)))
            .collect::<Result<Vec<_>>>()?;
        rows.sort_by_key(|&(k, _)| k);
        let entries = rows
            .into_iter()
            .map(|(k, v)| {
                Ok(Entry {
                    key: cstr(sidb, k, "SIDB")?.to_owned(),
                    value: cstr(lngb, v, "LNGB")?.to_owned(),
                })
            })
            .collect::<Result<_>>()?;
        Ok(Self { entries })
    }

    pub fn to_bytes(&self) -> Vec<u8> {
        let n = self.entries.len();
        let buckets = n / 2 + 1;
        let mut order: Vec<(usize, usize)> = self
            .entries
            .iter()
            .enumerate()
            .map(|(i, e)| (hash(e.key.as_bytes(), SEED, MULT) as usize % buckets, i))
            .collect();
        order.sort_by_key(|&(b, _)| b);

        let mut sidb = Vec::new();
        let mut lngb = Vec::new();
        let mut offsets = Vec::with_capacity(n);
        for e in &self.entries {
            offsets.push((sidb.len() as u32, lngb.len() as u32));
            sidb.extend_from_slice(e.key.as_bytes());
            sidb.push(0);
            lngb.extend_from_slice(e.value.as_bytes());
            lngb.push(0);
        }

        let mut ranges = vec![(0u32, 0u32); buckets];
        let mut sida = Vec::with_capacity(4 + n * 8);
        sida.extend_from_slice(&(n as u32).to_be_bytes());
        for (row, &(b, i)) in order.iter().enumerate() {
            if ranges[b].1 == 0 {
                ranges[b].0 = row as u32;
            }
            ranges[b].1 += 1;
            sida.extend_from_slice(&offsets[i].0.to_be_bytes());
            sida.extend_from_slice(&offsets[i].1.to_be_bytes());
        }
        let mut hsht = Vec::with_capacity(buckets * 8);
        for (first, count) in ranges {
            hsht.extend_from_slice(&first.to_be_bytes());
            hsht.extend_from_slice(&count.to_be_bytes());
        }
        let mut hshs = Vec::with_capacity(12);
        for v in [buckets as u32, SEED, MULT] {
            hshs.extend_from_slice(&v.to_be_bytes());
        }

        let chunks: [(&[u8; 4], &[u8]); 5] = [
            (b"HSHS", &hshs),
            (b"HSHT", &hsht),
            (b"SIDA", &sida[4..]),
            (b"SIDB", &sidb),
            (b"LNGB", &lngb),
        ];
        let size = 8 + 4 + chunks.iter().map(|(_, b)| 8 + b.len()).sum::<usize>();
        let mut out = Vec::with_capacity(size);
        out.extend_from_slice(b"LNGT");
        out.extend_from_slice(&(size as u32).to_be_bytes());
        for (tag, body) in chunks {
            out.extend_from_slice(tag);
            out.extend_from_slice(&(body.len() as u32).to_be_bytes());
            if tag == b"SIDA" {
                out.extend_from_slice(&sida[..4]);
            }
            out.extend_from_slice(body);
        }
        out
    }

    pub fn get(&self, key: &str) -> Option<&str> {
        self.entries
            .iter()
            .find(|e| e.key == key)
            .map(|e| e.value.as_str())
    }

    pub fn set(&mut self, key: &str, value: impl Into<String>) -> bool {
        let value = value.into();
        if let Some(e) = self.entries.iter_mut().find(|e| e.key == key) {
            e.value = value;
            return true;
        }
        self.entries.push(Entry {
            key: key.to_owned(),
            value,
        });
        false
    }

    pub fn remove(&mut self, key: &str) -> Option<Entry> {
        let i = self.entries.iter().position(|e| e.key == key)?;
        Some(self.entries.remove(i))
    }

    pub fn diff(&self, other: &Lng) -> Diff {
        let stock: HashMap<&str, &str> = self
            .entries
            .iter()
            .map(|e| (e.key.as_str(), e.value.as_str()))
            .collect();
        let mut diff = Diff::default();
        for e in &other.entries {
            match stock.get(e.key.as_str()) {
                Some(&v) if v == e.value => {}
                Some(_) => diff.changed.push(e.clone()),
                None => diff.added.push(e.clone()),
            }
        }
        diff
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn sample() -> Lng {
        let mut lng = Lng::default();
        lng.set("lng_team_tor", "Racing Bulls");
        lng.set("lng_team_tor_hashtag", "VCARB");
        lng.set("lng_1_stop", "1 Stop");
        lng.set("lng_showroom_cem_bölükbasi", "Cem Bölükbaşı");
        lng
    }

    fn lookup<'a>(data: &'a [u8], key: &str) -> Option<&'a str> {
        let buckets = be32(data, 16).unwrap() as usize;
        let hsht = 36;
        let sida = hsht + buckets * 8 + 12;
        let sidb = sida + (be32(data, sida - 4).unwrap() as usize) * 8;
        let sidb_size = be32(data, sidb + 4).unwrap() as usize;
        let lngb = sidb + 8 + sidb_size + 8;
        let b = hash(key.as_bytes(), SEED, MULT) as usize % buckets;
        let first = be32(data, hsht + b * 8).unwrap() as usize;
        let n = be32(data, hsht + b * 8 + 4).unwrap() as usize;
        (first..first + n).find_map(|i| {
            let k = be32(data, sida + i * 8).unwrap();
            let v = be32(data, sida + i * 8 + 4).unwrap();
            (cstr(&data[sidb + 8..], k, "SIDB").unwrap() == key)
                .then(|| cstr(&data[lngb..], v, "LNGB").unwrap())
        })
    }

    #[test]
    fn write_then_parse() {
        let lng = sample();
        let bytes = lng.to_bytes();
        assert!(is_lng(&bytes));
        assert_eq!(be32(&bytes, 4).unwrap() as usize, bytes.len());
        let back = Lng::parse(&bytes).unwrap();
        assert_eq!(back, lng);
        assert_eq!(back.to_bytes(), bytes);
    }

    #[test]
    fn buckets_follow_the_game_lookup() {
        let lng = sample();
        let bytes = lng.to_bytes();
        for e in &lng.entries {
            assert_eq!(lookup(&bytes, &e.key), Some(e.value.as_str()), "{}", e.key);
        }
        assert_eq!(lookup(&bytes, "lng_missing"), None);
    }

    #[test]
    fn hash_sign_extends() {
        assert_eq!(hash(b"", SEED, MULT), 5381);
        assert_eq!(hash(b"a", SEED, MULT), 5381 * 33 + 97);
        assert_eq!(hash(&[0xc3], SEED, MULT), (5381u32 * 33).wrapping_sub(61));
    }

    #[test]
    fn empty_table() {
        let bytes = Lng::default().to_bytes();
        assert_eq!(Lng::parse(&bytes).unwrap(), Lng::default());
    }

    #[test]
    fn diff_lists_changed_and_added() {
        let stock = sample();
        let mut modded = stock.clone();
        modded.set("lng_team_tor", "AlphaTauri");
        modded.set("lng_team_new", "New");
        modded.remove("lng_1_stop");
        let diff = stock.diff(&modded);
        assert_eq!(
            diff.changed,
            vec![Entry {
                key: "lng_team_tor".into(),
                value: "AlphaTauri".into()
            }]
        );
        assert_eq!(
            diff.added,
            vec![Entry {
                key: "lng_team_new".into(),
                value: "New".into()
            }]
        );
        assert_eq!(diff.len(), 2);
        assert_eq!(diff.to_lng().get("lng_team_tor"), Some("AlphaTauri"));
        assert!(stock.diff(&stock).is_empty());
    }

    #[test]
    fn rejects_bad_tables() {
        let bytes = sample().to_bytes();
        let mut longer = bytes.clone();
        longer.push(0);
        assert!(Lng::parse(&longer).is_err());
        assert!(Lng::parse(&bytes[..bytes.len() - 1]).is_err());
        let mut tag = bytes.clone();
        tag[8] = b'X';
        assert!(Lng::parse(&tag).is_err());
        let mut range = bytes.clone();
        range[36..40].copy_from_slice(&100u32.to_be_bytes());
        range[40..44].copy_from_slice(&1u32.to_be_bytes());
        assert!(Lng::parse(&range).is_err());
    }

    #[test]
    fn a_wrong_header_size_is_a_warning_when_the_chunks_fill_the_file() {
        let lng = sample();
        let bytes = lng.to_bytes();
        for total in [bytes.len() + 13, bytes.len() - 13] {
            let mut off = bytes.clone();
            off[4..8].copy_from_slice(&(total as u32).to_be_bytes());
            let (back, warnings) = Lng::parse_with_warnings(&off).unwrap();
            assert_eq!(back, lng);
            assert_eq!(
                warnings,
                [format!(
                    "LNGT size {total} is not the file size {}; its chunks end at the file size",
                    bytes.len()
                )]
            );
            assert_eq!(Lng::parse(&off).unwrap(), lng);
            assert_eq!(back.to_bytes(), bytes);
        }
        assert!(Lng::parse_with_warnings(&bytes).unwrap().1.is_empty());
    }

    #[test]
    fn a_wrong_header_size_still_rejects_short_or_padded_files() {
        let bytes = sample().to_bytes();
        let mut short = bytes[..bytes.len() - 13].to_vec();
        let err = Lng::parse(&short).unwrap_err().to_string();
        assert!(err.contains("LNGB"), "{err}");
        let n = short.len() as u32;
        short[4..8].copy_from_slice(&n.to_be_bytes());
        assert!(Lng::parse(&short).is_err());
        let mut padded = bytes.clone();
        padded.extend_from_slice(&[0; 13]);
        let err = Lng::parse(&padded).unwrap_err().to_string();
        assert!(err.contains("trailing bytes"), "{err}");
    }
}
