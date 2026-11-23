/* Shared by the core's files; not part of the API. */
#ifndef TP_INTERNAL_H
#define TP_INTERNAL_H

#include <stdint.h>
#include <string.h>

#include "sha256.h"
#include "tp.h"

#define CHUNK 0x4000u            /* read, inflate and sink buffer size */
#define BLOCK 0x1000u            /* IVFC block */
#define MEDIA 0x200u             /* NCCH media unit */

void *tp_alloc(size_t size);
void tp_free(void *ptr);
void tp_phase(int phase);        /* tp_set_phase's hook, if one is set */
void *tp_zalloc(void *opaque, unsigned items, unsigned size);   /* for zlib */
void tp_zfree(void *opaque, void *ptr);

static inline uint64_t align_up(uint64_t x, uint64_t a) { return (x + a - 1) / a * a; }

static inline void put_le16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static inline void put_le32(uint8_t *p, uint32_t v) { put_le16(p, (uint16_t)v); put_le16(p + 2, (uint16_t)(v >> 16)); }
static inline void put_le64(uint8_t *p, uint64_t v) { put_le32(p, (uint32_t)v); put_le32(p + 4, (uint32_t)(v >> 32)); }
static inline void put_be16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static inline void put_be32(uint8_t *p, uint32_t v) { put_be16(p, (uint16_t)(v >> 16)); put_be16(p + 2, (uint16_t)v); }
static inline void put_be64(uint8_t *p, uint64_t v) { put_be32(p, (uint32_t)(v >> 32)); put_be32(p + 4, (uint32_t)v); }
static inline uint16_t get_le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static inline uint32_t get_le32(const uint8_t *p) { return get_le16(p) | (uint32_t)get_le16(p + 2) << 16; }
static inline uint16_t get_be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static inline uint32_t get_be32(const uint8_t *p) { return (uint32_t)get_be16(p) << 16 | get_be16(p + 2); }
static inline uint64_t get_be64(const uint8_t *p) { return (uint64_t)get_be32(p) << 32 | get_be32(p + 4); }


/* ---------------------------------------------------------------- zip (zip.c) */

typedef int (*data_fn)(void *ctx, const uint8_t *data, size_t size);

/* Finds body_LZ.bin, bgm.bcstm and info.smdh in the central directory; checks their headers and bounds. */
int zip_find(const tp_source *src, tp_member *body, tp_member *bgm, tp_member *smdh);
/* Streams a member's uncompressed bytes to fn; checks the size, the deflate stream's end and the CRC-32. */
int zip_stream(const tp_source *src, const tp_member *member, data_fn fn, void *ctx);

/* ---------------------------------------------------------------- LZ11 check (theme.c) */

typedef struct lz11 {
    uint8_t header[8];
    unsigned have;              /* header bytes seen */
    uint64_t size, out;         /* declared and decoded size */
    uint8_t head[6];            /* the first decoded bytes; 0x5 is the use-BGM byte */
    uint8_t flags, bit;         /* current flag byte, next bit (8 = need a new flag byte) */
    uint8_t token[4];
    unsigned token_have, token_need;
    int bad, done, too_big;     /* too_big: the declared size is over TP_BODY_MAX */
} lz11;

void lz11_init(lz11 *z);
void lz11_feed(lz11 *z, const uint8_t *p, size_t n);
int lz11_valid(const lz11 *z);  /* the stream decoded to exactly its declared size (6..TP_BODY_MAX) */

/* Whole buffers (lz11.c). lz11_size: the declared size and where the tokens start. lz11_decode: into out, which
 * holds cap bytes (TP_E_TOO_BIG below the declared size). lz11_encode: a stream that decodes to in, padded with
 * zeros to a multiple of 4; TP_E_TOO_BIG if it would take more than cap bytes. */
int lz11_size(const uint8_t *in, size_t n, size_t *size, size_t *start);
int lz11_decode(const uint8_t *in, size_t n, uint8_t *out, size_t cap);
int lz11_encode(const uint8_t *in, size_t n, uint8_t *out, size_t cap, size_t *size);

/* ---------------------------------------------------------------- text (theme.c) */

/* Valid UTF-8 without NUL. */
int utf8_valid(const char *s);
/* NUL-terminated UTF-8 in a field of size bytes, cut at a character boundary, NUL-padded. */
void utf8_field(uint8_t *field, size_t size, const char *text);

/* ---------------------------------------------------------------- RomFS and NCCH (ncch.c) */

/* The RomFS of a content: a root with files and at most one subdirectory with files. The file table, and the
 * data, list the root's files first, then the subdirectory's, each in the order given. */
typedef struct rfs_file {
    const char *name;           /* ASCII */
    uint32_t size;
    uint8_t in_subdir;
} rfs_file;

typedef struct rfs {
    const char *subdir;         /* NULL: none */
    const rfs_file *files;
    size_t count;
    uint8_t *meta;              /* level 3 header and tables, padded to 0x10 */
    uint32_t meta_size;
    uint64_t l3_size;           /* meta + data */
} rfs;

int rfs_layout(rfs *r);         /* allocates meta */
void rfs_free(rfs *r);

/* Level 3 writer: hashes every 4 KiB block (stores the hashes, or compares them with stored ones) and passes
 * the bytes on to the content writer. */
typedef struct content_writer content_writer;
typedef struct l3_writer {
    sha256_ctx block;
    uint32_t fill;
    uint64_t total, expect;
    uint8_t *l2;                /* n_blocks * 32 */
    size_t blocks, n_blocks;
    int verify;
    content_writer *down;
} l3_writer;

int l3_write(l3_writer *w, const void *data, size_t size);
int l3_pad(l3_writer *w, size_t size);            /* zeros */

/* Writes the files' data (rfs order); the RomFS code adds the metadata and the padding between files. */
typedef int (*produce_fn)(void *ctx, size_t file, l3_writer *w);

/* Pass 1: the block hashes of level 3 (allocated, n_blocks * 32 bytes). */
int rfs_hash(const rfs *r, produce_fn produce, void *ctx, uint8_t **l2);

/* Pass 2: the NCCH (header, optional ExeFS with an icon, RomFS) to out (NULL: measure), checking level 3
 * against l2; returns its size and SHA-256. */
int ncch_emit(const rfs *r, produce_fn produce, void *ctx, const uint8_t *l2, uint64_t title_id,
              const uint8_t *exefs_icon, const tp_sink *out, uint64_t *size, uint8_t sha256[32]);

#endif
