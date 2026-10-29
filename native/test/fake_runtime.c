#define main fake_game_main
#include "fake_game.c"
#undef main
#include <zstd.h>

typedef struct {
    uint64_t unk;
    void *alloc;
    uint64_t mult;
    uint64_t *slots;
    uint8_t *info;
    uint64_t count, mask, max_count;
    uint32_t inc, shift;
} RMap;
_Static_assert(sizeof(RMap) == 0x48, "RobinMapU64 layout");

static void *fa_alloc(void *self, size_t size, size_t align)
{
    (void)self, (void)align;
    return calloc(1, size ? size : 1);
}

static void fa_free(void *self, void *p)
{
    (void)self;
    free(p);
}

static void *fa_vt[16];
static void *fake_allocator[1] = {fa_vt};

static size_t rm_buffer(const RMap *m)
{
    uint64_t n = m->mask + 1, a = n * 80 / 100;
    return (size_t)(n + (a < 0xff ? a : 0xff));
}

static void rm_init(RMap *m, uint64_t n)
{
    memset(m, 0, sizeof *m);
    m->alloc = fake_allocator;
    m->mult = 0xc4ceb9fe1a85ec53ull;
    m->mask = n - 1;
    size_t b = rm_buffer(m);
    m->slots = calloc(b + 1, 16);
    m->info = calloc(b + 8, 1);
    m->info[b] = 1;
    m->max_count = n * 80 / 100;
    m->inc = 32;
}

static void rm_index(const RMap *m, uint64_t key, size_t *idx, uint32_t *inf)
{
    uint64_t h = key * m->mult;
    h ^= h >> 33;
    *inf = m->inc + (((uint32_t)h & 0x1f) >> (m->shift & 0x3f));
    *idx = (size_t)((h >> 5) & m->mask);
}

static uint64_t *rm_find(const RMap *m, uint64_t key)
{
    size_t idx;
    uint32_t inf;
    rm_index(m, key, &idx, &inf);
    do {
        if (inf == m->info[idx] && m->slots[idx * 2] == key)
            return &m->slots[idx * 2];
        idx++;
        inf += m->inc;
    } while (inf <= m->info[idx]);
    return NULL;
}

static uint64_t *rm_insert(RMap *m, uint64_t key, uint64_t value)
{
    size_t idx;
    uint32_t inf;
    rm_index(m, key, &idx, &inf);
    while (inf < m->info[idx])
        idx++, inf += m->inc;
    for (; inf == m->info[idx]; idx++, inf += m->inc)
        if (m->slots[idx * 2] == key)
            return &m->slots[idx * 2];
    if (m->count >= m->max_count) {
        printf("FAIL: fake map full\n");
        fails++;
        return NULL;
    }
    size_t ins = idx;
    uint32_t ins_inf = inf;
    while (m->info[idx])
        idx++;
    for (size_t i = idx; i != ins; --i) {
        m->slots[i * 2] = m->slots[(i - 1) * 2];
        m->slots[i * 2 + 1] = m->slots[(i - 1) * 2 + 1];
        m->info[i] = (uint8_t)(m->info[i - 1] + m->inc);
    }
    m->slots[ins * 2] = key;
    m->slots[ins * 2 + 1] = value;
    m->info[ins] = (uint8_t)ins_inf;
    m->count++;
    return &m->slots[ins * 2];
}

static uint64_t fnv64(const char *s)
{
    uint64_t h = 0xcbf29ce484222325ull;
    for (; *s; s++)
        h = (h ^ (uint8_t)*s) * 0x100000001b3ull;
    return h;
}

static uint32_t fnv32(const char *s)
{
    uint32_t h = 0x811c9dc5u;
    for (; *s; s++)
        h = (h ^ (uint8_t)*s) * 0x01000193u;
    return h;
}

static void md5(const uint8_t *msg, size_t len, uint8_t out[16])
{
    static const uint32_t K[64] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501, 0x698098d8, 0x8b44f7af,
        0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa,
        0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8, 0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8,
        0x676f02d9, 0x8d2a4c8a, 0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
        0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665, 0xf4292244, 0x432aff97,
        0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1, 0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
        0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
    static const uint8_t R[16] = {7, 12, 17, 22, 5, 9, 14, 20, 4, 11, 16, 23, 6, 10, 15, 21};
    uint32_t h[4] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};
    size_t total = ((len + 8) / 64 + 1) * 64;
    uint8_t *p = calloc(total, 1);
    memcpy(p, msg, len);
    p[len] = 0x80;
    uint64_t bits = (uint64_t)len * 8;
    memcpy(p + total - 8, &bits, 8);
    for (size_t o = 0; o < total; o += 64) {
        uint32_t w[16], a = h[0], b = h[1], c = h[2], d = h[3];
        memcpy(w, p + o, 64);
        for (int i = 0; i < 64; i++) {
            uint32_t f;
            int g;
            if (i < 16)
                f = (b & c) | (~b & d), g = i;
            else if (i < 32)
                f = (d & b) | (~d & c), g = (5 * i + 1) & 15;
            else if (i < 48)
                f = b ^ c ^ d, g = (3 * i + 5) & 15;
            else
                f = c ^ (b | ~d), g = (7 * i) & 15;
            uint32_t t = d, x = a + f + K[i] + w[g], r = R[(i / 16) * 4 + i % 4];
            d = c, c = b, b = b + ((x << r) | (x >> (32 - r))), a = t;
        }
        h[0] += a, h[1] += b, h[2] += c, h[3] += d;
    }
    memcpy(out, h, 16);
    free(p);
}

static uint64_t res_id(const char *uri)
{
    uint8_t d[16];
    md5((const uint8_t *)uri, strlen(uri), d);
    uint64_t v = 0;
    for (int i = 0; i < 8; i++)
        v = v << 8 | d[i];
    return v;
}

static wchar_t game_dir_w[MAX_PATH];

static int read_disk(const char *rel, uint8_t **out, size_t *size)
{
    wchar_t path[MAX_PATH * 2];
    swprintf(path, MAX_PATH * 2, L"%ls\\%hs", game_dir_w, rel);
    for (wchar_t *c = path; *c; c++)
        if (*c == L'/')
            *c = L'\\';
    FILE *f = _wfopen(path, L"rb");
    if (!f)
        return 0;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    *out = malloc(n + 1);
    *size = fread(*out, 1, n, f);
    (*out)[*size] = 0;
    fclose(f);
    return 1;
}

void *g_fake_gdb;
uint8_t k_fake_xor_key[4] = {0x17, 0x2d, 0x57, 0x5a};
static void *vt_gint[4], *vt_gfloat[4], *vt_gstr[4], *vt_gbool[4], *vt_gother[4];
static int gdb_dtor_calls;

static void *gdb_dtor(uint8_t *v, int flags)
{
    (void)flags;
    gdb_dtor_calls++;
    if (*(void ***)v == vt_gstr)
        free(*(char **)(v + 8));
    return v;
}

static void **gdb_class(int tag)
{
    switch (tag) {
    case 0:
        return vt_gint;
    case 1:
        return vt_gfloat;
    case 2:
    case 3:
    case 4:
    case 17:
        return vt_gstr;
    case 5:
        return vt_gbool;
    case 10:
    case 15:
    case 16:
        return NULL;
    default:
        return vt_gother;
    }
}

void gdb_insert_impl(uint8_t *db, uint64_t key, uint8_t tag, char *text)
{
    uint64_t *slot = rm_insert((RMap *)(db + 0xa8), key, 0);
    void **cls = gdb_class(tag);
    if (!slot || !cls)
        return;
    uint8_t *v = (uint8_t *)slot[1];
    if (!v) {
        v = calloc(1, 0x40);
        *(void ***)v = cls;
        slot[1] = (uint64_t)v;
    }
    switch (tag) {
    case 0:
        *(int32_t *)(v + 8) = atoi(text);
        break;
    case 1:
        *(float *)(v + 8) = (float)atof(text);
        break;
    case 2:
    case 3:
    case 4:
    case 17: {
        size_t n = strlen(text) + 1;
        uint8_t *enc = malloc(n);
        for (size_t i = 0; i + 1 < n; i++)
            enc[i] = (uint8_t)text[i] ^ k_fake_xor_key[(i - 1) & 3];
        enc[n - 1] = 0x5a;
        *(char **)(v + 8) = _strdup(text);
        *(uint64_t *)(v + 0x10) = n;
        *(uint8_t **)(v + 0x18) = enc;
        *(uint32_t *)(v + 0x30) = fnv32(text);
        break;
    }
    case 5:
        v[8] = _stricmp(text, "true") == 0;
        break;
    default:
        *(uint64_t *)(v + 8) = strtoull(text, NULL, 10);
    }
}

void **gdb_lookup_impl(const char *path)
{
    if (!g_fake_gdb)
        return NULL;
    uint64_t *s = rm_find((RMap *)((uint8_t *)g_fake_gdb + 0xa8), fnv64(path));
    return s ? (void **)&s[1] : NULL;
}

void fake_gdb_insert(void *db, uint64_t key, uint8_t tag, char *text);

static void gdb_put(uint8_t *db, const char *path, uint8_t tag, const char *text)
{
    char buf[256];
    snprintf(buf, sizeof buf, "%s", text);
    fake_gdb_insert(db, fnv64(path), tag, buf);
}

static uint64_t gdb_slots = 1024;

void *gdb_load_impl(uint8_t *db, void *cfg)
{
    (void)cfg;
    memset(db, 0, 0xf8);
    g_fake_gdb = db;
    *(void ***)(db + 8) = fa_vt;
    rm_init((RMap *)(db + 0xa8), gdb_slots);
    gdb_put(db, "ai.general_ai_settings.max_test_time", 1, "300");
    gdb_put(db, "fake.retype_me", 0, "5");
    gdb_put(db, "fake.keep", 0, "7");
    gdb_put(db, "fake.flag", 5, "False");
    gdb_put(db, "fake.name", 2, "Lewis");
    gdb_put(db, "fake.items._item_instancecount_", 0, "3");
    gdb_put(db, "fake.items.item_0._metadata_", 2, "alpha");
    gdb_put(db, "fake.items.item_1._metadata_", 2, "bravo");
    gdb_put(db, "fake.items.item_2._metadata_", 2, "charlie");
    gdb_put(db, "fake.items.item_0.rank", 0, "1");
    gdb_put(db, "fake.items.item_1.rank", 0, "2");
    gdb_put(db, "fake.items.item_2.rank", 0, "3");
    db[0xf0] = 1;
    return db;
}

static uint8_t mount_nefs[0x148], mount_raw[0x148];
static uint8_t fake_fs[0x500];
static int resolve_calls, wild_redirected;

void *fs_resolve_impl(uint8_t *fs, uint8_t *after, const char *path, uint8_t *out, uint8_t wild, uint8_t want)
{
    (void)want;
    resolve_calls++;
    char full[0x110];
    if (!path || !*path)
        return NULL;
    if (path[0] == '/')
        snprintf(full, sizeof full, "%s", path);
    else
        snprintf(full, sizeof full, "%s%s", (char *)(fs + 0x370), path);
    if (wild && strstr(full, "swofty/"))
        wild_redirected++;
    uint8_t *m = after ? *(uint8_t **)(after + 0x140) : *(uint8_t **)(fs + 0xf8);
    for (; m; m = *(uint8_t **)(m + 0x140)) {
        const char *prefix = *(const char **)(m + 0x18);
        size_t n = strlen(prefix);
        if (!strncmp(full, prefix, n)) {
            *(char **)(out + 0x10) = (char *)(out + 0x18);
            snprintf((char *)(out + 0x18), 0x104, "%s", full + n);
            return m;
        }
    }
    return NULL;
}

void *fake_fs_resolve(void *fs, void *after, const char *path, void *out, uint8_t wild, uint8_t want);

static const char *archive_lng;
static size_t archive_lng_size;
static const char ARCHIVE_CONFIG[] = "<archive config/>";

static int fs_read(const char *path, uint8_t **buf, size_t *size)
{
    uint8_t out[0x120];
    uint8_t *m = fake_fs_resolve(fake_fs, NULL, path, out, 0, 0);
    while (m) {
        const char *rel = *(const char **)(out + 0x10);
        if (m[0x131]) {
            const char *content = NULL;
            size_t n = 0;
            if (!strcmp(rel, "localisation/2025_release/language_eng.lng"))
                content = archive_lng, n = archive_lng_size;
            else if (!strcmp(rel, "system/config.xml"))
                content = ARCHIVE_CONFIG, n = sizeof ARCHIVE_CONFIG - 1;
            if (content) {
                *buf = malloc(n + 1);
                memcpy(*buf, content, n);
                (*buf)[n] = 0;
                *size = n;
                return 1;
            }
            m = fake_fs_resolve(fake_fs, m, path, out, 0, 0);
            continue;
        }
        return read_disk(rel, buf, size) ? 2 : 0;
    }
    return 0;
}

typedef struct {
    void **vt;
    const uint8_t *ptr;
    uint64_t size, pos;
} MemStream;

static int64_t ms_read(MemStream *s, void *dst, uint32_t n)
{
    if (s->pos + n > s->size) {
        memset(dst, 0, n);
        s->pos = s->size;
        return 0;
    }
    memcpy(dst, s->ptr + s->pos, n);
    s->pos += n;
    return n;
}

static int64_t ms_reset(MemStream *s)
{
    (void)s;
    return 0;
}

void *fake_memstream_vt[20];

typedef struct LngTable {
    uint32_t nb, seed, mult;
    uint32_t *buckets, *sida, n;
    char *sidb, *lngb;
    uint32_t priority;
    struct LngTable *next;
} LngTable;

typedef struct {
    void **vt;
    LngTable *head;
} LangSet;

static uint32_t be(uint32_t v)
{
    return v >> 24 | (v >> 8 & 0xff00) | (v << 8 & 0xff0000) | v << 24;
}

static uint32_t rd32(MemStream *s)
{
    uint32_t v = 0;
    ((int64_t(*)(MemStream *, void *, uint32_t))s->vt[6])(s, &v, 4);
    return be(v);
}

static int parse_failures;

int32_t loc_add_impl(LangSet *set, MemStream *s, uint32_t priority, uint8_t flag)
{
    (void)flag;
    ((int64_t(*)(MemStream *))s->vt[13])(s);
    LngTable *t = calloc(1, sizeof *t);
    char tag[5] = {0};
#define TAG(want)                                                                                                     \
    ((int64_t(*)(MemStream *, void *, uint32_t))s->vt[6])(s, tag, 4);                                               \
    if (memcmp(tag, want, 4)) {                                                                                       \
        printf("      fake LNGT parser: expected %s, got '%.4s'\n", want, tag);                                       \
        parse_failures++;                                                                                             \
        free(t);                                                                                                      \
        return 0x4f;                                                                                                  \
    }
    TAG("LNGT");
    rd32(s);
    TAG("HSHS");
    rd32(s);
    t->nb = rd32(s);
    t->seed = rd32(s);
    t->mult = rd32(s);
    TAG("HSHT");
    rd32(s);
    t->buckets = calloc(t->nb * 2 + 2, 4);
    for (uint32_t i = 0; i < t->nb * 2; i++)
        t->buckets[i] = rd32(s);
    TAG("SIDA");
    rd32(s);
    t->n = rd32(s);
    t->sida = calloc(t->n * 2 + 2, 4);
    for (uint32_t i = 0; i < t->n * 2; i++)
        t->sida[i] = rd32(s);
    TAG("SIDB");
    uint32_t nb = rd32(s);
    t->sidb = calloc(nb + 1, 1);
    ((int64_t(*)(MemStream *, void *, uint32_t))s->vt[6])(s, t->sidb, nb);
    TAG("LNGB");
    uint32_t nl = rd32(s);
    t->lngb = calloc(nl + 1, 1);
    ((int64_t(*)(MemStream *, void *, uint32_t))s->vt[6])(s, t->lngb, nl);
#undef TAG
    t->priority = priority;
    t->next = set->head;
    set->head = t;
    LngTable *sorted = NULL, **tail = &sorted;
    for (uint32_t p = 0; p <= 8; p++)
        for (LngTable *x = set->head; x; x = x->next)
            if (x->priority == p) {
                LngTable *c = malloc(sizeof *c);
                *c = *x;
                c->next = NULL;
                *tail = c;
                tail = &c->next;
            }
    set->head = sorted;
    return 0;
}

static const char *table_find(LngTable *t, const char *key)
{
    uint32_t h = t->seed;
    for (const char *p = key; *p; p++)
        h = h * t->mult + (int32_t)(signed char)*p;
    uint32_t b = h % t->nb, start = t->buckets[b * 2], count = t->buckets[b * 2 + 1];
    for (uint32_t i = start; count--; i++)
        if (!strcmp(t->sidb + t->sida[i * 2], key))
            return t->lngb + t->sida[i * 2 + 1];
    return NULL;
}

static const char *set_find(LangSet *set, const char *key, int mode)
{
    (void)mode;
    for (LngTable *t = set->head; t; t = t->next) {
        const char *v = table_find(t, key);
        if (v)
            return v;
    }
    return NULL;
}

static void *vt_langset[8];
int32_t fake_loc_add(void *set, void *stream, uint32_t priority, uint8_t flag);

int32_t loc_load_impl(uint8_t *module, uint32_t lang, uint8_t *path)
{
    LangSet *set = *(LangSet **)(module + 0x28 + lang * 0x10);
    uint8_t *buf = NULL;
    size_t size = 0;
    if (!set || !fs_read(*(const char **)(path + 0x10), &buf, &size))
        return 2;
    set->head = NULL;
    MemStream ms = {fake_memstream_vt, buf, size, 0};
    int32_t r = fake_loc_add(set, &ms, 0, 0);
    free(buf);
    return r;
}

static uint8_t fake_manifest[0x800];
void *g_fake_manifest;
static char *words;
static uint16_t *tokens;
static uint32_t ntokens;
static size_t words_used = 8;
static uint64_t *erp_list;
static uint32_t nerps;

static uint16_t word_of(const char *w, size_t n)
{
    for (size_t o = 8; o < words_used; o += 8)
        if (strlen(words + o) == n && !memcmp(words + o, w, n))
            return (uint16_t)(o / 8);
    size_t o = words_used;
    memcpy(words + o, w, n);
    words[o + n] = 0;
    words_used += (n + 8) & ~(size_t)7;
    return (uint16_t)(o / 8);
}

static uint32_t encode(const char *path)
{
    uint32_t start = ntokens;
    const char *cur = path;
    for (const char *p = path;; p++) {
        int sep = *p == '/' ? 0 : *p == '.' ? 1 : *p == '_' ? 2 : *p == 0 ? 3 : -1;
        if (sep < 0)
            continue;
        tokens[ntokens++] = (uint16_t)(word_of(cur, (size_t)(p - cur)) | sep << 14);
        cur = p + 1;
        if (!*p)
            break;
    }
    return start;
}

static void add_group(const char *group, const char *const *erps, int n)
{
    char display[256];
    snprintf(display, sizeof display, "2025_%s", group);
    uint64_t gh = fnv64(group);
    rm_insert((RMap *)(fake_manifest + 0x1f8), gh, 0x80000000ull | encode(display));
    uint32_t start = nerps;
    for (int i = 0; i < n; i++) {
        uint64_t eh = fnv64(erps[i]);
        erp_list[nerps++] = eh;
        if (!rm_find((RMap *)(fake_manifest + 0x1f8), eh))
            rm_insert((RMap *)(fake_manifest + 0x1f8), eh, 0x80000000ull | encode(erps[i]));
    }
    rm_insert((RMap *)(fake_manifest + 0x10), gh, (uint64_t)n << 32 | start);
}

uint64_t parse_manifest_impl(uint8_t *m, void *path, void *a3)
{
    (void)path, (void)a3;
    *(void **)(m + 0) = fake_allocator;
    *(void **)(m + 0x8) = fake_allocator;
    rm_init((RMap *)(m + 0x10), 256);
    rm_init((RMap *)(m + 0x78), 64);
    rm_init((RMap *)(m + 0x1f8), 1024);
    *(void **)(m + 0x58) = fake_allocator;
    *(void **)(m + 0xc0) = fake_allocator;
    *(void **)(m + 0x1b8) = fake_allocator;
    *(void **)(m + 0x1d0) = fake_allocator;
    words = calloc(0x20000, 1);
    tokens = calloc(4096, 2);
    erp_list = calloc(256, 8);
    static const char *const app[] = {"2025_asset_groups/ego_package/application.erp"};
    static const char *const boot[] = {"2025_asset_groups/ui_package/flow_boot.erp"};
    static const char *const splash[] = {"2025_asset_groups/ui_package/screens/splash.erp"};
    static const char *const other[] = {"2025_asset_groups/ui_package/other.erp"};
    static const char *const pre[] = {"2025_asset_groups/ui_package/prebuilt.erp"};
    add_group("asset_groups/ego_package/application.assetgroup", app, 1);
    add_group("asset_groups/ui_package/flow_boot.assetgroup", boot, 1);
    add_group("asset_groups/ui_package/screens/splash.assetgroup", splash, 1);
    add_group("asset_groups/ui_package/other.assetgroup", other, 1);
    add_group("asset_groups/ui_package/prebuilt.assetgroup", pre, 1);
    *(char **)(m + 0x1c0) = words;
    *(uint16_t **)(m + 0x1d8) = tokens;
    *(uint64_t *)(m + 0x1e0) = ntokens * 2;
    *(uint32_t *)(m + 0x1e8) = ntokens;
    *(uint64_t **)(m + 0x60) = erp_list;
    *(uint64_t *)(m + 0x68) = nerps * 8;
    *(uint32_t *)(m + 0x70) = nerps;
    return 0;
}

static void decode_name(uint8_t *m, uint64_t value, char *out, size_t cap)
{
    static const char seps[] = {'/', '.', '_', 0};
    const uint16_t *tok = *(uint16_t **)(m + 0x1d8);
    const char *w = *(char **)(m + 0x1c0);
    size_t n = 0;
    for (uint32_t i = (uint32_t)value & 0xffffff;; i++) {
        n += snprintf(out + n, cap - n, "%s", w + (size_t)(tok[i] & 0x3fff) * 8);
        if (tok[i] >> 14 == 3 || n + 2 >= cap)
            break;
        out[n++] = seps[tok[i] >> 14];
        out[n] = 0;
    }
}

typedef struct {
    uint64_t id;
    char *uri;
    char type[16];
    uint32_t arg, frag_count;
    void *frags;
} IdxRes;

static RMap resmap;
static uint8_t fake_loader[0x400];
static uint64_t loaded_erps[64];
static int nloaded, zstd_frags;
void fake_site_fn(void *loader, IdxRes *idx, uint64_t created, uint8_t *res);

static int process_erp(uint8_t *m, uint64_t hash)
{
    for (int i = 0; i < nloaded; i++)
        if (loaded_erps[i] == hash)
            return 1;
    uint64_t *ns = rm_find((RMap *)(m + 0x1f8), hash);
    if (!ns)
        return 0;
    char name[300];
    decode_name(m, ns[1], name, sizeof name);
    uint8_t *f = NULL;
    size_t size = 0;
    if (!read_disk(name, &f, &size)) {
        printf("      fake ERP loader: %s not on disk (skipped)\n", name);
        return 0;
    }
    loaded_erps[nloaded++] = hash;
    uint64_t io = *(uint64_t *)(f + 0x10), dof = *(uint64_t *)(f + 0x20);
    uint8_t *p = f + io + 8;
    uint32_t n = *(uint32_t *)(f + io);
    uint64_t info[8] = {hash};
    *(uint64_t **)(fake_loader + 0x330) = info;
    printf("      fake ERP loader: read %s (%u resource(s))\n", name, n);
    for (uint32_t i = 0; i < n; i++) {
        uint32_t esz = *(uint32_t *)p;
        uint16_t nlen = *(uint16_t *)(p + 4);
        IdxRes idx = {0};
        idx.uri = _strdup((char *)p + 6);
        idx.id = res_id(idx.uri);
        uint8_t *q = p + 6 + nlen;
        memcpy(idx.type, q, 16);
        idx.arg = *(uint32_t *)(q + 16);
        idx.frag_count = q[20];
        uint8_t *fr = q + 21;
        uint64_t *slot = rm_find(&resmap, idx.id);
        uint8_t *res = slot ? (uint8_t *)slot[1] : NULL;
        int created = 0;
        if (res)
            (*(int32_t *)(res + 0x1c))++;
        else {
            res = calloc(1, 0x80);
            *(uint64_t *)res = idx.id;
            *(char **)(res + 8) = idx.uri;
            *(int32_t *)(res + 0x1c) = 1;
            res[0x20] = (uint8_t)idx.frag_count;
            rm_insert(&resmap, idx.id, (uint64_t)res);
            created = 1;
        }
        fake_site_fn(fake_loader, &idx, (uint64_t)created, res);
        if (created) {
            uint8_t *last = fr + (idx.frag_count - 1) * 0x21;
            uint64_t off = *(uint64_t *)(last + 4), usize = *(uint64_t *)(last + 12), packed = *(uint64_t *)(last + 25);
            uint8_t comp = last[24];
            *(uint64_t *)(res + 0x40) = hash;
            if (comp == 0x11) {
                uint8_t *raw = malloc(usize + 1);
                size_t got = ZSTD_decompress(raw, usize, f + dof + off, packed);
                if (ZSTD_isError(got) || got != usize) {
                    printf("      fake ERP loader: %s: zstd fragment of %s did not decompress to %llu bytes\n", name, idx.uri,
                           (unsigned long long)usize);
                    fails++;
                } else
                    zstd_frags++;
                memcpy(res + 0x48, raw, 8);
                free(raw);
            } else
                memcpy(res + 0x48, f + dof + off, 8);
            res[0x22] = 3;
        }
        p += 4 + esz;
    }
    free(f);
    return 1;
}

static void load_group(const char *group)
{
    uint8_t *m = g_fake_manifest;
    uint64_t *s = rm_find((RMap *)(m + 0x10), fnv64(group));
    if (!s) {
        printf("      fake group loader: %s not in the manifest\n", group);
        return;
    }
    uint32_t start = (uint32_t)s[1], count = (uint32_t)(s[1] >> 32) & 0xffffff;
    const uint64_t *erps = *(uint64_t **)(m + 0x60);
    printf("      fake group loader: %s -> %u ERP(s) from index %u\n", group, count, start);
    for (uint32_t i = 0; i < count; i++)
        process_erp(m, erps[start + i]);
}

static uint64_t released[64];
static int nreleased;
HANDLE fake_create_mutex(const wchar_t *name);

void expand_impl(void *loader, uint64_t *h, void *out)
{
    (void)loader, (void)out;
    uint8_t *m = g_fake_manifest;
    if (((uint8_t *)h)[0xc] & 4) {
        const uint64_t *b = (const uint64_t *)h[3], *e = (const uint64_t *)h[4];
        printf("      fake expand: flag-4 handle 0x%016llx -> %d ERP(s) from its own list\n", (unsigned long long)h[0], (int)(e - b));
        for (; b != e; b++)
            process_erp(m, *b);
        return;
    }
    uint64_t *s = rm_find((RMap *)(m + 0x10), h[0]);
    if (!s)
        return;
    uint32_t start = (uint32_t)s[1], count = (uint32_t)(s[1] >> 32) & 0xffffff;
    for (uint32_t i = 0; i < count; i++)
        process_erp(m, (*(uint64_t **)(m + 0x60))[start + i]);
}

void release_impl(void *loader, uint64_t *h, void *out)
{
    (void)loader, (void)out;
    nreleased = 0;
    if (((uint8_t *)h)[0xc] & 4)
        for (const uint64_t *b = (const uint64_t *)h[3]; b != (const uint64_t *)h[4] && nreleased < 64; b++)
            released[nreleased++] = *b;
    printf("      fake release: handle 0x%016llx -> %d ERP(s)\n", (unsigned long long)h[0], nreleased);
}

static uint8_t *res_of(const char *uri)
{
    uint64_t *s = rm_find(&resmap, res_id(uri));
    return s ? (uint8_t *)s[1] : NULL;
}

static int is_overlay(uint64_t hash)
{
    uint64_t *ns = rm_find((RMap *)((uint8_t *)g_fake_manifest + 0x1f8), hash);
    char name[300] = {0};
    if (ns)
        decode_name(g_fake_manifest, ns[1], name, sizeof name);
    return ns && !strncmp(name, "swofty/cache/rt_", 16);
}

static const char *src_name(uint8_t *res)
{
    static char name[300];
    uint64_t *ns = res ? rm_find((RMap *)((uint8_t *)g_fake_manifest + 0x1f8), *(uint64_t *)(res + 0x40)) : NULL;
    name[0] = 0;
    if (ns)
        decode_name(g_fake_manifest, ns[1], name, sizeof name);
    return name;
}

static void early_wait_mode(void)
{
    HMODULE u32 = GetModuleHandleW(L"user32.dll");
    ATOM(WINAPI * reg)(const WNDCLASSW *) = (void *)GetProcAddress(u32, "RegisterClassW");
    HWND(WINAPI * mk)(DWORD, LPCWSTR, LPCWSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID) =
        (void *)GetProcAddress(u32, "CreateWindowExW");
    WNDCLASSW wc = {.lpfnWndProc = DefWindowProcW, .hInstance = GetModuleHandleW(NULL), .lpszClassName = L"SwoftyWaitTest"};
    reg(&wc);
    HWND w = mk(0, L"SwoftyWaitTest", L"wait test", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 200, 100, NULL, NULL,
                GetModuleHandleW(NULL), NULL);
    ShowWindow(w, SW_SHOW);
    DWORD t0 = GetTickCount();
    while (!GetModuleHandleW(L"swofty_core.dll") && GetTickCount() - t0 < 10000) {
        MSG m;
        while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE))
            DispatchMessageW(&m);
        Sleep(5);
    }
    expect(GetModuleHandleW(L"swofty_core.dll") != NULL, "earlywait: the watcher thread started loading mods (main window visible)");
    char done[8] = {0};
    GetEnvironmentVariableA("SWOFTY_SLOW_DONE", done, sizeof done);
    expect(!done[0], "earlywait: the slow mod is still initialising when the game reaches CreateMutexW");
    DWORD t1 = GetTickCount();
    HANDLE mx = fake_create_mutex(L"Codemasters F1 25");
    DWORD waited = GetTickCount() - t1;
    GetEnvironmentVariableA("SWOFTY_SLOW_DONE", done, sizeof done);
    printf("      CreateMutexW returned after %lu ms, slow mod done='%s'\n", (unsigned long)waited, done);
    expect(mx && !strcmp(done, "1"), "earlywait: CreateMutexW('Codemasters F1 25') returned only after the other thread's load finished");
    printf("%s (%d failure(s))\n", fails ? "FAILED" : "ALL PASSED", fails);
    ExitProcess(fails != 0);
}

uint64_t fake_parse_manifest(void *m, void *path, void *a3);
void *fake_gdb_load(void *db, void *cfg);
void **fake_gdb_lookup(const char *path);
int32_t fake_loc_load(void *module, uint32_t lang, void *path);
HANDLE fake_create_mutex(const wchar_t *name);

static void second_copy_mode(void)
{
    HANDLE(WINAPI * real)(LPSECURITY_ATTRIBUTES, BOOL, LPCWSTR) =
        (void *)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "CreateMutexW");
    HANDLE held = real(NULL, FALSE, L"Codemasters F1 25");
    expect(held && GetLastError() == 0, "secondcopy: another copy holds the game's single-instance mutex");
    SetLastError(12345);
    HANDLE mx = fake_create_mutex(L"Codemasters F1 25");
    DWORD e = GetLastError();
    expect(GetModuleHandleW(L"swofty_core.dll") != NULL, "secondcopy: mods loaded inside CreateMutexW('Codemasters F1 25')");
    expect(mx && e == ERROR_ALREADY_EXISTS,
           "secondcopy: the game still sees the real call's ERROR_ALREADY_EXISTS (0xb7) after the loader logs it");
    printf("%s (%d failure(s)); closing before the window, as F1 25 does when another copy runs\n",
           fails ? "FAILED" : "ALL PASSED", fails);
    ExitProcess(fails != 0);
}
void fake_expand(void *loader, uint64_t *h, void *out);
void fake_release(void *loader, uint64_t *h, void *out);
void *fake_alias_ref(uint64_t hash);
static uint8_t fake_alias_obj[0x60];
void *g_fake_alias = fake_alias_obj;

__asm__(
    ".section .xtls,\"xr\"\n"
    ".globl fake_gdb_load\nfake_gdb_load:\n"
    ".byte 0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x56,0x57,0x55,0x53\n"
    ".byte 0x48,0x81,0xEC,0x28,0x02,0x00,0x00,0x48,0x89,0xD7,0x49,0x89,0xCC\n"
    "mov %r12, %rcx\nmov %rdi, %rdx\ncall gdb_load_impl\nadd $0x228, %rsp\n"
    "pop %rbx\npop %rbp\npop %rdi\npop %rsi\npop %r12\npop %r13\npop %r14\npop %r15\nret\n"

    ".globl fake_gdb_insert\nfake_gdb_insert:\n"
    ".byte 0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x56,0x57,0x55,0x53\n"
    ".byte 0x48,0x81,0xEC,0xD8,0x00,0x00,0x00,0xC5,0xF8,0x29,0xB4,0x24,0xC0,0x00,0x00,0x00\n"
    ".byte 0x4C,0x89,0xCE,0x44,0x88,0x44,0x24,0x37\n"
    "call gdb_insert_impl\nvmovaps 0xc0(%rsp), %xmm6\nadd $0xd8, %rsp\n"
    "pop %rbx\npop %rbp\npop %rdi\npop %rsi\npop %r12\npop %r13\npop %r14\npop %r15\nret\n"

    ".globl fake_gdb_xor_key_ref\nfake_gdb_xor_key_ref:\n"
    ".byte 0x48,0xB8,0x53,0x57,0x46,0x54,0x58,0x4B,0x45,0x59\n"
    ".byte 0x4C,0x8D,0x25\n.long k_fake_xor_key-(.+4)\nret\n"

    ".globl fake_gdb_lookup\nfake_gdb_lookup:\n"
    ".byte 0x56,0x57,0x55,0x53,0x49,0xB8,0x25,0x23,0x22,0x84,0xE4,0x9C,0xF2,0xCB\n"
    ".byte 0x48,0x8B,0x15\n.long g_fake_gdb-(.+4)\n.byte 0x44,0x0F,0xB6,0x11\n"
    "sub $0x28, %rsp\ncall gdb_lookup_impl\nadd $0x28, %rsp\npop %rbx\npop %rbp\npop %rdi\npop %rsi\nret\n"

    ".globl fake_fs_resolve\nfake_fs_resolve:\n"
    ".byte 0x41,0x56,0x56,0x57,0x55,0x53,0x48,0x81,0xEC,0x30,0x01,0x00,0x00,0x4C,0x89,0xCF\n"
    "mov 0x180(%rsp), %rax\nmov %rax, 0x20(%rsp)\nmov 0x188(%rsp), %rax\nmov %rax, 0x28(%rsp)\n"
    "call fs_resolve_impl\nadd $0x130, %rsp\npop %rbx\npop %rbp\npop %rdi\npop %rsi\npop %r14\nret\n"

    ".globl fake_loc_load\nfake_loc_load:\n"
    ".byte 0x41,0x56,0x56,0x57,0x53,0x48,0x81,0xEC,0xA8,0x00,0x00,0x00,0x83,0xFA,0x12,0x0F,0x87\n"
    ".long 1f-(.+4)\n"
    ".byte 0x48,0x8D,0x05\n.long fake_memstream_vt-(.+4)\n"
    ".byte 0x48,0x89,0x44,0x24,0x68,0xC5,0xF8,0x28,0x84,0x24,0x90,0x00,0x00,0x00,0xC5,0xF8,0x11,0x44,0x24,0x70\n"
    "call loc_load_impl\njmp 2f\n1:\nmov $0xe, %eax\n2:\nadd $0xa8, %rsp\npop %rbx\npop %rdi\npop %rsi\npop %r14\nret\n"

    ".globl fake_loc_add\nfake_loc_add:\n"
    ".byte 0x41,0x57,0x41,0x56,0x41,0x54,0x56,0x57,0x55,0x53,0x48,0x83,0xEC,0x30,0x44,0x89,0xCB,0x44,0x89,0xC5\n"
    "call loc_add_impl\nadd $0x30, %rsp\npop %rbx\npop %rbp\npop %rdi\npop %rsi\npop %r12\npop %r14\npop %r15\nret\n"

    ".globl fake_parse_manifest\nfake_parse_manifest:\n"
    ".byte 0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x56,0x57,0x55,0x53\n"
    ".byte 0x48,0x81,0xEC,0xD8,0x00,0x00,0x00,0xC5,0xF8,0x29,0xB4,0x24,0xC0,0x00,0x00,0x00\n"
    ".byte 0x4C,0x89,0xC7,0x49,0x89,0xD0\n"
    "mov %rdi, %r8\ncall parse_manifest_impl\nvmovaps 0xc0(%rsp), %xmm6\nadd $0xd8, %rsp\n"
    "pop %rbx\npop %rbp\npop %rdi\npop %rsi\npop %r12\npop %r13\npop %r14\npop %r15\nret\n"

    ".globl fake_manifest_global_ref\nfake_manifest_global_ref:\n"
    ".byte 0x48,0xC7,0x41,0x08,0x04,0x01,0x00,0x00,0x48,0x89,0x79,0x10,0xC6,0x41,0x18,0x00\n"
    ".byte 0x48,0x8D,0x05\n.long fake_manifest-(.+4)\n.byte 0x48,0x89,0x01\n"
    ".byte 0x4C,0x8B,0x35\n.long g_fake_manifest-(.+4)\n.byte 0x49,0x8B,0x86,0x08,0x02,0x00,0x00\nret\n"

    ".globl fake_site_fn\nfake_site_fn:\n"
    "push %r15\npush %r13\npush %rdi\npush %rsi\nsub $0x88, %rsp\n"
    "mov %rcx, %r13\nmov %rdx, %rdi\nxor %esi, %esi\nmov %r9, 0x80(%rsp)\nmov %r8, %rax\n"
    "jmp fake_site_at\n"
    ".org fake_site_fn+0x8f, 0xCC\n"
    "jmp fake_site_done\n"
    ".org fake_site_fn+0x100, 0xCC\n"
    "fake_site_at:\n"
    ".byte 0x4C,0x8B,0xBC,0x24,0x80,0x00,0x00,0x00,0x84,0xC0,0x74,0x83\n"
    "fake_site_done:\n"
    "add $0x88, %rsp\npop %rsi\npop %rdi\npop %r13\npop %r15\nret\n"

    ".globl fake_expand\nfake_expand:\n"
    ".byte 0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x56,0x57,0x55,0x53,0x48,0x83,0xEC,0x68\n"
    ".byte 0xC5,0xF8,0x29,0x74,0x24,0x50,0x4D,0x89,0xC5,0x49,0x89,0xD4\n"
    "call expand_impl\nvmovaps 0x50(%rsp), %xmm6\nadd $0x68, %rsp\n"
    "pop %rbx\npop %rbp\npop %rdi\npop %rsi\npop %r12\npop %r13\npop %r14\npop %r15\nret\n"

    ".globl fake_release\nfake_release:\n"
    ".byte 0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x56,0x57,0x55,0x53,0x48,0x83,0xEC,0x38\n"
    ".byte 0x4C,0x89,0xC6,0x48,0x89,0xD3,0x48,0x89,0xCF,0x4C,0x8B,0x25\n.long g_fake_manifest-(.+4)\n"
    "call release_impl\nadd $0x38, %rsp\n"
    "pop %rbx\npop %rbp\npop %rdi\npop %rsi\npop %r12\npop %r13\npop %r14\npop %r15\nret\n"

    ".globl fake_alias_ref\nfake_alias_ref:\n"
    ".byte 0x56,0x57,0x55,0x53,0x48,0x83,0xEC,0x28,0x48,0x89,0xC8,0x48,0x8B,0x15\n.long g_fake_alias-(.+4)\n"
    ".byte 0x48,0x8B,0x4A,0x18,0x48,0x0F,0xAF,0xC8\n"
    "add $0x28, %rsp\npop %rbx\npop %rbp\npop %rdi\npop %rsi\nret\n"

    ".globl fake_create_mutex\nfake_create_mutex:\n"
    "sub $0x28, %rsp\nmov %rcx, %r8\nxor %ecx, %ecx\nxor %edx, %edx\ncall *__imp_CreateMutexW(%rip)\nadd $0x28, %rsp\nret\n"
    ".text\n");

static void *fake_vt_stub(void)
{
    return NULL;
}

static void files_mode(void)
{
    static const struct {
        const char *path, *want, *what;
    } reads[] = {
        {"audio/2025/bundles/t_v10.bdl", "T.FILES2 V10", "files: the game opens the bundle path and reads the later addon's file"},
        {"Audio/2025/Bundles/T_V10.BDL", "T.FILES2 V10", "files: the path matches in any case"},
        {"/game/audio/2025/bundles/t_v10.bdl", "T.FILES2 V10", "files: a /game/ path is redirected too"},
        {"audio/2025/bundles/t_kept.bdl", "GAME KEPT", "files: a bundle the addon's folder lacks reads the game folder's copy"},
        {"audio/2025/bundles/ferrari_2025.bdl", "STOCK FERRARI", "files: other bundles are untouched"},
    };
    for (size_t i = 0; i < sizeof reads / sizeof *reads; i++) {
        uint8_t *buf = NULL;
        size_t n = 0;
        int src = fs_read(reads[i].path, &buf, &n);
        printf("      %s from %s: '%s'\n", reads[i].path, src == 1 ? "archive" : src == 2 ? "disk" : "nowhere", buf ? (char *)buf : "");
        expect(src == 2 && buf && !strcmp((char *)buf, reads[i].want), reads[i].what);
        free(buf);
    }
    void **count = fake_gdb_lookup("audio.audio_bundle_settings._audio_bundle_settings_instancecount_");
    void **name = fake_gdb_lookup("audio.audio_bundle_settings.audio_bundle_settings_4.bundle_name");
    expect(count && *count && *(int32_t *)((uint8_t *)*count + 8) == 5 && name && *name &&
               !strcmp(*(char **)((uint8_t *)*name + 8), "t_v10"),
           "files: the engine sounds' bundle settings rows went into the database inside the load");
}

int main(int argc, char **argv)
{
    const char *mode = argc > 1 ? argv[1] : "none";
    rm_init((RMap *)(fake_alias_obj + 8), 64);
    rm_insert((RMap *)(fake_alias_obj + 8), fnv64("2025_asset_groups/swofty/aliased.erp"), 1);
    int real = !strcmp(mode, "real");
    int files = !strcmp(mode, "files");
    if (files)
        gdb_slots = 1 << 14;
    int gated = !strcmp(mode, "gated");
    int early = !strcmp(mode, "early") || real || files, late = !strcmp(mode, "late") || gated, replace = !strcmp(mode, "replace");
    int on = (early || late) && !real;
    setvbuf(stdout, NULL, _IONBF, 0);
    GetModuleFileNameW(NULL, game_dir_w, MAX_PATH);
    *wcsrchr(game_dir_w, L'\\') = 0;
    for (int i = 0; i < 16; i++)
        fa_vt[i] = (void *)fake_vt_stub;
    fa_vt[3] = (void *)fa_alloc;
    fa_vt[7] = (void *)fa_free;
    void **classes[] = {vt_gint, vt_gfloat, vt_gstr, vt_gbool, vt_gother};
    for (int c = 0; c < 5; c++)
        classes[c][0] = (void *)fake_vt_stub, classes[c][1] = (void *)gdb_dtor;
    for (int i = 0; i < 20; i++)
        fake_memstream_vt[i] = (void *)fake_vt_stub;
    fake_memstream_vt[6] = (void *)ms_read;
    fake_memstream_vt[13] = (void *)ms_reset;
    vt_langset[1] = (void *)set_find;
    *(uint8_t **)(mount_nefs + 0x18) = (uint8_t *)"/game/";
    *(uint8_t **)(mount_raw + 0x18) = (uint8_t *)"/game/";
    mount_nefs[0x131] = 1;
    *(uint8_t **)(mount_nefs + 0x140) = mount_raw;
    *(uint8_t **)(fake_fs + 0xf8) = mount_nefs;
    strcpy((char *)fake_fs + 0x370, "/game/");
    rm_init(&resmap, 1 << 16);
    uint8_t *lng = NULL;
    if (read_disk("archive_language_eng.lng", &lng, &archive_lng_size))
        archive_lng = (const char *)lng;
    expect(archive_lng != NULL, "fake archive language file present");

    if (!strcmp(mode, "earlywait"))
        early_wait_mode();
    if (!strcmp(mode, "secondcopy"))
        second_copy_mode();
    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    void *real_mutex = (void *)GetProcAddress(k32, "CreateMutexW");
    extern void *__imp_CreateMutexW;
    expect(early ? __imp_CreateMutexW != real_mutex : __imp_CreateMutexW == real_mutex,
           early ? "early stage on: KERNEL32!CreateMutexW IAT slot armed" : "early stage off: CreateMutexW slot untouched");
    HANDLE other = fake_create_mutex(L"Global\\SomethingElse");
    expect(other != NULL && (!early || __imp_CreateMutexW != real_mutex),
           "a CreateMutexW with another name does not fire the early stage");
    HMODULE core_before = GetModuleHandleW(L"swofty_core.dll");
    SetLastError(12345);
    HANDLE mx = fake_create_mutex(L"Codemasters F1 25");
    DWORD e1 = GetLastError();
    HMODULE core_after = GetModuleHandleW(L"swofty_core.dll");
    expect(mx && e1 == 0, "the game's single-instance mutex is created and GetLastError() is the real call's (0)");
    expect(early ? !core_before && core_after != NULL : !core_after,
           early ? "mods loaded inside CreateMutexW('Codemasters F1 25')" : "mods not loaded at the mutex");
    expect(__imp_CreateMutexW == real_mutex, "CreateMutexW IAT slot pristine after the early stage");
    HANDLE mx2 = fake_create_mutex(L"Codemasters F1 25");
    expect(mx2 && GetLastError() == ERROR_ALREADY_EXISTS, "second CreateMutexW reports ERROR_ALREADY_EXISTS (0xb7)");

    static uint8_t db_storage[0x100];
    void *dbr = fake_gdb_load(db_storage, NULL);
    expect(dbr == db_storage && db_storage[0xf0] == 1, "boot step 10: Gdb_Load_GameDatabase built the database");

    WNDCLASSEXW wc = {.cbSize = sizeof wc, .lpfnWndProc = DefWindowProcW, .hInstance = GetModuleHandleW(NULL),
                      .lpszClassName = L"EGO_WindowClass"};
    ATOM(WINAPI * rc)(const WNDCLASSEXW *) = (void *)__imp_RegisterClassExW;
    rc(&wc);
    expect(GetModuleHandleW(L"swofty_core.dll") != NULL, "mods loaded by the window trigger at the latest");

    uint64_t mres = fake_parse_manifest(fake_manifest, NULL, NULL);
    g_fake_manifest = fake_manifest;
    expect(mres == 0, "boot step 24: manifest parsed");
    if (files) {
        files_mode();
        printf("%s (%d failure(s))\n", fails ? "FAILED" : "ALL PASSED", fails);
        return fails != 0;
    }

    static uint8_t module[0x200];
    static LangSet sets[0x13];
    for (int i = 0; i < 0x13; i++)
        sets[i].vt = vt_langset, *(LangSet **)(module + 0x28 + i * 0x10) = &sets[i];
    static uint8_t lng_path[0x120];
    *(char **)(lng_path + 0x10) = (char *)lng_path + 0x18;
    strcpy((char *)lng_path + 0x18, "localisation/2025_release/language_eng.lng");
    int32_t lr = fake_loc_load(module, 0, lng_path);
    expect(lr == 0 && parse_failures == 0, "postConnect: language file loaded and every table parsed");
    const char *ps = set_find(&sets[0], "lng_press_start", 0), *car = set_find(&sets[0], "lng_mode_career", 0);
    printf("      lng_press_start='%s' lng_mode_career='%s'\n", ps ? ps : "(none)", car ? car : "(none)");
    if (real)
        expect(ps && !strcmp(ps, "SWOFTY RUNTIME STRINGS - PRESS ANY BUTTON") && car && !strcmp(car, "CAREER (SWOFTYMOD)"),
               "real stage: both '*' strings override the stock table");
    else if (on) {
        expect(ps && !strcmp(ps, "SWOFTY STRINGS OK"), "lang: the mod's string table is searched before the stock one");
        expect(car && !strcmp(car, "Career"), "lang: keys the mod does not set fall through to the stock table");
        const char *tab = set_find(&sets[0], "lng_table_only", 0);
        expect(tab && !strcmp(tab, "FROM TABLE"), "lang: a valid mod LNGT table file passes validation and is searched");
    } else if (replace) {
        expect(ps && !strcmp(ps, "REPLACED PRESS") && car && !strcmp(car, "REPLACED CAREER"),
               "lang replace: the whole language file came from swofty/lang/ through the fs overlay");
    } else
        expect(ps && !strcmp(ps, "PRESS ANY BUTTON") && car && !strcmp(car, "Career"), "lang: stock strings");

    uint8_t *cfg = NULL;
    size_t cfg_size = 0;
    int src = fs_read("system/config.xml", &cfg, &cfg_size);
    printf("      system/config.xml from %s: '%s'\n", src == 1 ? "archive" : src == 2 ? "loose" : "nowhere", cfg ? (char *)cfg : "");
    if (real)
        expect(src == 1, "real stage: no overlay folder, the archive copy wins");
    else if (on && !gated)
        expect(src == 2 && cfg && !strcmp((char *)cfg, "<swofty overlay config/>"),
               "overlay: an archive path is served from swofty/overlay/ (NeFS probe misses, re-resolve opens the loose file)");
    else
        expect(src == 1 && cfg && !strcmp((char *)cfg, ARCHIVE_CONFIG), "overlay off: the archive copy wins");
    uint8_t out[0x120];
    void *wm = fake_fs_resolve(fake_fs, NULL, "system/config.xml", out, 1, 0);
    expect(wm == mount_nefs && wild_redirected == 0, "overlay: wildcard (enumeration) resolves are not redirected");
    uint8_t *loose = NULL;
    size_t loose_size = 0;
    expect(fs_read("/game/2025_asset_groups/ui_package/flow_boot.erp", &loose, &loose_size) == 2,
           "overlay: other loose paths are untouched");

    uint64_t boot_hash = fnv64("2025_asset_groups/ui_package/flow_boot.erp");
    const char *logo_uri = "eaid://ui_package/art/textures/press_start/ps_logo.tif.image";
    const char *added_uri = "eaid://swofty/runtime_test/added_white.tif.image";
    if (on) {
        load_group("asset_groups/ui_package/screens/splash.assetgroup");
        uint8_t *l1 = res_of(logo_uri);
        printf("      after splash: ps_logo from '%s' refs %d data '%.8s', added %p\n", src_name(l1), l1 ? *(int32_t *)(l1 + 0x1c) : -1,
               l1 ? (char *)l1 + 0x48 : "", (void *)res_of(added_uri));
        expect(l1 && is_overlay(*(uint64_t *)(l1 + 0x40)) && !memcmp(l1 + 0x48, "SWOFTYPX", 8) && *(int32_t *)(l1 + 0x1c) == 2,
               "resources: the splash group's own overlay created ps_logo with the later declaration's bytes, splash.erp bound (refs 2)");
        expect(!res_of(added_uri), "resources: the splash group's overlay does not carry the uri only flow_boot.erp lists");
        uint64_t list[1] = {boot_hash}, handle[5] = {0x5157f7aa5157f7aaull, 1, 0, (uint64_t)list, (uint64_t)(list + 1)};
        ((uint8_t *)handle)[0xc] = 4;
        fake_expand(fake_loader, handle, NULL);
        uint8_t *a1 = res_of(added_uri);
        expect(a1 && is_overlay(*(uint64_t *)(a1 + 0x40)) && *(int32_t *)(a1 + 0x1c) == 1,
               "resources: a flag-4 group listing flow_boot.erp in its own list read flow_boot's overlay first (added uri created)");
        fake_release(fake_loader, handle, NULL);
        expect(nreleased == 2 && is_overlay(released[0]) && released[1] == boot_hash,
               "resources: release of the flag-4 group sees the same augmented list (overlay, flow_boot.erp)");
        expect(list[0] == boot_hash && handle[3] == (uint64_t)list, "resources: the game's own handle and list are not modified");
    }
    load_group("asset_groups/ui_package/flow_boot.assetgroup");
    load_group("asset_groups/ui_package/screens/splash.assetgroup");
    load_group("asset_groups/ui_package/other.assetgroup");
    uint8_t *logo = res_of(logo_uri);
    uint8_t *white = res_of("eaid://ui_package/art/textures/small_white.tif.image");
    uint8_t *added = res_of(added_uri);
    printf("      ps_logo source '%s' refs %d data '%.8s'; small_white source 0x%016llx; added %p; zstd fragments %d\n",
           src_name(logo), logo ? *(int32_t *)(logo + 0x1c) : -1, logo ? (char *)logo + 0x48 : "",
           white ? (unsigned long long)*(uint64_t *)(white + 0x40) : 0ull, (void *)added, zstd_frags);
    uint8_t *mag = NULL;
    size_t mag_size = 0;
    if (real) {
        expect(read_disk("swofty/res/ps_logo.1.temp", &mag, &mag_size) && mag_size == 176416, "real stage: magenta fragment present");
        expect(logo && mag && is_overlay(*(uint64_t *)(logo + 0x40)) && !memcmp(logo + 0x48, mag, 8),
               "real stage: ps_logo created from an overlay with the magenta BC7 bytes (zstd-decompressed)");
        expect(zstd_frags >= 1, "real stage: the overlay stores the 176 KB pixel fragment as zstd (0x11)");
        expect(logo && *(int32_t *)(logo + 0x1c) >= 3, "real stage: the real flow_boot.erp and splash.erp bound to it");
        expect(added && is_overlay(*(uint64_t *)(added + 0x40)), "real stage: added uri created by an overlay");
    } else if (on) {
        expect(logo && is_overlay(*(uint64_t *)(logo + 0x40)) && !memcmp(logo + 0x48, "SWOFTYPX", 8),
               "resources: ps_logo still holds the overlay's bytes after every group loaded");
        expect(logo && *(int32_t *)(logo + 0x1c) == 4, "resources: both overlays and both stock ERPs share one copy (refs 4)");
        expect(zstd_frags >= 1, "resources: the 4 KB pixel fragment was stored as zstd 0x11 and decompressed by the loader");
        uint64_t *g = rm_find((RMap *)(fake_manifest + 0x10), fnv64("asset_groups/ui_package/other.assetgroup"));
        expect(g && (uint32_t)(g[1] >> 32) == 1, "resources: a group without target ERPs keeps its range");
        uint64_t *gs = rm_find((RMap *)(fake_manifest + 0x10), fnv64("asset_groups/ui_package/screens/splash.assetgroup"));
        expect(gs && (uint32_t)(gs[1] >> 32) == 2, "resources: splash group = its own overlay + splash.erp");
        load_group("asset_groups/ui_package/prebuilt.assetgroup");
        uint8_t *pb = res_of("eaid://ui_package/prebuilt/stock.bin"), *pa = res_of("eaid://ui_package/prebuilt/new.bin");
        expect(pb && !strcmp(src_name(pb), "swofty/assets/resources/0.erp") && !memcmp(pb + 0x48, "PREBUILT", 8),
               "resources: a launcher-built overlay ERP replaced a stock uri before its target ERP");
        expect(pa && !strcmp(src_name(pa), "swofty/assets/resources/0.erp") && !memcmp(pa + 0x48, "PREADDED", 8),
               "resources: a launcher-built overlay ERP added a uri with the target's group");
        uint64_t *rg = rm_find((RMap *)(fake_manifest + 0x10), fnv64("asset_groups/swofty/rt_test.assetgroup"));
        load_group("asset_groups/swofty/rt_test.assetgroup");
        uint8_t *rt = res_of("eaid://swofty/rt_test/thing.bin");
        expect(rg && rt && *(uint64_t *)(rt + 0x40) == fnv64("2025_asset_groups/swofty/rt_test.erp"),
               "asset groups: a registered group loads its own ERP (T007 shape)");
        expect(!rm_find((RMap *)(fake_manifest + 0x10), fnv64("asset_groups/swofty/aliased.assetgroup")),
               "asset groups: an ERP hash that has an alias name is not re-registered");
    } else
        expect(logo && *(uint64_t *)(logo + 0x40) == boot_hash && !memcmp(logo + 0x48, "STOCKPIX", 8) && !added,
               "resources off: ps_logo comes from flow_boot.erp");
    expect(white && *(uint64_t *)(white + 0x40) == boot_hash, "resources: untouched uris still come from the stock ERP");
    if (real) {
        printf("%s (%d failure(s))\n", fails ? "FAILED" : "ALL PASSED", fails);
        return fails != 0;
    }

    void **v = fake_gdb_lookup("ai.general_ai_settings.max_test_time");
    float mtt = v && *v ? *(float *)((uint8_t *)*v + 8) : -1;
    void **m1 = fake_gdb_lookup("swofty.runtime_test.marker"), **m2 = fake_gdb_lookup("swofty.runtime_test.answer");
    void **rt = fake_gdb_lookup("fake.retype_me"), **kp = fake_gdb_lookup("fake.keep"), **fl = fake_gdb_lookup("fake.flag");
    void **big = fake_gdb_lookup("swofty.runtime_test.big"), **u64 = fake_gdb_lookup("swofty.runtime_test.u64");
    void **u64neg = fake_gdb_lookup("swofty.runtime_test.u64neg");
    void **tb = fake_gdb_lookup("swofty.runtime_test.toobig"), **fv = fake_gdb_lookup("swofty.runtime_test.f");
    printf("      db: max_test_time=%g marker=%s answer=%d retype_me vt=%s keep=%d flag=%d dtor calls %d\n", mtt,
           m1 && *m1 ? *(char **)((uint8_t *)*m1 + 8) : "(none)", m2 && *m2 ? *(int32_t *)((uint8_t *)*m2 + 8) : -1,
           rt && *rt ? (*(void ***)*rt == vt_gstr ? "string" : *(void ***)*rt == vt_gint ? "int" : "?") : "(none)",
           kp && *kp ? *(int32_t *)((uint8_t *)*kp + 8) : -1, fl && *fl ? ((uint8_t *)*fl)[8] : -1, gdb_dtor_calls);
    if (early) {
        expect(mtt == 301.0f, "db: existing float overwritten (300 -> 301)");
        expect(fl && *fl && ((uint8_t *)*fl)[8] == 1, "db: existing bool overwritten by an edit without a type (False -> True)");
    } else
        expect(mtt == 300.0f && fl && *fl && ((uint8_t *)*fl)[8] == 0,
               late ? "db late: the in-place fallback refuses an exe that is not the analysed build" : "db: untouched without edits");
    if (early) {
        expect(m1 && *m1 && *(void ***)*m1 == vt_gstr && !strcmp(*(char **)((uint8_t *)*m1 + 8), "hello from swofty") &&
                   *(uint32_t *)((uint8_t *)*m1 + 0x30) == fnv32("hello from swofty"),
               "db: new string key created through the game's insert");
        expect(m2 && *m2 && *(void ***)*m2 == vt_gint && *(int32_t *)((uint8_t *)*m2 + 8) == 42, "db: new int key created");
        expect(rt && *rt && *(void ***)*rt == vt_gstr && !strcmp(*(char **)((uint8_t *)*rt + 8), "five") && gdb_dtor_calls == 1,
               "db: retype with \"retype\": true destroyed the int record and inserted a string record");
        expect(big && *big && *(int32_t *)((uint8_t *)*big + 8) == 1234567890, "db: a 10-digit int keeps its exact text (1234567890)");
        expect(u64 && *u64 && *(uint64_t *)((uint8_t *)*u64 + 8) == 12345678901234567ull,
               "db: a 17-digit t12 value keeps its exact text (12345678901234567)");
        expect(u64neg && *u64neg && *(uint64_t *)((uint8_t *)*u64neg + 8) == UINT64_MAX,
               "db: a negative t12 value is created and wraps like strtoull (-1 -> 2^64 - 1)");
        expect(fv && *fv && *(float *)((uint8_t *)*fv + 8) == 0.1f, "db: a float keeps its exact text (0.1)");
        expect(!tb, "db: an int outside the 32-bit range is refused, not wrapped");
    } else if (late) {
        expect(!m1 && !m2, "db late: new keys are not inserted after boot");
        expect(rt && *rt && *(void ***)*rt == vt_gint && *(int32_t *)((uint8_t *)*rt + 8) == 5 && gdb_dtor_calls == 0,
               "db late: no retype after boot");
    }
    expect(kp && *kp && *(void ***)*kp == vt_gint && *(int32_t *)((uint8_t *)*kp + 8) == 7,
           "db: an int key edited as a string without \"retype\" keeps its int record");
    printf("%s (%d failure(s))\n", fails ? "FAILED" : "ALL PASSED", fails);
    return fails != 0;
}
