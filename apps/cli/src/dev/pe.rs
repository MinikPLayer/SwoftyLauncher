pub const REGISTER_EXPORT: &str = "SwoftyAddonRegister";

fn u16_at(b: &[u8], at: usize) -> Option<u16> {
    Some(u16::from_le_bytes(b.get(at..at + 2)?.try_into().ok()?))
}

fn u32_at(b: &[u8], at: usize) -> Option<u32> {
    Some(u32::from_le_bytes(b.get(at..at + 4)?.try_into().ok()?))
}

pub fn exports(image: &[u8]) -> Option<Vec<String>> {
    let pe = u32_at(image, 0x3c)? as usize;
    if image.get(pe..pe + 4)? != b"PE\0\0" {
        return None;
    }
    let sections = u16_at(image, pe + 6)? as usize;
    let opt_size = u16_at(image, pe + 20)? as usize;
    let opt = pe + 24;
    let dirs = match u16_at(image, opt)? {
        0x20b => opt + 112,
        0x10b => opt + 96,
        _ => return None,
    };
    let export_rva = u32_at(image, dirs)?;
    if export_rva == 0 {
        return Some(Vec::new());
    }
    let table = opt + opt_size;
    let offset = |rva: u32| -> Option<usize> {
        (0..sections.min(96)).find_map(|i| {
            let s = table + i * 40;
            let size = u64::from(u32_at(image, s + 8)?.max(u32_at(image, s + 16)?));
            let (va, raw, rva) = (
                u64::from(u32_at(image, s + 12)?),
                u64::from(u32_at(image, s + 20)?),
                u64::from(rva),
            );
            (rva >= va && rva < va + size)
                .then(|| usize::try_from(raw + (rva - va)).ok())
                .flatten()
        })
    };
    let dir = offset(export_rva)?;
    let count = u32_at(image, dir + 24)? as usize;
    let names = offset(u32_at(image, dir + 32)?)?;
    let mut out = Vec::with_capacity(count.min(4096));
    for i in 0..count.min(65536) {
        let at = offset(u32_at(image, names + i * 4)?)?;
        let end = image.get(at..)?.iter().position(|&c| c == 0)?;
        out.push(String::from_utf8_lossy(&image[at..at + end]).into_owned());
    }
    Some(out)
}

pub fn has_register(image: &[u8]) -> bool {
    exports(image).is_some_and(|e| e.iter().any(|n| n == REGISTER_EXPORT))
}

#[cfg(test)]
mod tests {
    use super::*;

    pub fn image(names: &[&str]) -> Vec<u8> {
        let mut b = vec![0u8; 0x400];
        b[0..2].copy_from_slice(b"MZ");
        b[0x3c..0x40].copy_from_slice(&0x80u32.to_le_bytes());
        b[0x80..0x84].copy_from_slice(b"PE\0\0");
        b[0x86..0x88].copy_from_slice(&1u16.to_le_bytes());
        b[0x94..0x96].copy_from_slice(&240u16.to_le_bytes());
        let opt = 0x98;
        b[opt..opt + 2].copy_from_slice(&0x20bu16.to_le_bytes());
        b[opt + 112..opt + 116].copy_from_slice(&0x1000u32.to_le_bytes());
        let sec = opt + 240;
        b[sec..sec + 6].copy_from_slice(b".edata");
        b[sec + 8..sec + 12].copy_from_slice(&0x200u32.to_le_bytes());
        b[sec + 12..sec + 16].copy_from_slice(&0x1000u32.to_le_bytes());
        b[sec + 16..sec + 20].copy_from_slice(&0x200u32.to_le_bytes());
        b[sec + 20..sec + 24].copy_from_slice(&0x200u32.to_le_bytes());
        let dir = 0x200;
        b[dir + 24..dir + 28].copy_from_slice(&(names.len() as u32).to_le_bytes());
        b[dir + 32..dir + 36].copy_from_slice(&0x1040u32.to_le_bytes());
        let mut at = 0x280;
        for (i, n) in names.iter().enumerate() {
            let rva = 0x1000 + (at - 0x200) as u32;
            b[0x240 + i * 4..0x244 + i * 4].copy_from_slice(&rva.to_le_bytes());
            b[at..at + n.len()].copy_from_slice(n.as_bytes());
            at += n.len() + 1;
        }
        b
    }

    #[test]
    fn finds_the_register_export() {
        let with = image(&["SwoftyAddonApi", "SwoftyAddonInit", "SwoftyAddonRegister"]);
        assert_eq!(
            exports(&with).unwrap(),
            ["SwoftyAddonApi", "SwoftyAddonInit", "SwoftyAddonRegister"]
        );
        assert!(has_register(&with));
        assert!(!has_register(&image(&["SwoftyAddonApi", "SwoftyAddonInit"])));
        assert!(!has_register(b"not a dll"));
    }
}

#[cfg(test)]
mod built {
    use super::*;

    #[test]
    fn reads_the_recorder_tests_dlls_when_built() {
        let dir = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../native/build/recorder-test");
        let (Ok(with), Ok(without)) = (
            std::fs::read(dir.join("t_register.dll")),
            std::fs::read(dir.join("t_plain.dll")),
        ) else {
            eprintln!("skipped: run packages/sdk/recorder/test/run_recorder_test.sh to build the test DLLs");
            return;
        };
        assert!(has_register(&with));
        assert!(!has_register(&without));
        assert!(exports(&without).unwrap().iter().any(|n| n == "SwoftyAddonInit"));
    }
}
