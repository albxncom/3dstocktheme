/* The part of the zip format a theme zip needs: the central directory, stored and deflated members. Every size
 * and offset is checked against the source before it is used. Not supported, and refused: ZIP64, archives split
 * over disks, data before the archive, encrypted members, methods other than stored and deflate. */
#include <zlib.h>

#include "internal.h"

#define SIG_LOCAL   0x04034b50u
#define SIG_CENTRAL 0x02014b50u
#define SIG_END     0x06054b50u
#define END_SIZE     22u
#define CENTRAL_SIZE 46u
#define LOCAL_SIZE   30u
#define NAME_MAX_LEN 11u            /* the longest wanted name, body_LZ.bin */

static int source_read(const tp_source *src, uint64_t offset, void *buf, size_t size)
{
    if (offset > src->size || size > src->size - offset)
        return TP_E_ZIP;            /* the zip points past its own end */
    return src->read(src->ctx, offset, buf, size) ? TP_E_READ : TP_OK;
}

static int locate(const tp_source *src, const uint8_t *name, uint16_t name_len, uint16_t flags, uint16_t method,
                  uint32_t crc, uint32_t csize, uint32_t size, uint32_t local, uint64_t directory, tp_member *m)
{
    uint8_t h[LOCAL_SIZE + NAME_MAX_LEN];
    if (flags & 1)
        return TP_E_ZIP;            /* encrypted */
    if (method != 0 && method != 8)
        return TP_E_ZIP;
    if (method == 0 && csize != size)
        return TP_E_ZIP;
    if (csize == 0xFFFFFFFFu || size == 0xFFFFFFFFu || local == 0xFFFFFFFFu)
        return TP_E_ZIP;            /* ZIP64 */
    if ((uint64_t)local + LOCAL_SIZE + name_len > directory)
        return TP_E_ZIP;
    int r = source_read(src, local, h, LOCAL_SIZE + name_len);
    if (r)
        return r;
    if (get_le32(h) != SIG_LOCAL || get_le16(h + 26) != name_len || memcmp(h + LOCAL_SIZE, name, name_len))
        return TP_E_ZIP;
    uint64_t data = (uint64_t)local + LOCAL_SIZE + name_len + get_le16(h + 28);
    if (data > directory || csize > directory - data)
        return TP_E_ZIP;            /* member data must end before the central directory */
    m->offset = data;
    m->csize = csize;
    m->size = size;
    m->crc = crc;
    m->method = method;
    m->present = 1;
    return TP_OK;
}

int zip_find(const tp_source *src, tp_member *body, tp_member *bgm, tp_member *smdh)
{
    static const char *const wanted[3] = {"body_LZ.bin", "bgm.bcstm", "info.smdh"};
    tp_member *slot[3] = {body, bgm, smdh};
    for (int i = 0; i < 3; i++)
        memset(slot[i], 0, sizeof *slot[i]);
    if (src->size < END_SIZE)
        return TP_E_ZIP;

    /* The end record is in the last 22 + 65535 bytes (its comment is at most 65535 bytes). Take the last one whose
     * comment ends the file, else the last one (a comment can hold the signature; data can follow the zip). */
    size_t tail = src->size < END_SIZE + 0xFFFFu ? (size_t)src->size : END_SIZE + 0xFFFFu;
    uint8_t *buf = tp_alloc(tail);
    if (!buf)
        return TP_E_NOMEM;
    int r = source_read(src, src->size - tail, buf, tail);
    size_t at = tail, last = tail;
    for (size_t i = tail - END_SIZE + 1; !r && i-- > 0;)
        if (get_le32(buf + i) == SIG_END) {
            if (last == tail)
                last = i;
            if (i + END_SIZE + get_le16(buf + i + 20) == tail) {
                at = i;
                break;
            }
        }
    if (at == tail)
        at = last;
    uint8_t end[END_SIZE] = {0};
    if (!r && at < tail)
        memcpy(end, buf + at, END_SIZE);
    tp_free(buf);
    if (r)
        return r;
    if (at == tail)
        return TP_E_ZIP;
    uint64_t end_at = src->size - tail + at;
    uint16_t disk = get_le16(end + 4), directory_disk = get_le16(end + 6);
    uint16_t entries_here = get_le16(end + 8), entries = get_le16(end + 10);
    uint32_t directory_size = get_le32(end + 12), directory = get_le32(end + 16);
    if (disk || directory_disk || entries_here != entries)
        return TP_E_ZIP;
    if (entries == 0xFFFFu || directory_size == 0xFFFFFFFFu || directory == 0xFFFFFFFFu)
        return TP_E_ZIP;            /* ZIP64 */
    if ((uint64_t)directory + directory_size != end_at)
        return TP_E_ZIP;

    for (uint64_t pos = directory; pos < end_at;) {
        uint8_t h[CENTRAL_SIZE], name[NAME_MAX_LEN];
        if (end_at - pos < CENTRAL_SIZE)
            return TP_E_ZIP;
        if ((r = source_read(src, pos, h, CENTRAL_SIZE)) != TP_OK)
            return r;
        if (get_le32(h) != SIG_CENTRAL)
            return TP_E_ZIP;
        uint16_t name_len = get_le16(h + 28);
        uint64_t next = pos + CENTRAL_SIZE + name_len + get_le16(h + 30) + get_le16(h + 32);
        if (next > end_at)
            return TP_E_ZIP;
        if (name_len == 9 || name_len == 11) {
            if ((r = source_read(src, pos + CENTRAL_SIZE, name, name_len)) != TP_OK)
                return r;
            for (int w = 0; w < 3; w++) {
                if (strlen(wanted[w]) != name_len || memcmp(name, wanted[w], name_len))
                    continue;
                if (slot[w]->present)
                    return TP_E_ZIP;    /* the same name twice */
                r = locate(src, name, name_len, get_le16(h + 8), get_le16(h + 10), get_le32(h + 16),
                           get_le32(h + 20), get_le32(h + 24), get_le32(h + 42), directory, slot[w]);
                if (r)
                    return r;
            }
        }
        pos = next;
    }
    return TP_OK;
}

int zip_stream(const tp_source *src, const tp_member *m, data_fn fn, void *ctx)
{
    uint8_t *in = tp_alloc(CHUNK), *out = NULL;
    uLong crc = crc32(0L, Z_NULL, 0);
    uint64_t done = 0;
    z_stream z;
    int inflating = 0, r = TP_OK;
    if (!in)
        return TP_E_NOMEM;

    if (m->method == 0) {
        for (uint64_t offset = m->offset, left = m->size; left && !r;) {
            size_t n = left < CHUNK ? (size_t)left : CHUNK;
            if ((r = source_read(src, offset, in, n)) == TP_OK) {
                tp_phase(TP_PHASE_UNZIP);
                crc = crc32(crc, in, (uInt)n);
                r = fn(ctx, in, n);
            }
            offset += n;
            left -= n;
            done += n;
        }
    } else {
        memset(&z, 0, sizeof z);
        z.zalloc = tp_zalloc;
        z.zfree = tp_zfree;
        out = tp_alloc(CHUNK);
        if (!out)
            r = TP_E_NOMEM;
        else if ((r = inflateInit2(&z, -MAX_WBITS)) != Z_OK)
            r = r == Z_MEM_ERROR ? TP_E_NOMEM : TP_E_ZIP;
        else
            inflating = 1;
        uint64_t offset = m->offset, left = m->csize;
        for (int finished = 0; !r && !finished;) {
            if (z.avail_in == 0 && left) {
                size_t n = left < CHUNK ? (size_t)left : CHUNK;
                if ((r = source_read(src, offset, in, n)) != TP_OK)
                    break;
                z.next_in = in;
                z.avail_in = (uInt)n;
                offset += n;
                left -= n;
            }
            z.next_out = out;
            z.avail_out = CHUNK;
            tp_phase(TP_PHASE_UNZIP);
            int zr = inflate(&z, Z_NO_FLUSH);
            size_t got = CHUNK - z.avail_out;
            if (zr == Z_STREAM_END)
                finished = 1;
            else if (zr == Z_MEM_ERROR)
                r = TP_E_NOMEM;
            else if (zr == Z_BUF_ERROR ? z.avail_in == 0 && left == 0 : zr != Z_OK)
                r = TP_E_ZIP;       /* corrupt, or the compressed data ends before the stream does */
            if (!r && got > m->size - done)
                r = TP_E_ZIP;       /* longer than the central directory says */
            if (!r && got) {
                crc = crc32(crc, out, (uInt)got);
                done += got;
                r = fn(ctx, out, got);
            }
        }
    }
    if (!r && (done != m->size || crc != m->crc))
        r = TP_E_ZIP;
    if (inflating)
        inflateEnd(&z);
    tp_free(out);
    tp_free(in);
    tp_phase(TP_PHASE_OTHER);
    return r;
}
