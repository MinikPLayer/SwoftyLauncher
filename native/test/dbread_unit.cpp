// Host-side unit test for the build-independent db-read logic in dbread_logic.hpp:
// fnv1a64 key hashing, incremental continuation, table/row/cell key construction, the row
// stem derivation, and the typed record decode. No Windows, no Wine, no live game.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <unordered_map>
#include <vector>

#include "../src/core/dbread_logic.hpp"

static int failures;

static void ok(bool cond, const char *what)
{
    printf("%s: %s\n", cond ? "ok  " : "FAIL", what);
    if (!cond)
        failures++;
}

static uint64_t full(const std::string &s)
{
    return dbread::fnv64(s);
}

// A database shaped like the game's in memory: one value record per stored key, an int at +8 and,
// for string records, the FNV-1a-32 of the text at +0x30.
struct FakeDb {
    std::unordered_map<uint64_t, std::vector<uint8_t>> recs;

    void text(uint64_t key, const std::string &t)
    {
        std::vector<uint8_t> r(0x50, 0);
        uint32_t h = dbread::fnv32(t.c_str());
        memcpy(r.data() + dbread::REC_META, &h, 4);
        recs[key] = r;
    }
    void integer(uint64_t key, int32_t v)
    {
        std::vector<uint8_t> r(0x50, 0);
        memcpy(r.data() + dbread::REC_SCALAR, &v, 4);
        recs[key] = r;
    }
    const void *get(uint64_t key) const
    {
        auto it = recs.find(key);
        return it == recs.end() ? nullptr : it->second.data();
    }
};

// gamedatabases.<year>.bin: u32 count, then per record u32 key_lo, u32 key_hi, u8 type, u32 n, n
// masked bytes and a NUL; byte j of the stream is plain + MASK[(10 + j) % 30] (tools/gamedb.py).
static bool load_bin(const char *path, FakeDb &db)
{
    std::ifstream f(path, std::ios::binary);
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    static const char MASK[] = "tG5MYmBZEZZnrH7yg2qTC4qj42Jcq9";
    if (d.size() < 4)
        return false;
    uint32_t count;
    memcpy(&count, d.data(), 4);
    size_t p = 4, stream = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (p + 13 > d.size())
            return false;
        uint32_t lo, hi, n;
        memcpy(&lo, &d[p], 4), memcpy(&hi, &d[p + 4], 4), memcpy(&n, &d[p + 9], 4);
        uint8_t type = d[p + 8];
        if (p + 14 + n > d.size())
            return false;
        std::string text(n, '\0');
        for (uint32_t j = 0; j < n; j++)
            text[j] = char(uint8_t(d[p + 13 + j] - uint8_t(MASK[(10 + stream + j) % 30])));
        uint64_t key = uint64_t(hi) << 32 | lo;
        if (type == 0)
            db.integer(key, int32_t(strtol(text.c_str(), nullptr, 10)));
        else if (type >= 2 && type <= 4)
            db.text(key, text);
        stream += n;
        p += 14 + n;
    }
    return p == d.size();
}

int main()
{
    // fnv1a64 against the hashes recorded in re/db/db_keys.tsv.
    ok(full("entrants.teams._team_instancecount_") == 0x3f0b53f9d57b748eull, "fnv64 teams instancecount");
    ok(full("entrants.vehicles._vehicle_instancecount_") == 0x7b6eb7739e186f54ull, "fnv64 vehicles instancecount");
    ok(full("entrants.drivers._driver_instancecount_") == 0x11ff62da5ecc0d64ull, "fnv64 drivers instancecount");
    ok(full("entrants.teams.team_28.stat_id") == 0x83a0da0da7de0118ull, "fnv64 team_28.stat_id");
    ok(full("ai.general_ai_settings.max_test_time") == 0x2273a5bae867b23bull, "fnv64 max_test_time");

    // The game database map grows at 80% of its slots: 2^20 slots grow at the 838,861st record.
    ok(dbread::grows_at(1ull << 20) == 838860, "grows_at 2^20");
    ok(dbread::grows_at(1ull << 21) == 1677721, "grows_at 2^21");
    ok(dbread::fits_without_growing(742857, 90000, 1ull << 20, 1024), "742,857 + 90,000 fits under 838,860");
    ok(!dbread::fits_without_growing(758755, 82446, 1ull << 20, 1024), "758,755 + 82,446 (the reported crash) does not fit");
    ok(dbread::fits_without_growing(838860, 0, 1ull << 20, 0), "a full map with nothing added fits");

    // Incremental continuation of a prefix state equals hashing the whole string.
    uint64_t pref = dbread::fnv64("entrants.teams");
    ok(dbread::fnv_cont(pref, ".team_28.stat_id") == full("entrants.teams.team_28.stat_id"), "incremental continuation");

    // Row stem derivation strips one trailing 's' from the last path segment.
    ok(dbread::row_stem("entrants.teams") == "team", "stem teams->team");
    ok(dbread::row_stem("entrants.vehicles") == "vehicle", "stem vehicles->vehicle");
    ok(dbread::row_stem("entrants.entrant_lists") == "entrant_list", "stem entrant_lists->entrant_list");
    ok(dbread::row_stem("entrants.entrant_setup_modes") == "entrant_setup_mode", "stem setup_modes");

    // table_keys builds the same hashes as the full dotted strings.
    dbread::TableKeys tk = dbread::table_keys("entrants.teams");
    ok(tk.ok, "table_keys ok");
    ok(tk.count_key == full("entrants.teams._team_instancecount_"), "table_keys count key");
    ok(dbread::row_key(tk.row_prefix, 0) == full("entrants.teams.team_0"), "row_key 0");
    ok(dbread::row_key(tk.row_prefix, 28) == full("entrants.teams.team_28"), "row_key 28");
    ok(dbread::cell_key(tk.row_prefix, 28, "stat_id") == full("entrants.teams.team_28.stat_id"), "cell_key 28.stat_id");

    dbread::TableKeys bad = dbread::table_keys("");
    ok(!bad.ok, "empty table rejected");

    // Typed decode over synthetic value records: vtable at +0, scalar at +8, string text at +8,
    // fnv32 metadata at +0x30.
    uint8_t rec[0x40];
    SwoftyDbValue v;

    memset(rec, 0, sizeof rec);
    *reinterpret_cast<int32_t *>(rec + 8) = -1234;
    ok(dbread::read_i32(rec) == -1234, "read_i32");
    dbread::decode(rec, SWOFTY_DB_INT, &v);
    ok(v.tag == SWOFTY_DB_INT && v.i == -1234 && v.f == -1234.0, "decode int");

    memset(rec, 0, sizeof rec);
    *reinterpret_cast<float *>(rec + 8) = 0.5f;
    ok(dbread::read_f32(rec) == 0.5f, "read_f32");
    dbread::decode(rec, SWOFTY_DB_FLOAT, &v);
    ok(v.tag == SWOFTY_DB_FLOAT && v.f == 0.5 && v.i == 0, "decode float");

    memset(rec, 0, sizeof rec);
    rec[8] = 1;
    ok(dbread::read_u8(rec) == 1, "read_u8");
    dbread::decode(rec, SWOFTY_DB_BOOL, &v);
    ok(v.tag == SWOFTY_DB_BOOL && v.i == 1 && v.f == 1.0, "decode bool true");
    rec[8] = 0;
    dbread::decode(rec, SWOFTY_DB_BOOL, &v);
    ok(v.tag == SWOFTY_DB_BOOL && v.i == 0, "decode bool false");

    // String records carry XOR-encoded text (GdbStringRecord); decode() leaves s null and the core
    // fills it from decode_text, so a pointer's own bytes are never handed out as text.
    memset(rec, 0, sizeof rec);
    *reinterpret_cast<uint32_t *>(rec + 0x30) = 0xdeadbeefu;
    ok(dbread::read_meta(rec) == 0xdeadbeefu, "read_meta");
    dbread::decode(rec, SWOFTY_DB_STRING, &v);
    ok(v.tag == SWOFTY_DB_STRING && v.s == nullptr && uint32_t(v.i) == 0xdeadbeefu, "decode string leaves text to the core");
    // ENUM and ID share the string record class; decode reports them as string.
    dbread::decode(rec, SWOFTY_DB_ENUM, &v);
    ok(v.tag == SWOFTY_DB_STRING && v.s == nullptr, "decode enum as string family");

    // k_Gdb_StringXorKey (17 2d 57 5a): plain[i] = key[(i - 1) & 3] ^ enc[i], size counts the NUL.
    const uint8_t key[4] = {0x17, 0x2d, 0x57, 0x5a};
    std::string enc, out;
    dbread::encode_text("Lewis Hamilton", key, enc);
    ok(enc.size() == 15 && uint8_t(enc[0]) == uint8_t('L' ^ 0x5a) && uint8_t(enc[1]) == uint8_t('e' ^ 0x17) &&
           uint8_t(enc[3]) == uint8_t('i' ^ 0x57) && uint8_t(enc[4]) == uint8_t('s' ^ 0x5a),
       "encode uses key[(i - 1) & 3]");
    ok(dbread::decode_text(reinterpret_cast<const uint8_t *>(enc.data()), enc.size(), key, out) && out == "Lewis Hamilton",
       "decode_text round trip");
    dbread::encode_text("Pérez", key, enc);
    ok(dbread::decode_text(reinterpret_cast<const uint8_t *>(enc.data()), enc.size(), key, out) && out == "Pérez",
       "decode_text keeps UTF-8");
    ok(dbread::decode_text(reinterpret_cast<const uint8_t *>(""), 1, key, out) && out.empty(), "size 1 is the empty string");
    const char *plain = "Lewis";
    ok(!dbread::decode_text(reinterpret_cast<const uint8_t *>(plain), 6, key, out) && out.empty(),
       "plain (undecoded) bytes are rejected, not returned as garbage");
    uint64_t ptr = 0x00007ff6a1b2c3d4ull;
    std::string raw(reinterpret_cast<const char *>(&ptr), 8);
    raw.push_back('\0');
    ok(!dbread::decode_text(reinterpret_cast<const uint8_t *>(raw.data()), raw.size(), key, out), "pointer bytes are rejected");
    ok(!dbread::decode_text(nullptr, 4, key, out) && !dbread::decode_text(reinterpret_cast<const uint8_t *>("x"), 0, key, out),
       "null or zero-size text rejected");
    std::string big(dbread::TEXT_MAX + 1, 'a');
    ok(!dbread::decode_text(reinterpret_cast<const uint8_t *>(big.data()), big.size(), key, out), "over-long text rejected");

    // Printable UTF-8 (swofty/mem.hpp), shared by db strings and c_string().
    ok(swofty::mem::printable_utf8("StateTeamHQUpdate") && swofty::mem::printable_utf8("Kimi Räikkönen\tline\n"),
       "printable ASCII and UTF-8");
    ok(!swofty::mem::printable_utf8("bell\x07"), "C0 control rejected");
    ok(!swofty::mem::printable_utf8("\x7f"), "DEL rejected");
    ok(!swofty::mem::printable_utf8("\xc2\x85"), "C1 control rejected");
    ok(!swofty::mem::printable_utf8("\xc0\xaf"), "overlong rejected");
    ok(!swofty::mem::printable_utf8("\xed\xa0\x80"), "surrogate rejected");
    ok(!swofty::mem::printable_utf8("\xe2\x82"), "truncated sequence rejected");
    ok(!swofty::mem::printable_utf8("\xf4\x90\x80\x80"), "above U+10FFFF rejected");
    ok(swofty::mem::printable_utf8("\xf0\x9f\x8f\x81"), "4-byte sequence accepted");

    // Rows by label, shaped like the built 1990 database: entrants.entrant_lists has 71 rows and
    // the add-row clone f1_career_1990 is the last one. A row is only its fields; the label is the
    // _metadata_ string record and the row path itself has no record.
    {
        FakeDb db;
        dbread::TableKeys lists = dbread::table_keys("entrants.entrant_lists");
        db.integer(lists.count_key, 71);
        for (uint64_t i = 0; i < 71; i++) {
            std::string label = i == 70 ? "f1_career_1990" : i == 64 ? "f1_2026" : "list_" + std::to_string(i);
            db.text(dbread::label_key(lists.row_prefix, i), label);
            db.integer(dbread::cell_key(lists.row_prefix, i, "stat_id"), i == 70 ? 107 : int32_t(i));
        }
        db.text(dbread::label_key(lists.row_prefix, 71), "past_the_count");
        auto rec = [&](uint64_t key) { return db.get(key); };
        ok(dbread::label_key(lists.row_prefix, 70) == full("entrants.entrant_lists.entrant_list_70._metadata_"),
           "label_key is the row's _metadata_ field");
        ok(!db.get(dbread::row_key(lists.row_prefix, 70)), "the row path has no record of its own");
        int64_t at = dbread::find_row(rec, lists.row_prefix, 71, "f1_career_1990");
        const void *stat = at < 0 ? nullptr : db.get(dbread::cell_key(lists.row_prefix, uint64_t(at), "stat_id"));
        ok(at == 70 && stat && dbread::read_i32(stat) == 107, "find_row: the add-row clone f1_career_1990 is row 70, stat_id 107");
        ok(dbread::find_row(rec, lists.row_prefix, 71, "f1_2026") == 64, "find_row: a stock row by its label");
        ok(dbread::find_row(rec, lists.row_prefix, 71, "past_the_count") == -1, "find_row: a row past the instance count is not found");
        ok(dbread::find_row(rec, lists.row_prefix, 71, "f1_career_1991") == -1, "find_row: an unknown label is not found");
    }

    // SWOFTY_GAMEDB_BIN=<gamedatabases.2025.bin>: the same lookup over a built database file, for
    // SWOFTY_GAMEDB_LABEL (default f1_2026) in entrants.entrant_lists.
    if (const char *bin = getenv("SWOFTY_GAMEDB_BIN")) {
        const char *label = getenv("SWOFTY_GAMEDB_LABEL") ? getenv("SWOFTY_GAMEDB_LABEL") : "f1_2026";
        FakeDb db;
        bool loaded = load_bin(bin, db);
        ok(loaded, "gamedb file parses");
        dbread::TableKeys lists = dbread::table_keys("entrants.entrant_lists");
        const void *count = db.get(lists.count_key);
        uint64_t n = count ? uint64_t(dbread::read_i32(count)) : 0;
        int64_t at = dbread::find_row([&](uint64_t key) { return db.get(key); }, lists.row_prefix, n, label);
        const void *stat = at < 0 ? nullptr : db.get(dbread::cell_key(lists.row_prefix, uint64_t(at), "stat_id"));
        printf("      %s: %zu records, %llu entrant lists, '%s' at row %lld, stat_id %d\n", bin, db.recs.size(),
               (unsigned long long)n, label, (long long)at, stat ? dbread::read_i32(stat) : -1);
        ok(at >= 0 && stat, "gamedb file: the label's entrant list row and its stat_id are found");
        ok(at < 0 || !db.get(dbread::row_key(lists.row_prefix, uint64_t(at))), "gamedb file: the row path has no record");
    }

    printf("%s: %d failure(s)\n", failures ? "FAIL" : "ok  ", failures);
    return failures ? 1 : 0;
}
