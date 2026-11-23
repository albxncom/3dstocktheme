/* The badge collection, as badge_sets.py reads and changes it: results, names, the strict reader, the manifest
 * rules, append, remove-last with its undo record, and the check of the saved layouts. */
#include "badge_internal.h"

/* ---------------------------------------------------------------- results */

static const char *const NAMES[] = {
    "BADGE_OK", "BADGE_E_ARG", "BADGE_E_NOMEM", "BADGE_E_SIZE", "BADGE_E_FORMAT", "BADGE_E_PNG", "BADGE_E_PNG_KIND",
    "BADGE_E_IMAGE_SIZE", "BADGE_E_FOLDER", "BADGE_E_NAME", "BADGE_E_MANIFEST", "BADGE_E_IDS", "BADGE_E_PRESENT",
    "BADGE_E_FULL", "BADGE_E_INDEX", "BADGE_E_TAKEN", "BADGE_E_REFUSED", "BADGE_E_EMPTY", "BADGE_E_NOT_OURS",
    "BADGE_E_LAST", "BADGE_E_PLACED", "BADGE_E_NO_UNDO", "BADGE_E_CHANGED", "BADGE_E_SET_CHANGED",
    "BADGE_E_AMBIGUOUS", "BADGE_E_UNDO", "BADGE_E_SLOTS", "BADGE_E_ZIP",
};

static const char *const TEXTS[] = {
    "ok",
    "an argument is out of range",
    "out of memory",
    "a badge file has the wrong size: not a decrypted copy, or another file",
    "the badge collection is not laid out the way a set can be appended to it safely",
    "a PNG file is damaged",
    "a PNG kind that is not read: save it as 8-bit RGBA without interlacing",
    "a badge image's sides must be multiples of 64, a set icon is 48x48, at most 4096x4096",
    "only PNG files belong in a set, no subfolders, and at least one badge",
    "a name is empty, longer than 68 characters, or has characters that would need normalising (a separate accent mark)",
    "the manifest has missing, repeated or out-of-range IDs or counters",
    "no set ID or badge ID is left",
    "this set is in the collection already",
    "HOME takes at most 100 sets and 1000 badges",
    "the new set's index is taken by an existing set",
    "a badge ID of the manifest is in the collection already",
    "refused: the append would not keep the existing collection intact",
    "the collection has no set",
    "the last set was not added by this application",
    "the last set has no badges, or does not end at the last badge",
    "badges of the last set are placed: put them back into the case first",
    "no undo record of the last set: its earlier bytes are unknown",
    "the badge files changed since the set was added (by HOME, for example)",
    "the set itself changed since it was added: no undo record fits",
    "several undo records fit these files",
    "an undo record is damaged",
    "a layout file's header is not valid",
    "not a zip this reader accepts, or a member is damaged",
};

const char *badge_result_name(int result)
{
    return result >= 0 && (size_t)result < sizeof NAMES / sizeof *NAMES ? NAMES[result] : "BADGE_E_UNKNOWN";
}

const char *badge_result_text(int result)
{
    return result >= 0 && (size_t)result < sizeof TEXTS / sizeof *TEXTS ? TEXTS[result] : "unknown result";
}

/* ---------------------------------------------------------------- names */

/* str.isspace() of Python 3, which strip() uses */
int name_space(uint32_t c)
{
    return (c >= 0x9 && c <= 0xD) || (c >= 0x1C && c <= 0x20) || c == 0x85 || c == 0xA0 || c == 0x1680 ||
           (c >= 0x2000 && c <= 0x200A) || c == 0x2028 || c == 0x2029 || c == 0x202F || c == 0x205F || c == 0x3000;
}

/* The code points beyond U+00FF the core takes: blocks whose characters Python's NFC leaves as they are in any
 * string (none is a combining mark, none is the second part of a canonical composition or decomposes into one)
 * and whose casefold is the character itself, fullwidth A to Z aside. So every combining mark is outside, among
 * them U+0300..U+036F, U+1AB0..U+1AFF, U+1DC0..U+1DFF, U+20D0..U+20FF, U+302A..U+302F, U+3099..U+309A (kana
 * voicing) and U+FE20..U+FE2F, and so are the conjoining Hangul jamo, the CJK compatibility ideographs and every
 * cased script but Latin-1. host/test_badges.py checks each of them against Python's unicodedata. */
static const uint32_t TAKEN[][2] = {
    {0x2010, 0x2027},   /* dashes, quotes, bullets, ellipsis */
    {0x2030, 0x205E},   /* per mille, primes, other punctuation */
    {0x20A0, 0x20BF},   /* currency signs */
    {0x2190, 0x21FF},   /* arrows */
    {0x2500, 0x27BF},   /* box drawing, shapes, symbols, dingbats */
    {0x3000, 0x3029},   /* CJK symbols and punctuation, without the tone marks U+302A..U+302F */
    {0x3030, 0x303F},
    {0x3041, 0x3096},   /* hiragana */
    {0x309B, 0x30FF},   /* the spacing voicing marks, hiragana iteration marks, katakana */
    {0x31F0, 0x31FF},   /* katakana extensions */
    {0x3400, 0x4DBF},   /* CJK extension A */
    {0x4E00, 0x9FFF},   /* CJK unified ideographs */
    {0xAC00, 0xD7A3},   /* Hangul syllables */
    {0xFF01, 0xFF9F},   /* fullwidth ASCII, halfwidth katakana */
    {0xFFE0, 0xFFE6},
    {0x1F300, 0x1F64F}, /* emoji (UTF-16 surrogate pairs) */
    {0x1F680, 0x1F6FF},
    {0x1F900, 0x1F9FF},
    {0x20000, 0x2A6DF}, /* CJK extension B */
};

int name_accepted(uint32_t c)
{
    if (c >= 0x1 && c <= 0xFF)
        return 1;
    for (size_t i = 0; i < sizeof TAKEN / sizeof *TAKEN; i++)
        if (c >= TAKEN[i][0] && c <= TAKEN[i][1])
            return 1;
    return 0;
}

/* One code point of strict UTF-8 (no overlong forms, no surrogates) at *i, or -1 */
static int utf8_next(const uint8_t *p, size_t len, size_t *i, uint32_t *out)
{
    uint32_t c = p[*i];
    int n = c < 0x80 ? 0 : (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : -1;
    if (n < 0 || len - *i <= (size_t)n)
        return -1;
    c &= n == 0 ? 0x7F : 0x3F >> n;
    for (int k = 1; k <= n; k++) {
        if ((p[*i + k] & 0xC0) != 0x80)
            return -1;
        c = c << 6 | (p[*i + k] & 0x3F);
    }
    static const uint32_t least[4] = {0, 0x80, 0x800, 0x10000};
    if (c < least[n] || c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF))
        return -1;
    *i += (size_t)n + 1;
    *out = c;
    return 0;
}

int name_points(const char *s, size_t len, uint32_t *cp, size_t cap, size_t *n, size_t *units)
{
    const uint8_t *p = (const uint8_t *)s;
    size_t k = 0, u = 0;
    for (size_t i = 0; i < len;) {
        uint32_t c;
        if (utf8_next(p, len, &i, &c) || !name_accepted(c))
            return BADGE_E_NAME;    /* not UTF-8, NUL, or a code point the core does not take */
        if (cp) {
            if (k >= cap)
                return BADGE_E_NAME;
            cp[k] = c;
        }
        k++;
        u += c > 0xFFFF ? 2 : 1;
    }
    *n = k;
    if (units)
        *units = u;
    return BADGE_OK;
}

int name_check(const char *s, size_t len)
{
    uint32_t cp[BADGE_NAME_UNITS + 1];
    size_t n, units;
    if (name_points(s, len, cp, sizeof cp / sizeof *cp, &n, &units) || units > BADGE_NAME_UNITS)
        return BADGE_E_NAME;    /* also: longer than 68 UTF-16 units (a surrogate pair counts two) */
    for (size_t i = 0; i < n; i++)
        if (!name_space(cp[i]))
            return BADGE_OK;
    return BADGE_E_NAME;        /* empty after strip() */
}

void name_block(const char *s, size_t len, uint8_t block[BD_TITLE_BLOCK])
{
    uint32_t cp[BADGE_NAME_UNITS];
    size_t n = 0, units = 0;
    name_points(s, len, cp, BADGE_NAME_UNITS, &n, NULL);
    memset(block, 0, BD_TITLE_SIZE);
    for (size_t i = 0; i < n; i++) {
        if (cp[i] > 0xFFFF) {           /* UTF-16LE, as Python's encode("utf-16-le") writes it */
            put_le16(block + 2 * units++, (uint16_t)(0xD800 + ((cp[i] - 0x10000) >> 10)));
            put_le16(block + 2 * units++, (uint16_t)(0xDC00 + ((cp[i] - 0x10000) & 0x3FF)));
        } else {
            put_le16(block + 2 * units++, (uint16_t)cp[i]);
        }
    }
    for (unsigned lang = 1; lang < BD_LANGUAGES; lang++)
        memcpy(block + lang * BD_TITLE_SIZE, block, BD_TITLE_SIZE);
}

/* str.casefold() on the code points the core takes: A-Z, the Latin-1 capitals and fullwidth A-Z to small,
 * U+00B5 to U+03BC, U+00DF to "ss"; every other one stays */
int name_fold(uint32_t c, uint32_t out[2])
{
    out[0] = c;
    if ((c >= 'A' && c <= 'Z') || (c >= 0xC0 && c <= 0xDE && c != 0xD7) || (c >= 0xFF21 && c <= 0xFF3A))
        out[0] = c + 32;
    else if (c == 0xB5)
        out[0] = 0x3BC;
    else if (c == 0xDF) {
        out[0] = out[1] = 's';
        return 2;
    }
    return 1;
}

typedef struct fold_it {
    const uint8_t *p;
    size_t len, i;
    uint32_t pending[2];
    int have, next;
} fold_it;

/* The next code point of the casefolded name (checked by name_points), -1 at the end */
static long fold_next(fold_it *it)
{
    if (it->next < it->have)
        return it->pending[it->next++];
    uint32_t c;
    if (it->i >= it->len || utf8_next(it->p, it->len, &it->i, &c))
        return -1;
    it->have = name_fold(c, it->pending);
    it->next = 1;
    return it->pending[0];
}

int name_order(const char *a, const char *b)
{
    fold_it x = {(const uint8_t *)a, strlen(a), 0, {0, 0}, 0, 0};
    fold_it y = {(const uint8_t *)b, strlen(b), 0, {0, 0}, 0, 0};
    for (;;) {
        long ca = fold_next(&x), cb = fold_next(&y);
        if (ca != cb)
            return ca < cb ? -1 : 1;
        if (ca < 0)
            break;
    }
    int r = strcmp(a, b);       /* UTF-8 bytes sort as their code points */
    return r < 0 ? -1 : r > 0;
}

/* A UTF-16LE title up to its first NUL unit, decoded like Python's utf-16-le with "replace", as UTF-8. Returns
 * whether it holds anything but whitespace (title() and the strip() of set_name). */
static int title_text(const uint8_t *p, char out[BADGE_NAME_BYTES])
{
    size_t units = 0, o = 0;
    while (units < BD_TITLE_SIZE / 2 && get_le16(p + 2 * units))
        units++;
    int visible = 0;
    for (size_t i = 0; i < units;) {
        uint32_t c = get_le16(p + 2 * i++);
        if (c >= 0xD800 && c <= 0xDBFF && i < units && get_le16(p + 2 * i) >= 0xDC00 && get_le16(p + 2 * i) <= 0xDFFF)
            c = 0x10000 + ((c - 0xD800) << 10 | (get_le16(p + 2 * i++) - 0xDC00));
        else if (c >= 0xD800 && c <= 0xDFFF)
            c = 0xFFFD;
        visible |= !name_space(c);
        if (c < 0x80) {
            out[o++] = (char)c;
        } else if (c < 0x800) {
            out[o++] = (char)(0xC0 | c >> 6);
            out[o++] = (char)(0x80 | (c & 0x3F));
        } else if (c < 0x10000) {
            out[o++] = (char)(0xE0 | c >> 12);
            out[o++] = (char)(0x80 | (c >> 6 & 0x3F));
            out[o++] = (char)(0x80 | (c & 0x3F));
        } else {
            out[o++] = (char)(0xF0 | c >> 18);
            out[o++] = (char)(0x80 | (c >> 12 & 0x3F));
            out[o++] = (char)(0x80 | (c >> 6 & 0x3F));
            out[o++] = (char)(0x80 | (c & 0x3F));
        }
    }
    out[o] = 0;
    return visible;
}

/* set_name: the English name (language 1), else the Japanese one (0), else "" */
static void set_name(const uint8_t *data, uint32_t index, char out[BADGE_NAME_BYTES])
{
    const uint8_t *block = data + BD_SET_TITLES + index * BD_TITLE_BLOCK;
    if (title_text(block + BD_TITLE_SIZE, out) || title_text(block, out))
        return;
    out[0] = 0;
}

/* ---------------------------------------------------------------- the strict reader */

static const uint8_t EMPTY_RECORD[BM_LAYOUT_RECORD] = {
    0, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF,
};

/* A free layout record: the one HOME writes, or zeros */
static int record_free(const uint8_t *r)
{
    if (!memcmp(r, EMPTY_RECORD, BM_LAYOUT_RECORD))
        return 1;
    for (unsigned i = 0; i < BM_LAYOUT_RECORD; i++)
        if (r[i])
            return 0;
    return 1;
}

static const uint8_t *layout_record(const uint8_t *mng, unsigned n) { return mng + BM_LAYOUT + n * BM_LAYOUT_RECORD; }
static const uint8_t *badge_info(const uint8_t *mng, uint32_t i) { return mng + BM_BADGE_INFO + i * BM_BADGE_INFO_SIZE; }
static const uint8_t *set_info(const uint8_t *mng, uint32_t k) { return mng + BM_SET_INFO + k * BM_SET_INFO_SIZE; }
static int bit(const uint8_t *buf, uint32_t base, uint32_t n) { return buf[base + n / 8] >> (n % 8) & 1; }

static int sub_known(unsigned sub)
{
    static const uint16_t SUBS[] = {0, 0x100, 0x101, 0x1000, 0x1010, 0x1100, 0x1101, 0x1110, 0x1111};
    for (size_t i = 0; i < sizeof SUBS / sizeof *SUBS; i++)
        if (sub == SUBS[i])
            return 1;
    return 0;
}

/* badge_sets.py parse, without the manifest: BADGE_E_FORMAT where it raises */
static int parse(const uint8_t *data, const uint8_t *mng, badge_collection *col)
{
    memset(col, 0, sizeof *col);
    uint32_t n_sets = get_le32(mng + BM_SET_COUNT), unique = get_le32(mng + BM_UNIQUE);
    uint32_t placed = get_le32(mng + BM_PLACED), total = get_le32(mng + BM_TOTAL);
    if (get_le32(mng) || n_sets > BADGE_SETS_MAX || unique > BADGE_BADGES_MAX || placed > BADGE_PLACED_MAX)
        return BADGE_E_FORMAT;
    for (uint32_t n = 0; n < BM_USED_BADGES_SIZE * 8; n++)
        if (bit(mng, BM_USED_BADGES, n) != (n < unique))
            return BADGE_E_FORMAT;  /* not one block from index 0 */
    for (uint32_t n = BADGE_SETS_MAX; n < BM_USED_SETS_SIZE * 8; n++)
        if (bit(mng, BM_USED_SETS, n))
            return BADGE_E_FORMAT;
    for (uint32_t k = n_sets; k < BADGE_SETS_MAX; k++) {
        uint32_t id = get_le32(set_info(mng, k) + 0x10);
        if (id != 0 && id != B_NONE)
            return BADGE_E_FORMAT;  /* a set record beyond the header's count */
    }
    for (uint32_t k = 0; k < n_sets; k++) {
        uint32_t id = get_le32(set_info(mng, k) + 0x10), index = get_le32(set_info(mng, k) + 0x14);
        if (id == 0 || id == B_NONE || index >= BADGE_SETS_MAX)
            return BADGE_E_FORMAT;
        for (uint32_t j = 0; j < k; j++)
            if (get_le32(set_info(mng, j) + 0x10) == id || get_le32(set_info(mng, j) + 0x14) == index)
                return BADGE_E_FORMAT;
    }

    uint8_t owner[BADGE_BADGES_MAX];
    uint32_t members[BADGE_SETS_MAX] = {0};
    uint64_t all = 0;
    for (uint32_t i = 0; i < unique; i++) {
        const uint8_t *b = badge_info(mng, i);
        uint32_t badge_id = get_le32(b + 4), set_id = get_le32(b + 8);
        unsigned index = get_le16(b + 12), sub = get_le16(b + 14);
        if (index != i)
            return BADGE_E_FORMAT;
        uint32_t k = 0;
        while (k < n_sets && get_le32(set_info(mng, k) + 0x10) != set_id)
            k++;
        if (k == n_sets || !sub_known(sub) || badge_id == 0 || badge_id == B_NONE)
            return BADGE_E_FORMAT;  /* no set record, or an empty or unknown identity */
        for (uint32_t j = 0; j < i; j++)
            if (!memcmp(badge_info(mng, j) + 4, b + 4, 8) && get_le16(badge_info(mng, j) + 14) == sub)
                return BADGE_E_FORMAT;  /* a repeated identity */
        owner[i] = (uint8_t)k;
        members[k]++;
        all += get_le16(b + 0x12);
    }
    uint32_t start = 0;
    for (uint32_t k = 0; k < n_sets; k++) {
        const uint8_t *s = set_info(mng, k);
        uint64_t copies = 0, on = 0;
        for (uint32_t i = start; i < start + members[k]; i++) {
            if (owner[i] != k)
                return BADGE_E_FORMAT;  /* its badges are not one block in record order */
            copies += get_le16(badge_info(mng, i) + 0x12);
            on += get_le16(badge_info(mng, i) + 0x10);
        }
        if (get_le32(s + 0x24) != start || get_le32(s + 0x1C) != members[k] || get_le32(s + 0x20) != (uint32_t)copies)
            return BADGE_E_FORMAT;
        badge_set_info *info = &col->set[k];
        info->slot = k;
        info->set_id = get_le32(s + 0x10);
        info->index = get_le32(s + 0x14);
        info->count = members[k];
        info->start = start;
        info->total = (uint32_t)copies;
        info->placed = (uint32_t)on;
        set_name(data, info->index, info->name);
        start += members[k];
    }
    if (total != (uint32_t)all)
        return BADGE_E_FORMAT;

    /* Placement problems are warnings: HOME recounts placements and empties those whose badge is gone */
    uint16_t counts[BADGE_BADGES_MAX] = {0};
    for (unsigned n = 0; n < BADGE_PLACED_MAX; n++) {
        const uint8_t *r = layout_record(mng, n);
        if (record_free(r))
            continue;
        col->layout++;
        unsigned i = get_le16(r + 12);   /* a badge's identity holds its index */
        if (i < unique && !memcmp(r, badge_info(mng, i), 16))
            counts[i]++;
        else
            col->unknown++;
    }
    if (col->unknown)
        col->warnings |= BADGE_WARN_UNKNOWN;
    if (col->layout != placed)
        col->warnings |= BADGE_WARN_PLACED;
    for (uint32_t i = 0; i < unique; i++)
        if (get_le16(badge_info(mng, i) + 0x10) != counts[i])
            col->warnings |= BADGE_WARN_COUNTS;
    col->sets = n_sets;
    col->badges = unique;
    col->placed = placed;
    col->nnid_set = get_le32(mng + BM_NNID) != 0;
    return BADGE_OK;
}

/* ---------------------------------------------------------------- the manifest */

static int mark(uint8_t *bits, uint32_t first, uint32_t end, uint32_t id)
{
    if (id < first || id >= end || bits[(id - first) / 8] >> ((id - first) % 8) & 1)
        return 0;
    bits[(id - first) / 8] |= (uint8_t)(1u << ((id - first) % 8));
    return 1;
}

int badge_manifest_check(const badge_manifest *m)
{
    if (!m)
        return BADGE_OK;
    if (m->count && !m->sets)
        return BADGE_E_ARG;
    uint8_t sets[(BADGE_SET_ID_END - BADGE_SET_ID_FIRST) / 8] = {0};
    uint8_t badges[(BADGE_ID_END - BADGE_ID_FIRST) / 8] = {0};
    uint32_t top_set = BADGE_SET_ID_FIRST - 1, top_badge = BADGE_ID_FIRST - 1;
    for (size_t k = 0; k < m->count; k++) {
        const badge_manifest_set *s = &m->sets[k];
        if (!s->folder || !s->name || (s->count && !s->badges))
            return BADGE_E_MANIFEST;
        if (!mark(sets, BADGE_SET_ID_FIRST, BADGE_SET_ID_END, s->set_id))
            return BADGE_E_MANIFEST;
        top_set = s->set_id > top_set ? s->set_id : top_set;
        for (size_t j = 0; j < k; j++)
            if (!strcmp(m->sets[j].folder, s->folder))
                return BADGE_E_MANIFEST;
        for (size_t i = 0; i < s->count; i++) {
            if (!s->badges[i].file || !mark(badges, BADGE_ID_FIRST, BADGE_ID_END, s->badges[i].badge_id))
                return BADGE_E_MANIFEST;
            top_badge = s->badges[i].badge_id > top_badge ? s->badges[i].badge_id : top_badge;
        }
    }
    if (m->next_set_id <= top_set || m->next_badge_id <= top_badge)
        return BADGE_E_MANIFEST;
    return BADGE_OK;
}

static const badge_manifest_set *manifest_by_id(const badge_manifest *m, uint32_t set_id)
{
    for (size_t k = 0; m && k < m->count; k++)
        if (m->sets[k].set_id == set_id)
            return &m->sets[k];
    return NULL;
}

/* What badge_remove_last says before it looks at undo records, in its order */
static int last_check(const badge_collection *col, const uint8_t *mng, const badge_manifest *m)
{
    if (!col->sets)
        return BADGE_E_EMPTY;
    const badge_set_info *last = &col->set[col->sets - 1];
    if (!manifest_by_id(m, last->set_id))
        return BADGE_E_NOT_OURS;
    if (last->start + last->count != col->badges || last->count == 0)
        return BADGE_E_LAST;
    for (uint32_t i = last->start; i < col->badges; i++)
        if (get_le16(badge_info(mng, i) + 0x10))
            return BADGE_E_PLACED;
    for (unsigned n = 0; n < BADGE_PLACED_MAX; n++)
        if (!record_free(layout_record(mng, n)) && get_le32(layout_record(mng, n) + 8) == last->set_id)
            return BADGE_E_PLACED;
    return BADGE_OK;
}

int badge_read(const uint8_t *data, size_t data_size, const uint8_t *mng, size_t mng_size,
               const badge_manifest *manifest, badge_collection *col)
{
    if (!data || !mng || !col)
        return BADGE_E_ARG;
    memset(col, 0, sizeof *col);
    if (data_size != BADGE_DATA_SIZE || mng_size != BADGE_MNG_SIZE)
        return BADGE_E_SIZE;
    int r = parse(data, mng, col);
    if (!r)
        r = badge_manifest_check(manifest);
    if (r) {
        memset(col, 0, sizeof *col);
        return r;
    }
    for (uint32_t k = 0; k < col->sets; k++)
        col->set[k].mine = manifest_by_id(manifest, col->set[k].set_id) != NULL;
    col->remove = last_check(col, mng, manifest);
    if (col->remove == BADGE_OK)
        col->set[col->sets - 1].removable = 1;
    return BADGE_OK;
}

int badge_set_icon(const uint8_t *data, size_t data_size, const badge_set_info *set, uint8_t icon[BADGE_ICON_BYTES])
{
    if (!data || !set || !icon || set->index >= BADGE_SETS_MAX)
        return BADGE_E_ARG;
    if (data_size != BADGE_DATA_SIZE)
        return BADGE_E_SIZE;
    const uint8_t *src = data + BD_SET_ICONS + set->index * BD_SET_ICON_SIZE;
    for (uint32_t y = 0; y < B_ICON; y++)
        for (uint32_t x = 0; x < B_ICON; x++)
            memcpy(icon + 2 * badge_tiled(x, y, B_ICON), src + 2 * badge_tiled(x, y, 64), 2);
    return BADGE_OK;
}

/* ---------------------------------------------------------------- the areas of one set, and the checks */

void badge_regions(uint32_t s, uint32_t u, uint32_t n, badge_region r[REGIONS])
{
    int64_t last = (int64_t)u + n - 1;
    uint32_t bits = (uint32_t)((last >= 0 ? last / 8 : -1) - u / 8 + 1);
    const badge_region list[REGIONS] = {
        {REG_MNG, BM_SET_COUNT, 4},
        {REG_MNG, BM_UNIQUE, 4},
        {REG_MNG, BM_TOTAL, 4},
        {REG_MNG, BM_USED_BADGES + u / 8, bits},
        {REG_MNG, BM_USED_SETS + s / 8, 1},
        {REG_MNG, BM_BADGE_INFO + u * BM_BADGE_INFO_SIZE, n * BM_BADGE_INFO_SIZE},
        {REG_MNG, BM_SET_INFO + s * BM_SET_INFO_SIZE, BM_SET_INFO_SIZE},
        {REG_DATA, BD_SET_TITLES + s * BD_TITLE_BLOCK, BD_TITLE_BLOCK},
        {REG_DATA, BD_BADGE_TITLES + u * BD_TITLE_BLOCK, n * BD_TITLE_BLOCK},
        {REG_DATA, BD_SET_ICONS + s * BD_SET_ICON_SIZE, BD_SET_ICON_SIZE},
        {REG_DATA, BD_BADGE_ICONS + u * BD_BADGE_ICON_SIZE, n * BD_BADGE_ICON_SIZE},
        {REG_DATA, BD_SMALL_ICONS + u * BD_SMALL_ICON_SIZE, n * BD_SMALL_ICON_SIZE},
    };
    memcpy(r, list, sizeof list);
}

static size_t regions_size(const badge_region *r)
{
    size_t total = 0;
    for (int i = 0; i < REGIONS; i++)
        total += r[i].size;
    return total;
}

static uint8_t *file_of(int file, uint8_t *data, uint8_t *mng) { return file == REG_DATA ? data : mng; }

/* SHA-256 of the regions' bytes, one after the other (region_bytes) */
static void regions_hash(const uint8_t *data, const uint8_t *mng, const badge_region *r, uint8_t digest[32])
{
    sha256_ctx c;
    sha256_init(&c);
    for (int i = 0; i < REGIONS; i++)
        sha256_update(&c, (r[i].file == REG_DATA ? data : mng) + r[i].offset, r[i].size);
    sha256_final(&c, digest);
}

/* The spans of one file among the regions (and an extra span), sorted by start */
static size_t spans_of(const badge_region *r, int file, const badge_region *extra, uint32_t start[REGIONS + 1],
                       uint32_t end[REGIONS + 1])
{
    size_t n = 0;
    for (int i = 0; i <= REGIONS; i++) {
        const badge_region *g = i < REGIONS ? &r[i] : extra;
        if (!g || g->file != file)
            continue;
        size_t at = n++;
        while (at > 0 && start[at - 1] > g->offset) {
            start[at] = start[at - 1];
            end[at] = end[at - 1];
            at--;
        }
        start[at] = g->offset;
        end[at] = g->offset + g->size;
    }
    return n;
}

/* BadgeData.dat in one pass: the SHA-256 of the whole file and of every byte outside the regions */
static void data_hashes(const uint8_t *data, const badge_region *r, uint8_t full[32], uint8_t outside[32])
{
    uint32_t start[REGIONS + 1], end[REGIONS + 1];
    size_t n = spans_of(r, REG_DATA, NULL, start, end);
    sha256_ctx a, b;
    sha256_init(&a);
    sha256_init(&b);
    uint32_t pos = 0;
    for (size_t i = 0; i <= n; i++) {
        uint32_t s = i < n ? start[i] : BADGE_DATA_SIZE, e = i < n ? end[i] : BADGE_DATA_SIZE;
        if (s > pos) {
            sha256_update(&a, data + pos, s - pos);
            sha256_update(&b, data + pos, s - pos);
            pos = s;
        }
        if (e > pos) {
            sha256_update(&a, data + pos, e - pos);
            pos = e;
        }
    }
    sha256_final(&a, full);
    sha256_final(&b, outside);
}

int badge_mng_changed(const uint8_t *before, const uint8_t *after, const badge_region *r, const badge_region *extra,
                       uint32_t s, int set_allowed, uint32_t u, uint32_t n)
{
    uint32_t start[REGIONS + 1], end[REGIONS + 1];
    size_t count = spans_of(r, REG_MNG, extra, start, end);
    uint32_t pos = 0;
    for (size_t i = 0; i <= count; i++) {
        uint32_t a = i < count ? start[i] : BADGE_MNG_SIZE, b = i < count ? end[i] : BADGE_MNG_SIZE;
        if (a > pos && memcmp(before + pos, after + pos, a - pos))
            return 1;
        pos = b > pos ? b : pos;
    }
    for (uint32_t k = 0; k < BM_USED_BADGES_SIZE * 8; k++)
        if (bit(before, BM_USED_BADGES, k) != bit(after, BM_USED_BADGES, k) && !(k >= u && k < u + n))
            return 1;
    for (uint32_t k = 0; k < BM_USED_SETS_SIZE * 8; k++)
        if (bit(before, BM_USED_SETS, k) != bit(after, BM_USED_SETS, k) && !(set_allowed && k == s))
            return 1;
    return 0;
}

/* same_parse: the first s sets and u badges of the new reading differ in any field from the old one, or the layout
 * differs */
static int old_changed(const uint8_t *old_mng, const badge_collection *old, const uint8_t *new_mng,
                       const badge_collection *now, uint32_t s, uint32_t u)
{
    for (uint32_t k = 0; k < s && k < now->sets; k++)
        if (memcmp(set_info(old_mng, k), set_info(new_mng, k), BM_SET_INFO_SIZE) ||
            strcmp(old->set[k].name, now->set[k].name) || old->set[k].placed != now->set[k].placed)
            return 1;
    for (uint32_t i = 0; i < u && i < now->badges; i++)
        if (memcmp(badge_info(old_mng, i), badge_info(new_mng, i), BM_BADGE_INFO_SIZE))
            return 1;
    for (unsigned n = 0; n < BADGE_PLACED_MAX; n++) {
        const uint8_t *a = layout_record(old_mng, n), *b = layout_record(new_mng, n);
        int fa = record_free(a), fb = record_free(b);
        if (fa != fb || (!fa && memcmp(a, b, BM_LAYOUT_RECORD)))
            return 1;
    }
    return now->sets < s || now->badges < u;
}

/* ---------------------------------------------------------------- the undo record
 * All little-endian: magic, version, set ID, slot, start, count, size of the areas, the case's view before, the
 * SHA-256 of both files before and after the append, of the areas after it, and of what the record restores (the
 * view's 8 bytes, then the areas' earlier bytes), then those bytes in the order of badge_regions(). */

static const uint8_t UNDO_MAGIC[8] = {'B', 'A', 'D', 'G', 'U', 'N', 'D', 'O'};
#define UNDO_VERSION   1u
#define U_SET_ID       0x0Cu
#define U_SLOT         0x10u
#define U_START        0x14u
#define U_COUNT        0x18u
#define U_SIZE         0x1Cu
#define U_VIEW         0x20u
#define U_IN_DATA      0x28u
#define U_IN_MNG       0x48u
#define U_OUT_DATA     0x68u
#define U_OUT_MNG      0x88u
#define U_AFTER        0xA8u
#define U_BEFORE_SHA   0xC8u
#define U_HEADER       0xE8u

/* The SHA-256 of what a record puts back: the view, then the areas */
static void restored_hash(const uint8_t *rec, size_t areas, uint8_t digest[32])
{
    sha256_ctx c;
    sha256_init(&c);
    sha256_update(&c, rec + U_VIEW, 8);
    sha256_update(&c, rec + U_HEADER, areas);
    sha256_final(&c, digest);
}

/* Puts the regions' bytes from src (the undo bytes) back into the files */
static void put_regions(uint8_t *data, uint8_t *mng, const badge_region *r, const uint8_t *src)
{
    for (int i = 0; i < REGIONS; i++) {
        memcpy(file_of(r[i].file, data, mng) + r[i].offset, src, r[i].size);
        src += r[i].size;
    }
}

static void get_regions(const uint8_t *data, const uint8_t *mng, const badge_region *r, uint8_t *dst)
{
    for (int i = 0; i < REGIONS; i++) {
        memcpy(dst, (r[i].file == REG_DATA ? data : mng) + r[i].offset, r[i].size);
        dst += r[i].size;
    }
}

/* ---------------------------------------------------------------- append */

void badge_append_free(badge_append_result *result)
{
    if (!result)
        return;
    tp_free(result->badges);
    tp_free(result->undo);
    memset(result, 0, sizeof *result);
}

static int same_tile(const badge_file *files, const badge_tile *t, const badge_known *k)
{
    return t->tile == k->tile && !strcmp(files[t->file].name, k->file);
}

/* assign_ids: the set's ID, and its badges in manifest order (the known ones present in the folder, then the new
 * ones in tile order with the next IDs) */
static int assign(const badge_manifest *m, const badge_manifest_set *known, const badge_file *files,
                  const badge_folder *folder, badge_append_result *res)
{
    if (known) {
        res->set_id = known->set_id;
        res->next_set_id = m->next_set_id;
    } else {
        uint32_t next = m ? m->next_set_id : BADGE_SET_ID_FIRST;
        if (next < BADGE_SET_ID_FIRST || next >= BADGE_SET_ID_END)
            return BADGE_E_IDS;
        res->set_id = next;
        res->new_set = 1;
        res->next_set_id = next + 1;
    }
    res->next_badge_id = m ? m->next_badge_id : BADGE_ID_FIRST;
    size_t kn = known ? known->count : 0, cap = kn + folder->count;
    if (!(res->badges = tp_alloc((cap ? cap : 1) * sizeof *res->badges)))
        return BADGE_E_NOMEM;
    size_t n = 0;
    for (size_t j = 0; j < kn; j++)
        for (size_t t = 0; t < folder->count; t++)
            if (same_tile(files, &folder->tiles[t], &known->badges[j])) {
                res->badges[n++] = (badge_written){0, known->badges[j].badge_id, folder->tiles[t].file,
                                                   folder->tiles[t].tile, 0};
                break;
            }
    for (size_t t = 0; t < folder->count; t++) {
        size_t j = 0;
        while (j < kn && !same_tile(files, &folder->tiles[t], &known->badges[j]))
            j++;
        if (j < kn)
            continue;
        if (res->next_badge_id < BADGE_ID_FIRST || res->next_badge_id >= BADGE_ID_END)
            return BADGE_E_IDS;
        res->badges[n++] = (badge_written){0, res->next_badge_id++, folder->tiles[t].file, folder->tiles[t].tile, 1};
    }
    if (n > 0xFFFFFFFFu)
        return BADGE_E_FULL;
    res->count = (uint32_t)n;
    return BADGE_OK;
}

static const badge_tile *tile_of(const badge_folder *folder, const badge_written *w)
{
    for (size_t t = 0; t < folder->count; t++)
        if (folder->tiles[t].file == w->file && folder->tiles[t].tile == w->tile)
            return &folder->tiles[t];
    return NULL;
}

/* The new set's images: every PNG decoded once more, each of its badges written; the icon */
static int write_images(uint8_t *data, const badge_file *files, const badge_folder *folder,
                        const badge_append_result *res)
{
    uint32_t u = res->start, s = res->slot;
    for (uint32_t k = 0; k < res->count; k++) {
        uint32_t f = res->badges[k].file, j = 0;
        while (j < k && res->badges[j].file != f)
            j++;
        if (j < k)
            continue;               /* this file is done */
        uint32_t w, h;
        uint8_t *rgba;
        int r = badge_png_decode(files[f].data, files[f].size, &w, &h, &rgba);
        if (r)
            return r;
        for (j = k; j < res->count; j++) {
            if (res->badges[j].file != f)
                continue;
            const badge_tile *t = tile_of(folder, &res->badges[j]);
            if (!t || t->x0 + B_TILE > w || t->y0 + B_TILE > h) {
                tp_free(rgba);
                return BADGE_E_REFUSED; /* the file decoded differently the second time */
            }
            badge_tile_images(rgba, w, t->x0, t->y0, data + BD_BADGE_ICONS + (u + j) * BD_BADGE_ICON_SIZE,
                              data + BD_SMALL_ICONS + (u + j) * BD_SMALL_ICON_SIZE);
            if (j == 0 && folder->icon < 0)     /* the first badge's 64x64 tile */
                badge_icon_image(rgba, w, t->x0, t->y0, B_TILE, data + BD_SET_ICONS + s * BD_SET_ICON_SIZE);
        }
        tp_free(rgba);
    }
    if (folder->icon >= 0) {
        const badge_file *icon = &files[folder->icon];
        uint32_t w, h;
        uint8_t *rgba;
        int r = badge_png_decode(icon->data, icon->size, &w, &h, &rgba);
        if (r)
            return r;
        if (w != B_ICON || h != B_ICON) {
            tp_free(rgba);
            return BADGE_E_REFUSED;
        }
        badge_icon_image(rgba, w, 0, 0, B_ICON, data + BD_SET_ICONS + s * BD_SET_ICON_SIZE);
        tp_free(rgba);
    }
    return BADGE_OK;
}

int badge_append(uint8_t *data, size_t data_size, uint8_t *mng, size_t mng_size, const badge_file *files,
                 size_t count, const badge_manifest *manifest, const char *folder_name, const char *name,
                 badge_append_result *res)
{
    if (!res)
        return BADGE_E_ARG;
    memset(res, 0, sizeof *res);
    if (!data || !mng || !folder_name || (count && !files))
        return BADGE_E_ARG;
    if (data_size != BADGE_DATA_SIZE || mng_size != BADGE_MNG_SIZE)
        return BADGE_E_SIZE;
    badge_collection *col = tp_alloc(2 * sizeof *col), *now = col ? col + 1 : NULL;
    uint8_t *old_mng = tp_alloc(BADGE_MNG_SIZE);
    badge_folder folder = {NULL, 0, -1};
    int r = col && old_mng ? BADGE_OK : BADGE_E_NOMEM, written = 0;
    if (!r)
        r = parse(data, mng, col);
    if (!r)
        r = badge_folder_read(files, count, &folder);
    if (!r)
        r = badge_manifest_check(manifest);
    const badge_manifest_set *known = NULL;
    for (size_t k = 0; !r && manifest && k < manifest->count; k++)
        if (!strcmp(manifest->sets[k].folder, folder_name))
            known = &manifest->sets[k];
    const char *set_name = name ? name : known ? known->name : folder_name;
    if (!r)
        r = name_check(set_name, strlen(set_name));
    if (!r)
        r = assign(manifest, known, files, &folder, res);

    uint32_t s = col ? col->sets : 0, u = col ? col->badges : 0, n = res->count;
    for (uint32_t k = 0; !r && k < s; k++)
        if (col->set[k].set_id == res->set_id)
            r = BADGE_E_PRESENT;
    if (!r && (s + 1 > BADGE_SETS_MAX || (uint64_t)u + n > BADGE_BADGES_MAX))
        r = BADGE_E_FULL;
    for (uint32_t k = 0; !r && k < s; k++)
        if (col->set[k].index == s)
            r = BADGE_E_INDEX;      /* this file numbers its sets differently from Anemone3DS */
    for (uint32_t k = 0; !r && k < n; k++)
        for (uint32_t i = 0; i < u; i++)
            if (get_le32(badge_info(mng, i) + 4) == res->badges[k].badge_id)
                r = BADGE_E_TAKEN;

    /* The undo record first: the areas' bytes before, the sums of the input */
    badge_region regs[REGIONS];
    uint8_t outside[32], outside_after[32];
    if (!r) {
        badge_regions(s, u, n, regs);
        size_t areas = regions_size(regs);
        res->undo_size = U_HEADER + areas;
        if (!(res->undo = tp_alloc(res->undo_size)))
            r = BADGE_E_NOMEM;
    }
    if (!r) {
        uint8_t *rec = res->undo;
        memset(rec, 0, U_HEADER);
        memcpy(rec, UNDO_MAGIC, 8);
        put_le32(rec + 8, UNDO_VERSION);
        put_le32(rec + U_SET_ID, res->set_id);
        put_le32(rec + U_SLOT, s);
        put_le32(rec + U_START, u);
        put_le32(rec + U_COUNT, n);
        put_le32(rec + U_SIZE, (uint32_t)(res->undo_size - U_HEADER));
        memcpy(rec + U_VIEW, mng + BM_VIEW, 8);
        get_regions(data, mng, regs, rec + U_HEADER);
        restored_hash(rec, res->undo_size - U_HEADER, rec + U_BEFORE_SHA);
        data_hashes(data, regs, rec + U_IN_DATA, outside);
        sha256(mng, BADGE_MNG_SIZE, rec + U_IN_MNG);
        memcpy(old_mng, mng, BADGE_MNG_SIZE);

        /* The new set */
        written = 1;
        res->slot = s;
        res->start = u;
        uint32_t old_total = get_le32(mng + BM_TOTAL);
        put_le32(mng + BM_SET_COUNT, s + 1);
        put_le32(mng + BM_UNIQUE, u + n);
        put_le32(mng + BM_TOTAL, old_total + n * B_QUANTITY);
        res->set_bit = s ? bit(old_mng, BM_USED_SETS, col->set[s - 1].slot) : 1;
        if (res->set_bit)
            mng[BM_USED_SETS + s / 8] |= (uint8_t)(1u << (s % 8));
        for (uint32_t k = 0; k < n; k++) {
            uint32_t i = u + k;
            badge_written *w = &res->badges[k];
            const badge_tile *t = tile_of(&folder, w);
            uint8_t *b = mng + BM_BADGE_INFO + i * BM_BADGE_INFO_SIZE;
            w->index = i;
            mng[BM_USED_BADGES + i / 8] |= (uint8_t)(1u << (i % 8));
            put_le32(b, 0);
            put_le32(b + 4, w->badge_id);
            put_le32(b + 8, res->set_id);
            put_le16(b + 12, (uint16_t)i);
            put_le16(b + 14, 0);                /* sub */
            put_le16(b + 16, 0);                /* placed */
            put_le16(b + 18, B_QUANTITY);
            put_le32(b + 20, 0);                /* flags */
            memset(b + 24, 0xFF, 16);           /* no shortcut */
            name_block(files[t->file].name, t->name_len, data + BD_BADGE_TITLES + i * BD_TITLE_BLOCK);
        }
        uint8_t *si = mng + BM_SET_INFO + s * BM_SET_INFO_SIZE;
        memset(si, 0xFF, 8);
        put_le32(si + 0x8, 0);
        put_le32(si + 0xC, B_SET_UNKNOWN_0C);
        put_le32(si + 0x10, res->set_id);
        put_le32(si + 0x14, s);
        put_le32(si + 0x18, B_NONE);
        put_le32(si + 0x1C, n);
        put_le32(si + 0x20, n * B_QUANTITY);
        put_le32(si + 0x24, u);
        put_le32(si + 0x28, 0);
        put_le32(si + 0x2C, 0);
        name_block(set_name, strlen(set_name), data + BD_SET_TITLES + s * BD_TITLE_BLOCK);
        res->icon_from_file = folder.icon >= 0;
        r = write_images(data, files, &folder, res);
    }

    /* The invariant, checked on the finished buffers: nothing that exists changes */
    if (!r) {
        uint8_t *rec = res->undo;
        data_hashes(data, regs, rec + U_OUT_DATA, outside_after);
        int bad = memcmp(outside, outside_after, 32) ||
                  badge_mng_changed(old_mng, mng, regs, NULL, s, res->set_bit, u, n);
        r = parse(data, mng, now);
        if (!r) {
            bad |= old_changed(old_mng, col, mng, now, s, u);
            bad |= get_le32(mng + BM_SET_COUNT) != s + 1 || get_le32(mng + BM_UNIQUE) != u + n ||
                   get_le32(mng + BM_TOTAL) != get_le32(old_mng + BM_TOTAL) + n * B_QUANTITY;
            const badge_set_info *added = now->sets == s + 1 ? &now->set[s] : NULL;
            bad |= !added || added->set_id != res->set_id || added->start != u || added->count != n ||
                   strcmp(added->name, set_name);
            for (uint32_t k = 0; !bad && k < n; k++)
                bad |= get_le32(badge_info(mng, u + k) + 4) != res->badges[k].badge_id;
            if (bad)
                r = BADGE_E_REFUSED;
        }
    }
    if (!r) {
        uint8_t *rec = res->undo;
        sha256(mng, BADGE_MNG_SIZE, rec + U_OUT_MNG);
        regions_hash(data, mng, regs, rec + U_AFTER);
        res->warnings = now->warnings;
        res->unknown = now->unknown;
        size_t len = strlen(set_name);
        memcpy(res->name, set_name, len + 1 <= sizeof res->name ? len + 1 : 0);
    } else if (written) {
        put_regions(data, mng, regs, res->undo + U_HEADER);
        memcpy(mng, old_mng, BADGE_MNG_SIZE);
    }
    badge_folder_free(&folder);
    tp_free(old_mng);
    tp_free(col);
    if (r) {
        badge_append_free(res);
        return r;
    }
    return BADGE_OK;
}

/* ---------------------------------------------------------------- remove the last set */

/* Where the set's badge records and its set record are among badge_regions' areas */
enum { AREA_BADGES = 5, AREA_SET = 6 };

/* The SHA-256 of the set's areas with HOME's own fields in them at the value every append writes there (0): each
 * badge's placed count (record + 0x10) and the set record's + 0x28, which HOME was seen to change once it had shown
 * the set (its meaning is not known). It equals the record's sum of the areas as the append left them exactly when
 * every byte the application wrote is unchanged, whatever HOME put into its two fields since. */
static void own_hash(const uint8_t *data, const uint8_t *mng, const badge_region *r, uint8_t digest[32])
{
    sha256_ctx c;
    sha256_init(&c);
    for (int i = 0; i < REGIONS; i++) {
        const uint8_t *p = (r[i].file == REG_DATA ? data : mng) + r[i].offset;
        uint8_t rec[BM_SET_INFO_SIZE];
        if (i == AREA_BADGES) {
            for (uint32_t off = 0; off < r[i].size; off += BM_BADGE_INFO_SIZE) {
                memcpy(rec, p + off, BM_BADGE_INFO_SIZE);
                put_le16(rec + 0x10, 0);
                sha256_update(&c, rec, BM_BADGE_INFO_SIZE);
            }
        } else if (i == AREA_SET) {
            memcpy(rec, p, BM_SET_INFO_SIZE);
            put_le32(rec + 0x28, 0);
            sha256_update(&c, rec, BM_SET_INFO_SIZE);
        } else {
            sha256_update(&c, p, r[i].size);
        }
    }
    sha256_final(&c, digest);
}

/* The badge case's view (0x10 the selected set, 0x14 its column; slot and index are equal, remove_decide refuses
 * otherwise) is HOME's and stays, unless it names the set that goes or no set at all (an index from s on, other than 0xFFFFFFFF: none selected); then it must
 * change, and it gets the value HOME had given it before the append (test B4: HOME took that value back). */
static int view_goes(const uint8_t *mng, uint32_t s)
{
    uint32_t v = get_le32(mng + BM_VIEW);
    return v != B_NONE && v >= s;
}

typedef struct removal {
    uint32_t s, u, n;
    badge_region regs[REGIONS];
    size_t areas, record;
    const uint8_t *rec;
    int exact;
    uint8_t outside[32];        /* BadgeData.dat outside the set's areas */
} removal;

/* Everything a removal decides before it changes a byte, in its order: the collection (into col) and its last set,
 * the records of that set at these indices, the one the files are exactly the output of, else (allow_changes) the
 * one whose areas hold what the application wrote, and that record's own sum. */
static int remove_decide(const uint8_t *data, const uint8_t *mng, const badge_manifest *manifest,
                         const badge_undo *records, size_t count, int allow_changes, badge_collection *col,
                         removal *p)
{
    memset(p, 0, sizeof *p);
    int r = parse(data, mng, col);
    if (!r && !col->sets)
        r = BADGE_E_EMPTY;
    if (!r)
        r = badge_manifest_check(manifest);
    if (!r)
        r = last_check(col, mng, manifest);
    for (uint32_t k = 0; !r && k < col->sets; k++)
        if (col->set[k].index != k)
            r = BADGE_E_FORMAT;     /* the view names a set by a number that is the slot or the index: not known */
    if (r)
        return r;
    const badge_set_info *last = &col->set[col->sets - 1];
    p->s = last->slot, p->u = last->start, p->n = last->count;
    badge_regions(p->s, p->u, p->n, p->regs);
    p->areas = regions_size(p->regs);
    uint8_t given_data[32], given_mng[32], own[32];
    data_hashes(data, p->regs, given_data, p->outside);
    sha256(mng, BADGE_MNG_SIZE, given_mng);
    own_hash(data, mng, p->regs, own);

    size_t fits = 0, matching = 0;
    for (int pass = 0; pass < 3; pass++) {
        for (size_t i = 0; i < count; i++) {
            const uint8_t *q = records[i].data;
            if (!q || records[i].size < U_HEADER || memcmp(q, UNDO_MAGIC, 8) || get_le32(q + 8) != UNDO_VERSION)
                return BADGE_E_UNDO;
            if (get_le32(q + U_SET_ID) != last->set_id || get_le32(q + U_SLOT) != p->s ||
                get_le32(q + U_START) != p->u || get_le32(q + U_COUNT) != p->n)
                continue;
            if (pass == 0) {
                if (get_le32(q + U_SIZE) != p->areas || records[i].size != U_HEADER + p->areas)
                    return BADGE_E_UNDO;
                matching++;
            } else if (pass == 1 ? !memcmp(q + U_OUT_DATA, given_data, 32) && !memcmp(q + U_OUT_MNG, given_mng, 32)
                                 : !memcmp(q + U_AFTER, own, 32)) {
                fits++;
                p->record = i;
            }
        }
        if (pass == 0 && !matching)
            return BADGE_E_NO_UNDO;
        if (pass == 1) {
            p->exact = fits > 0;
            if (fits)
                break;
            if (!allow_changes)
                return BADGE_E_CHANGED;
        }
        if (pass == 2 && !fits)
            return BADGE_E_SET_CHANGED;
    }
    if (fits > 1)
        return BADGE_E_AMBIGUOUS;
    p->rec = records[p->record].data;
    uint8_t check[32];
    restored_hash(p->rec, p->areas, check);
    return memcmp(check, p->rec + U_BEFORE_SHA, 32) ? BADGE_E_UNDO : BADGE_OK;
}

static void planned(badge_remove_result *res, const badge_collection *col, const removal *p, const uint8_t *mng)
{
    res->set_id = col->set[col->sets - 1].set_id;
    res->slot = p->s;
    res->start = p->u;
    res->count = p->n;
    memcpy(res->name, col->set[col->sets - 1].name, sizeof res->name);
    res->record = p->record;
    res->exact = p->exact;
    res->view_reset = view_goes(mng, p->s) && memcmp(mng + BM_VIEW, p->rec + U_VIEW, 8) != 0;
}

int badge_remove_check(const uint8_t *data, size_t data_size, const uint8_t *mng, size_t mng_size,
                       const badge_manifest *manifest, const badge_undo *records, size_t count, int allow_changes,
                       badge_remove_result *res)
{
    if (!res)
        return BADGE_E_ARG;
    memset(res, 0, sizeof *res);
    if (!data || !mng || (count && !records))
        return BADGE_E_ARG;
    if (data_size != BADGE_DATA_SIZE || mng_size != BADGE_MNG_SIZE)
        return BADGE_E_SIZE;
    badge_collection *col = tp_alloc(sizeof *col);
    removal p;
    int r = col ? remove_decide(data, mng, manifest, records, count, allow_changes, col, &p) : BADGE_E_NOMEM;
    if (!r)
        planned(res, col, &p, mng);
    tp_free(col);
    return r;
}

int badge_remove_last(uint8_t *data, size_t data_size, uint8_t *mng, size_t mng_size,
                      const badge_manifest *manifest, const badge_undo *records, size_t count, int allow_changes,
                      badge_remove_result *res)
{
    if (!res)
        return BADGE_E_ARG;
    memset(res, 0, sizeof *res);
    if (!data || !mng || (count && !records))
        return BADGE_E_ARG;
    if (data_size != BADGE_DATA_SIZE || mng_size != BADGE_MNG_SIZE)
        return BADGE_E_SIZE;
    badge_collection *col = tp_alloc(2 * sizeof *col), *now = col ? col + 1 : NULL;
    uint8_t *old_mng = tp_alloc(BADGE_MNG_SIZE), *saved = NULL;
    removal p;
    int r = col && old_mng ? BADGE_OK : BADGE_E_NOMEM;
    if (!r)
        r = remove_decide(data, mng, manifest, records, count, allow_changes, col, &p);
    if (!r && !(saved = tp_alloc(p.areas)))
        r = BADGE_E_NOMEM;

    int written = 0;
    if (!r) {
        planned(res, col, &p, mng);
        get_regions(data, mng, p.regs, saved);
        memcpy(old_mng, mng, BADGE_MNG_SIZE);
        written = 1;
        /* Only what takes the set out: its areas get their bytes from before the append (its own record whole, with
         * HOME's + 0x28 in it; its badges' placed counts are 0), the view only if it names the set; every other field
         * keeps the value HOME left in it */
        put_regions(data, mng, p.regs, p.rec + U_HEADER);
        if (view_goes(old_mng, p.s))
            memcpy(mng + BM_VIEW, p.rec + U_VIEW, 8);

        uint8_t after_data[32], after_mng[32], outside_after[32];
        const badge_region view = {REG_MNG, BM_VIEW, BM_TOTAL - BM_VIEW};
        data_hashes(data, p.regs, after_data, outside_after);
        sha256(mng, BADGE_MNG_SIZE, after_mng);
        int bad = memcmp(p.outside, outside_after, 32) ||
                  badge_mng_changed(old_mng, mng, p.regs, &view, p.s, 1, p.u, p.n);
        r = parse(data, mng, now);
        if (!r) {
            bad |= old_changed(old_mng, col, mng, now, p.s, p.u);
            bad |= now->sets != p.s || now->badges != p.u;
            res->identical = !memcmp(after_data, p.rec + U_IN_DATA, 32) && !memcmp(after_mng, p.rec + U_IN_MNG, 32);
            bad |= res->exact && !res->identical;
            if (bad)
                r = BADGE_E_REFUSED;
        }
    }
    if (r && written) {
        put_regions(data, mng, p.regs, saved);
        memcpy(mng, old_mng, BADGE_MNG_SIZE);
    }
    tp_free(saved);
    tp_free(old_mng);
    tp_free(col);
    if (r)
        memset(res, 0, sizeof *res);
    return r;
}

/* ---------------------------------------------------------------- the saved layouts */

static void check_layout(const uint8_t *copy, const uint8_t *mng, badge_layout_check *c)
{
    if (get_le32(copy)) {
        c->damaged = 1;
        return;
    }
    c->header_placed = get_le32(copy + BM_PLACED);
    for (unsigned n = 0; n < BADGE_PLACED_MAX; n++) {
        const uint8_t *r = layout_record(copy, n);
        if (record_free(r))
            continue;
        c->placements++;
        if (get_le32(r + 0x14) == B_FOLDER_ICON)
            c->folder_icons++;
        int by_ids = 0, same = 0;
        for (uint32_t i = 0; i < BADGE_BADGES_MAX && !same; i++) {
            if (!bit(mng, BM_USED_BADGES, i))
                continue;
            const uint8_t *b = badge_info(mng, i);
            if (!memcmp(r + 4, b + 4, 8) && !memcmp(r + 14, b + 14, 2)) {
                by_ids = 1;
                same = !memcmp(r, b, 16);
            }
        }
        if (!by_ids)
            c->missing++;
        else if (!same)
            c->differs++;
    }
}

static int layout_good(const badge_layout_check *c) { return !c->damaged && !c->missing && !c->differs; }

int badge_check_slots(const uint8_t *mng, size_t mng_size, const uint8_t *slots, size_t slots_size,
                      badge_slots_check *check)
{
    if (!mng || !slots || !check)
        return BADGE_E_ARG;
    memset(check, 0, sizeof *check);
    if (mng_size != BADGE_MNG_SIZE)
        return BADGE_E_SIZE;
    if (get_le32(mng) || get_le32(mng + BM_UNIQUE) > BADGE_BADGES_MAX)
        return BADGE_E_SLOTS;
    if (slots_size != BADGE_SLOTS_SIZE)
        return BADGE_E_SIZE;
    if (get_le32(slots))
        return BADGE_E_SLOTS;
    /* catalogue(): the identities of the used badges, each counted once */
    for (uint32_t i = 0; i < BADGE_BADGES_MAX; i++) {
        if (!bit(mng, BM_USED_BADGES, i))
            continue;
        uint32_t j = 0;
        while (j < i && !(bit(mng, BM_USED_BADGES, j) && !memcmp(badge_info(mng, j), badge_info(mng, i), 16)))
            j++;
        check->badges += j == i;
    }
    check->live.saved = 1;
    check_layout(mng, mng, &check->live);
    check->ok = layout_good(&check->live);
    for (unsigned n = 0; n < BADGE_LAYOUT_SLOTS; n++) {
        badge_layout_check *c = &check->slot[n];
        c->saved = slots[SL_FLAGS + n] != 0;
        check_layout(slots + SL_BADGES + n * BADGE_MNG_SIZE, mng, c);
        check->ok = check->ok && (layout_good(c) || !c->saved);
    }
    return BADGE_OK;
}
