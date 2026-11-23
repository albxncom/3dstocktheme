/* Content 0 (the catalogue), the TMD, the ticket, the delta and the index rule; formats as theme_pack.py writes
 * them (README there: "What the pack contains, and why"). */
#include "internal.h"

/* ---------------------------------------------------------------- catalogue checks */

static int check_catalogue(const tp_catalogue *c, const tp_entry **by_index)
{
    if (!c || (c->count && (!c->entries || !c->icon)))
        return TP_E_ARG;
    if (c->count > TP_THEMES_MAX)
        return TP_E_ARG;
    if (c->top < 1 || c->top > TP_INDEX_MAX)
        return TP_E_INDEX;
    for (size_t i = 0; i < c->count; i++) {
        const tp_entry *e = &c->entries[i];
        if (e->index < 1 || e->index > c->top || by_index[e->index])
            return TP_E_INDEX;
        if (!e->name || !utf8_valid(e->name) || (e->description && !utf8_valid(e->description)))
            return TP_E_ARG;
        by_index[e->index] = e;
    }
    return TP_OK;
}

/* ---------------------------------------------------------------- content 0 */

#define META_SIZE 0x54u
#define ENTRY_SIZE 0xC8u

/* Region 2 = EUR; languages as CFG numbers them, in the file order of theme_pack.py. */
static const struct { const char *code; uint16_t id; } LANGUAGES[8] = {
    {"en", 1}, {"es", 5}, {"fr", 2}, {"de", 3}, {"it", 4}, {"nl", 8}, {"pt", 9}, {"ru", 10},
};

static const char *const ARCHIVES[8] = {
    "ContentInfoArchive_EUR_en.bin", "ContentInfoArchive_EUR_es.bin", "ContentInfoArchive_EUR_fr.bin",
    "ContentInfoArchive_EUR_de.bin", "ContentInfoArchive_EUR_it.bin", "ContentInfoArchive_EUR_nl.bin",
    "ContentInfoArchive_EUR_pt.bin", "ContentInfoArchive_EUR_ru.bin",
};

static void solid_rgb565(uint8_t *out, size_t size, uint8_t r, uint8_t g, uint8_t b)
{
    uint16_t v = (uint16_t)((r >> 3) << 11 | (g >> 2) << 5 | b >> 3);
    for (size_t i = 0; i + 1 < size; i += 2)
        put_le16(out + i, v);
}

/* The ExeFS icon of content 0: what Data Management shows for the pack. */
static void pack_smdh(uint8_t *s)
{
    static const char *const fields[3] = {"Custom themes", "Custom themes", "theme_pack"};
    static const size_t offsets[3] = {0, 0x80, 0x180};
    memset(s, 0, TP_SMDH_SIZE);
    memcpy(s, "SMDH", 4);
    for (int slot = 0; slot < 16; slot++)
        for (int f = 0; f < 3; f++)
            for (size_t i = 0; fields[f][i]; i++)
                put_le16(s + 0x8 + 0x200 * slot + offsets[f] + 2 * i, (uint8_t)fields[f][i]);
    put_le32(s + 0x2018, 0x7FFFFFFF);              /* region lockout: every region */
    solid_rgb565(s + 0x2040, 0x480, 0x40, 0x60, 0x80);
    solid_rgb565(s + 0x24C0, TP_ICON_SIZE, 0x40, 0x60, 0x80);
}

struct catalogue_ctx {
    const tp_catalogue *c;
    const tp_entry **by_index;
    uint8_t *icon;
};

static void catalogue_entry(uint8_t e[ENTRY_SIZE], const char *name, const char *description, uint32_t index,
                            uint32_t enabled)
{
    utf8_field(e, 0x40, name);
    utf8_field(e + 0x40, 0x80, description ? description : "");
    put_le32(e + 0xC0, index);
    put_le32(e + 0xC4, enabled);
}

static int catalogue_produce(void *ctx, size_t file, l3_writer *w)
{
    struct catalogue_ctx *k = ctx;
    uint16_t top = k->c->top;
    if (file == 0) {            /* MetaDataContentHeader.bin, the EUR example on 3dbrew with the counts set */
        uint8_t m[META_SIZE];
        put_le32(m, 1);
        put_le32(m + 4, top);
        put_le32(m + 8, top);
        put_le32(m + 12, 0x00300030);
        put_le32(m + 16, 0xDEADBEEF);
        for (int region = 0; region < 7; region++) {
            if (region == 2) {
                put_le16(m + 0x14 + 4 * region, 2);
                put_le16(m + 0x16 + 4 * region, 1);
            } else {
                memset(m + 0x14 + 4 * region, 0xFF, 4);
            }
        }
        put_le32(m + 0x30, 8);
        for (int l = 0; l < 8; l++) {
            put_le16(m + 0x34 + 4 * l, 2);
            put_le16(m + 0x36 + 4 * l, LANGUAGES[l].id);
        }
        return l3_write(w, m, sizeof m);
    }
    if (file <= 8) {            /* ContentInfoArchive: entry i at 8 + 0xC8 * i, entry 0 a disabled dummy */
        uint8_t e[ENTRY_SIZE];
        put_le32(e, 1);
        put_le32(e + 4, top);
        int r = l3_write(w, e, 8);
        catalogue_entry(e, "Dummy", "", 0, 0);
        if (!r)
            r = l3_write(w, e, sizeof e);
        for (uint16_t i = 1; !r && i <= top; i++) {
            const tp_entry *t = k->by_index[i];
            catalogue_entry(e, t ? t->name : "(removed)", t ? t->description : "", i, 1);
            r = l3_write(w, e, sizeof e);
        }
        return r;
    }
    uint16_t index = (uint16_t)(file - 8);
    if (k->by_index[index]) {
        if (k->c->icon(k->c->icon_ctx, index, k->icon))
            return TP_E_WRITE;
    } else {
        solid_rgb565(k->icon, TP_ICON_SIZE, 0x80, 0x80, 0x80);
    }
    return l3_write(w, k->icon, TP_ICON_SIZE);
}

int tp_catalogue_content(const tp_catalogue *c, uint64_t title_id, uint32_t content_id, const tp_sink *out,
                         tp_content *rec)
{
    if (!rec || (out && !out->write))
        return TP_E_ARG;
    if (title_id != TP_TITLE_ID)
        return TP_E_TITLE;
    const tp_entry **by_index = tp_alloc(sizeof *by_index * (TP_INDEX_MAX + 1));
    rfs_file *files = tp_alloc(sizeof *files * (9 + TP_INDEX_MAX));
    char *names = tp_alloc(8 * TP_INDEX_MAX);
    uint8_t *icon = tp_alloc(TP_ICON_SIZE), *smdh = tp_alloc(TP_SMDH_SIZE), *l2 = NULL;
    rfs layout;
    memset(&layout, 0, sizeof layout);
    int r = by_index && files && names && icon && smdh ? TP_OK : TP_E_NOMEM;
    if (!r) {
        memset(by_index, 0, sizeof *by_index * (TP_INDEX_MAX + 1));
        r = check_catalogue(c, by_index);
    }
    if (!r) {
        files[0] = (rfs_file){"MetaDataContentHeader.bin", META_SIZE, 0};
        for (int l = 0; l < 8; l++)
            files[1 + l] = (rfs_file){ARCHIVES[l], 8 + ENTRY_SIZE * (uint32_t)(c->top + 1), 0};
        for (uint16_t i = 1; i <= c->top; i++) {
            char *n = names + 8 * (i - 1);
            int len = 0;
            char digits[4];
            for (uint16_t v = i; v; v /= 10)
                digits[len++] = (char)('0' + v % 10);
            for (int d = 0; d < len; d++)
                n[d] = digits[len - 1 - d];
            memcpy(n + len, ".icn", 5);
            files[8 + i] = (rfs_file){n, TP_ICON_SIZE, 1};
        }
        layout.subdir = "icons";
        layout.files = files;
        layout.count = 9 + (size_t)c->top;
        r = rfs_layout(&layout);
    }
    struct catalogue_ctx k = {c, by_index, icon};
    if (!r)
        r = rfs_hash(&layout, catalogue_produce, &k, &l2);
    if (!r) {
        pack_smdh(smdh);
        r = ncch_emit(&layout, catalogue_produce, &k, l2, title_id, smdh, out, &rec->size, rec->sha256);
    }
    if (!r) {
        rec->id = content_id;
        rec->index = 0;
        rec->type = 0;
    }
    rfs_free(&layout);
    tp_free(l2);
    tp_free(smdh);
    tp_free(icon);
    tp_free(names);
    tp_free(files);
    tp_free(by_index);
    return r;
}

/* ---------------------------------------------------------------- TMD */

#define SIG_RSA2048_SHA256 0x00010004u
#define HEADER 0x140u               /* after the signature type, the signature and its padding */
#define INFO (HEADER + 0xC4u)       /* 64 content info records of 0x24 */
#define RECORDS (INFO + 0x900u)     /* content records of 0x30 */

size_t tp_tmd_size(size_t count) { return RECORDS + 0x30 * count; }

static int write_chunked(const tp_sink *out, const uint8_t *p, size_t size)
{
    for (size_t at = 0; at < size; at += CHUNK) {
        size_t n = size - at < CHUNK ? size - at : CHUNK;
        if (out->write(out->ctx, p + at, n))
            return TP_E_WRITE;
    }
    return TP_OK;
}

static int check_records(const tp_content *recs, size_t count)
{
    if (!recs || count < 1 || count > TP_INDEX_MAX + 1 || recs[0].index != 0)
        return TP_E_ARG;
    for (size_t i = 0; i < count; i++) {
        if (recs[i].index > TP_INDEX_MAX)
            return TP_E_INDEX;
        if (i && recs[i].index <= recs[i - 1].index)
            return TP_E_ARG;
        for (size_t j = 0; j < i; j++)
            if (recs[j].id == recs[i].id)
                return TP_E_ID;
    }
    return TP_OK;
}

/* base: the first RECORDS bytes of an installed TMD (signature and header kept), or NULL for a new one. */
static int tmd_emit(const uint8_t *base, uint64_t title_id, uint16_t version, const tp_content *recs, size_t count,
                    const tp_sink *out)
{
    if (!out || !out->write)
        return TP_E_ARG;
    if (title_id != TP_TITLE_ID)
        return TP_E_TITLE;
    int r = check_records(recs, count);
    if (r)
        return r;
    size_t size = tp_tmd_size(count);
    uint8_t *t = tp_alloc(size);
    if (!t)
        return TP_E_NOMEM;
    memset(t, 0, size);
    if (base) {
        memcpy(t, base, INFO);
    } else {
        put_be32(t, SIG_RSA2048_SHA256);
        memset(t + 4, 0xFF, 0x100);                 /* signature: see README, differences */
        memcpy(t + HEADER, "Root-CA00000003-CP0000000b", 26);
        t[HEADER + 0x40] = 1;                       /* format version */
        put_be64(t + HEADER + 0x4C, title_id);
        put_be32(t + HEADER + 0x54, 0x40);          /* title type CTR */
    }
    put_be16(t + HEADER + 0x9C, version);
    put_be16(t + HEADER + 0x9E, (uint16_t)count);
    for (size_t i = 0; i < count; i++) {
        uint8_t *e = t + RECORDS + 0x30 * i;
        put_be32(e, recs[i].id);
        put_be16(e + 4, recs[i].index);
        put_be16(e + 6, recs[i].type);
        put_be64(e + 8, recs[i].size);
        memcpy(e + 16, recs[i].sha256, 32);
    }
    put_be16(t + INFO + 2, (uint16_t)count);        /* one info record: offset 0, all records */
    sha256(t + RECORDS, 0x30 * count, t + INFO + 4);
    sha256(t + INFO, 0x900, t + HEADER + 0xA4);
    r = write_chunked(out, t, size);
    tp_free(t);
    return r;
}

int tp_tmd(uint64_t title_id, uint16_t version, const tp_content *recs, size_t count, const tp_sink *out)
{
    return tmd_emit(NULL, title_id, version, recs, count, out);
}

int tp_tmd_record(const uint8_t *tmd, size_t size, size_t k, tp_content *rec)
{
    if (!tmd || !rec || size < RECORDS || get_be32(tmd) != SIG_RSA2048_SHA256)
        return TP_E_TMD;
    size_t count = get_be16(tmd + HEADER + 0x9E);
    if (size != tp_tmd_size(count) || k >= count)
        return TP_E_TMD;
    const uint8_t *e = tmd + RECORDS + 0x30 * k;
    rec->id = get_be32(e);
    rec->index = get_be16(e + 4);
    rec->type = get_be16(e + 6);
    rec->size = get_be64(e + 8);
    memcpy(rec->sha256, e + 16, 32);
    return TP_OK;
}

int tp_tmd_read(const uint8_t *tmd, size_t size, uint64_t *title_id, uint16_t *version, size_t *count)
{
    if (!tmd || size < RECORDS || get_be32(tmd) != SIG_RSA2048_SHA256)
        return TP_E_TMD;
    size_t n = get_be16(tmd + HEADER + 0x9E);
    if (n < 1 || n > TP_INDEX_MAX + 1 || size != tp_tmd_size(n))
        return TP_E_TMD;
    uint8_t digest[32];
    sha256(tmd + INFO, 0x900, digest);
    if (memcmp(digest, tmd + HEADER + 0xA4, 32))
        return TP_E_TMD;
    size_t covered = 0;
    for (int i = 0; i < 64; i++) {
        const uint8_t *info = tmd + INFO + 0x24 * i;
        size_t offset = get_be16(info), records = get_be16(info + 2);
        if (!records)
            continue;
        if (offset != covered || records > n - covered)
            return TP_E_TMD;
        sha256(tmd + RECORDS + 0x30 * offset, 0x30 * records, digest);
        if (memcmp(digest, info + 4, 32))
            return TP_E_TMD;
        covered += records;
    }
    if (covered != n)
        return TP_E_TMD;
    if (get_be64(tmd + HEADER + 0x4C) != TP_TITLE_ID)
        return TP_E_TITLE;
    for (size_t i = 0; i < n; i++) {
        tp_content a, b;
        tp_tmd_record(tmd, size, i, &a);
        if (a.index > TP_INDEX_MAX || (i == 0 && a.index != 0))
            return TP_E_TMD;
        for (size_t j = 0; j < i; j++) {
            tp_tmd_record(tmd, size, j, &b);
            if (b.id == a.id || (j + 1 == i && b.index >= a.index))
                return TP_E_TMD;
        }
    }
    if (title_id)
        *title_id = get_be64(tmd + HEADER + 0x4C);
    if (version)
        *version = get_be16(tmd + HEADER + 0x9C);
    if (count)
        *count = n;
    return TP_OK;
}

/* ---------------------------------------------------------------- ticket */

int tp_ticket(uint64_t title_id, uint64_t ticket_id, const uint8_t title_key[16], const tp_sink *out)
{
    static const uint32_t index_header[10] = {
        0x00010014, 0x000000AC, 0x00000014, 0x00010014, 0, 0x28, 1, 0x84, 0x84, 0x00030000,
    };
    uint8_t t[TP_TICKET_SIZE];
    if (!out || !out->write || !title_key)
        return TP_E_ARG;
    if (title_id != TP_TITLE_ID)
        return TP_E_TITLE;
    memset(t, 0, sizeof t);
    put_be32(t, SIG_RSA2048_SHA256);
    memset(t + 4, 0xFF, 0x100);                     /* signature: see README, differences */
    memcpy(t + HEADER, "Root-CA00000003-XS0000000c", 26);
    t[HEADER + 0x7C] = 1;                           /* format version */
    memcpy(t + HEADER + 0x7F, title_key, 16);
    put_be64(t + HEADER + 0x90, ticket_id);
    put_be64(t + HEADER + 0x9C, title_id);
    for (int i = 0; i < 10; i++)                    /* content index: one segment from 0 */
        put_be32(t + HEADER + 0x164 + 4 * i, index_header[i]);
    memset(t + HEADER + 0x164 + 0x2C, 0xFF, 0x80);  /* rights for 0..0x3FF */
    return out->write(out->ctx, t, sizeof t) ? TP_E_WRITE : TP_OK;
}

/* ---------------------------------------------------------------- indices and the delta */

int tp_next_index(const uint16_t *used, size_t count, uint16_t top, uint16_t *index)
{
    if (!index || (count && !used))
        return TP_E_ARG;
    uint32_t highest = top;
    for (size_t i = 0; i < count; i++)
        if (used[i] > highest)
            highest = used[i];
    if (highest >= TP_INDEX_MAX)
        return TP_E_INDEX;
    *index = (uint16_t)(highest + 1);
    return TP_OK;
}

int tp_delta_plan(const uint8_t *tmd, size_t size, uint64_t title_id, const tp_catalogue *c, tp_delta *plan)
{
    uint16_t version;
    size_t count;
    if (!plan)
        return TP_E_ARG;
    memset(plan, 0, sizeof *plan);
    if (title_id != TP_TITLE_ID)
        return TP_E_TITLE;
    int r = tp_tmd_read(tmd, size, NULL, &version, &count);
    if (r)
        return r;
    const tp_entry *by_index[TP_INDEX_MAX + 1] = {0};
    if ((r = check_catalogue(c, by_index)) != TP_OK)
        return r;
    uint8_t installed[TP_INDEX_MAX + 1] = {0};
    for (size_t k = 1; k < count; k++) {
        tp_content rec;
        tp_tmd_record(tmd, size, k, &rec);
        if (!by_index[rec.index])
            return TP_E_GONE;       /* the new catalogue would list it as removed */
        installed[rec.index] = 1;
        plan->kept++;
    }
    for (uint16_t i = 1; i <= TP_INDEX_MAX; i++)
        if (by_index[i] && !installed[i])
            plan->add[plan->count++] = i;
    if (!plan->count)
        return TP_E_NOTHING;
    if (version == 0xFFFF)
        return TP_E_VERSION;
    plan->version = (uint16_t)(version + 1);
    plan->content0_id = 0x10000u + plan->version;
    for (size_t k = 0; k < count; k++) {
        tp_content rec;
        tp_tmd_record(tmd, size, k, &rec);
        if (rec.id == plan->content0_id)
            return TP_E_ID;
    }
    return TP_OK;
}

size_t tp_delta_tmd_size(const tp_delta *plan, size_t installed_count)
{
    return tp_tmd_size(installed_count + (plan ? plan->count : 0));
}

int tp_delta_tmd(const uint8_t *tmd, size_t size, const tp_delta *plan, const tp_content *content0,
                 const tp_content *added, const tp_sink *out)
{
    uint64_t title_id;
    uint16_t version;
    size_t count;
    if (!plan || !content0 || (plan->count && !added) || plan->count > TP_INDEX_MAX)
        return TP_E_ARG;
    int r = tp_tmd_read(tmd, size, &title_id, &version, &count);
    if (r)
        return r;
    if (plan->version != version + 1 || plan->kept != count - 1 || content0->index != 0 ||
        content0->id != plan->content0_id)
        return TP_E_ARG;
    for (size_t i = 0; i < plan->count; i++)
        if (added[i].index != plan->add[i])
            return TP_E_ARG;
    tp_content *recs = tp_alloc(sizeof *recs * (count + plan->count));
    if (!recs)
        return TP_E_NOMEM;
    size_t n = 0, a = 0;
    recs[n++] = *content0;
    for (size_t k = 1; k <= count; k++) {
        tp_content kept;
        int more = k < count && tp_tmd_record(tmd, size, k, &kept) == TP_OK;
        while (a < plan->count && (!more || added[a].index < kept.index))
            recs[n++] = added[a++];
        if (more)
            recs[n++] = kept;
    }
    r = n == count + plan->count ? tmd_emit(tmd, title_id, plan->version, recs, n, out) : TP_E_ARG;
    tp_free(recs);
    return r;
}
