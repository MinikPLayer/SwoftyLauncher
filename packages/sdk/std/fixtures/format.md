# swofty_registry fixtures

Each `<launcher>-<case>.bin` is the body of a `swofty_registry` root container as the save splice
writes it into the profile save. Its `.json` names the career to load, an optional layer to add
after the load, and the reads that must come from the snapshot. `native/test/run_snapshots_test.sh`
loads every fixture here; `snapshots_test --generate <dir>` writes the current version's fixture.
A released fixture is never edited again.

## The bag body

Game_SaveData_WriteBag's layout, little-endian:

```text
u64 1                         codec header
u64 n                         entry count
n x { u8 len, name[len],      "format", then "d000".."d999" (any order on read)
      u32 type 'dstr',        the bytes "dstr"
      u32 size, chars[size] } "format" = "swofty_registry/1"; the chunks are base64 of the payload
```

## The payload

```text
"SWRG" u16 major (1) u16 minor (0) u32 crc32(items)
items: { u16 tag, u32 size, body[size] }*
  1 career: items
      1 id                   the career id
      2 freeze               u8: 0 per season, 1 at creation
      3 stat-id map          zstd(JSON object "<registry>/<id>": stat id)
      4 majors               JSON object registry: "major.minor"
      5 season: items
          1 year             i32; 0 holds the records with no year of a career frozen at creation
          2 registry: items
              1 name
              2 version      u16 major, u16 minor
              3 records      zstd(JSON {"records": {id: value}, "loose": [ids with no year],
                                        "ranged": [ids with a year range]})
          3 refused          a registry over its budget that season: the career reads it live
```

An item with a tag a reader does not know is kept and written back byte for byte, at every level.
A registry snapshot of a newer major than the reader's is kept the same way and read live. A career
that does not decode is kept as its raw item. A payload of a newer entry major is left in the save
untouched.
