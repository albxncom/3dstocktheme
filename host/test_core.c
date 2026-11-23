/* Unit tests of the core: SHA-256 vectors, the LZ11 check, the index rule, text, TMD and delta rules, and every
 * failure path of a theme and a catalogue (allocation, source and sink failing at each call in turn), plus a
 * mutation run over theme zips; with a theme installer CIA, its reader the same way.
 * Run: test_core ZIP_WITH_BGM ZIP_WITHOUT_BGM [THEME_CIA] (the fixtures of fixtures.py; the CIA from test_host.py) */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "internal.h"

static int failures, checks;

#define CHECK(cond) do { checks++; if (!(cond)) { failures++; fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_EQ(a, b) do { long long a_ = (long long)(a), b_ = (long long)(b); checks++; if (a_ != b_) { failures++; fprintf(stderr, "%s:%d: %s = %lld, want %lld\n", __FILE__, __LINE__, #a, a_, b_); } } while (0)

/* ---------------------------------------------------------------- instrumented memory, source and sink */

static size_t live, allocs, fail_alloc_at = (size_t)-1;

static void *t_alloc(void *ctx, size_t size)
{
    (void)ctx;
    if (allocs++ == fail_alloc_at)
        return NULL;
    void *p = malloc(size);
    if (p)
        live++;
    return p;
}

static void t_free(void *ctx, void *p)
{
    (void)ctx;
    live--;
    free(p);
}

typedef struct mem_src {
    const uint8_t *p;
    size_t reads, fail_at;
} mem_src;

static int mem_read(void *ctx, uint64_t offset, void *buf, size_t len)
{
    mem_src *m = ctx;
    if (m->reads++ == m->fail_at)
        return -1;
    memcpy(buf, m->p + offset, len);
    return 0;
}

static tp_source source_of(mem_src *m, const uint8_t *p, size_t size)
{
    m->p = p;
    m->reads = 0;
    m->fail_at = (size_t)-1;
    return (tp_source){m, size, mem_read};
}

typedef struct mem_sink {
    sha256_ctx sha;
    uint64_t size;
    size_t writes, fail_at;
    uint8_t *keep;              /* optional copy */
    size_t keep_cap;
} mem_sink;

static int sink_write(void *ctx, const void *buf, size_t len)
{
    mem_sink *s = ctx;
    if (s->writes++ == s->fail_at)
        return -1;
    if (len > CHUNK)
        return -1;              /* the API promises pieces of at most 16 KiB */
    if (s->keep && s->size + len <= s->keep_cap)
        memcpy(s->keep + s->size, buf, len);
    sha256_update(&s->sha, buf, len);
    s->size += len;
    return 0;
}

static tp_sink sink_of(mem_sink *s)
{
    memset(s, 0, sizeof *s);
    sha256_init(&s->sha);
    s->fail_at = (size_t)-1;
    return (tp_sink){s, sink_write};
}

static uint8_t *slurp(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "cannot open %s\n", path);
        exit(2);
    }
    fseek(f, 0, SEEK_END);
    *size = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *p = malloc(*size);
    if (!p || fread(p, 1, *size, f) != *size)
        exit(2);
    fclose(f);
    return p;
}

/* A stored (method 0) zip of the given members, for tests that need to control the bytes. */
typedef struct member {
    const char *name;
    const uint8_t *data;
    size_t size;
} member;

static size_t stored_zip(uint8_t *out, const member *m, size_t n)
{
    size_t at = 0, local[8];
    for (size_t i = 0; i < n; i++) {
        size_t len = strlen(m[i].name);
        uint32_t crc = (uint32_t)crc32(0, m[i].data, (uInt)m[i].size);
        local[i] = at;
        uint8_t *h = out + at;
        memset(h, 0, 30);
        put_le32(h, 0x04034b50);
        put_le16(h + 4, 10);
        put_le32(h + 14, crc);
        put_le32(h + 18, (uint32_t)m[i].size);
        put_le32(h + 22, (uint32_t)m[i].size);
        put_le16(h + 26, (uint16_t)len);
        memcpy(h + 30, m[i].name, len);
        memcpy(h + 30 + len, m[i].data, m[i].size);
        at += 30 + len + m[i].size;
    }
    size_t directory = at;
    for (size_t i = 0; i < n; i++) {
        size_t len = strlen(m[i].name);
        uint8_t *h = out + at;
        memset(h, 0, 46);
        put_le32(h, 0x02014b50);
        put_le16(h + 4, 20);
        put_le16(h + 6, 10);
        put_le32(h + 16, (uint32_t)crc32(0, m[i].data, (uInt)m[i].size));
        put_le32(h + 20, (uint32_t)m[i].size);
        put_le32(h + 24, (uint32_t)m[i].size);
        put_le16(h + 28, (uint16_t)len);
        put_le32(h + 42, (uint32_t)local[i]);
        memcpy(h + 46, m[i].name, len);
        at += 46 + len;
    }
    uint8_t *e = out + at;
    memset(e, 0, 22);
    put_le32(e, 0x06054b50);
    put_le16(e + 8, (uint16_t)n);
    put_le16(e + 10, (uint16_t)n);
    put_le32(e + 12, (uint32_t)(at - directory));
    put_le32(e + 16, (uint32_t)directory);
    return at + 22;
}

static size_t make_tmd(uint8_t *out, size_t cap, const uint16_t *indices, size_t n, uint16_t version);
static void rehash(uint8_t *tmd, size_t size);

/* ---------------------------------------------------------------- SHA-256 */

static void hexeq(const uint8_t d[32], const char *want)
{
    char got[65];
    for (int i = 0; i < 32; i++)
        snprintf(got + 2 * i, 3, "%02x", d[i]);
    checks++;
    if (strcmp(got, want)) {
        failures++;
        fprintf(stderr, "sha256 %s, want %s\n", got, want);
    }
}

static void test_sha256(void)
{
    /* FIPS 180-2 examples (checked against Python's hashlib when this file was written) */
    static const struct { const char *msg; const char *digest; } v[] = {
        {"", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
        {"abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
        {"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
         "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"},
        {"abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrst"
         "nopqrstu", "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1"},
    };
    uint8_t d[32];
    for (size_t i = 0; i < sizeof v / sizeof *v; i++) {
        sha256(v[i].msg, strlen(v[i].msg), d);
        hexeq(d, v[i].digest);
    }
    static uint8_t a[1000000];
    memset(a, 'a', sizeof a);
    static const size_t steps[] = {1, 55, 56, 63, 64, 65, 4096, 99991};
    for (size_t s = 0; s < sizeof steps / sizeof *steps; s++) {
        sha256_ctx c;
        sha256_init(&c);
        for (size_t at = 0; at < sizeof a; at += steps[s])
            sha256_update(&c, a + at, sizeof a - at < steps[s] ? sizeof a - at : steps[s]);
        sha256_final(&c, d);
        hexeq(d, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    }
}

/* ---------------------------------------------------------------- LZ11 */

static int lz(const uint8_t *p, size_t n, int bytewise, uint8_t *byte5)
{
    lz11 z;
    lz11_init(&z);
    if (bytewise)
        for (size_t i = 0; i < n; i++)
            lz11_feed(&z, p + i, 1);
    else
        lz11_feed(&z, p, n);
    if (byte5)
        *byte5 = z.head[5];
    return lz11_valid(&z);
}

static void test_lz11(void)
{
    static const uint8_t literals[] = {0x11, 8, 0, 0, 0x00, 'a', 'b', 'c', 'd', 'e', 'F', 'g', 'h'};
    static const uint8_t copy2[] = {0x11, 10, 0, 0, 0x20, 'A', 'B', 0x70, 0x01};          /* AB + 8 from 2 back */
    static const uint8_t copy3[] = {0x11, 0x12, 0, 0, 0x40, 'A', 0x00, 0x00, 0x00};       /* A + 0x11 from 1 back */
    static const uint8_t copy4[] = {0x11, 0x12, 0x01, 0, 0x40, 'A', 0x10, 0x00, 0x00, 0x00}; /* A + 0x111 */
    static const uint8_t extended[] = {0x11, 0, 0, 0, 8, 0, 0, 0, 0x00, 1, 2, 3, 4, 5, 6, 7, 8};
    static const uint8_t trailing[] = {0x11, 8, 0, 0, 0x00, 1, 2, 3, 4, 5, 6, 7, 8, 0, 0, 0};
    static const uint8_t not_lz[] = {0x10, 8, 0, 0, 0x00, 1, 2, 3, 4, 5, 6, 7, 8};
    static const uint8_t too_small[] = {0x11, 5, 0, 0, 0x00, 1, 2, 3, 4, 5};
    static const uint8_t truncated[] = {0x11, 8, 0, 0, 0x00, 1, 2, 3, 4};
    static const uint8_t before_start[] = {0x11, 8, 0, 0, 0x40, 'A', 0x70, 0x01};         /* 2 back after 1 byte */
    static const uint8_t overrun[] = {0x11, 9, 0, 0, 0x20, 'A', 'B', 0x70, 0x01};         /* 10 bytes for 9 */
    static const uint8_t zero_size[] = {0x11, 0, 0, 0, 0, 0, 0, 0, 0x00, 1};
    uint8_t b5;
    for (int bytewise = 0; bytewise < 2; bytewise++) {
        CHECK(lz(literals, sizeof literals, bytewise, &b5) && b5 == 'F');
        CHECK(lz(copy2, sizeof copy2, bytewise, &b5) && b5 == 'B');
        CHECK(lz(copy3, sizeof copy3, bytewise, &b5) && b5 == 'A');
        CHECK(lz(copy4, sizeof copy4, bytewise, NULL));
        CHECK(lz(extended, sizeof extended, bytewise, &b5) && b5 == 6);
        CHECK(lz(trailing, sizeof trailing, bytewise, NULL));
        CHECK(!lz(not_lz, sizeof not_lz, bytewise, NULL));
        CHECK(!lz(too_small, sizeof too_small, bytewise, NULL));
        CHECK(!lz(truncated, sizeof truncated, bytewise, NULL));
        CHECK(!lz(before_start, sizeof before_start, bytewise, NULL));
        CHECK(!lz(overrun, sizeof overrun, bytewise, NULL));
        CHECK(!lz(zero_size, sizeof zero_size, bytewise, NULL));
        CHECK(!lz(copy4, sizeof copy4 - 1, bytewise, NULL));
        CHECK(!lz(literals, 3, bytewise, NULL));
    }
}

/* ---------------------------------------------------------------- index rule and text */

static void test_index(void)
{
    uint16_t i = 0, used[] = {1, 2, 3}, two[] = {1, 2}, gap[] = {1, 3}, last[] = {348}, full[] = {349}, over[] = {350};
    CHECK(tp_next_index(NULL, 0, 0, &i) == TP_OK && i == 1);
    CHECK(tp_next_index(used, 3, 0, &i) == TP_OK && i == 4);
    CHECK(tp_next_index(used, 3, 3, &i) == TP_OK && i == 4);
    CHECK(tp_next_index(used, 3, 4, &i) == TP_OK && i == 5);        /* index 4 was handed out, its theme is gone */
    CHECK(tp_next_index(two, 2, 3, &i) == TP_OK && i == 4);         /* the reviewer's case: never 3 again */
    CHECK(tp_next_index(gap, 2, 0, &i) == TP_OK && i == 4);         /* a gap is never filled */
    CHECK(tp_next_index(gap, 2, 7, &i) == TP_OK && i == 8);
    CHECK(tp_next_index(NULL, 0, 6, &i) == TP_OK && i == 7);
    CHECK(tp_next_index(last, 1, 0, &i) == TP_OK && i == 349);
    CHECK(tp_next_index(NULL, 0, 348, &i) == TP_OK && i == 349);
    CHECK_EQ(tp_next_index(full, 1, 0, &i), TP_E_INDEX);
    CHECK_EQ(tp_next_index(over, 1, 0, &i), TP_E_INDEX);
    CHECK_EQ(tp_next_index(NULL, 0, 349, &i), TP_E_INDEX);
    CHECK_EQ(tp_next_index(NULL, 0, 0xFFFF, &i), TP_E_INDEX);
}

/* Every call that takes a title, and every installed TMD, accepts 0004008C00009809 only. */
static void test_title(void)
{
    static const uint64_t others[] = {
        0x0004008C00009800ull, 0x0004008C00009804ull, 0x0004008C00009808ull,     /* official and spare variations */
        0x0004008C0000980Aull, 0x0004008C00009819ull, 0x0004008C00008F00ull,     /* not a variation, another region */
        0x0004000000055D00ull, 0ull,                                             /* a game, nothing */
    };
    CHECK_EQ(TP_TITLE_ID, 0x0004008C00009809ull);
    tp_content r[2] = {{0, 0, 0, 0x1000, {0}}, {1, 1, 0x4000, 0x1000, {0}}};
    uint8_t key[16] = {0};
    mem_sink s;
    tp_sink sink = sink_of(&s);
    tp_entry e = {1, "One", NULL};
    tp_catalogue cat = {&e, 1, 1, NULL, NULL};
    tp_catalogue empty = {NULL, 0, 1, NULL, NULL};
    tp_content rec;
    tp_delta plan;
    for (size_t k = 0; k < sizeof others / sizeof *others; k++) {
        uint64_t t = others[k];
        CHECK_EQ(tp_tmd(t, 0, r, 2, &sink), TP_E_TITLE);
        CHECK_EQ(tp_ticket(t, 0x0004000000000001ull, key, &sink), TP_E_TITLE);
        CHECK_EQ(tp_catalogue_content(&empty, t, 0, NULL, &rec), TP_E_TITLE);
        CHECK_EQ(s.size, 0);
    }
    CHECK_EQ(tp_tmd(TP_TITLE_ID, 0, r, 2, &sink), TP_OK);
    CHECK_EQ(tp_ticket(TP_TITLE_ID, 0x0004000000000001ull, key, &sink), TP_OK);
    CHECK_EQ(tp_catalogue_content(&empty, TP_TITLE_ID, 0, NULL, &rec), TP_OK);

    /* installed TMDs of another title, hash chain intact */
    static uint8_t tmd[0x1000];
    const uint16_t base[] = {0, 1};
    for (size_t k = 0; k < sizeof others / sizeof *others; k++) {
        size_t size = make_tmd(tmd, sizeof tmd, base, 2, 0);
        put_be64(tmd + 0x140 + 0x4C, others[k]);
        rehash(tmd, size);
        CHECK_EQ(tp_tmd_read(tmd, size, NULL, NULL, NULL), TP_E_TITLE);
        CHECK_EQ(tp_delta_plan(tmd, size, TP_TITLE_ID, &cat, &plan), TP_E_TITLE);
        tp_difference d;
        tp_am_content am[2] = {{0, 0, 0x1000}, {1, 1, 0x2000}};
        CHECK_EQ(tp_tmd_check(tmd, size, 0, am, 2, &d), TP_E_TITLE);
        tp_delta fake = {1, 0x10001, 1, 0, {0}};
        tp_content c0 = {0x10001, 0, 0, 0x1000, {0}};
        CHECK_EQ(tp_delta_tmd(tmd, size, &fake, &c0, NULL, &sink), TP_E_TITLE);
        size = make_tmd(tmd, sizeof tmd, base, 2, 0);
        CHECK_EQ(tp_delta_plan(tmd, size, others[k], &cat, &plan), TP_E_TITLE);
    }
}

static void test_text(void)
{
    CHECK(utf8_valid("plain") && utf8_valid("\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80"));
    CHECK(!utf8_valid("\xc3"));             /* cut */
    CHECK(!utf8_valid("\xc0\xaf"));         /* overlong */
    CHECK(!utf8_valid("\xed\xa0\x80"));     /* surrogate */
    CHECK(!utf8_valid("\xf4\x90\x80\x80")); /* above U+10FFFF */
    CHECK(!utf8_valid("\x80"));
    uint8_t f[4];
    utf8_field(f, sizeof f, "ab\xc3\xa9");  /* 4 bytes into 3: the 2-byte character does not fit */
    CHECK(!memcmp(f, "ab\0\0", 4));
    utf8_field(f, sizeof f, "a\xc3\xa9z");
    CHECK(!memcmp(f, "a\xc3\xa9\0", 4));
    utf8_field(f, sizeof f, "\xe2\x82\xac");
    CHECK(!memcmp(f, "\xe2\x82\xac\0", 4));
    utf8_field(f, sizeof f, "\xf0\x9f\x98\x80");
    CHECK(!memcmp(f, "\0\0\0\0", 4));
}

/* ---------------------------------------------------------------- TMD and delta */

static const uint64_t TID = 0x0004008C00009809ull;

static size_t make_tmd(uint8_t *out, size_t cap, const uint16_t *indices, size_t n, uint16_t version)
{
    tp_content recs[8];
    for (size_t i = 0; i < n; i++) {
        memset(&recs[i], 0, sizeof recs[i]);
        recs[i].id = indices[i];
        recs[i].index = indices[i];
        recs[i].type = indices[i] ? 0x4000 : 0;
        recs[i].size = 0x1000 * (i + 1);
        memset(recs[i].sha256, (int)(0x10 + i), 32);
    }
    mem_sink s;
    tp_sink sink = sink_of(&s);
    s.keep = out;
    s.keep_cap = cap;
    return tp_tmd(TID, version, recs, n, &sink) == TP_OK ? (size_t)s.size : 0;
}

static void rehash(uint8_t *tmd, size_t size)
{
    size_t n = (size - 0xB04) / 0x30;
    sha256(tmd + 0xB04, 0x30 * n, tmd + 0x204 + 4);
    sha256(tmd + 0x204, 0x900, tmd + 0x140 + 0xA4);
}

static int no_icon(void *ctx, uint16_t index, uint8_t icon[TP_ICON_SIZE])
{
    (void)ctx;
    memset(icon, index, TP_ICON_SIZE);
    return 0;
}

static void test_tmd_delta(void)
{
    static uint8_t tmd[0x4000], bad[0x4000], out[0x4000];
    const uint16_t base[] = {0, 1, 2};
    size_t size = make_tmd(tmd, sizeof tmd, base, 3, 0);
    CHECK_EQ(size, tp_tmd_size(3));
    uint64_t tid;
    uint16_t version;
    size_t count;
    CHECK(tp_tmd_read(tmd, size, &tid, &version, &count) == TP_OK && tid == TID && version == 0 && count == 3);

    /* malformed installed TMDs */
    memcpy(bad, tmd, size);
    bad[3] = 3;                                         /* RSA-4096 signature type */
    CHECK_EQ(tp_tmd_read(bad, size, NULL, NULL, NULL), TP_E_TMD);
    CHECK_EQ(tp_tmd_read(tmd, size - 1, NULL, NULL, NULL), TP_E_TMD);
    CHECK_EQ(tp_tmd_read(tmd, 0x100, NULL, NULL, NULL), TP_E_TMD);
    memcpy(bad, tmd, size);
    bad[0xB04 + 0x30 + 12] ^= 1;                         /* a record changed, hashes not */
    CHECK_EQ(tp_tmd_read(bad, size, NULL, NULL, NULL), TP_E_TMD);
    rehash(bad, size);
    CHECK_EQ(tp_tmd_read(bad, size, NULL, NULL, NULL), TP_OK);
    memcpy(bad, tmd, size);
    put_be16(bad + 0xB04 + 0x60 + 4, 1);                 /* indices 0, 1, 1 */
    rehash(bad, size);
    CHECK_EQ(tp_tmd_read(bad, size, NULL, NULL, NULL), TP_E_TMD);
    memcpy(bad, tmd, size);
    put_be16(bad + 0xB04 + 0x60 + 4, 350);               /* index above 349 */
    rehash(bad, size);
    CHECK_EQ(tp_tmd_read(bad, size, NULL, NULL, NULL), TP_E_TMD);
    memcpy(bad, tmd, size);
    put_be16(bad + 0xB04 + 4, 1);                        /* content 0 not first */
    put_be16(bad + 0xB04 + 0x30 + 4, 2);
    put_be16(bad + 0xB04 + 0x60 + 4, 3);
    rehash(bad, size);
    CHECK_EQ(tp_tmd_read(bad, size, NULL, NULL, NULL), TP_E_TMD);
    memcpy(bad, tmd, size);
    put_be32(bad + 0xB04 + 0x30, 0);                     /* two contents with ID 0 */
    rehash(bad, size);
    CHECK_EQ(tp_tmd_read(bad, size, NULL, NULL, NULL), TP_E_TMD);
    memcpy(bad, tmd, size);
    bad[0x140 + 0xA4] ^= 1;                              /* header hash */
    CHECK_EQ(tp_tmd_read(bad, size, NULL, NULL, NULL), TP_E_TMD);

    /* tp_tmd refuses bad record lists */
    tp_content r[3] = {{0, 0, 0, 0x1000, {0}}, {1, 1, 0x4000, 0x1000, {0}}, {2, 350, 0x4000, 0x1000, {0}}};
    mem_sink s;
    tp_sink sink = sink_of(&s);
    CHECK_EQ(tp_tmd(TID, 0, r, 3, &sink), TP_E_INDEX);
    r[2].index = 1;
    CHECK_EQ(tp_tmd(TID, 0, r, 3, &sink), TP_E_ARG);
    r[2].index = 2;
    r[2].id = 1;
    CHECK_EQ(tp_tmd(TID, 0, r, 3, &sink), TP_E_ID);
    r[2].id = 2;
    CHECK_EQ(tp_tmd(TID - 9 + 10, 0, r, 3, &sink), TP_E_TITLE); /* variation 0x0A */
    CHECK_EQ(tp_tmd(TID, 0, r + 1, 2, &sink), TP_E_ARG);        /* no content 0 */
    CHECK_EQ(tp_tmd(TID, 0, r, 3, &sink), TP_OK);

    /* the delta plan */
    tp_entry e[4] = {{1, "One", "first"}, {2, "Two", NULL}, {3, "Three", ""}, {5, "Five", NULL}};
    tp_catalogue cat = {e, 3, 3, no_icon, NULL};
    tp_delta plan;
    CHECK_EQ(tp_delta_plan(tmd, size, TID, &cat, &plan), TP_OK);
    CHECK(plan.version == 1 && plan.content0_id == 0x10001 && plan.kept == 2 && plan.count == 1 && plan.add[0] == 3);
    cat.count = 2;
    cat.top = 2;
    CHECK_EQ(tp_delta_plan(tmd, size, TID, &cat, &plan), TP_E_NOTHING);
    tp_entry gone[2] = {{1, "One", NULL}, {3, "Three", NULL}};
    tp_catalogue cat_gone = {gone, 2, 3, no_icon, NULL};
    CHECK_EQ(tp_delta_plan(tmd, size, TID, &cat_gone, &plan), TP_E_GONE);
    CHECK_EQ(tp_delta_plan(tmd, size, TID - 1, &cat, &plan), TP_E_TITLE);
    tp_catalogue cat_gap = {e, 4, 5, no_icon, NULL};               /* 4 reserved, 3 and 5 new */
    CHECK_EQ(tp_delta_plan(tmd, size, TID, &cat_gap, &plan), TP_OK);
    CHECK(plan.count == 2 && plan.add[0] == 3 && plan.add[1] == 5);

    size_t vsize = make_tmd(bad, sizeof bad, base, 3, 0xFFFF);
    cat.count = 3;
    cat.top = 3;
    CHECK_EQ(tp_delta_plan(bad, vsize, TID, &cat, &plan), TP_E_VERSION);
    const uint16_t idbase[] = {0, 1, 2};
    vsize = make_tmd(bad, sizeof bad, idbase, 3, 0);
    put_be32(bad + 0xB04 + 0x60, 0x10001);               /* an installed content already has the new ID */
    rehash(bad, vsize);
    CHECK_EQ(tp_delta_plan(bad, vsize, TID, &cat, &plan), TP_E_ID);

    /* catalogue checks */
    tp_entry dup[2] = {{1, "A", NULL}, {1, "B", NULL}};
    tp_catalogue c_dup = {dup, 2, 1, no_icon, NULL};
    CHECK_EQ(tp_delta_plan(tmd, size, TID, &c_dup, &plan), TP_E_INDEX);
    tp_entry high[1] = {{4, "A", NULL}};
    tp_catalogue c_high = {high, 1, 3, no_icon, NULL};
    CHECK_EQ(tp_delta_plan(tmd, size, TID, &c_high, &plan), TP_E_INDEX);
    tp_catalogue c_top = {e, 3, 350, no_icon, NULL};
    CHECK_EQ(tp_delta_plan(tmd, size, TID, &c_top, &plan), TP_E_INDEX);
    tp_entry utf[1] = {{1, "\xff", NULL}};
    tp_catalogue c_utf = {utf, 1, 1, no_icon, NULL};
    CHECK_EQ(tp_delta_plan(tmd, size, TID, &c_utf, &plan), TP_E_ARG);
    tp_tmd_record(tmd, size, 0, &r[0]);

    /* the delta TMD: installed records unchanged, content 0 replaced, the new record added */
    CHECK_EQ(tp_delta_plan(tmd, size, TID, &cat_gap, &plan), TP_OK);
    tp_content c0 = {plan.content0_id, 0, 0, 0x5000, {0}}, add[2] = {{3, 3, 0x4000, 0x2000, {3}}, {5, 5, 0x4000, 0x3000, {5}}};
    sink = sink_of(&s);
    s.keep = out;
    s.keep_cap = sizeof out;
    CHECK_EQ(tp_delta_tmd(tmd, size, &plan, &c0, add, &sink), TP_OK);
    CHECK_EQ(s.size, tp_delta_tmd_size(&plan, 3));
    CHECK(tp_tmd_read(out, (size_t)s.size, &tid, &version, &count) == TP_OK && version == 1 && count == 5);
    CHECK(!memcmp(out, tmd, 0x140 + 0x9C));                                /* signature and header kept */
    CHECK(!memcmp(out + 0xB04 + 0x30, tmd + 0xB04 + 0x30, 0x60));         /* records 1 and 2 byte for byte */
    tp_content got;
    tp_tmd_record(out, (size_t)s.size, 0, &got);
    CHECK(got.id == 0x10001 && got.index == 0);
    tp_tmd_record(out, (size_t)s.size, 3, &got);
    CHECK(got.index == 3 && got.id == 3);
    tp_tmd_record(out, (size_t)s.size, 4, &got);
    CHECK(got.index == 5 && got.size == 0x3000);
    add[0].index = 4;
    CHECK_EQ(tp_delta_tmd(tmd, size, &plan, &c0, add, &sink), TP_E_ARG);
}

/* ---------------------------------------------------------------- a theme and a catalogue, every failure path */

typedef struct run {
    int open, measure, write, catalogue;
} run;

static int icon_of(void *ctx, uint16_t index, uint8_t icon[TP_ICON_SIZE])
{
    (void)index;
    memcpy(icon, ((tp_theme *)ctx)->icon, TP_ICON_SIZE);
    return 0;
}

/* Opens, measures and writes one theme and a catalogue with it; returns the first failing result. */
static int full_run(const uint8_t *zip, size_t size, size_t fail_read, size_t fail_write, run *res)
{
    mem_src m;
    tp_source src = source_of(&m, zip, size);
    m.fail_at = fail_read;
    tp_theme t;
    tp_content rec, rec2;
    mem_sink s;
    tp_sink sink = sink_of(&s);
    s.fail_at = fail_write;
    memset(res, 0, sizeof *res);
    int r = res->open = tp_theme_open(&t, &src, "fallback");
    if (!r)
        r = res->measure = tp_theme_content(&t, TID, 7, NULL, &rec);
    if (!r) {
        r = res->write = tp_theme_content(&t, TID, 7, &sink, &rec2);
        if (!r) {
            uint8_t d[32];
            sha256_final(&s.sha, d);
            CHECK(rec2.size == rec.size && s.size == rec.size && !memcmp(d, rec.sha256, 32));
            CHECK(!memcmp(rec2.sha256, rec.sha256, 32));
        }
    }
    if (!r) {
        tp_entry e = {7, t.name, t.description};
        tp_catalogue cat = {&e, 1, 9, icon_of, &t};
        sink = sink_of(&s);
        s.fail_at = fail_write;
        r = res->catalogue = tp_catalogue_content(&cat, TID, 0, &sink, &rec);
        if (!r)
            CHECK(s.size == rec.size);
    }
    tp_theme_close(&t);
    tp_theme_close(&t);
    return r;
}

static void test_failure_paths(const char *path)
{
    size_t size;
    uint8_t *zip = slurp(path, &size);
    run res;
    allocs = 0;
    fail_alloc_at = (size_t)-1;
    CHECK_EQ(full_run(zip, size, (size_t)-1, (size_t)-1, &res), TP_OK);
    CHECK_EQ(live, 0);
    size_t total_allocs = allocs;
    for (size_t k = 0; k < total_allocs; k++) {
        allocs = 0;
        fail_alloc_at = k;
        CHECK_EQ(full_run(zip, size, (size_t)-1, (size_t)-1, &res), TP_E_NOMEM);
        CHECK_EQ(live, 0);
    }
    fail_alloc_at = (size_t)-1;
    mem_src probe;
    tp_source src = source_of(&probe, zip, size);
    tp_theme t;
    tp_content rec;
    tp_theme_open(&t, &src, NULL);
    tp_theme_content(&t, TID, 1, NULL, &rec);
    tp_theme_close(&t);
    size_t reads = probe.reads;
    for (size_t k = 0; k < reads + 8; k++) {
        int r = full_run(zip, size, k, (size_t)-1, &res);
        CHECK(r == TP_E_READ || (k >= reads && r == TP_OK));
        CHECK_EQ(live, 0);
    }
    for (size_t k = 0; k < 64; k++) {
        int r = full_run(zip, size, (size_t)-1, k, &res);
        CHECK(r == TP_E_WRITE || r == TP_OK);
        CHECK_EQ(live, 0);
    }
    free(zip);
}

/* tp_theme_peek: what tp_theme_open reads from the SMDH and the directory, without decoding or keeping memory. */
static void test_peek(const char *path, int with_bgm)
{
    size_t size;
    uint8_t *zip = slurp(path, &size);
    mem_src ms;
    tp_source src = source_of(&ms, zip, size);
    tp_theme open, peek;
    tp_content rec;
    allocs = 0;
    fail_alloc_at = (size_t)-1;
    CHECK_EQ(tp_theme_open(&open, &src, "fallback"), TP_OK);
    CHECK_EQ(tp_theme_peek(&peek, &src, "fallback"), TP_OK);
    CHECK_EQ(live, 1);                                  /* only the open theme's block hashes */
    CHECK(!strcmp(peek.name, open.name) && !strcmp(peek.description, open.description));
    CHECK(!memcmp(peek.icon, open.icon, TP_ICON_SIZE));
    CHECK_EQ(peek.has_bgm, with_bgm);
    CHECK_EQ(peek.has_bgm, open.has_bgm);
    CHECK_EQ(peek.body_size, open.body_size);
    CHECK_EQ(peek.bgm_size, open.bgm_size);
    CHECK_EQ(peek.use_bgm, 0);
    CHECK_EQ(peek.warnings & ~(unsigned)TP_WARN_NAME_FALLBACK, 0);
    CHECK(peek.l2 == NULL);
    CHECK_EQ(tp_theme_content(&peek, TID, 1, NULL, &rec), TP_E_ARG);
    tp_theme_close(&peek);
    tp_theme_close(&open);
    CHECK_EQ(live, 0);

    size_t reads_before = ms.reads;
    allocs = 0;
    CHECK_EQ(tp_theme_peek(&peek, &src, NULL), TP_OK);
    size_t peek_allocs = allocs, peek_reads = ms.reads - reads_before;
    CHECK(peek_allocs >= 1);
    for (size_t k = 0; k < peek_allocs; k++) {
        allocs = 0;
        fail_alloc_at = k;
        CHECK_EQ(tp_theme_peek(&peek, &src, NULL), TP_E_NOMEM);
        CHECK_EQ(live, 0);
    }
    fail_alloc_at = (size_t)-1;
    for (size_t k = 0; k < peek_reads; k++) {
        ms.reads = 0;
        ms.fail_at = k;
        CHECK_EQ(tp_theme_peek(&peek, &src, NULL), TP_E_READ);
        CHECK_EQ(live, 0);
    }
    ms.fail_at = (size_t)-1;
    CHECK_EQ(tp_theme_peek(NULL, &src, NULL), TP_E_ARG);
    CHECK_EQ(tp_theme_peek(&peek, NULL, NULL), TP_E_ARG);
    free(zip);
}

/* A body that is not LZ11 peeks fine (it is not decoded) and fails to open. */
static void test_peek_does_not_decode(void)
{
    static uint8_t body[0x40], smdh[TP_SMDH_SIZE], zip[0x4000];
    memset(body, 0x5A, sizeof body);
    memset(smdh, 0, sizeof smdh);
    memcpy(smdh, "SMDH", 4);
    for (int k = 0; k < 4; k++)
        put_le16(smdh + 8 + 2 * k, (uint16_t)"Peek"[k]);
    member m[2] = {{"body_LZ.bin", body, sizeof body}, {"info.smdh", smdh, sizeof smdh}};
    size_t n = stored_zip(zip, m, 2);
    mem_src ms;
    tp_source src = source_of(&ms, zip, n);
    tp_theme t;
    CHECK_EQ(tp_theme_peek(&t, &src, NULL), TP_OK);
    CHECK(!strcmp(t.name, "Peek"));
    CHECK_EQ(t.has_bgm, 0);
    CHECK_EQ(tp_theme_open(&t, &src, NULL), TP_E_LZ11);
    CHECK_EQ(live, 0);
    smdh[0] = 'X';
    n = stored_zip(zip, m, 2);
    src = source_of(&ms, zip, n);
    CHECK_EQ(tp_theme_peek(&t, &src, NULL), TP_E_SMDH);
    member only[1] = {{"info.smdh", smdh, sizeof smdh}};
    n = stored_zip(zip, only, 1);
    src = source_of(&ms, zip, n);
    CHECK_EQ(tp_theme_peek(&t, &src, NULL), TP_E_MISSING);
    CHECK_EQ(live, 0);
}

/* A source that gives other bytes on the second pass: a block hash differs before the member's CRC-32 is
 * reached (body over 4 KiB), or the CRC-32 differs (the change in the last block). */
static void test_changed(void)
{
    enum { GROUPS = 0x480 };            /* 9 bytes per 8 literals: 0x2880 bytes of LZ11 */
    static uint8_t body[4 + 9 * GROUPS], zip_a[sizeof body + TP_SMDH_SIZE + 256], zip_b[sizeof zip_a];
    static uint8_t smdh[TP_SMDH_SIZE];
    put_le32(body, 0x11 | (uint32_t)(8 * GROUPS) << 8);
    for (int g = 0; g < GROUPS; g++)
        for (int k = 0; k < 8; k++)
            body[4 + 9 * g + 1 + k] = (uint8_t)(g + k);
    memset(smdh, 0, sizeof smdh);
    memcpy(smdh, "SMDH", 4);
    member m[2] = {{"body_LZ.bin", body, sizeof body}, {"info.smdh", smdh, sizeof smdh}};
    size_t na = stored_zip(zip_a, m, 2);
    for (int where = 0; where < 2; where++) {
        size_t at = where ? sizeof body - 1 : 100;
        body[at] ^= 0x55;
        size_t nb = stored_zip(zip_b, m, 2);
        body[at] ^= 0x55;
        CHECK_EQ(na, nb);
        mem_src ms;
        tp_source src = source_of(&ms, zip_a, na);
        tp_theme t;
        tp_content rec;
        CHECK_EQ(tp_theme_open(&t, &src, "x"), TP_OK);
        CHECK(!strcmp(t.name, "x") && (t.warnings & TP_WARN_NAME_FALLBACK));
        ms.p = zip_b;                               /* t.src reads through ms */
        CHECK_EQ(tp_theme_content(&t, TID, 1, NULL, &rec), where ? TP_E_ZIP : TP_E_CHANGED);
        ms.p = zip_a;
        CHECK_EQ(tp_theme_content(&t, TID, 1, NULL, &rec), TP_OK);
        CHECK_EQ(tp_theme_content(&t, TID, 350, NULL, &rec), TP_E_INDEX);
        CHECK_EQ(tp_theme_content(&t, TID, 0, NULL, &rec), TP_E_INDEX);
        CHECK_EQ(tp_theme_content(&t, TID - 9, 1, NULL, &rec), TP_E_TITLE);
        CHECK_EQ(tp_theme_content(&t, TID - 5, 1, NULL, &rec), TP_E_TITLE);
        tp_theme_close(&t);
        CHECK_EQ(live, 0);
    }
}


/* ---------------------------------------------------------------- the decoded body size limit */

/* An LZ11 stream that decodes to `size` bytes: one literal, then copies from 1 back of at most 0x10110 bytes. */
static size_t long_lz11(uint8_t *out, uint32_t size, int extended)
{
    size_t at = 0, flag_at = 0;
    int tokens = 0;
    out[at++] = 0x11;
    if (extended) {
        memset(out + at, 0, 3);
        put_le32(out + at + 3, size);
        at += 7;
    } else {
        out[at++] = (uint8_t)size;
        out[at++] = (uint8_t)(size >> 8);
        out[at++] = (uint8_t)(size >> 16);
    }
    for (uint64_t left = size; left;) {
        if (tokens % 8 == 0) {
            flag_at = at;
            out[at++] = 0;
        }
        if (tokens == 0) {
            out[at++] = 'A';
            left--;
        } else {
            uint32_t n = left < 0x10110 ? (uint32_t)left : 0x10110;
            out[flag_at] |= (uint8_t)(0x80 >> (tokens % 8));
            if (n >= 0x111) {
                uint32_t v = n - 0x111;
                out[at++] = (uint8_t)(0x10 | v >> 12);
                out[at++] = (uint8_t)(v >> 4);
                out[at++] = (uint8_t)((v & 0xF) << 4);
                out[at++] = 0;
            } else if (n >= 0x11) {
                uint32_t v = n - 0x11;
                out[at++] = (uint8_t)(v >> 4);
                out[at++] = (uint8_t)((v & 0xF) << 4);
                out[at++] = 0;
            } else if (n >= 3) {
                out[at++] = (uint8_t)((n - 1) << 4);
                out[at++] = 0;
            } else {
                out[flag_at] &= (uint8_t)~(0x80 >> (tokens % 8));
                out[at++] = 'A';
                n = 1;
            }
            left -= n;
        }
        tokens++;
    }
    return at;
}

static void test_body_limit(void)
{
    static uint8_t body[0x200], smdh[TP_SMDH_SIZE], zip[0x4000];
    memset(smdh, 0, sizeof smdh);
    memcpy(smdh, "SMDH", 4);
    static const struct { uint32_t size; int extended, want; } cases[] = {
        {TP_BODY_MAX, 0, TP_OK}, {TP_BODY_MAX, 1, TP_OK}, {TP_BODY_MAX + 1, 0, TP_E_TOO_BIG},
        {TP_BODY_MAX + 1, 1, TP_E_TOO_BIG}, {0xFFFFFF, 0, TP_E_TOO_BIG}, {0xFFFFFFFFu, 1, TP_E_TOO_BIG},
        {0x1000, 0, TP_OK},
    };
    for (size_t k = 0; k < sizeof cases / sizeof *cases; k++) {
        uint32_t declared = cases[k].size;
        /* Over the limit only the header matters: the stream is built for a size that fits. */
        size_t n = long_lz11(body, declared > TP_BODY_MAX ? 0x1000 : declared, cases[k].extended);
        if (cases[k].extended)
            put_le32(body + 4, declared);
        else {
            body[1] = (uint8_t)declared;
            body[2] = (uint8_t)(declared >> 8);
            body[3] = (uint8_t)(declared >> 16);
        }
        CHECK_EQ(lz(body, n, 0, NULL), cases[k].want == TP_OK);
        member m[2] = {{"body_LZ.bin", body, n}, {"info.smdh", smdh, sizeof smdh}};
        size_t zn = stored_zip(zip, m, 2);
        mem_src ms;
        tp_source src = source_of(&ms, zip, zn);
        tp_theme t;
        CHECK_EQ(tp_theme_open(&t, &src, "big"), cases[k].want);
        tp_theme_close(&t);
    }
    CHECK_EQ(live, 0);
}

/* ---------------------------------------------------------------- the installed catalogue and the AM check */

static size_t make_archive(uint8_t *out, uint16_t top)
{
    put_le32(out, 1);
    put_le32(out + 4, top);
    for (uint32_t i = 0; i <= top; i++) {
        uint8_t *e = out + 8 + 0xC8 * i;
        memset(e, 0, 0xC8);
        snprintf((char *)e, 0x40, i ? "Theme %u \xc3\xa9" : "Dummy", i);
        snprintf((char *)e + 0x40, 0x80, "Description %u", i);
        put_le32(e + 0xC0, i);
        put_le32(e + 0xC4, i != 0);
    }
    return 8 + 0xC8 * ((size_t)top + 1);
}

static void test_installed(void)
{
    static uint8_t archive[8 + 0xC8 * (TP_INDEX_MAX + 2)], icon[TP_ICON_SIZE + 1];
    mem_src ma, mi;
    tp_installed e;
    uint16_t top;
    size_t n = make_archive(archive, 5);
    tp_source a = source_of(&ma, archive, n);
    memset(icon, 0x5A, sizeof icon);
    tp_source ic = source_of(&mi, icon, TP_ICON_SIZE);
    CHECK(tp_installed_top(&a, &top) == TP_OK && top == 5);
    CHECK_EQ(tp_installed_entry(&a, &ic, 3, &e), TP_OK);
    CHECK(e.index == 3 && e.enabled && !strcmp(e.name, "Theme 3 \xc3\xa9") && !strcmp(e.description, "Description 3"));
    CHECK(e.has_icon && e.icon[0] == 0x5A && e.icon[TP_ICON_SIZE - 1] == 0x5A);
    CHECK(tp_installed_entry(&a, NULL, 0, &e) == TP_OK && !e.enabled && !strcmp(e.name, "Dummy") && !e.has_icon);
    CHECK_EQ(tp_installed_entry(&a, NULL, 6, &e), TP_E_INDEX);
    ic.size = TP_ICON_SIZE + 1;
    CHECK_EQ(tp_installed_entry(&a, &ic, 1, &e), TP_E_CATALOGUE);
    ic.size = TP_ICON_SIZE - 1;
    CHECK_EQ(tp_installed_entry(&a, &ic, 1, &e), TP_E_CATALOGUE);
    ic.size = TP_ICON_SIZE;
    mi.fail_at = mi.reads;
    CHECK_EQ(tp_installed_entry(&a, &ic, 1, &e), TP_E_READ);
    ma.fail_at = ma.reads;
    CHECK_EQ(tp_installed_top(&a, &top), TP_E_READ);

    /* a full name field without its NUL reads as 64 bytes */
    memset(archive + 8 + 0xC8 * 2, 'N', 0x40);
    a = source_of(&ma, archive, n);
    CHECK(tp_installed_entry(&a, NULL, 2, &e) == TP_OK && strlen(e.name) == 0x40);

    /* malformed archives */
    struct { size_t at; uint32_t value; size_t size; } bad[] = {
        {0, 2, 0},                      /* version */
        {4, 0, 0},                      /* count 0 */
        {4, 350, 0},                    /* count above 349 */
        {4, 6, 0},                      /* count does not match the size */
        {8 + 0xC8 * 4 + 0xC0, 3, 0},    /* entry 4 says index 3 */
        {0, 1, 7},                      /* shorter than its header */
    };
    for (size_t k = 0; k < sizeof bad / sizeof *bad; k++) {
        n = make_archive(archive, 5);
        put_le32(archive + bad[k].at, bad[k].value);
        a = source_of(&ma, archive, bad[k].size ? bad[k].size : n);
        int r = tp_installed_top(&a, &top);
        if (r == TP_OK)
            r = tp_installed_entry(&a, NULL, 4, &e);
        CHECK_EQ(r, TP_E_CATALOGUE);
    }
    n = make_archive(archive, 5);
    archive[8 + 0xC8 * 1 + 2] = 0xFF;                   /* not UTF-8 */
    a = source_of(&ma, archive, n);
    CHECK_EQ(tp_installed_entry(&a, NULL, 1, &e), TP_E_CATALOGUE);
    n = make_archive(archive, 5);
    archive[8 + 0xC8 * 1 + 0x40] = 0xC3;                /* cut description */
    archive[8 + 0xC8 * 1 + 0x41] = 0;
    CHECK_EQ(tp_installed_entry(&a, NULL, 1, &e), TP_E_CATALOGUE);
    n = make_archive(archive, TP_INDEX_MAX);
    a = source_of(&ma, archive, n);
    CHECK(tp_installed_top(&a, &top) == TP_OK && top == TP_INDEX_MAX);
    CHECK(tp_installed_entry(&a, NULL, TP_INDEX_MAX, &e) == TP_OK && e.index == TP_INDEX_MAX);
}

static void test_tmd_check(void)
{
    static uint8_t tmd[0x2000];
    const uint16_t base[] = {0, 1, 2, 5};
    size_t size = make_tmd(tmd, sizeof tmd, base, 4, 3);
    tp_am_content am[6];
    tp_difference d;
    for (size_t k = 0; k < 4; k++) {
        tp_content rec;
        tp_tmd_record(tmd, size, k, &rec);
        am[3 - k] = (tp_am_content){rec.index, rec.id, rec.size};  /* AM's order need not be the TMD's */
    }
    CHECK(tp_tmd_check(tmd, size, 3, am, 4, &d) == TP_OK && d.what == TP_DIFF_NONE);
    CHECK(tp_tmd_check(tmd, size, 4, am, 4, &d) == TP_E_STALE && d.what == TP_DIFF_VERSION);
    tp_am_content saved = am[1];                         /* index 2 */
    am[1].id ^= 1;
    CHECK(tp_tmd_check(tmd, size, 3, am, 4, &d) == TP_E_STALE && d.what == TP_DIFF_ID && d.index == 2 && d.record == 2);
    am[1] = saved;
    am[1].size += 0x200;
    CHECK(tp_tmd_check(tmd, size, 3, am, 4, &d) == TP_E_STALE && d.what == TP_DIFF_SIZE && d.index == 2);
    am[1] = saved;
    CHECK(tp_tmd_check(tmd, size, 3, am + 1, 3, &d) == TP_E_STALE && d.what == TP_DIFF_MISSING && d.index == 5);
    am[4] = (tp_am_content){3, 3, 0x1000};
    CHECK(tp_tmd_check(tmd, size, 3, am, 5, &d) == TP_E_STALE && d.what == TP_DIFF_EXTRA && d.index == 3 &&
          d.record == 4);
    am[4] = am[2];                                        /* index 1 listed twice */
    CHECK(tp_tmd_check(tmd, size, 3, am, 5, &d) == TP_E_STALE && d.what == TP_DIFF_EXTRA && d.index == 1);
    am[4] = (tp_am_content){400, 400, 0x1000};
    CHECK(tp_tmd_check(tmd, size, 3, am, 5, &d) == TP_E_STALE && d.what == TP_DIFF_EXTRA && d.index == 400);
    CHECK_EQ(tp_tmd_check(tmd, size, 3, NULL, 0, &d), TP_E_STALE);
    CHECK_EQ(tp_tmd_check(tmd, size, 3, NULL, 1, &d), TP_E_ARG);
    CHECK_EQ(tp_tmd_check(tmd, size - 1, 3, am, 4, &d), TP_E_TMD);
}

/* ---------------------------------------------------------------- mutation run */

static uint32_t rng = 0x12345678u;
static uint32_t next_rand(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static void mutate_run(const char *label, const uint8_t *zip, size_t size, int rounds)
{
    size_t seen[TP_E_STALE + 1] = {0};
    uint8_t *buf = malloc(size);
    static const uint32_t interesting[] = {0, 1, 0x7F, 0x80, 0xFF, 0xFFFF, 0x10000, 0x36C0, 0x150001, 0x7FFFFFFF,
                                           0xFFFFFFFF};
    for (int round = 0; round < rounds; round++) {
        memcpy(buf, zip, size);
        size_t n = size;
        for (int k = 1 + (int)(next_rand() % 4); k > 0 && n > 0; k--) {
            uint32_t what = next_rand() % 4, at = next_rand() % (uint32_t)n;
            if (what == 0)
                buf[at] ^= (uint8_t)(1 << (next_rand() % 8));
            else if (what == 1)
                buf[at] = (uint8_t)next_rand();
            else if (what == 2 && at + 4 <= n)
                put_le32(buf + at, interesting[next_rand() % (sizeof interesting / sizeof *interesting)]);
            else if (what == 3 && round % 7 == 0)
                n = at;
        }
        run res;
        int r = full_run(buf, n, (size_t)-1, (size_t)-1, &res);
        CHECK(r >= TP_OK && r <= TP_E_STALE);
        CHECK_EQ(live, 0);
        if (r >= TP_OK && r <= TP_E_STALE)
            seen[r]++;
    }
    printf("mutations of %s:", label);
    for (int r = 0; r <= TP_E_STALE; r++)
        if (seen[r])
            printf(" %s %zu", tp_result_name(r), seen[r]);
    printf("\n");
    free(buf);
}

/* Mutates the members themselves and rebuilds the zip with fresh CRCs, so the LZ11 and SMDH checks see the
 * damage instead of the zip layer. */
static void mutate_members(const member *base, int rounds)
{
    static uint8_t data[3][TP_SMDH_SIZE], zip[3 * TP_SMDH_SIZE + 512];
    size_t seen[TP_E_STALE + 1] = {0};
    for (int round = 0; round < rounds; round++) {
        member m[3];
        for (int i = 0; i < 3; i++) {
            memcpy(data[i], base[i].data, base[i].size);
            m[i] = (member){base[i].name, data[i], base[i].size};
        }
        for (int k = 1 + (int)(next_rand() % 3); k > 0; k--) {
            int i = next_rand() % 4 ? 1 : (int)(next_rand() % 3);     /* mostly the body */
            if (!m[i].size)
                continue;
            uint32_t at = next_rand() % (uint32_t)m[i].size;
            if (next_rand() % 8 == 0)
                m[i].size = at;
            else
                data[i][at] = (uint8_t)next_rand();
        }
        size_t n = stored_zip(zip, m, 3);
        run res;
        int r = full_run(zip, n, (size_t)-1, (size_t)-1, &res);
        CHECK(r >= TP_OK && r <= TP_E_STALE);
        CHECK_EQ(live, 0);
        if (r >= TP_OK && r <= TP_E_STALE)
            seen[r]++;
    }
    printf("mutations of the members of a stored zip:");
    for (int r = 0; r <= TP_E_STALE; r++)
        if (seen[r])
            printf(" %s %zu", tp_result_name(r), seen[r]);
    printf("\n");
}

/* ---------------------------------------------------------------- the theme installer CIA */

static int cia_run(const uint8_t *cia, size_t size, int verify, size_t fail_read)
{
    mem_src ms;
    tp_source src = source_of(&ms, cia, size);
    ms.fail_at = fail_read;
    tp_package p;
    int r = tp_cia_open(&src, verify, &p);
    if (r == TP_OK) {
        CHECK(p.offset + p.size <= size && p.size == p.zip.size);
        uint8_t b;
        CHECK(p.zip.read(p.zip.ctx, p.size, &b, 1) != 0);       /* the window ends with the zip */
        tp_theme t;
        memset(&t, 0, sizeof t);
        r = tp_kind(&p.zip) == TP_KIND_ZIP ? tp_theme_peek(&t, &p.zip, p.location) : TP_E_ZIP;
        tp_theme_close(&t);
    }
    return r;
}

static void test_cia(const char *path)
{
    size_t size;
    uint8_t *cia = slurp(path, &size);
    mem_src ms;
    tp_source src = source_of(&ms, cia, size);
    tp_package p;
    tp_theme t;
    CHECK_EQ(tp_kind(&src), TP_KIND_CIA);
    allocs = 0;
    fail_alloc_at = (size_t)-1;
    ms.reads = 0;
    CHECK_EQ(tp_cia_open(&src, 1, &p), TP_OK);
    size_t open_allocs = allocs, open_reads = ms.reads;
    CHECK_EQ(live, 0);
    CHECK(!strcmp(p.location, "theme.zip"));
    CHECK_EQ(tp_kind(&p.zip), TP_KIND_ZIP);
    CHECK_EQ(tp_theme_open(&t, &p.zip, NULL), TP_OK);
    tp_theme_close(&t);
    CHECK_EQ(live, 0);
    for (size_t k = 0; k < open_allocs; k++) {
        allocs = 0;
        fail_alloc_at = k;
        CHECK_EQ(tp_cia_open(&src, 1, &p), TP_E_NOMEM);
        CHECK_EQ(live, 0);
    }
    fail_alloc_at = (size_t)-1;
    for (size_t k = 0; k < open_reads; k++) {
        CHECK_EQ(cia_run(cia, size, 1, k), TP_E_READ);
        CHECK_EQ(live, 0);
    }
    CHECK_EQ(tp_cia_open(NULL, 0, &p), TP_E_ARG);
    CHECK_EQ(tp_cia_open(&src, 0, NULL), TP_E_ARG);
    /* cut anywhere: refused, never read past the end */
    for (size_t n = 0; n < size; n += n < 0x6000 ? 0x11 : 0x401) {
        int r = cia_run(cia, n, n % 2, (size_t)-1);
        CHECK(r != TP_OK);
        CHECK_EQ(live, 0);
    }
    /* mutations, weighted to the headers and tables at the start */
    static const uint32_t interesting[] = {0, 1, 0x28, 0x40, 0x200, 0x2020, 0x10004, 0x7FFFFFFF, 0x80000000,
                                           0xFFFFFFFF};
    uint8_t *buf = malloc(size);
    size_t seen[TP_E_ENCRYPTED + 1] = {0};
    /* where the structures are in this CIA (it opened fine above): header, TMD, NCCH header, RomFS headers and
     * tables; most mutations go there, the rest anywhere */
    uint64_t tmd = align_up(0x2020, 0x40) + align_up(get_le32(cia + 8), 0x40) + align_up(get_le32(cia + 12), 0x40);
    uint64_t content = tmd + align_up(get_le32(cia + 16), 0x40);
    uint64_t romfs = content + (uint64_t)get_le32(cia + content + 0x1B0) * 0x200;
    const struct { uint64_t at, len; } regions[] = {
        {0, 0x40}, {tmd, get_le32(cia + 16)}, {content, 0x200}, {romfs, 0x60}, {romfs + 0x1000, 0x200},
    };
    for (int round = 0; round < 20000; round++) {
        memcpy(buf, cia, size);
        for (int k = 1 + (int)(next_rand() % 3); k > 0; k--) {
            uint32_t what = next_rand() % 3, at = (uint32_t)(next_rand() % size);
            if (next_rand() % 8) {
                size_t g = next_rand() % (sizeof regions / sizeof *regions);
                uint64_t a = regions[g].at + next_rand() % regions[g].len;
                at = a < size ? (uint32_t)a : at;
            }
            if (what == 0)
                buf[at] ^= (uint8_t)(1 << (next_rand() % 8));
            else if (what == 1)
                buf[at] = (uint8_t)next_rand();
            else if (at + 4 <= size)
                put_le32(buf + at, interesting[next_rand() % (sizeof interesting / sizeof *interesting)]);
        }
        int r = cia_run(buf, size, round % 16 == 0, (size_t)-1);
        CHECK(r >= TP_OK && r <= TP_E_ENCRYPTED);
        CHECK_EQ(live, 0);
        if (r >= TP_OK && r <= TP_E_ENCRYPTED)
            seen[r]++;
    }
    printf("mutations of the theme installer CIA:");
    for (int r = 0; r <= TP_E_ENCRYPTED; r++)
        if (seen[r])
            printf(" %s %zu", tp_result_name(r), seen[r]);
    printf("\n");
    /* the kind of other files */
    static const uint8_t zip_head[8] = {'P', 'K', 3, 4}, nothing[8] = {'<', 'h', 't', 'm', 'l', '>'};
    CHECK_EQ(tp_kind(&(tp_source){&ms, 8, mem_read}), TP_KIND_CIA);   /* ms still points at the CIA */
    ms.p = zip_head;
    CHECK_EQ(tp_kind(&(tp_source){&ms, 8, mem_read}), TP_KIND_ZIP);
    ms.p = nothing;
    CHECK_EQ(tp_kind(&(tp_source){&ms, 8, mem_read}), TP_KIND_NONE);
    CHECK_EQ(tp_kind(&(tp_source){&ms, 7, mem_read}), TP_KIND_NONE);
    CHECK_EQ(tp_kind(NULL), TP_KIND_NONE);
    free(buf);
    free(cia);
}

/* ---------------------------------------------------------------- LZ11 on whole buffers, and a theme without music */

/* in as literals only: the plainest LZ11 stream, so a test body never comes from the encoder under test. */
static size_t literal_stream(const uint8_t *in, size_t n, uint8_t *out)
{
    size_t at = 0;
    out[at++] = 0x11;
    out[at++] = (uint8_t)n;
    out[at++] = (uint8_t)(n >> 8);
    out[at++] = (uint8_t)(n >> 16);
    for (size_t i = 0; i < n; i++) {
        if (i % 8 == 0)
            out[at++] = 0;
        out[at++] = in[i];
    }
    return at;
}

/* A body-like buffer: runs of one byte, short repeats, a period longer than the window, noise. */
static void body_like(uint8_t *p, size_t n, unsigned seed)
{
    uint32_t x = seed * 2654435761u + 1;
    for (size_t i = 0; i < n; i++) {
        x = x * 1103515245u + 12345;
        size_t zone = i / 0x3000 % 5;
        p[i] = zone == 0 ? 0 : zone == 1 ? (uint8_t)(i % 7) : zone == 2 ? (uint8_t)(i % 0x1003) :
               zone == 3 ? (uint8_t)(x >> 24) : (uint8_t)((i / 64) & 0xFF);
    }
}

static void round_trip(const uint8_t *in, size_t n)
{
    size_t cap = n + n / 8 + 64, made = 0;
    uint8_t *out = malloc(cap), *back = malloc(n);
    CHECK_EQ(lz11_encode(in, n, out, cap, &made), TP_OK);
    CHECK_EQ(made % 4, 0);
    CHECK(made <= cap);
    CHECK_EQ(lz11_decode(out, made, back, n), TP_OK);
    CHECK(!memcmp(back, in, n));
    if (n >= 6) {               /* the streaming check reads the same stream as valid, with the same byte 5 */
        uint8_t b5 = 0;
        CHECK(lz(out, made, 0, &b5) && b5 == in[5]);
    }
    if (made > 8)               /* too little room: refused, never written past the end */
        CHECK_EQ(lz11_encode(in, n, out, made - 5, &made), TP_E_TOO_BIG);
    free(out);
    free(back);
}

static void test_lz11_buffers(void)
{
    static const uint8_t literals[] = {0x11, 8, 0, 0, 0x00, 'a', 'b', 'c', 'd', 'e', 'F', 'g', 'h'};
    static const uint8_t copy2[] = {0x11, 10, 0, 0, 0x20, 'A', 'B', 0x70, 0x01};
    static const uint8_t copy4[] = {0x11, 0x12, 0x01, 0, 0x40, 'A', 0x10, 0x00, 0x00, 0x00};
    static const uint8_t extended[] = {0x11, 0, 0, 0, 8, 0, 0, 0, 0x00, 1, 2, 3, 4, 5, 6, 7, 8};
    static const uint8_t truncated[] = {0x11, 8, 0, 0, 0x00, 1, 2, 3, 4};
    static const uint8_t before_start[] = {0x11, 8, 0, 0, 0x40, 'A', 0x70, 0x01};
    static const uint8_t overrun[] = {0x11, 9, 0, 0, 0x20, 'A', 'B', 0x70, 0x01};
    static const uint8_t zero_size[] = {0x11, 0, 0, 0, 0, 0, 0, 0, 0x00, 1};
    static const uint8_t not_lz[] = {0x10, 8, 0, 0, 0x00, 1, 2, 3, 4, 5, 6, 7, 8};
    uint8_t out[0x200];
    CHECK_EQ(lz11_decode(literals, sizeof literals, out, 8), TP_OK);
    CHECK(!memcmp(out, "abcdeFgh", 8));
    CHECK_EQ(lz11_decode(copy2, sizeof copy2, out, 10), TP_OK);
    CHECK(!memcmp(out, "ABABABABAB", 10));
    CHECK_EQ(lz11_decode(copy4, sizeof copy4, out, sizeof out), TP_OK);
    CHECK(out[0] == 'A' && out[0x111] == 'A');
    CHECK_EQ(lz11_decode(extended, sizeof extended, out, 8), TP_OK);
    CHECK_EQ(out[7], 8);
    CHECK_EQ(lz11_decode(literals, sizeof literals, out, 7), TP_E_TOO_BIG);
    CHECK_EQ(lz11_decode(truncated, sizeof truncated, out, sizeof out), TP_E_LZ11);
    CHECK_EQ(lz11_decode(before_start, sizeof before_start, out, sizeof out), TP_E_LZ11);
    CHECK_EQ(lz11_decode(overrun, sizeof overrun, out, sizeof out), TP_E_LZ11);
    CHECK_EQ(lz11_decode(zero_size, sizeof zero_size, out, sizeof out), TP_E_LZ11);
    CHECK_EQ(lz11_decode(not_lz, sizeof not_lz, out, sizeof out), TP_E_LZ11);
    CHECK_EQ(lz11_decode(copy4, sizeof copy4 - 1, out, sizeof out), TP_E_LZ11);

    /* every copy form and its limits: lengths 3, 16, 17, 272, 273 and 0x10110 from 1 and 0x1000 back */
    static const size_t runs[] = {1, 2, 3, 4, 16, 17, 18, 272, 273, 274, 0x10110, 0x10111, 0x20221};
    for (size_t k = 0; k < sizeof runs / sizeof *runs; k++) {
        size_t n = runs[k] + 1;
        uint8_t *p = calloc(n, 1);
        p[0] = 0x5A;
        round_trip(p, n);
        free(p);
    }
    uint8_t *far = malloc(0x3000);
    for (size_t i = 0; i < 0x3000; i++)
        far[i] = (uint8_t)(i < 0x1000 ? i * 13 : far[i - 0x1000]);   /* repeats exactly 0x1000 back */
    round_trip(far, 0x3000);
    for (size_t i = 0x1001; i < 0x3000; i++)
        far[i] = far[i - 0x1001];                                     /* one past the window: no such copy */
    round_trip(far, 0x3000);
    free(far);
    size_t n = TP_BODY_MAX;
    uint8_t *big = malloc(n);
    for (unsigned seed = 1; seed <= 3; seed++) {
        body_like(big, n - seed * 1000, seed);
        round_trip(big, n - seed * 1000);
    }
    memset(big, 0, n);
    round_trip(big, n);
    for (size_t i = 0; i < 0x20000; i++)
        big[i] = (uint8_t)(rand() >> 7);
    round_trip(big, 0x20000);
    free(big);
    CHECK_EQ(lz11_encode(out, 0, out, sizeof out, &n), TP_E_ARG);
    CHECK_EQ(live, 0);
}

/* A stored theme zip: a literal-only body (byte 0x5 as given), optional bgm.bcstm, an SMDH named "Quiet". */
static size_t quiet_zip(uint8_t *zip, uint8_t *packed, const uint8_t *plain, size_t n, int with_bgm)
{
    static uint8_t smdh[TP_SMDH_SIZE], bgm[0x3000];
    memcpy(smdh, "SMDH", 4);
    for (int k = 0; k < 5; k++)
        put_le16(smdh + 8 + 2 * k, (uint16_t)"Quiet"[k]);
    memset(bgm, 0x42, sizeof bgm);
    size_t packed_n = literal_stream(plain, n, packed);
    member m[3] = {{"bgm.bcstm", bgm, sizeof bgm}, {"body_LZ.bin", packed, packed_n}, {"info.smdh", smdh, sizeof smdh}};
    return with_bgm ? stored_zip(zip, m, 3) : stored_zip(zip, m + 1, 2);
}

static int has_utf16(const uint8_t *p, size_t n, const char *name)
{
    size_t len = strlen(name);
    for (size_t i = 0; i + 2 * len <= n; i++) {
        size_t k = 0;
        while (k < len && p[i + 2 * k] == (uint8_t)name[k] && p[i + 2 * k + 1] == 0)
            k++;
        if (k == len)
            return 1;
    }
    return 0;
}

static void test_drop_bgm(void)
{
    enum { N = 0x6000 };
    static uint8_t plain[N], packed[N + N / 8 + 16], zip[N + N / 8 + 0x9000], content[0x40000];
    body_like(plain, N, 7);
    plain[0] = 1;
    plain[5] = 1;
    size_t zip_n = quiet_zip(zip, packed, plain, N, 1);
    mem_src ms;
    tp_source src = source_of(&ms, zip, zip_n);
    tp_theme t;
    tp_content with, without;
    CHECK_EQ(tp_theme_open(&t, &src, NULL), TP_OK);
    CHECK(t.has_bgm && t.use_bgm && !t.warnings);
    CHECK_EQ(tp_theme_content(&t, TID, 1, NULL, &with), TP_OK);
    uint32_t stored_body = t.body_size;
    CHECK_EQ(tp_theme_drop_bgm(&t), TP_OK);
    CHECK(!t.has_bgm && !t.use_bgm && !t.bgm_size && !t.warnings);
    CHECK(t.body_mem && t.body_size == t.body_mem_size && t.body_size < stored_body);
    static uint8_t back[N];
    CHECK_EQ(lz11_decode(t.body_mem, t.body_mem_size, back, N), TP_OK);
    plain[5] = 0;
    CHECK(!memcmp(back, plain, N));     /* only byte 0x5 differs from the zip's body */
    plain[5] = 1;
    mem_sink s;
    tp_sink sink = sink_of(&s);
    s.keep = content;
    s.keep_cap = sizeof content;
    CHECK_EQ(tp_theme_content(&t, TID, 1, &sink, &without), TP_OK);
    CHECK(without.size < with.size && s.size == without.size);
    CHECK(has_utf16(content, (size_t)s.size, "body_LZ.bin") && !has_utf16(content, (size_t)s.size, "bgm.bcstm"));
    int found = 0;
    for (size_t i = 0; i + t.body_mem_size <= s.size && !found; i++)
        found = !memcmp(content + i, t.body_mem, t.body_mem_size);
    CHECK(found);
    CHECK_EQ(tp_theme_drop_bgm(&t), TP_E_ARG);      /* no music left to drop */
    tp_theme_close(&t);
    CHECK_EQ(live, 0);

    /* every allocation failing in turn: refused, the theme as it was, nothing kept */
    allocs = 0;
    CHECK_EQ(tp_theme_open(&t, &src, NULL), TP_OK);
    size_t opened = allocs;
    CHECK_EQ(tp_theme_drop_bgm(&t), TP_OK);
    size_t dropping = allocs - opened;
    tp_theme_close(&t);
    for (size_t k = 0; k < dropping; k++) {
        CHECK_EQ(tp_theme_open(&t, &src, NULL), TP_OK);
        allocs = 0;
        fail_alloc_at = k;
        CHECK_EQ(tp_theme_drop_bgm(&t), TP_E_NOMEM);
        fail_alloc_at = (size_t)-1;
        tp_content again;
        CHECK(t.has_bgm && t.use_bgm && !t.body_mem);
        CHECK(tp_theme_content(&t, TID, 1, NULL, &again) == TP_OK && again.size == with.size &&
              !memcmp(again.sha256, with.sha256, 32));
        tp_theme_close(&t);
        CHECK_EQ(live, 0);
    }
    /* a read failing in turn: refused the same way */
    CHECK_EQ(tp_theme_open(&t, &src, NULL), TP_OK);
    size_t before = ms.reads;
    CHECK_EQ(tp_theme_drop_bgm(&t), TP_OK);
    size_t reads = ms.reads - before;
    tp_theme_close(&t);
    for (size_t k = 0; k < reads; k++) {
        CHECK_EQ(tp_theme_open(&t, &src, NULL), TP_OK);
        ms.fail_at = ms.reads + k;
        CHECK_EQ(tp_theme_drop_bgm(&t), TP_E_READ);
        ms.fail_at = (size_t)-1;
        CHECK(t.has_bgm && !t.body_mem);
        tp_theme_close(&t);
    }
    CHECK_EQ(live, 0);

    /* music in the zip, but the body does not switch it on: the body stays as it is, only bgm.bcstm goes */
    plain[5] = 0;
    zip_n = quiet_zip(zip, packed, plain, N, 1);
    src = source_of(&ms, zip, zip_n);
    CHECK_EQ(tp_theme_open(&t, &src, NULL), TP_OK);
    CHECK(t.has_bgm && !t.use_bgm && (t.warnings & TP_WARN_BGM_UNUSED));
    CHECK_EQ(tp_theme_drop_bgm(&t), TP_OK);
    CHECK(!t.body_mem && t.body_size == stored_body && !t.warnings);
    CHECK(tp_theme_content(&t, TID, 1, NULL, &without) == TP_OK && without.size < with.size);
    tp_theme_close(&t);

    /* no music file, or not opened: nothing to drop */
    plain[5] = 1;
    zip_n = quiet_zip(zip, packed, plain, N, 0);
    src = source_of(&ms, zip, zip_n);
    CHECK_EQ(tp_theme_open(&t, &src, NULL), TP_OK);
    CHECK_EQ(tp_theme_drop_bgm(&t), TP_E_ARG);
    tp_theme_close(&t);
    zip_n = quiet_zip(zip, packed, plain, N, 1);
    src = source_of(&ms, zip, zip_n);
    CHECK_EQ(tp_theme_peek(&t, &src, NULL), TP_OK);
    CHECK_EQ(tp_theme_drop_bgm(&t), TP_E_ARG);
    CHECK_EQ(tp_theme_drop_bgm(NULL), TP_E_ARG);
    CHECK_EQ(live, 0);
}

int main(int argc, char **argv)
{
    if (argc != 3 && argc != 4) {
        fprintf(stderr, "usage: test_core ZIP_WITH_BGM ZIP_WITHOUT_BGM [THEME_CIA]\n");
        return 2;
    }
    tp_set_allocator(t_alloc, t_free, NULL);
    test_sha256();
    test_lz11();
    test_lz11_buffers();
    test_drop_bgm();
    test_index();
    test_title();
    test_text();
    test_tmd_delta();
    test_body_limit();
    test_installed();
    test_tmd_check();
    test_changed();
    test_peek(argv[1], 1);
    test_peek(argv[2], 0);
    test_peek_does_not_decode();
    if (argc == 4)
        test_cia(argv[3]);
    for (int i = 1; i <= 2; i++)
        test_failure_paths(argv[i]);

    /* mutation run over both fixture zips and a stored zip, whose members are not hidden behind deflate */
    for (int i = 1; i <= 2; i++) {
        size_t size;
        uint8_t *zip = slurp(argv[i], &size);
        const char *base = strrchr(argv[i], '/');
        mutate_run(base ? base + 1 : argv[i], zip, size, 4000);
        free(zip);
    }
    static uint8_t body[0x40], smdh[TP_SMDH_SIZE], bgm[0x100], stored[0x5000];
    body[0] = 0x11;
    body[1] = 0x30;
    for (int g = 0; g < 6; g++)
        for (int k = 0; k < 8; k++)
            body[4 + 9 * g + 1 + k] = (uint8_t)(g * 8 + k);
    memcpy(smdh, "SMDH", 4);
    for (int k = 0; k < 5; k++)
        put_le16(smdh + 8 + 2 * k, (uint16_t)"Store"[k]);
    member m[3] = {{"bgm.bcstm", bgm, sizeof bgm}, {"body_LZ.bin", body, 4 + 9 * 6}, {"info.smdh", smdh, sizeof smdh}};
    size_t n = stored_zip(stored, m, 3);
    run res;
    CHECK_EQ(full_run(stored, n, (size_t)-1, (size_t)-1, &res), TP_OK);
    mutate_run("a stored zip", stored, n, 4000);
    mutate_members(m, 6000);

    printf("test_core: %d checks, %d failed\n", checks, failures);
    return failures != 0;
}
