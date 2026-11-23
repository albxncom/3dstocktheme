/* PNG (8-bit, non-interlaced) and the badge image formats, the same as badge_sets.py read_png, badge_images and
 * set_icon_image: the same pixels for the same file, and a refusal where it refuses (the checks in its order). */
#include <zlib.h>

#include "badge_internal.h"

#define MAX_PIXELS (4096u * 4096u)

static const uint8_t SIGNATURE[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};

static unsigned channels(unsigned colour_type)
{
    switch (colour_type) {
    case 0: return 1;           /* grey */
    case 2: return 3;           /* RGB */
    case 3: return 1;           /* palette */
    case 4: return 2;           /* grey + alpha */
    case 6: return 4;           /* RGBA */
    default: return 0;
    }
}

typedef struct chunk {
    uint32_t length;
    const uint8_t *kind, *body;
} chunk;

/* The chunk at *pos, its CRC checked; *pos moves past it. */
static int next_chunk(const uint8_t *png, size_t size, size_t *pos, chunk *c)
{
    if (*pos > size || size - *pos < 12)
        return BADGE_E_PNG;     /* the PNG ends before its IEND chunk */
    c->length = get_be32(png + *pos);
    if (c->length > size - *pos - 12)
        return BADGE_E_PNG;
    c->kind = png + *pos + 4;
    c->body = png + *pos + 8;
    uLong crc = crc32(crc32(0L, Z_NULL, 0), c->kind, 4);
    crc = crc32(crc, c->body, (uInt)c->length);
    if (crc != get_be32(c->body + c->length))
        return BADGE_E_PNG;
    *pos += 12 + (size_t)c->length;
    return BADGE_OK;
}

static int is_kind(const chunk *c, const char *kind) { return !memcmp(c->kind, kind, 4); }

typedef struct png_head {
    uint32_t width, height;
    unsigned depth, colour_type, compression, filtering, interlace;
    const uint8_t *palette, *trns;
    size_t palette_size, trns_size;
    int has_trns;
} png_head;

static int read_head(const uint8_t *png, size_t size, png_head *h)
{
    memset(h, 0, sizeof *h);
    if (size < 8 || memcmp(png, SIGNATURE, 8))
        return BADGE_E_PNG;     /* not a PNG file */
    size_t pos = 8;
    chunk c;
    int r = next_chunk(png, size, &pos, &c);
    if (r)
        return r;
    if (!is_kind(&c, "IHDR") || c.length != 13)
        return BADGE_E_PNG;     /* does not start with IHDR (an IEND first included) */
    h->width = get_be32(c.body);
    h->height = get_be32(c.body + 4);
    h->depth = c.body[8];
    h->colour_type = c.body[9];
    h->compression = c.body[10];
    h->filtering = c.body[11];
    h->interlace = c.body[12];
    for (;;) {
        if ((r = next_chunk(png, size, &pos, &c)) != BADGE_OK)
            return r;
        if (is_kind(&c, "IEND"))
            break;
        if (is_kind(&c, "PLTE")) {
            h->palette = c.body;
            h->palette_size = c.length;
        } else if (is_kind(&c, "tRNS")) {
            h->trns = c.body;
            h->trns_size = c.length;
            h->has_trns = 1;
        } else if (is_kind(&c, "IDAT")) {
            continue;
        } else if (is_kind(&c, "IHDR") || !(c.kind[0] & 0x20)) {
            return BADGE_E_PNG;  /* an unexpected critical chunk */
        }
    }
    if (h->depth != 8 || !channels(h->colour_type))
        return BADGE_E_PNG_KIND;
    if (h->interlace || h->compression || h->filtering)
        return BADGE_E_PNG_KIND;
    if (h->colour_type == 3 && (!h->palette_size || h->palette_size % 3))
        return BADGE_E_PNG;      /* a palette PNG without a valid PLTE chunk */
    if (h->width == 0 || h->height == 0 || (uint64_t)h->width * h->height > MAX_PIXELS)
        return BADGE_E_IMAGE_SIZE;
    return BADGE_OK;
}

/* Undoes the row filter of `line` (stride bytes, after the filter byte) against prev, then writes the row as RGBA. */
static int finish_row(const png_head *h, unsigned bpp, size_t stride, uint8_t *row, uint8_t *prev, uint8_t *out)
{
    unsigned kind = row[0];
    uint8_t *line = row + 1;
    if (kind == 1) {
        for (size_t i = bpp; i < stride; i++)
            line[i] = (uint8_t)(line[i] + line[i - bpp]);
    } else if (kind == 2) {
        for (size_t i = 0; i < stride; i++)
            line[i] = (uint8_t)(line[i] + prev[i]);
    } else if (kind == 3) {
        for (size_t i = 0; i < stride; i++) {
            unsigned left = i >= bpp ? line[i - bpp] : 0;
            line[i] = (uint8_t)(line[i] + ((left + prev[i]) >> 1));
        }
    } else if (kind == 4) {
        for (size_t i = 0; i < stride; i++) {
            int a = i >= bpp ? line[i - bpp] : 0, b = prev[i], c = i >= bpp ? prev[i - bpp] : 0;
            int p = a + b - c, pa = p > a ? p - a : a - p, pb = p > b ? p - b : b - p, pc = p > c ? p - c : c - p;
            line[i] = (uint8_t)(line[i] + (pa <= pb && pa <= pc ? a : pb <= pc ? b : c));
        }
    } else if (kind) {
        return BADGE_E_PNG;     /* an unknown filter */
    }
    size_t colours = h->palette_size / 3;
    for (uint32_t x = 0; x < h->width; x++, out += 4) {
        switch (h->colour_type) {
        case 6:
            memcpy(out, line + 4 * (size_t)x, 4);
            break;
        case 2: {
            const uint8_t *p = line + 3 * (size_t)x;
            int key = h->has_trns && h->trns_size == 6 && p[0] == h->trns[1] && p[1] == h->trns[3] &&
                      p[2] == h->trns[5];
            out[0] = p[0], out[1] = p[1], out[2] = p[2], out[3] = key ? 0 : 255;
            break;
        }
        case 3: {
            unsigned i = line[x];
            if (i >= colours)
                return BADGE_E_PNG;     /* a palette index outside the palette */
            memcpy(out, h->palette + 3 * i, 3);
            out[3] = h->trns && i < h->trns_size ? h->trns[i] : 255;
            break;
        }
        case 4:
            out[0] = out[1] = out[2] = line[2 * (size_t)x];
            out[3] = line[2 * (size_t)x + 1];
            break;
        default: {
            uint8_t g = line[x];
            int key = h->has_trns && h->trns_size == 2 && g == h->trns[1];
            out[0] = out[1] = out[2] = g;
            out[3] = key ? 0 : 255;
        }
        }
    }
    memcpy(prev, line, stride);
    return BADGE_OK;
}

int badge_png_decode(const uint8_t *png, size_t size, uint32_t *width, uint32_t *height, uint8_t **rgba)
{
    if (!rgba || !width || !height || (!png && size))
        return BADGE_E_ARG;
    *rgba = NULL;
    *width = *height = 0;
    png_head h;
    int r = read_head(png, size, &h);
    if (r)
        return r;
    unsigned bpp = channels(h.colour_type);
    size_t stride = (size_t)h.width * bpp, need = stride + 1;
    uint8_t *out = tp_alloc((size_t)h.width * h.height * 4), *row = tp_alloc(need), *prev = tp_alloc(stride);
    z_stream z;
    memset(&z, 0, sizeof z);
    z.zalloc = tp_zalloc;
    z.zfree = tp_zfree;
    int inflating = 0;
    if (!out || !row || !prev)
        r = BADGE_E_NOMEM;
    else if ((r = inflateInit(&z)) != Z_OK)
        r = r == Z_MEM_ERROR ? BADGE_E_NOMEM : BADGE_E_PNG;
    else
        inflating = 1;

    /* The IDAT bodies in file order as one zlib stream; anything after its end is ignored, as zlib.decompress
     * does. The data must be exactly height rows of a filter byte and stride bytes. */
    uint8_t spare[64];
    uint32_t y = 0;
    size_t have = 0;
    int done = 0;
    if (!r)
        memset(prev, 0, stride);
    for (size_t pos = 8; !r && !done;) {
        chunk c;
        next_chunk(png, size, &pos, &c);       /* read_head checked every chunk */
        if (is_kind(&c, "IEND"))
            break;
        if (!is_kind(&c, "IDAT"))
            continue;
        z.next_in = (Bytef *)(uintptr_t)c.body;
        z.avail_in = c.length;
        while (!r) {
            if (y < h.height) {
                z.next_out = row + have;
                z.avail_out = (uInt)(need - have);
            } else {
                z.next_out = spare;
                z.avail_out = sizeof spare;
            }
            uInt room = z.avail_out;
            int zr = inflate(&z, Z_NO_FLUSH);
            size_t got = room - z.avail_out;
            if (zr == Z_MEM_ERROR) {
                r = BADGE_E_NOMEM;
                break;
            }
            if (zr != Z_OK && zr != Z_STREAM_END && zr != Z_BUF_ERROR) {
                r = BADGE_E_PNG;    /* damaged image data */
                break;
            }
            if (got && y >= h.height) {
                r = BADGE_E_PNG;    /* more data than the image has rows */
                break;
            }
            have += got;
            if (have == need) {
                r = finish_row(&h, bpp, stride, row, prev, out + (size_t)y * h.width * 4);
                y++;
                have = 0;
            }
            if (zr == Z_STREAM_END) {
                done = 1;
                break;
            }
            if (z.avail_in == 0 && z.avail_out != 0)
                break;              /* this chunk is used up: the next one */
            if (zr == Z_BUF_ERROR && z.avail_in != 0 && z.avail_out != 0)
                r = BADGE_E_PNG;    /* no progress with input and room: not a stream zlib can go on with */
        }
    }
    if (!r && (!done || y != h.height || have))
        r = BADGE_E_PNG;            /* the stream ends early, or the image has fewer rows */
    if (inflating)
        inflateEnd(&z);
    tp_free(row);
    tp_free(prev);
    if (r) {
        tp_free(out);
        return r;
    }
    *width = h.width;
    *height = h.height;
    *rgba = out;
    return BADGE_OK;
}

/* ---------------------------------------------------------------- the badge image formats */

uint32_t badge_tiled(uint32_t x, uint32_t y, uint32_t size)
{
    uint32_t m = (x & 1) | (y & 1) << 1 | (x & 2) << 1 | (y & 2) << 2 | (x & 4) << 2 | (y & 4) << 3;
    return (y >> 3) * (size / 8) * 64 + (x >> 3) * 64 + m;
}

/* RGB565 and A4 by truncation; the 32x32 image is the mean of each 2x2 block, truncated (Anemone3DS's rule). */
void badge_tile_images(const uint8_t *rgba, uint32_t width, uint32_t x0, uint32_t y0, uint8_t *big, uint8_t *small)
{
    for (uint32_t size = 64; size >= 32; size /= 2) {
        uint8_t *out = size == 64 ? big : small, *alpha = out + size * size * 2;
        memset(alpha, 0, size * size / 2);
        for (uint32_t y = 0; y < size; y++)
            for (uint32_t x = 0; x < size; x++) {
                unsigned r = 0, g = 0, b = 0, a = 0;
                uint32_t n = size == 64 ? 1 : 2;
                for (uint32_t dy = 0; dy < n; dy++)
                    for (uint32_t dx = 0; dx < n; dx++) {
                        const uint8_t *p = rgba + ((size_t)(y0 + n * y + dy) * width + x0 + n * x + dx) * 4;
                        r += p[0], g += p[1], b += p[2], a += p[3];
                    }
                unsigned extra = n == 2 ? 2 : 0;    /* the sum of four is the mean times four */
                r >>= 3 + extra, g >>= 2 + extra, b >>= 3 + extra, a >>= 4 + extra;
                uint32_t i = badge_tiled(x, y, size);
                put_le16(out + 2 * i, (uint16_t)(r << 11 | g << 5 | b));
                alpha[i >> 1] |= (uint8_t)(a << ((i & 1) * 4));
            }
    }
}

void badge_icon_image(const uint8_t *rgba, uint32_t width, uint32_t x0, uint32_t y0, uint32_t side, uint8_t *out)
{
    memset(out, 0, BD_SET_ICON_SIZE);
    for (uint32_t y = 0; y < B_ICON; y++)
        for (uint32_t x = 0; x < B_ICON; x++) {
            /* badge_sets.py takes int(y * 4 / 3) for a 64x64 source; for y below 48 that is 4 * y // 3 exactly */
            uint32_t sx = side == B_ICON ? x : 4 * x / 3, sy = side == B_ICON ? y : 4 * y / 3;
            const uint8_t *p = rgba + ((size_t)(y0 + sy) * width + x0 + sx) * 4;
            put_le16(out + 2 * badge_tiled(x, y, 64), (uint16_t)((p[0] >> 3) << 11 | (p[1] >> 2) << 5 | p[2] >> 3));
        }
}
