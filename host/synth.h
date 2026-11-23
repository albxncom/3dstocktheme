/* Synthetic theme parts for the host tests of the preview (test_audio.c, test_main.c), written from the format
 * documentation (3dbrew "Home Menu/Themes", the zip and LZ11 forms the core reads): a body with a wallpaper
 * texture for each screen or with plain colours, its LZ11 stream, an SMDH, and a theme zip around them; the small
 * BCSTM that plays is the kit's (uikit/host/synth_bcstm.h). Nothing here comes from a real theme. */
#ifndef SYNTH_H
#define SYNTH_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <zlib.h>

#include "../uikit/host/synth_bcstm.h"

#define SYNTH_BODY_MAX (0xD0u + 2u * 1024 * 256 * 2)    /* a body with two 1024x256 textures */
#define SYNTH_LZ_MAX (SYNTH_BODY_MAX + SYNTH_BODY_MAX / 8 + 16)
#define SYNTH_SMDH 0x36C0u
#define SYNTH_ZIP_MAX(body_lz, bgm) ((body_lz) + (bgm) + SYNTH_SMDH + 512u)

/* The value of texel (x, y) of a synthetic wallpaper: its own in every texel of a tile and from tile to tile. */
static inline uint16_t synth_texel(unsigned x, unsigned y, unsigned salt)
{
    return (uint16_t)(x * 31u + y * 977u + salt);
}

/* A body with a texture on each screen: version 1, the music byte as given, the top screen's frame type top_frame
 * (0 or 3: 1024x256, 1: 512x256), the bottom screen's bottom_frame (0, 2, 3, 4: 1024x256, 1: 512x256), the top
 * texture at 0xD0 and the bottom one after it, texel (x, y) holding synth_texel(x, y, 0) on top and
 * synth_texel(x, y, 0x5A5A) below. Returns its size. */
static inline size_t synth_body(uint8_t *out, int use_bgm, unsigned top_frame, unsigned bottom_frame)
{
    unsigned width[2] = {top_frame == 1 ? 512u : 1024u, bottom_frame == 1 ? 512u : 1024u};
    size_t at = 0xD0;
    memset(out, 0, at);
    synth_le32(out, 1);
    out[5] = (uint8_t)(use_bgm != 0);
    synth_le32(out + 0xC, 3);
    synth_le32(out + 0x10, top_frame);
    synth_le32(out + 0x20, 3);
    synth_le32(out + 0x24, bottom_frame);
    for (int s = 0; s < 2; s++) {
        synth_le32(out + (s ? 0x28 : 0x18), (uint32_t)at);
        for (unsigned y = 0; y < 256; y++)
            for (unsigned x = 0; x < width[s]; x++) {
                unsigned tile = (y >> 3) * (width[s] >> 3) + (x >> 3);
                unsigned z = (x & 1) | (y & 1) << 1 | (x & 2) << 1 | (y & 2) << 2 | (x & 4) << 2 | (y & 4) << 3;
                synth_le16(out + at + ((size_t)tile * 64 + z) * 2, synth_texel(x, y, s ? 0x5A5A : 0));
            }
        at += (size_t)width[s] * 256 * 2;
    }
    return at;
}

/* A body without textures: the top screen a colour that fades half way to white (draw type 1: 10, 100, 250 and
 * gradient 128), the bottom screen the colour 40, 50, 60 (draw type 1 with the outer area's block switched on). */
static inline size_t synth_body_colors(uint8_t *out, int use_bgm)
{
    static const uint8_t top[5] = {10, 100, 250, 128, 0}, bottom[9] = {1, 2, 3, 40, 50, 60, 7, 8, 9};
    memset(out, 0, 0xF0);
    synth_le32(out, 1);
    out[5] = (uint8_t)(use_bgm != 0);
    synth_le32(out + 0xC, 1);
    synth_le32(out + 0x14, 0xD0);
    synth_le32(out + 0x20, 1);
    synth_le32(out + 0x88, 1);
    synth_le32(out + 0x8C, 0xE0);
    memcpy(out + 0xD0, top, sizeof top);
    memcpy(out + 0xE0, bottom, sizeof bottom);
    return 0xF0;
}

/* An LZ11 stream of literals only (a flag byte of 0 before every eight bytes), padded with zeros to a multiple of
 * 4. out holds SYNTH_LZ_MAX bytes for a body of SYNTH_BODY_MAX. Returns its size. */
static inline size_t synth_lz11(const uint8_t *in, size_t n, uint8_t *out)
{
    size_t o = 4;
    out[0] = 0x11;
    out[1] = (uint8_t)n;
    out[2] = (uint8_t)(n >> 8);
    out[3] = (uint8_t)(n >> 16);
    for (size_t i = 0; i < n; i++) {
        if (i % 8 == 0)
            out[o++] = 0;
        out[o++] = in[i];
    }
    while (o % 4)
        out[o++] = 0;
    return o;
}

/* An SMDH as the core reads it: the magic, `shown` (ASCII) as the first short title, every icon texel 0x1234. */
static inline void synth_smdh(uint8_t *out, const char *shown)
{
    memset(out, 0, SYNTH_SMDH);
    memcpy(out, "SMDH", 4);
    for (size_t k = 0; shown[k] && k < 0x3F; k++)
        out[8 + 2 * k] = (uint8_t)shown[k];
    for (size_t k = 0; k < 0x1200; k += 2)
        synth_le16(out + 0x24C0 + k, 0x1234);
}

/* A zip of stored members: body_LZ.bin, info.smdh with the title `shown`, and bgm.bcstm when bgm is not NULL. out holds
 * SYNTH_ZIP_MAX(body_lz_size, bgm_size) bytes. Returns its size. */
static inline size_t synth_zip(uint8_t *out, const uint8_t *body_lz, size_t body_lz_size, const uint8_t *bgm,
                               size_t bgm_size, const char *shown)
{
    static uint8_t smdh[SYNTH_SMDH];
    synth_smdh(smdh, shown);
    const char *member[3] = {"body_LZ.bin", "info.smdh", "bgm.bcstm"};
    const uint8_t *data[3] = {body_lz, smdh, bgm};
    size_t sizes[3] = {body_lz_size, SYNTH_SMDH, bgm_size}, local[3], at = 0;
    uint32_t crc[3];
    int count = bgm ? 3 : 2;
    for (int k = 0; k < count; k++) {
        size_t name = strlen(member[k]);
        crc[k] = (uint32_t)crc32(crc32(0L, Z_NULL, 0), data[k], (uInt)sizes[k]);
        local[k] = at;
        memset(out + at, 0, 30);
        synth_le32(out + at, 0x04034b50);
        synth_le16(out + at + 4, 20);
        synth_le32(out + at + 14, crc[k]);
        synth_le32(out + at + 18, (uint32_t)sizes[k]);
        synth_le32(out + at + 22, (uint32_t)sizes[k]);
        synth_le16(out + at + 26, (unsigned)name);
        memcpy(out + at + 30, member[k], name);
        memcpy(out + at + 30 + name, data[k], sizes[k]);
        at += 30 + name + sizes[k];
    }
    size_t directory = at;
    for (int k = 0; k < count; k++) {
        size_t name = strlen(member[k]);
        memset(out + at, 0, 46);
        synth_le32(out + at, 0x02014b50);
        synth_le16(out + at + 4, 20);
        synth_le16(out + at + 6, 20);
        synth_le32(out + at + 16, crc[k]);
        synth_le32(out + at + 20, (uint32_t)sizes[k]);
        synth_le32(out + at + 24, (uint32_t)sizes[k]);
        synth_le16(out + at + 28, (unsigned)name);
        synth_le32(out + at + 42, (uint32_t)local[k]);
        memcpy(out + at + 46, member[k], name);
        at += 46 + name;
    }
    memset(out + at, 0, 22);
    synth_le32(out + at, 0x06054b50);
    synth_le16(out + at + 8, (unsigned)count);
    synth_le16(out + at + 10, (unsigned)count);
    synth_le32(out + at + 12, (uint32_t)(at - directory));
    synth_le32(out + at + 16, (uint32_t)directory);
    return at + 22;
}

#endif
