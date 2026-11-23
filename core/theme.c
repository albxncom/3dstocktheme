/* Results, memory, the LZ11 check, SMDH text and one theme zip. */
#include <stdlib.h>

#include "internal.h"

/* ---------------------------------------------------------------- results */

static const char *const NAMES[] = {
    "TP_OK", "TP_E_ARG", "TP_E_NOMEM", "TP_E_READ", "TP_E_WRITE", "TP_E_ZIP", "TP_E_MISSING", "TP_E_TOO_BIG",
    "TP_E_LZ11", "TP_E_SMDH", "TP_E_INDEX", "TP_E_TMD", "TP_E_GONE", "TP_E_NOTHING", "TP_E_VERSION", "TP_E_ID",
    "TP_E_CHANGED", "TP_E_TITLE", "TP_E_CATALOGUE", "TP_E_STALE", "TP_E_CIA", "TP_E_ENCRYPTED", "TP_E_KIND",
};

static const char *const TEXTS[] = {
    "ok",
    "an argument is out of range",
    "out of memory",
    "the source could not be read",
    "the output could not be written",
    "not a zip this reader accepts, or a member is damaged",
    "body_LZ.bin and info.smdh must be at the zip root",
    "body_LZ.bin is over 0x150000 bytes, stored or decoded (HOME's body buffer), or bgm.bcstm over 0x337000",
    "body_LZ.bin is not valid LZ11",
    "info.smdh is not an SMDH of 0x36C0 bytes",
    "content index outside 1..349, used twice, or none left (indices are never reused)",
    "the installed TMD is malformed",
    "the installed pack has a theme the new catalogue does not list",
    "nothing to add: every theme is installed already",
    "the title version is at its maximum; reinstall the whole pack",
    "the new content 0 ID is used by an installed content",
    "the source changed while it was read",
    "not the title 0004008C00009809: the core writes and reads no other",
    "an installed catalogue file is malformed",
    "the stored TMD does not match what AM reports: read the installed state again",
    "not a theme installer CIA this reader accepts, or damaged",
    "the CIA's content is encrypted; only plain contents can be read",
    "neither a theme zip nor a theme installer CIA",
};

const char *tp_result_name(int result)
{
    return result >= 0 && (size_t)result < sizeof NAMES / sizeof *NAMES ? NAMES[result] : "TP_E_UNKNOWN";
}

const char *tp_result_text(int result)
{
    return result >= 0 && (size_t)result < sizeof TEXTS / sizeof *TEXTS ? TEXTS[result] : "unknown result";
}

/* ---------------------------------------------------------------- memory */

static void *default_alloc(void *ctx, size_t size) { (void)ctx; return malloc(size); }
static void default_release(void *ctx, void *ptr) { (void)ctx; free(ptr); }

static void *(*alloc_fn)(void *, size_t) = default_alloc;
static void (*release_fn)(void *, void *) = default_release;
static void *alloc_ctx;

void tp_set_allocator(void *(*alloc)(void *ctx, size_t size), void (*release)(void *ctx, void *ptr), void *ctx)
{
    alloc_fn = alloc && release ? alloc : default_alloc;
    release_fn = alloc && release ? release : default_release;
    alloc_ctx = alloc && release ? ctx : NULL;
}

void *tp_alloc(size_t size) { return alloc_fn(alloc_ctx, size ? size : 1); }

static void (*phase_fn)(void *, int);
static void *phase_ctx;

void tp_set_phase(void (*tell)(void *ctx, int phase), void *ctx)
{
    phase_fn = tell;
    phase_ctx = ctx;
}

void tp_phase(int phase)
{
    if (phase_fn)
        phase_fn(phase_ctx, phase);
}

void tp_free(void *ptr)
{
    if (ptr)
        release_fn(alloc_ctx, ptr);
}

void *tp_zalloc(void *opaque, unsigned items, unsigned size)
{
    (void)opaque;
    if (size && items > SIZE_MAX / size)
        return NULL;
    return tp_alloc((size_t)items * size);
}

void tp_zfree(void *opaque, void *ptr)
{
    (void)opaque;
    tp_free(ptr);
}

/* ---------------------------------------------------------------- LZ11: header 0x11 + 24-bit size (0: a 32-bit
 * size follows); then a flag byte per 8 tokens, MSB first, 1 = back-reference of 2, 3 or 4 bytes. */

void lz11_init(lz11 *z)
{
    memset(z, 0, sizeof *z);
    z->bit = 8;
}

static void lz11_copy(lz11 *z)
{
    const uint8_t *t = z->token;
    uint64_t length, distance;
    if (z->token_need == 2) {
        length = (uint64_t)(t[0] >> 4) + 1;
        distance = (uint64_t)((t[0] & 0xF) << 8 | t[1]) + 1;
    } else if (z->token_need == 3) {
        length = (uint64_t)((t[0] & 0xF) << 4 | t[1] >> 4) + 0x11;
        distance = (uint64_t)((t[1] & 0xF) << 8 | t[2]) + 1;
    } else {
        length = (uint64_t)((t[0] & 0xF) << 12 | t[1] << 4 | t[2] >> 4) + 0x111;
        distance = (uint64_t)((t[2] & 0xF) << 8 | t[3]) + 1;
    }
    if (distance > z->out || length > z->size - z->out) {
        z->bad = 1;                 /* before the start, or past the declared end */
        return;
    }
    for (uint64_t k = 0; k < length && z->out + k < sizeof z->head; k++)
        z->head[z->out + k] = z->head[z->out + k - distance];
    z->out += length;
}

void lz11_feed(lz11 *z, const uint8_t *p, size_t n)
{
    for (size_t i = 0; i < n && !z->bad && !z->done; i++) {
        uint8_t b = p[i];
        if (z->size == 0) {         /* header */
            z->header[z->have++] = b;
            if (z->have == 1 && b != 0x11)
                z->bad = 1;
            else if (z->have == 4 && (z->header[1] | z->header[2] | z->header[3]))
                z->size = (uint64_t)z->header[1] | (uint64_t)z->header[2] << 8 | (uint64_t)z->header[3] << 16;
            else if (z->have == 8)
                z->size = get_le32(z->header + 4);
            if ((z->have == 4 && z->size) || z->have == 8) {
                if (z->size < sizeof z->head)
                    z->bad = 1;     /* too short for a theme body (and 0 would decode nothing) */
                if (z->size > TP_BODY_MAX)
                    z->bad = z->too_big = 1;    /* more than HOME's body buffer holds */
            }
            continue;
        }
        if (z->bit == 8) {
            z->flags = b;
            z->bit = 0;
            continue;
        }
        if (z->token_need == 0) {
            if (!(z->flags & 0x80 >> z->bit)) {
                if (z->out < sizeof z->head)
                    z->head[z->out] = b;
                z->out++;
                z->bit++;
                z->done = z->out == z->size;
                continue;
            }
            z->token_need = b >> 4 == 0 ? 3 : b >> 4 == 1 ? 4 : 2;
            z->token_have = 0;
        }
        z->token[z->token_have++] = b;
        if (z->token_have < z->token_need)
            continue;
        lz11_copy(z);
        z->token_need = 0;
        z->bit++;
        z->done = !z->bad && z->out == z->size;
    }
}

int lz11_valid(const lz11 *z) { return !z->bad && z->size && z->out == z->size; }

/* ---------------------------------------------------------------- text */

int utf8_valid(const char *s)
{
    const uint8_t *p = (const uint8_t *)s;
    while (*p) {
        uint32_t c = *p;
        int n = c < 0x80 ? 0 : (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : -1;
        if (n < 0)
            return 0;
        c &= n == 0 ? 0x7F : 0x3F >> n;
        for (int k = 1; k <= n; k++) {
            if ((p[k] & 0xC0) != 0x80)
                return 0;
            c = c << 6 | (p[k] & 0x3F);
        }
        static const uint32_t least[4] = {0, 0x80, 0x800, 0x10000};
        if (c < least[n] || c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF))
            return 0;
        p += n + 1;
    }
    return 1;
}

void utf8_field(uint8_t *field, size_t size, const char *text)
{
    size_t len = strlen(text), n = len < size - 1 ? len : size - 1;
    while (n > 0 && n < len && ((uint8_t)text[n] & 0xC0) == 0x80)
        n--;
    memcpy(field, text, n);
    memset(field + n, 0, size - n);
}

/* str.isspace() of Python 3, which theme_pack.py's strip() uses. */
static int is_space(uint32_t c)
{
    return (c >= 0x9 && c <= 0xD) || (c >= 0x1C && c <= 0x20) || c == 0x85 || c == 0xA0 || c == 0x1680 ||
           (c >= 0x2000 && c <= 0x200A) || c == 0x2028 || c == 0x2029 || c == 0x202F || c == 0x205F || c == 0x3000;
}

/* A UTF-16LE field up to its first NUL, decoded like Python's utf-16-le with "replace" (an unpaired surrogate
 * becomes U+FFFD), stripped of whitespace, as UTF-8. out holds 3 bytes per unit plus the NUL. */
static void smdh_text(const uint8_t *p, size_t units, char *out)
{
    uint32_t cp[128];
    size_t n = 0;
    for (size_t i = 0; i < units && n < sizeof cp / sizeof *cp; i++) {
        uint32_t u = get_le16(p + 2 * i);
        if (u == 0)
            break;
        if (u >= 0xD800 && u <= 0xDBFF && i + 1 < units) {
            uint32_t v = get_le16(p + 2 * i + 2);
            if (v >= 0xDC00 && v <= 0xDFFF) {
                cp[n++] = 0x10000 + ((u - 0xD800) << 10) + (v - 0xDC00);
                i++;
                continue;
            }
        }
        cp[n++] = u >= 0xD800 && u <= 0xDFFF ? 0xFFFD : u;
    }
    size_t first = 0, last = n;
    while (first < last && is_space(cp[first]))
        first++;
    while (last > first && is_space(cp[last - 1]))
        last--;
    uint8_t *o = (uint8_t *)out;
    for (size_t i = first; i < last; i++) {
        uint32_t c = cp[i];
        if (c < 0x80) {
            *o++ = (uint8_t)c;
        } else if (c < 0x800) {
            *o++ = (uint8_t)(0xC0 | c >> 6);
            *o++ = (uint8_t)(0x80 | (c & 0x3F));
        } else if (c < 0x10000) {
            *o++ = (uint8_t)(0xE0 | c >> 12);
            *o++ = (uint8_t)(0x80 | (c >> 6 & 0x3F));
            *o++ = (uint8_t)(0x80 | (c & 0x3F));
        } else {
            *o++ = (uint8_t)(0xF0 | c >> 18);
            *o++ = (uint8_t)(0x80 | (c >> 12 & 0x3F));
            *o++ = (uint8_t)(0x80 | (c >> 6 & 0x3F));
            *o++ = (uint8_t)(0x80 | (c & 0x3F));
        }
    }
    *o = 0;
}

#define SMDH_SLOTS 16           /* 0x200 each from 0x8: short title 0x80, long 0x100, publisher 0x80 */
#define SMDH_LARGE_ICON 0x24C0

/* Slot 0 first, as Anemone reads it; Theme Plaza fills every slot with the same text. */
static void read_smdh(tp_theme *t, const uint8_t *smdh, const char *fallback)
{
    for (int s = 0; s < SMDH_SLOTS && !t->name[0]; s++)
        smdh_text(smdh + 0x8 + 0x200 * s, 0x40, t->name);
    for (int s = 0; s < SMDH_SLOTS && !t->description[0]; s++)
        smdh_text(smdh + 0x8 + 0x200 * s + 0x80, 0x80, t->description);
    if (!t->name[0]) {
        utf8_field((uint8_t *)t->name, sizeof t->name, fallback && *fallback && utf8_valid(fallback) ? fallback : "Theme");
        t->warnings |= TP_WARN_NAME_FALLBACK;
    }
    memcpy(t->icon, smdh + SMDH_LARGE_ICON, TP_ICON_SIZE);
}

/* ---------------------------------------------------------------- one theme */

struct fill {
    uint8_t *buf;
    size_t have, size;
};

static int fill_fn(void *ctx, const uint8_t *data, size_t size)
{
    struct fill *f = ctx;
    if (size > f->size - f->have)
        return TP_E_ZIP;
    memcpy(f->buf + f->have, data, size);
    f->have += size;
    return TP_OK;
}

struct theme_feed {
    l3_writer *w;
    lz11 *check;
};

static int theme_feed_fn(void *ctx, const uint8_t *data, size_t size)
{
    struct theme_feed *f = ctx;
    if (f->check) {
        tp_phase(TP_PHASE_LZ11);
        lz11_feed(f->check, data, size);
    }
    tp_phase(TP_PHASE_HASH);
    return l3_write(f->w, data, size);
}

struct theme_produce {
    const tp_theme *theme;
    lz11 *check;                /* during tp_theme_open */
};

/* The RomFS files: bgm.bcstm before body_LZ.bin, the order makerom took from the Mac's directory listing in the
 * pack that passed on the console, so a theme content is the same bytes apart from the signature. */
static void theme_rfs(const tp_theme *t, rfs_file files[2], rfs *r)
{
    size_t n = 0;
    if (t->bgm.present)
        files[n++] = (rfs_file){"bgm.bcstm", t->bgm.size, 0};
    files[n++] = (rfs_file){"body_LZ.bin", t->body_mem ? t->body_mem_size : t->body.size, 0};
    memset(r, 0, sizeof *r);
    r->files = files;
    r->count = n;
}

static int theme_produce_fn(void *ctx, size_t file, l3_writer *w)
{
    struct theme_produce *p = ctx;
    const tp_theme *t = p->theme;
    int body = !t->bgm.present || file == 1;
    struct theme_feed f = {w, body ? p->check : NULL};
    if (body && t->body_mem) {
        int r = TP_OK;
        for (uint32_t at = 0; !r && at < t->body_mem_size; at += CHUNK)
            r = theme_feed_fn(&f, t->body_mem + at, t->body_mem_size - at < CHUNK ? t->body_mem_size - at : CHUNK);
        return r;
    }
    return zip_stream(&t->src, body ? &t->body : &t->bgm, theme_feed_fn, &f);
}

void tp_theme_close(tp_theme *t)
{
    if (!t)
        return;
    tp_free(t->l2);
    t->l2 = NULL;
    tp_free(t->body_mem);
    t->body_mem = NULL;
}

int tp_theme_peek(tp_theme *t, const tp_source *zip, const char *fallback_name)
{
    if (!t)
        return TP_E_ARG;
    memset(t, 0, sizeof *t);
    if (!zip || !zip->read)
        return TP_E_ARG;
    t->src = *zip;
    tp_member smdh;
    int r = zip_find(zip, &t->body, &t->bgm, &smdh);
    if (r)
        return r;
    if (!t->body.present || !smdh.present)
        return TP_E_MISSING;
    if (t->body.size > TP_BODY_MAX || (t->bgm.present && t->bgm.size > TP_BGM_MAX))
        return TP_E_TOO_BIG;
    if (smdh.size != TP_SMDH_SIZE)
        return TP_E_SMDH;

    uint8_t *buf = tp_alloc(TP_SMDH_SIZE);
    if (!buf)
        return TP_E_NOMEM;
    struct fill f = {buf, 0, TP_SMDH_SIZE};
    r = zip_stream(zip, &smdh, fill_fn, &f);
    if (!r && memcmp(buf, "SMDH", 4))
        r = TP_E_SMDH;
    if (!r)
        read_smdh(t, buf, fallback_name);
    tp_free(buf);
    if (r)
        return r;
    t->body_size = t->body.size;
    t->has_bgm = t->bgm.present;
    t->bgm_size = t->bgm.size;
    return TP_OK;
}

int tp_theme_open(tp_theme *t, const tp_source *zip, const char *fallback_name)
{
    int r = tp_theme_peek(t, zip, fallback_name);
    if (r)
        return r;

    rfs_file files[2];
    rfs layout;
    theme_rfs(t, files, &layout);
    if ((r = rfs_layout(&layout)) != TP_OK)
        return r;
    lz11 check;
    lz11_init(&check);
    struct theme_produce p = {t, &check};
    r = rfs_hash(&layout, theme_produce_fn, &p, &t->l2);
    t->l3_size = layout.l3_size;
    rfs_free(&layout);
    if (!r && !lz11_valid(&check))
        r = check.too_big ? TP_E_TOO_BIG : TP_E_LZ11;
    if (r) {
        tp_theme_close(t);
        return r;
    }
    t->use_bgm = check.head[5] != 0;
    if (t->has_bgm && !t->use_bgm)
        t->warnings |= TP_WARN_BGM_UNUSED;
    if (!t->has_bgm && t->use_bgm)
        t->warnings |= TP_WARN_BGM_MISSING;
    return TP_OK;
}

/* The zip's body with byte 0x5 cleared, as a new LZ11 stream of *size bytes (allocated), checked by decoding it. */
static int silent_body(const tp_theme *t, uint8_t **body, uint32_t *size)
{
    uint8_t *packed = tp_alloc(t->body.size), *plain = NULL, *again = NULL, *out = NULL;
    size_t declared = 0, start, made = 0;
    int r = packed ? TP_OK : TP_E_NOMEM;
    struct fill f = {packed, 0, t->body.size};
    if (!r)
        r = zip_stream(&t->src, &t->body, fill_fn, &f);
    if (!r)
        r = lz11_size(packed, f.have, &declared, &start);
    if (!r && (declared < 6 || declared > TP_BODY_MAX))
        r = TP_E_LZ11;
    if (!r && !(plain = tp_alloc(declared)))
        r = TP_E_NOMEM;
    if (!r)
        r = lz11_decode(packed, f.have, plain, declared);
    tp_free(packed);
    if (!r) {
        plain[5] = 0;
        r = (out = tp_alloc(TP_BODY_MAX)) ? lz11_encode(plain, declared, out, TP_BODY_MAX, &made) : TP_E_NOMEM;
    }
    if (!r && !(again = tp_alloc(declared)))
        r = TP_E_NOMEM;
    if (!r)
        r = lz11_decode(out, made, again, declared);
    if (!r && memcmp(again, plain, declared))
        r = TP_E_LZ11;
    tp_free(again);
    tp_free(plain);
    if (r) {
        tp_free(out);
        return r;
    }
    *body = out;
    *size = (uint32_t)made;
    return TP_OK;
}

int tp_theme_drop_bgm(tp_theme *t)
{
    if (!t || !t->l2 || !t->has_bgm || !t->bgm.present)
        return TP_E_ARG;
    tp_theme silent = *t;       /* a working copy for hashing; t keeps its own state until everything passed */
    silent.l2 = NULL;
    silent.bgm.present = 0;
    silent.body_mem = NULL;
    silent.body_mem_size = 0;
    int r = TP_OK;
    if (t->use_bgm)
        r = silent_body(t, &silent.body_mem, &silent.body_mem_size);
    rfs_file files[2];
    rfs layout;
    lz11 check;
    lz11_init(&check);
    if (!r) {
        theme_rfs(&silent, files, &layout);
        r = rfs_layout(&layout);
    }
    if (!r) {
        struct theme_produce p = {&silent, &check};
        r = rfs_hash(&layout, theme_produce_fn, &p, &silent.l2);
        silent.l3_size = layout.l3_size;
        rfs_free(&layout);
    }
    if (!r && (!lz11_valid(&check) || check.head[5] != 0))
        r = TP_E_LZ11;
    if (r) {
        tp_free(silent.l2);
        tp_free(silent.body_mem);
        return r;
    }
    tp_free(t->l2);
    tp_free(t->body_mem);
    *t = silent;
    t->has_bgm = 0;
    t->bgm_size = 0;
    t->use_bgm = 0;
    t->body_size = t->body_mem ? t->body_mem_size : t->body.size;
    t->warnings &= ~(unsigned)(TP_WARN_BGM_UNUSED | TP_WARN_BGM_MISSING);
    return TP_OK;
}

int tp_theme_content(const tp_theme *t, uint64_t title_id, uint16_t index, const tp_sink *out, tp_content *rec)
{
    if (!t || !t->l2 || !rec || (out && !out->write))
        return TP_E_ARG;
    if (title_id != TP_TITLE_ID)
        return TP_E_TITLE;
    if (index < 1 || index > TP_INDEX_MAX)
        return TP_E_INDEX;
    rfs_file files[2];
    rfs layout;
    theme_rfs(t, files, &layout);
    int r = rfs_layout(&layout);
    if (r)
        return r;
    if (layout.l3_size != t->l3_size) {
        rfs_free(&layout);
        return TP_E_ARG;
    }
    struct theme_produce p = {t, NULL};
    r = ncch_emit(&layout, theme_produce_fn, &p, t->l2, title_id, NULL, out, &rec->size, rec->sha256);
    rfs_free(&layout);
    if (r)
        return r;
    rec->id = index;
    rec->index = index;
    rec->type = 0x4000;
    return TP_OK;
}
