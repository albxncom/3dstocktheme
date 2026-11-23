/* RomFS (level 3 with an IVFC hash tree) inside an NCCH CFA, laid out as makerom 0.19.0 does (romfs_gen.c,
 * ncch.c, exefs.c): IVFC blocks of 0x1000; level 3 right after the master hashes, then levels 1 and 2; RomFS at a
 * 0x1000 boundary; no encryption. Written in two passes so that no content is held in memory: pass 1 hashes the
 * 4 KiB blocks of level 3, pass 2 writes the header (which needs those hashes) and the data again. */
#include "internal.h"

static const uint8_t ZEROS[0x400];

/* ---------------------------------------------------------------- content writer: whole-content SHA-256 + sink */

struct content_writer {
    sha256_ctx sha;
    uint64_t count;
    const tp_sink *sink;
    uint8_t *buf;
    size_t fill;
};

static int cw_flush(content_writer *c)
{
    if (c->fill && c->sink->write(c->sink->ctx, c->buf, c->fill))
        return TP_E_WRITE;
    c->fill = 0;
    return TP_OK;
}

static int cw_write(content_writer *c, const void *data, size_t size)
{
    const uint8_t *p = data;
    sha256_update(&c->sha, p, size);
    c->count += size;
    while (c->sink && size) {
        size_t take = CHUNK - c->fill < size ? CHUNK - c->fill : size;
        memcpy(c->buf + c->fill, p, take);
        c->fill += take;
        p += take;
        size -= take;
        if (c->fill == CHUNK && cw_flush(c))
            return TP_E_WRITE;
    }
    return TP_OK;
}

static int cw_zeros_to(content_writer *c, uint64_t offset)
{
    while (c->count < offset) {
        uint64_t n = offset - c->count < sizeof ZEROS ? offset - c->count : sizeof ZEROS;
        int r = cw_write(c, ZEROS, (size_t)n);
        if (r)
            return r;
    }
    return TP_OK;
}

/* ---------------------------------------------------------------- level 3 writer */

static int block_done(l3_writer *w)
{
    uint8_t digest[32];
    sha256_final(&w->block, digest);
    sha256_init(&w->block);
    w->fill = 0;
    if (w->blocks >= w->n_blocks)
        return TP_E_CHANGED;
    uint8_t *slot = w->l2 + 32 * w->blocks++;
    if (!w->verify)
        memcpy(slot, digest, 32);
    else if (memcmp(slot, digest, 32))
        return TP_E_CHANGED;
    return TP_OK;
}

static int l3_bytes(l3_writer *w, const uint8_t *p, size_t size)
{
    while (size) {
        size_t take = BLOCK - w->fill < size ? BLOCK - w->fill : size;
        sha256_update(&w->block, p, take);
        if (w->down) {
            int r = cw_write(w->down, p, take);
            if (r)
                return r;
        }
        w->fill += (uint32_t)take;
        p += take;
        size -= take;
        if (w->fill == BLOCK) {
            int r = block_done(w);
            if (r)
                return r;
        }
    }
    return TP_OK;
}

int l3_write(l3_writer *w, const void *data, size_t size)
{
    if (size > w->expect - w->total)
        return TP_E_CHANGED;        /* more data than the layout has room for */
    w->total += size;
    return l3_bytes(w, data, size);
}

int l3_pad(l3_writer *w, size_t size)
{
    while (size) {
        size_t n = size < sizeof ZEROS ? size : sizeof ZEROS;
        int r = l3_write(w, ZEROS, n);
        if (r)
            return r;
        size -= n;
    }
    return TP_OK;
}

/* ---------------------------------------------------------------- level 3 layout */

static uint32_t hash_count(uint32_t n)       /* makerom GetHashTableCount */
{
    if (n < 3)
        return 3;
    if (n < 19)
        return n | 1;
    while (n % 2 == 0 || n % 3 == 0 || n % 5 == 0 || n % 7 == 0 || n % 11 == 0 || n % 13 == 0 || n % 17 == 0)
        n++;
    return n;
}

static uint32_t path_hash(uint32_t parent, const char *name)     /* over the UTF-16 units; names are ASCII */
{
    uint32_t h = parent ^ 123456789u;
    for (; *name; name++)
        h = (h >> 5 | h << 27) ^ (uint8_t)*name;
    return h;
}

static uint32_t name_bytes(const char *name) { return (uint32_t)align_up(2 * strlen(name), 4); }

static void put_name(uint8_t *p, const char *name)
{
    for (size_t i = 0; name[i]; i++)
        put_le16(p + 2 * i, (uint8_t)name[i]);
}

static void insert(uint8_t *table, uint32_t buckets, uint32_t parent, const char *name, uint8_t *next_field,
                   uint32_t offset)
{
    uint8_t *bucket = table + 4 * (path_hash(parent, name) % buckets);
    put_le32(next_field, get_le32(bucket));
    put_le32(bucket, offset);
}

int rfs_layout(rfs *r)
{
    const uint32_t NONE = 0xFFFFFFFFu, SUBDIR = 0x18;
    uint32_t dirs = r->subdir ? 2 : 1, dir_table = 0x18 + (r->subdir ? 0x18 + name_bytes(r->subdir) : 0);
    uint32_t file_table = 0, root_table = 0, root_files = 0;
    r->meta = NULL;
    if (r->count == 0 || r->count > 0x10000)
        return TP_E_ARG;
    for (size_t i = 0; i < r->count; i++) {
        const rfs_file *f = &r->files[i];
        if ((f->in_subdir && !r->subdir) || (!f->in_subdir && i != root_files))
            return TP_E_ARG;        /* root files first, then the subdirectory's */
        root_files += !f->in_subdir;
        file_table += 0x20 + name_bytes(f->name);
        if (!f->in_subdir)
            root_table = file_table;
    }
    uint32_t dir_buckets = hash_count(dirs), file_buckets = hash_count((uint32_t)r->count);
    uint32_t dir_hash_at = 0x28, dir_at = dir_hash_at + 4 * dir_buckets;
    uint32_t file_hash_at = dir_at + dir_table, file_at = file_hash_at + 4 * file_buckets;
    r->meta_size = (uint32_t)align_up(file_at + file_table, 0x10);
    uint8_t *m = r->meta = tp_alloc(r->meta_size);
    if (!m)
        return TP_E_NOMEM;
    memset(m, 0, r->meta_size);

    put_le32(m, 0x28);
    put_le32(m + 0x04, dir_hash_at);
    put_le32(m + 0x08, 4 * dir_buckets);
    put_le32(m + 0x0C, dir_at);
    put_le32(m + 0x10, dir_table);
    put_le32(m + 0x14, file_hash_at);
    put_le32(m + 0x18, 4 * file_buckets);
    put_le32(m + 0x1C, file_at);
    put_le32(m + 0x20, file_table);
    put_le32(m + 0x24, r->meta_size);
    memset(m + dir_hash_at, 0xFF, 4 * dir_buckets);
    memset(m + file_hash_at, 0xFF, 4 * file_buckets);

    /* Directory entries: parent, sibling, first child, first file, hash chain, name size, name. */
    uint8_t *root = m + dir_at;
    put_le32(root, 0);
    put_le32(root + 4, NONE);
    put_le32(root + 8, r->subdir ? SUBDIR : NONE);
    put_le32(root + 12, root_files ? 0 : NONE);
    insert(m + dir_hash_at, dir_buckets, 0, "", root + 16, 0);
    if (r->subdir) {
        uint8_t *d = root + SUBDIR;
        put_le32(d, 0);
        put_le32(d + 4, NONE);
        put_le32(d + 8, NONE);
        put_le32(d + 12, root_files < r->count ? root_table : NONE);
        put_le32(d + 20, (uint32_t)(2 * strlen(r->subdir)));
        put_name(d + 24, r->subdir);
        insert(m + dir_hash_at, dir_buckets, 0, r->subdir, d + 16, SUBDIR);
    }

    /* File entries: parent, sibling, data offset, data size, hash chain, name size, name. */
    uint64_t data = 0;
    uint32_t at = 0;
    for (size_t i = 0; i < r->count; i++) {
        const rfs_file *f = &r->files[i];
        uint8_t *e = m + file_at + at;
        uint32_t next = at + 0x20 + name_bytes(f->name), parent = f->in_subdir ? SUBDIR : 0;
        int last = i + 1 == r->count || r->files[i + 1].in_subdir != f->in_subdir;
        put_le32(e, parent);
        put_le32(e + 4, last ? NONE : next);
        if (f->size) {
            data = align_up(data, 0x10);
            put_le64(e + 8, data);
            put_le64(e + 16, f->size);
            data += f->size;
        }
        put_le32(e + 28, (uint32_t)(2 * strlen(f->name)));
        put_name(e + 32, f->name);
        insert(m + file_hash_at, file_buckets, parent, f->name, e + 24, at);
        at = next;
    }
    r->l3_size = r->meta_size + data;
    return TP_OK;
}

void rfs_free(rfs *r)
{
    tp_free(r->meta);
    r->meta = NULL;
}

/* Level 3: the metadata, then each file's data (aligned to 0x10 before a non-empty file), then zeros to the
 * end of the last block, which its hash covers. */
static int rfs_stream(const rfs *r, produce_fn produce, void *ctx, l3_writer *w)
{
    int ret = l3_write(w, r->meta, r->meta_size);
    for (size_t i = 0; !ret && i < r->count; i++) {
        if (r->files[i].size)
            ret = l3_pad(w, (size_t)(align_up(w->total, 0x10) - w->total));
        uint64_t before = w->total;
        if (!ret)
            ret = produce(ctx, i, w);
        if (!ret && w->total - before != r->files[i].size)
            ret = TP_E_CHANGED;
    }
    if (!ret && w->total != r->l3_size)
        ret = TP_E_CHANGED;
    while (!ret && w->fill) {
        size_t n = BLOCK - w->fill < sizeof ZEROS ? BLOCK - w->fill : sizeof ZEROS;
        ret = l3_bytes(w, ZEROS, n);
    }
    if (!ret && w->blocks != w->n_blocks)
        ret = TP_E_CHANGED;
    return ret;
}

static size_t blocks_of(uint64_t size) { return (size_t)(align_up(size, BLOCK) / BLOCK); }

int rfs_hash(const rfs *r, produce_fn produce, void *ctx, uint8_t **l2)
{
    l3_writer w;
    memset(&w, 0, sizeof w);
    w.expect = r->l3_size;
    w.n_blocks = blocks_of(r->l3_size);
    w.l2 = *l2 = tp_alloc(32 * w.n_blocks);
    if (!w.l2)
        return TP_E_NOMEM;
    sha256_init(&w.block);
    int ret = rfs_stream(r, produce, ctx, &w);
    if (ret) {
        tp_free(*l2);
        *l2 = NULL;
    }
    return ret;
}

/* Hashes of the 4 KiB blocks of data, the last block zero-padded. */
static void hash_blocks(const uint8_t *data, uint64_t size, uint8_t *out)
{
    for (uint64_t at = 0; at < size; at += BLOCK, out += 32) {
        sha256_ctx c;
        uint64_t n = size - at < BLOCK ? size - at : BLOCK;
        sha256_init(&c);
        sha256_update(&c, data + at, (size_t)n);
        for (uint64_t pad = BLOCK - n; pad;) {
            size_t z = pad < sizeof ZEROS ? (size_t)pad : sizeof ZEROS;
            sha256_update(&c, ZEROS, z);
            pad -= z;
        }
        sha256_final(&c, out);
    }
}

#define IVFC_HEADER 0x5Cu
#define EXEFS_HEADER 0x200u

int ncch_emit(const rfs *r, produce_fn produce, void *ctx, const uint8_t *l2, uint64_t title_id,
              const uint8_t *exefs_icon, const tp_sink *out, uint64_t *size, uint8_t sha256_out[32])
{
    /* IVFC: level 3 = the file system, level 2 = its block hashes, level 1 = theirs, master = level 1's. */
    uint64_t l3_size = r->l3_size, l2_size = 32 * (uint64_t)blocks_of(l3_size);
    uint64_t l1_size = 32 * (uint64_t)blocks_of(l2_size), master_size = 32 * (uint64_t)blocks_of(l1_size);
    uint64_t l0_size = align_up(IVFC_HEADER, 0x10) + master_size;
    uint64_t l3_at = align_up(l0_size, BLOCK), l1_at = l3_at + align_up(l3_size, BLOCK);
    uint64_t l2_at = l1_at + align_up(l1_size, BLOCK), romfs_size = l2_at + align_up(l2_size, BLOCK);
    uint64_t exefs_size = exefs_icon ? EXEFS_HEADER + align_up(TP_SMDH_SIZE, MEDIA) : 0;
    uint64_t romfs_at = align_up(MEDIA + exefs_size, BLOCK);
    uint64_t ncch_size = align_up(romfs_at + align_up(romfs_size, MEDIA), MEDIA);
    uint64_t romfs_hashed = align_up(l0_size, MEDIA);

    uint8_t *l1 = tp_alloc((size_t)l1_size), *l0 = tp_alloc((size_t)romfs_hashed);
    content_writer c;
    memset(&c, 0, sizeof c);
    sha256_init(&c.sha);
    c.sink = out;
    if (out)
        c.buf = tp_alloc(CHUNK);
    int ret = TP_OK;
    if (!l1 || !l0 || (out && !c.buf)) {
        ret = TP_E_NOMEM;
        goto done;
    }
    hash_blocks(l2, l2_size, l1);

    memset(l0, 0, (size_t)romfs_hashed);
    memcpy(l0, "IVFC", 4);
    put_le32(l0 + 0x04, 0x10000);
    put_le32(l0 + 0x08, (uint32_t)master_size);
    uint64_t logical[3] = {0, align_up(l1_size, BLOCK), 0}, sizes[3] = {l1_size, l2_size, l3_size};
    logical[2] = align_up(logical[1] + l2_size, BLOCK);
    for (int i = 0; i < 3; i++) {
        put_le64(l0 + 0x0C + 0x18 * i, logical[i]);
        put_le64(l0 + 0x14 + 0x18 * i, sizes[i]);
        put_le32(l0 + 0x1C + 0x18 * i, 12);     /* log2 of the block size */
    }
    put_le32(l0 + 0x54, IVFC_HEADER);
    hash_blocks(l1, l1_size, l0 + align_up(IVFC_HEADER, 0x10));

    uint8_t exefs[EXEFS_HEADER], header[0x200];
    memset(exefs, 0, sizeof exefs);
    if (exefs_icon) {
        memcpy(exefs, "icon", 4);
        put_le32(exefs + 8, 0);
        put_le32(exefs + 12, TP_SMDH_SIZE);
        sha256(exefs_icon, TP_SMDH_SIZE, exefs + 0x1E0);   /* hashes run backwards from 0x1E0 */
    }
    memset(header, 0, sizeof header);
    memset(header, 0xFF, 0x100);                    /* signature: see README, differences */
    memcpy(header + 0x100, "NCCH", 4);
    put_le32(header + 0x104, (uint32_t)(ncch_size / MEDIA));
    put_le64(header + 0x108, title_id);              /* partition ID */
    memcpy(header + 0x110, "00", 2);                 /* maker code */
    put_le64(header + 0x118, title_id);              /* program ID */
    memcpy(header + 0x150, "CTR-N-THMP", 10);        /* product code */
    header[0x188 + 4] = 1;                           /* platform CTR */
    header[0x188 + 5] = 1;                           /* simple content (CFA), application */
    header[0x188 + 7] = 0x05;                        /* no crypto, fixed key */
    if (exefs_icon) {
        put_le32(header + 0x1A0, MEDIA / MEDIA);
        put_le32(header + 0x1A4, (uint32_t)(exefs_size / MEDIA));
        put_le32(header + 0x1A8, EXEFS_HEADER / MEDIA);
        sha256(exefs, EXEFS_HEADER, header + 0x1C0);
    }
    put_le32(header + 0x1B0, (uint32_t)(romfs_at / MEDIA));
    put_le32(header + 0x1B4, (uint32_t)(align_up(romfs_size, MEDIA) / MEDIA));
    put_le32(header + 0x1B8, (uint32_t)(romfs_hashed / MEDIA));
    sha256(l0, (size_t)romfs_hashed, header + 0x1E0);

    l3_writer w;
    memset(&w, 0, sizeof w);
    w.expect = l3_size;
    w.n_blocks = blocks_of(l3_size);
    w.l2 = (uint8_t *)l2;
    w.verify = 1;
    w.down = &c;
    sha256_init(&w.block);

    ret = cw_write(&c, header, sizeof header);
    if (!ret && exefs_icon) {
        ret = cw_write(&c, exefs, sizeof exefs);
        if (!ret)
            ret = cw_write(&c, exefs_icon, TP_SMDH_SIZE);
    }
    if (!ret)
        ret = cw_zeros_to(&c, romfs_at);
    if (!ret)
        ret = cw_write(&c, l0, (size_t)romfs_hashed);
    if (!ret)
        ret = cw_zeros_to(&c, romfs_at + l3_at);
    if (!ret)
        ret = rfs_stream(r, produce, ctx, &w);
    if (!ret)
        ret = cw_write(&c, l1, (size_t)l1_size);
    if (!ret)
        ret = cw_zeros_to(&c, romfs_at + l2_at);
    if (!ret)
        ret = cw_write(&c, l2, (size_t)l2_size);
    if (!ret)
        ret = cw_zeros_to(&c, ncch_size);
    if (!ret && c.count != ncch_size)
        ret = TP_E_CHANGED;
    if (!ret && out)
        ret = cw_flush(&c);
    if (!ret) {
        *size = ncch_size;
        sha256_final(&c.sha, sha256_out);
    }
done:
    tp_free(c.buf);
    tp_free(l0);
    tp_free(l1);
    return ret;
}
