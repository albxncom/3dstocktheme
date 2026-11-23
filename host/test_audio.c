/* Unit tests of the preview's parts in the core and the application: the wallpapers of a theme body (core/body.c),
 * what a preview takes out of an open theme (core/media.c) on a synthetic theme zip (synth.h), the settings file
 * (app/source/settings.c) and the track table's names in it. Every file is synthetic, built here; each one is
 * copied into a buffer of exactly its size before it is read, so the asan build catches a read past its end. The
 * kit's own parts (the BCSTM and WAV readers, the working loop, the track table, the sound files of uikit/sound)
 * are the kit's test_sound (uikit/host/test_sound.c).
 * Run: test_audio   (make test) */
#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "audio.h"
#include "bcstm.h"
#include "body.h"
#include "internal.h"
#include "media.h"
#include "settings.h"
#include "synth.h"
#include "tracks.h"
#include "wav.h"
#include "work.h"

#include "../uikit/host/check.h"
#include "../uikit/host/mutate.h"

/* ---------------------------------------------------------------- body */

#define TEX_TOP 0x1C7u          /* unaligned, as in a real Theme Plaza body */
#define COLOR_TOP 0xD0u
#define COLOR_BOTTOM 0xD8u

typedef struct body_spec {
    uint32_t top_draw, top_frame, bottom_draw, bottom_frame, flag88;
} body_spec;

static uint8_t body_buf[TEX_TOP + 2 * 1024 * 256 * 2 + 64];
static uint32_t body_bottom_at;     /* where body_build put the bottom texture */

static unsigned tex_width(uint32_t frame) { return frame == 1 ? 512 : 1024; }

/* The picture written into every texture: different in every texel of a tile and across tiles. */
static uint16_t picture(unsigned x, unsigned y, unsigned salt)
{
    return (uint16_t)((x * 0x9E37u) ^ (y * 0x85EBu) ^ (x >> 3) * 0x2F ^ (y >> 3) * 0x1D3 ^ salt);
}

/* Tiling written from the other side than tp_wall_texel: walk the bytes in order and take each texel's x and y
 * apart from its index (8x8 tiles row after row, Z order inside a tile). */
static void tile_picture(uint8_t *dst, unsigned width, unsigned height, unsigned salt)
{
    size_t i = 0;
    for (unsigned ty = 0; ty < height / 8; ty++)
        for (unsigned tx = 0; tx < width / 8; tx++)
            for (unsigned k = 0; k < 64; k++, i++) {
                unsigned x = (k & 1) | (k >> 2 & 1) << 1 | (k >> 4 & 1) << 2;
                unsigned y = (k >> 1 & 1) | (k >> 3 & 1) << 1 | (k >> 5 & 1) << 2;
                put_le16(dst + 2 * i, picture(tx * 8 + x, ty * 8 + y, salt));
            }
}

/* A body as spec says: colour blocks at 0xD0 (top, 7 bytes) and 0xD8 (bottom, 9 bytes), the top texture at 0x1C7,
 * the bottom one after it; ends exactly at the last texture or colour block. */
static size_t body_build(const body_spec *sp)
{
    memset(body_buf, 0, sizeof body_buf);
    put_le32(body_buf, 1);
    body_buf[5] = 1;
    put_le32(body_buf + 0xC, sp->top_draw);
    put_le32(body_buf + 0x10, sp->top_frame);
    put_le32(body_buf + 0x14, COLOR_TOP);
    put_le32(body_buf + 0x18, TEX_TOP);
    put_le32(body_buf + 0x20, sp->bottom_draw);
    put_le32(body_buf + 0x24, sp->bottom_frame);
    put_le32(body_buf + 0x88, sp->flag88);
    put_le32(body_buf + 0x8C, COLOR_BOTTOM);
    static const uint8_t top_color[7] = {10, 100, 250, 128, 0x40, 1, 2};
    static const uint8_t bottom_color[9] = {1, 2, 3, 40, 50, 60, 7, 8, 9};
    memcpy(body_buf + COLOR_TOP, top_color, sizeof top_color);
    memcpy(body_buf + COLOR_BOTTOM, bottom_color, sizeof bottom_color);
    size_t end = COLOR_BOTTOM + sizeof bottom_color;
    uint32_t at = TEX_TOP;
    if (sp->top_draw == 3) {
        tile_picture(body_buf + at, tex_width(sp->top_frame), 256, 0);
        at += tex_width(sp->top_frame) * 256 * 2;
        end = at;
    }
    body_bottom_at = at;
    put_le32(body_buf + 0x28, at);
    if (sp->bottom_draw == 3) {
        tile_picture(body_buf + at, tex_width(sp->bottom_frame), 256, 0x5A5A);
        end = at + tex_width(sp->bottom_frame) * 256 * 2;
    }
    if (end < 0xC4)
        end = 0xC4;
    return end;
}

static int body_read(size_t n, tp_body *b)
{
    uint8_t *p = exact_copy(body_buf, n);
    int r = tp_body_read(p, n, b);
    free(p);
    return r;
}

static void check_texture(const tp_wall *w, uint32_t offset, unsigned width, unsigned shown, int motion)
{
    CHECK_EQ(w->kind, TP_WALL_TEXTURE);
    CHECK_EQ(w->offset, offset);
    CHECK_EQ(w->width, width);
    CHECK_EQ(w->height, 256);
    CHECK_EQ(w->shown_w, shown);
    CHECK_EQ(w->motion, motion);
}

static void test_body_kinds(void)
{
    static const uint32_t top_frames[3] = {0, 1, 3};
    for (uint32_t td = 0; td < 4; td++)
        for (int tf = 0; tf < 3; tf++) {
            /* bottom: none, colour without and with its flag, texture with frames 0 to 4 */
            for (int bc = 0; bc < 8; bc++) {
                body_spec sp = {td, top_frames[tf], bc == 0 ? 0 : bc <= 2 ? 1 : 3, bc <= 2 ? 0 : (uint32_t)bc - 3,
                                bc == 2};
                size_t n = body_build(&sp);
                tp_body b;
                CHECK_EQ(body_read(n, &b), 0);
                CHECK_EQ(b.use_bgm, 1);
                if (td == 0) {
                    CHECK_EQ(b.top.kind, TP_WALL_NONE);
                } else if (td < 3) {
                    CHECK_EQ(b.top.kind, TP_WALL_COLOR);
                    CHECK(b.top.color[0] == 10 && b.top.color[1] == 100 && b.top.color[2] == 250);
                    /* draw type 1 fades to white by its fourth byte: 10 + 245 * 128 / 255, 100 + 155 * 128 / 255,
                     * 250 + 5 * 128 / 255; draw type 2's fourth byte is no gradient */
                    if (td == 1)
                        CHECK(b.top.color_top[0] == 132 && b.top.color_top[1] == 177 && b.top.color_top[2] == 252);
                    else
                        CHECK(!memcmp(b.top.color, b.top.color_top, 3));
                } else if (top_frames[tf] == 1) {
                    check_texture(&b.top, TEX_TOP, 512, 412, TP_WALL_STILL);
                } else {
                    check_texture(&b.top, TEX_TOP, 1024, 1008, TP_WALL_SCROLL);
                }
                if (bc == 0 || bc == 1) {
                    CHECK_EQ(b.bottom.kind, TP_WALL_NONE);
                } else if (bc == 2) {
                    CHECK_EQ(b.bottom.kind, TP_WALL_COLOR);
                    CHECK(b.bottom.color[0] == 40 && b.bottom.color[1] == 50 && b.bottom.color[2] == 60);
                    CHECK(!memcmp(b.bottom.color, b.bottom.color_top, 3));
                } else {
                    static const unsigned width[5] = {1024, 512, 1024, 1024, 1024}, shown[5] = {1008, 320, 960, 1008,
                                                                                                    960};
                    static const int motion[5] = {TP_WALL_SCROLL, TP_WALL_STILL, TP_WALL_FLIP, TP_WALL_SCROLL,
                                                  TP_WALL_FLIP_BACK};
                    int f = bc - 3;
                    check_texture(&b.bottom, body_bottom_at, width[f], shown[f], motion[f]);
                }
            }
        }
    body_spec sp = {0, 0, 0, 0, 0};
    size_t n = body_build(&sp);
    body_buf[5] = 0;
    tp_body b;
    CHECK_EQ(body_read(n, &b), 0);
    CHECK_EQ(b.use_bgm, 0);
}

static void test_body_texels(void)
{
    body_spec sp = {3, 0, 3, 1, 0};
    size_t n = body_build(&sp);
    tp_body b;
    CHECK_EQ(body_read(n, &b), 0);
    uint8_t *p = exact_copy(body_buf, n);
    static const unsigned xs[] = {0, 1, 6, 7, 8, 9, 15, 16, 411, 412, 1007, 1008, 1015, 1016, 1023};
    static const unsigned ys[] = {0, 1, 7, 8, 9, 63, 64, 239, 240, 247, 248, 255};
    const tp_wall *walls[2] = {&b.top, &b.bottom};
    for (int w = 0; w < 2; w++) {
        const uint8_t *tex = p + walls[w]->offset;
        unsigned width = walls[w]->width, salt = w ? 0x5A5A : 0;
        for (size_t i = 0; i < sizeof xs / sizeof *xs; i++)
            for (size_t j = 0; j < sizeof ys / sizeof *ys; j++)
                if (xs[i] < width)
                    CHECK_EQ(tp_wall_texel(tex, width, xs[i], ys[j]), picture(xs[i], ys[j], salt));
        size_t wrong = 0;
        for (unsigned y = 0; y < 256; y++)
            for (unsigned x = 0; x < width; x++)
                wrong += tp_wall_texel(tex, width, x, y) != picture(x, y, salt);
        CHECK_EQ(wrong, 0);
    }
    /* The byte order and the place of the first texels of tile 1, written by hand: texel (8, 0) is the first of
     * the second tile, (1, 0) the second of the first, (0, 1) the third. */
    uint8_t tiny[16 * 8 * 2] = {0};
    tiny[2] = 0x34;
    tiny[3] = 0x12;
    tiny[4] = 0xCD;
    tiny[5] = 0xAB;
    tiny[128] = 0xEF;
    tiny[129] = 0xBE;
    CHECK_EQ(tp_wall_texel(tiny, 16, 1, 0), 0x1234);
    CHECK_EQ(tp_wall_texel(tiny, 16, 0, 1), 0xABCD);
    CHECK_EQ(tp_wall_texel(tiny, 16, 8, 0), 0xBEEF);
    free(p);
}

static void test_body_refusals(void)
{
    tp_body b;
    body_spec tex = {3, 0, 3, 2, 0}, color = {1, 0, 1, 0, 1}, color2 = {2, 0, 0, 0, 0};
    body_spec empty = {0, 0, 0, 0, 0};
    body_build(&empty);
    CHECK_EQ(body_read(0xC4, &b), 0);
    CHECK_EQ(body_read(0xC3, &b), -1);
    CHECK_EQ(body_read(0, &b), -1);
    size_t n = body_build(&color);
    CHECK_EQ(body_read(n, &b), 0);
    for (uint32_t v = 0; v < 4; v++) {
        body_build(&color);
        put_le32(body_buf, v == 3 ? 0x01000000 : v * 2);
        CHECK_EQ(body_read(n, &b), -1);
        CHECK_EQ(b.top.kind, TP_WALL_NONE);
    }
    body_build(&color);
    put_le32(body_buf + 0xC, 4);
    CHECK_EQ(body_read(n, &b), -1);
    body_build(&color);
    put_le32(body_buf + 0x20, 4);
    CHECK_EQ(body_read(n, &b), -1);
    body_build(&color);
    put_le32(body_buf + 0x20, 2);
    CHECK_EQ(body_read(n, &b), -1);
    static const uint32_t bad_top[] = {2, 4, 5, 0xFFFFFFFF}, bad_bottom[] = {5, 6, 0xFFFFFFFF};
    for (size_t i = 0; i < sizeof bad_top / sizeof *bad_top; i++) {
        n = body_build(&tex);
        put_le32(body_buf + 0x10, bad_top[i]);
        CHECK_EQ(body_read(n, &b), -1);
    }
    for (size_t i = 0; i < sizeof bad_bottom / sizeof *bad_bottom; i++) {
        n = body_build(&tex);
        put_le32(body_buf + 0x24, bad_bottom[i]);
        CHECK_EQ(body_read(n, &b), -1);
    }
    /* A texture reaching one byte past the end, for each place and size. */
    for (uint32_t frame = 0; frame < 5; frame++) {
        body_spec sp = {3, frame == 1 ? 1 : 0, 3, frame, 0};
        n = body_build(&sp);
        CHECK_EQ(body_read(n, &b), 0);
        CHECK_EQ(body_read(n - 1, &b), -1);
        body_spec top_only = {3, frame == 1 ? 1 : 0, 0, 0, 0};
        n = body_build(&top_only);
        CHECK_EQ(body_read(n, &b), 0);
        CHECK_EQ(body_read(n - 1, &b), -1);
    }
    /* Colour blocks ending at the end, and one byte past it: 5 bytes for draw 1, 7 for draw 2, 9 at the bottom. */
    const struct { body_spec *sp; uint32_t field, need; } blocks[] = {
        {&color, 0x14, 5}, {&color2, 0x14, 7}, {&color, 0x8C, 9}};
    for (size_t i = 0; i < 3; i++) {
        n = body_build(blocks[i].sp);
        size_t size = 0x400;
        put_le32(body_buf + blocks[i].field, (uint32_t)(size - blocks[i].need));
        CHECK_EQ(body_read(size, &b), 0);
        put_le32(body_buf + blocks[i].field, (uint32_t)(size - blocks[i].need + 1));
        CHECK_EQ(body_read(size, &b), -1);
        static const uint32_t far[] = {0xFFFFFFFF, 0xFFFFFFFC, 0xFFFFFFF8, 0x80000000, 0x400};
        for (size_t k = 0; k < sizeof far / sizeof *far; k++) {
            put_le32(body_buf + blocks[i].field, far[k]);
            CHECK_EQ(body_read(size, &b), -1);
        }
    }
    static const uint32_t far_tex[] = {0xFFFFFFFF, 0xFFFFFFFF - 0x7FFFF, 0xFFF80000, 0x80000000, 0x10000000};
    for (size_t k = 0; k < sizeof far_tex / sizeof *far_tex; k++) {
        n = body_build(&tex);
        put_le32(body_buf + 0x18, far_tex[k]);
        CHECK_EQ(body_read(n, &b), -1);
        n = body_build(&tex);
        put_le32(body_buf + 0x28, far_tex[k]);
        CHECK_EQ(body_read(n, &b), -1);
    }
    /* With the flag at 0x88 not 1 the bottom colour block is not read, wherever it points. */
    body_spec off = {0, 0, 1, 0, 0};
    n = body_build(&off);
    put_le32(body_buf + 0x8C, 0xFFFFFFFF);
    CHECK_EQ(body_read(n, &b), 0);
    CHECK_EQ(b.bottom.kind, TP_WALL_NONE);
    put_le32(body_buf + 0x88, 2);
    CHECK_EQ(body_read(n, &b), 0);
    CHECK_EQ(b.bottom.kind, TP_WALL_NONE);
}

static void test_body_gradient(void)
{
    static const struct { uint8_t rgb[3], gradient, top[3]; } cases[] = {
        {{0, 100, 255}, 0, {0, 100, 255}},
        {{0, 100, 255}, 255, {255, 255, 255}},
        {{0, 100, 255}, 128, {128, 177, 255}},
        {{0, 100, 255}, 1, {1, 100, 255}},
        {{254, 0, 17}, 254, {254, 254, 254}},
        {{200, 201, 202}, 77, {216, 217, 218}},
    };
    for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
        body_spec sp = {1, 0, 0, 0, 0};
        size_t n = body_build(&sp);
        memcpy(body_buf + COLOR_TOP, cases[i].rgb, 3);
        body_buf[COLOR_TOP + 3] = cases[i].gradient;
        tp_body b;
        CHECK_EQ(body_read(n, &b), 0);
        for (int c = 0; c < 3; c++) {
            CHECK_EQ(b.top.color[c], cases[i].rgb[c]);
            CHECK_EQ(b.top.color_top[c], cases[i].top[c]);
        }
    }
}

static tp_wall wall_of(int motion, unsigned width, unsigned shown)
{
    tp_wall w;
    memset(&w, 0, sizeof w);
    w.kind = TP_WALL_TEXTURE;
    w.width = (uint16_t)width;
    w.height = 256;
    w.shown_w = (uint16_t)shown;
    w.motion = motion;
    return w;
}

static int near(float a, float b) { return a - b < 0.01f && b - a < 0.01f; }

static void test_wall_x(void)
{
    tp_wall none, color;
    memset(&none, 0, sizeof none);
    memset(&color, 0, sizeof color);
    color.kind = TP_WALL_COLOR;
    CHECK(tp_wall_x(&none, 400, 12345) == 0);
    CHECK(tp_wall_x(&color, 320, 12345) == 0);

    tp_wall top_still = wall_of(TP_WALL_STILL, 512, 412), bottom_still = wall_of(TP_WALL_STILL, 512, 320);
    CHECK(tp_wall_x(&top_still, 400, 0) == 6);
    CHECK(tp_wall_x(&top_still, 400, 987654321) == 6);
    CHECK(tp_wall_x(&bottom_still, 320, 5000) == 0);
    CHECK(tp_wall_x(&top_still, 512, 5000) == 0);
    CHECK(tp_wall_x(&top_still, 600, 5000) == 0);

    /* Top scroll: 608 texels to glide at 24 per second, the turn at 25333.3 ms, back at 0 after 50666.7 ms. */
    tp_wall scroll = wall_of(TP_WALL_SCROLL, 1024, 1008);
    CHECK(tp_wall_x(&scroll, 400, 0) == 0);
    CHECK(near(tp_wall_x(&scroll, 400, 1000), 24));
    CHECK(near(tp_wall_x(&scroll, 400, 25000), 600));
    CHECK(near(tp_wall_x(&scroll, 400, 25333), 607.992f));
    CHECK(near(tp_wall_x(&scroll, 400, 25334), 607.984f));
    CHECK(near(tp_wall_x(&scroll, 400, 26000), 592));
    CHECK(near(tp_wall_x(&scroll, 400, 50000), 16));
    CHECK(near(tp_wall_x(&scroll, 400, 50667), 0.008f));
    CHECK(near(tp_wall_x(&scroll, 400, 77000), 584));
    CHECK(near(tp_wall_x(&scroll, 400, 3 * 50666 + 2000), 48 - 3 * 0.016f));
    /* Bottom scroll: 688 texels. */
    CHECK(near(tp_wall_x(&scroll, 320, 28666), 687.984f));
    CHECK(near(tp_wall_x(&scroll, 320, 30000), 688 - 32));

    tp_wall flip = wall_of(TP_WALL_FLIP, 1024, 960), back = wall_of(TP_WALL_FLIP_BACK, 1024, 960);
    static const struct { uint64_t ms; float flip, back; } frames[] = {
        {0, 0, 0}, {899, 0, 0}, {900, 320, 320}, {1799, 320, 320}, {1800, 640, 640}, {2700, 0, 320},
        {3600, 320, 0}, {4500, 640, 320}, {5400, 0, 640}, {900ull * 3 * 4 * 1000000 + 900, 320, 320},
    };
    for (size_t i = 0; i < sizeof frames / sizeof *frames; i++) {
        CHECK(tp_wall_x(&flip, 320, frames[i].ms) == frames[i].flip);
        CHECK(tp_wall_x(&back, 320, frames[i].ms) == frames[i].back);
    }

    /* Always inside [0, width - screen_w], for every motion and screen, at short and very long times; a scroll
     * moves at most 24 texels a second. */
    const tp_wall *all[] = {&top_still, &bottom_still, &scroll, &flip, &back};
    static const int screens[] = {320, 400};
    size_t outside = 0, jumps = 0;
    for (size_t w = 0; w < sizeof all / sizeof *all; w++)
        for (int s = 0; s < 2; s++) {
            float max = (float)(all[w]->width - screens[s]);
            for (uint64_t ms = 0; ms < 200000; ms += 7) {
                float x = tp_wall_x(all[w], screens[s], ms);
                outside += x < 0 || x > max;
                if (all[w]->motion == TP_WALL_SCROLL && ms) {
                    float d = x - tp_wall_x(all[w], screens[s], ms - 7);
                    jumps += d > 0.17f || d < -0.17f;
                }
            }
            static const uint64_t far[] = {UINT64_MAX, UINT64_MAX - 1, (uint64_t)1 << 40, 1000000000000ull,
                                           0x8000000000000000ull, 4294967295ull, 4294967296ull};
            for (size_t k = 0; k < sizeof far / sizeof *far; k++) {
                float x = tp_wall_x(all[w], screens[s], far[k]);
                outside += x < 0 || x > max;
            }
        }
    CHECK_EQ(outside, 0);
    CHECK_EQ(jumps, 0);
}

/* ---------------------------------------------------------------- what a preview takes out of a theme (media.c) */

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

#define MUSIC_SAMPLES 3000u
#define MUSIC_LOOP 1234u

static void test_media(void)
{
    static uint8_t body[SYNTH_BODY_MAX], lz[SYNTH_LZ_MAX], music[SYNTH_BCSTM_MAX(MUSIC_SAMPLES)];
    static uint8_t zip[SYNTH_ZIP_MAX(SYNTH_LZ_MAX, sizeof music)], out[TP_BODY_MAX], bgm[sizeof music];
    size_t body_size = synth_body(body, 1, 1, 4), lz_size = synth_lz11(body, body_size, lz);
    size_t music_size = synth_bcstm(music, MUSIC_SAMPLES, MUSIC_LOOP), size = 0;
    tp_set_allocator(t_alloc, t_free, NULL);

    /* a stream by itself, as an installed content's body is read */
    CHECK_EQ(tp_body_decode(lz, lz_size, out, sizeof out, &size), TP_OK);
    CHECK(size == body_size && !memcmp(out, body, body_size));
    CHECK_EQ(tp_body_decode(lz, lz_size, out, body_size - 1, &size), TP_E_TOO_BIG);
    CHECK_EQ(tp_body_decode(lz, lz_size / 2, out, sizeof out, &size), TP_E_LZ11);
    CHECK_EQ(tp_body_decode(lz, 3, out, sizeof out, &size), TP_E_LZ11);
    lz[0] = 0x10;
    CHECK_EQ(tp_body_decode(lz, lz_size, out, sizeof out, &size), TP_E_LZ11);
    lz[0] = 0x11;
    {
        static const uint8_t tiny[8] = {0x11, 5, 0, 0, 0, 1, 2, 3};     /* declares 5 bytes: no body is that short */
        CHECK_EQ(tp_body_decode(tiny, sizeof tiny, out, sizeof out, &size), TP_E_LZ11);
    }

    /* a theme with music: its body and its music file as they went into the zip */
    size_t zip_size = synth_zip(zip, lz, lz_size, music, music_size, "Synthetic");
    uint8_t *file = exact_copy(zip, zip_size);
    mem_src m = {file, 0, (size_t)-1};
    tp_source src = {&m, zip_size, mem_read};
    static tp_theme t;
    CHECK_EQ(tp_theme_open(&t, &src, NULL), TP_OK);
    CHECK(t.has_bgm && t.use_bgm && t.bgm_size == music_size && !strcmp(t.name, "Synthetic"));
    size = 0;
    CHECK_EQ(tp_theme_body(&t, out, sizeof out, &size), TP_OK);
    CHECK(size == body_size && !memcmp(out, body, body_size));
    tp_body walls;
    CHECK_EQ(tp_body_read(out, size, &walls), 0);
    CHECK(walls.use_bgm && walls.top.kind == TP_WALL_TEXTURE && walls.top.width == 512 &&
          walls.top.motion == TP_WALL_STILL && walls.bottom.kind == TP_WALL_TEXTURE && walls.bottom.width == 1024 &&
          walls.bottom.motion == TP_WALL_FLIP_BACK);
    CHECK_EQ(tp_wall_texel(out + walls.top.offset, 512, 411, 239), synth_texel(411, 239, 0));
    CHECK_EQ(tp_wall_texel(out + walls.bottom.offset, 1024, 640, 17), synth_texel(640, 17, 0x5A5A));
    size = 0;
    CHECK_EQ(tp_theme_bgm(&t, bgm, sizeof bgm, &size), TP_OK);
    CHECK(size == music_size && !memcmp(bgm, music, music_size));
    bcstm_stream stream;
    static int16_t pcm[3 * MUSIC_SAMPLES];
    CHECK_EQ(bcstm_open(&stream, bgm, size), 0);
    CHECK(stream.loops && stream.channels == 1 && stream.rate == 8000 && stream.samples == MUSIC_SAMPLES &&
          stream.loop_start == MUSIC_LOOP);
    CHECK_EQ(bcstm_read(&stream, pcm, 3 * MUSIC_SAMPLES, 1), 3 * MUSIC_SAMPLES);
    int wrong = 0;
    for (uint32_t i = 0; i < 3 * MUSIC_SAMPLES; i++) {
        uint32_t at = i < MUSIC_SAMPLES ? i : MUSIC_LOOP + (i - MUSIC_SAMPLES) % (MUSIC_SAMPLES - MUSIC_LOOP);
        wrong += pcm[i] != synth_sample(at);
    }
    CHECK_EQ(wrong, 0);

    /* refusals, and nothing kept: too little room, no theme, every allocation and every read failing in turn */
    CHECK_EQ(tp_theme_body(&t, out, body_size - 1, &size), TP_E_TOO_BIG);
    CHECK_EQ(tp_theme_bgm(&t, bgm, music_size - 1, &size), TP_E_TOO_BIG);
    CHECK_EQ(tp_theme_body(NULL, out, sizeof out, &size), TP_E_ARG);
    CHECK_EQ(tp_theme_bgm(NULL, bgm, sizeof bgm, &size), TP_E_ARG);
    size_t held = live;
    for (int which = 0; which < 2; which++) {
        int fails = 0, passes = 0;
        for (size_t k = 0; k < 64 && !passes; k++) {
            allocs = 0;
            fail_alloc_at = k;
            int r = which ? tp_theme_bgm(&t, bgm, sizeof bgm, &size) : tp_theme_body(&t, out, sizeof out, &size);
            fail_alloc_at = (size_t)-1;
            fails += r == TP_E_NOMEM;
            passes += r == TP_OK;
            CHECK(r == TP_E_NOMEM || r == TP_OK);
            CHECK_EQ(live, held);
        }
        CHECK(fails >= 1 && passes == 1);
        fails = passes = 0;
        for (size_t k = 0; k < 4096 && !passes; k++) {
            m.reads = 0;
            m.fail_at = k;
            int r = which ? tp_theme_bgm(&t, bgm, sizeof bgm, &size) : tp_theme_body(&t, out, sizeof out, &size);
            m.fail_at = (size_t)-1;
            fails += r == TP_E_READ;
            passes += r == TP_OK;
            CHECK(r == TP_E_READ || r == TP_OK);
            CHECK_EQ(live, held);
        }
        CHECK(fails >= 1 && passes == 1);
    }

    /* installed without its music: the body the core made (its music byte cleared) and no music file */
    CHECK_EQ(tp_theme_drop_bgm(&t), TP_OK);
    size = 0;
    CHECK_EQ(tp_theme_body(&t, out, sizeof out, &size), TP_OK);
    CHECK(size == body_size && out[5] == 0 && !memcmp(out, body, 5) && !memcmp(out + 6, body + 6, body_size - 6));
    CHECK_EQ(tp_theme_bgm(&t, bgm, sizeof bgm, &size), TP_E_MISSING);
    tp_theme_close(&t);
    CHECK_EQ(tp_theme_body(&t, out, sizeof out, &size), TP_E_ARG);
    CHECK_EQ(live, 0);
    free(file);

    /* a theme without a music file, its screens plain colours */
    body_size = synth_body_colors(body, 0);
    lz_size = synth_lz11(body, body_size, lz);
    zip_size = synth_zip(zip, lz, lz_size, NULL, 0, "Plain");
    file = exact_copy(zip, zip_size);
    m = (mem_src){file, 0, (size_t)-1};
    src.size = zip_size;
    CHECK_EQ(tp_theme_open(&t, &src, NULL), TP_OK);
    CHECK(!t.has_bgm && !t.use_bgm);
    CHECK_EQ(tp_theme_body(&t, out, sizeof out, &size), TP_OK);
    CHECK_EQ(tp_body_read(out, size, &walls), 0);
    CHECK(walls.top.kind == TP_WALL_COLOR && walls.top.color[2] == 250 && walls.top.color_top[0] == 132 &&
          walls.bottom.kind == TP_WALL_COLOR && walls.bottom.color[0] == 40 && walls.bottom.color_top[2] == 60);
    CHECK_EQ(tp_theme_bgm(&t, bgm, sizeof bgm, &size), TP_E_MISSING);
    tp_theme_close(&t);
    CHECK_EQ(live, 0);
    free(file);
    tp_set_allocator(NULL, NULL, NULL);
}

/* ---------------------------------------------------------------- the track table's files in the settings file */

static void test_tracks(void)
{
    for (int i = 0; i < TRACK_COUNT; i++) {
        const track *t = &TRACK_LIST[i];
        /* the settings file can name it: its file name comes back from a written and read file */
        settings a, b;
        char text[SETTINGS_MAX];
        settings_defaults(&a, t->file);
        settings_defaults(&b, "");
        size_t n = settings_format(&a, text, sizeof text);
        CHECK(n > 0 && settings_parse(text, n, &b) == 1 && !strcmp(b.music, t->file));
    }
}

/* ---------------------------------------------------------------- the settings file (settings.c) */

static int settings_sane(const settings *s)
{
    size_t n = strlen(s->music);
    if (n >= SETTINGS_NAME || (s->sounds != 0 && s->sounds != 1) || s->logo < 0 || s->logo >= SETTINGS_STYLES ||
        s->pattern < 0 || s->pattern >= SETTINGS_STYLES || s->scheme < 0 || s->scheme >= SETTINGS_SCHEMES ||
        s->design < 0 || s->design >= SETTINGS_DESIGNS)
        return 0;
    for (size_t i = 0; i < n; i++) {
        char c = s->music[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-' ||
              c == '_'))
            return 0;
    }
    return 1;
}

/* text read over the defaults (music "default.bcstm", sounds on): what settings_parse says, and the result */
static int parsed(const char *text, settings *s)
{
    char *copy = (char *)exact_copy((const uint8_t *)text, strlen(text));
    settings_defaults(s, "default.bcstm");
    int r = settings_parse(copy, strlen(text), s);
    free(copy);
    return r;
}

static void test_settings(void)
{
    settings s;
    /* before any file is read the drawing gets the defaults: white, Shard glass, the small houses (0.9.3) */
    CHECK(settings_logo() == SETTINGS_LOGO_DEFAULT && settings_pattern() == SETTINGS_PATTERN_DEFAULT &&
          settings_scheme() == SETTINGS_SCHEME_DEFAULT);
    CHECK(SETTINGS_LOGO_DEFAULT == 1 && SETTINGS_PATTERN_DEFAULT == 1 && SETTINGS_STYLES == 3 &&
          SETTINGS_SCHEME_DEFAULT == 0 && SETTINGS_SCHEMES == 8);
    settings_defaults(&s, "default.bcstm");
    CHECK(!strcmp(s.music, "default.bcstm") && s.sounds == 1 && s.logo == 1 && s.pattern == 1 && s.scheme == 0);
    settings_defaults(&s, NULL);
    CHECK(!s.music[0] && s.sounds == 1 && s.logo == 1 && s.pattern == 1 && s.scheme == 0);

    /* the colour scheme: each of the file's eight words, read and applied; written as that word again; the other
     * choices untouched by its line */
    static const char *const scheme_words[SETTINGS_SCHEMES] = {"white", "yellow", "sand", "mint", "sky", "lilac", "rose",
                                                               "night"};
    for (int c = 0; c < SETTINGS_SCHEMES; c++) {
        char text[SETTINGS_MAX], line[40];
        snprintf(text, sizeof text, "3DStockTheme settings 1\ncolour=%s\nsounds=off\n", scheme_words[c]);
        CHECK(parsed(text, &s) == 1 && s.scheme == c && s.sounds == 0 && s.logo == SETTINGS_LOGO_DEFAULT &&
              s.pattern == SETTINGS_PATTERN_DEFAULT && !strcmp(s.music, "default.bcstm"));
        settings_apply(&s);
        CHECK(settings_scheme() == c && settings_logo() == SETTINGS_LOGO_DEFAULT);
        snprintf(line, sizeof line, "\ncolour=%s\n", scheme_words[c]);
        size_t n = settings_format(&s, text, sizeof text);
        CHECK(n > 0 && strstr(text, line) == text + strlen("3DStockTheme settings 1"));  /* the first line after the header */
        settings back;
        settings_defaults(&back, "");
        back.scheme = (c + 3) % SETTINGS_SCHEMES;
        CHECK(settings_parse(text, n, &back) == 1 && back.scheme == c);
    }
    /* a value that is no scheme: white is drawn, and white is written, never a number */
    static const int no_scheme[] = {-1, 8, 9, 1000, -1000000};
    for (size_t i = 0; i < sizeof no_scheme / sizeof *no_scheme; i++) {
        char text[SETTINGS_MAX];
        settings_defaults(&s, "");
        s.scheme = no_scheme[i];
        settings_apply(&s);
        CHECK(settings_scheme() == SETTINGS_SCHEME_DEFAULT);
        CHECK(settings_format(&s, text, sizeof text) > 0 && strstr(text, "\ncolour=white\n"));
    }
    /* a word the file does not know, a number, another key's word, the German name: the colour stays the default
     * and the rest of the file is read */
    static const char *const bad_scheme[] = {"", "0", "7", "White", "white ", " white", "weiss", "gelb", "purple", "clear",
                                             "night\tx", "nightly"};
    for (size_t i = 0; i < sizeof bad_scheme / sizeof *bad_scheme; i++) {
        char text[200];
        snprintf(text, sizeof text, "3DStockTheme settings 1\ncolour=%s\nsounds=off\nglass=platin\n", bad_scheme[i]);
        CHECK(parsed(text, &s) == 1 && s.scheme == SETTINGS_SCHEME_DEFAULT && s.sounds == 0 && s.logo == 2);
    }
    /* the last colour line counts, as with every key; "color" is not the key */
    CHECK(parsed("3DStockTheme settings 1\ncolour=rose\ncolour=mint\n", &s) == 1 && s.scheme == 3);
    CHECK(parsed("3DStockTheme settings 1\ncolor=rose\nscheme=mint\n", &s) == 1 && s.scheme == SETTINGS_SCHEME_DEFAULT);
    /* a file as 0.9.2 wrote it (no colour line, every other line): white, and each value it holds is kept, also
     * "glass=clear" although a new file's glass is Shard */
    CHECK(parsed("3DStockTheme settings 1\nmusic=urban-shop.bcstm\nsounds=off\nglass=clear\npattern=bubbles\n", &s) == 1 &&
          s.scheme == 0 && !strcmp(s.music, "urban-shop.bcstm") && s.sounds == 0 && s.logo == 0 && s.pattern == 2);
    CHECK(parsed("3DStockTheme settings 1\nmusic=off\nsounds=on\nglass=platin\npattern=plus\n", &s) == 1 &&
          s.scheme == 0 && !s.music[0] && s.sounds == 1 && s.logo == 2 && s.pattern == 0);
    settings_defaults(&s, "");
    settings_apply(&s);

    /* the start screen's two choices: what the file says once it is applied, every pair; out of range is the default */
    static const char *const logo_words[3] = {"clear", "shard", "platin"}, *const pattern_words[3] = {"plus", "houses", "bubbles"};
    for (int l = 0; l < 3; l++)
        for (int q = 0; q < 3; q++) {
            char text[200];
            snprintf(text, sizeof text, "3DStockTheme settings 1\npattern=%s\nglass=%s\n", pattern_words[q], logo_words[l]);
            CHECK(parsed(text, &s) == 1 && s.logo == l && s.pattern == q && s.sounds == 1 && !strcmp(s.music, "default.bcstm"));
            settings_apply(&s);
            CHECK(settings_logo() == l && settings_pattern() == q);
        }
    static const int out_of_range[] = {-1, 3, 4, 1000, -1000000};
    for (size_t i = 0; i < sizeof out_of_range / sizeof *out_of_range; i++) {
        settings_defaults(&s, "");
        s.logo = out_of_range[i];
        s.pattern = out_of_range[i];
        settings_apply(&s);
        CHECK(settings_logo() == SETTINGS_LOGO_DEFAULT && settings_pattern() == SETTINGS_PATTERN_DEFAULT);
        char text[SETTINGS_MAX];                    /* and such a value is written as its default, never as a number */
        size_t n = settings_format(&s, text, sizeof text);
        CHECK(n > 0 && strstr(text, "\nglass=shard\n") && strstr(text, "\npattern=houses\n"));
    }
    static const char *const bad_style[] = {"", "0", "1", "2", "Clear", "clear ", "houses", "glass", "shard\tx", "platinum"};
    for (size_t i = 0; i < sizeof bad_style / sizeof *bad_style; i++) {
        char text[200];
        snprintf(text, sizeof text, "3DStockTheme settings 1\nglass=%s\nsounds=off\n", bad_style[i]);
        CHECK(parsed(text, &s) == 1 && s.logo == SETTINGS_LOGO_DEFAULT && s.sounds == 0);
        snprintf(text, sizeof text, "3DStockTheme settings 1\npattern=%s\nsounds=off\n",
                 !strcmp(bad_style[i], "houses") ? "clear" : bad_style[i]);
        CHECK(parsed(text, &s) == 1 && s.pattern == SETTINGS_PATTERN_DEFAULT && s.sounds == 0);
    }
    /* a file from before the two choices existed: they keep their defaults, the rest is read */
    CHECK(parsed("3DStockTheme settings 1\nmusic=off\nsounds=off\n", &s) == 1 && !s.music[0] && s.sounds == 0 &&
          s.logo == SETTINGS_LOGO_DEFAULT && s.pattern == SETTINGS_PATTERN_DEFAULT);
    CHECK(parsed("3DStockTheme settings 1\nglass=platin\nglass=shard\npattern=bubbles\n", &s) == 1 && s.logo == 1 && s.pattern == 2);
    settings_defaults(&s, "");
    settings_apply(&s);

    /* what the application writes, and its variations */
    CHECK(parsed("3DStockTheme settings 1\nmusic=urban-shop.bcstm\nsounds=off\n", &s) == 1 &&
          !strcmp(s.music, "urban-shop.bcstm") && s.sounds == 0);
    CHECK(parsed("3DStockTheme settings 1\nsounds=on\nmusic=off\n", &s) == 1 && !s.music[0] && s.sounds == 1);
    CHECK(parsed("3DStockTheme settings 1\r\nmusic=a.bcstm\r\nsounds=off\r\n", &s) == 1 && !strcmp(s.music, "a.bcstm") &&
          s.sounds == 0);
    CHECK(parsed("3DStockTheme settings 1\nmusic=a.bcstm", &s) == 1 && !strcmp(s.music, "a.bcstm") && s.sounds == 1);
    CHECK(parsed("3DStockTheme settings 1\n", &s) == 1 && !strcmp(s.music, "default.bcstm") && s.sounds == 1);
    CHECK(parsed("3DStockTheme settings 1\nmusic=a.bcstm\nmusic=b.bcstm\n", &s) == 1 && !strcmp(s.music, "b.bcstm"));
    /* a later version's lines, empty lines and lines without "=" are skipped */
    CHECK(parsed("3DStockTheme settings 1\nvolume=7\n\nnonsense\n=x\nmusic=a.bcstm\nsounds=off\ntheme=dark\n", &s) == 1 &&
          !strcmp(s.music, "a.bcstm") && s.sounds == 0);

    /* a value this cannot use leaves that choice at its default, the other one is still read */
    static const char *const bad_music[] = {"", "../../boot.firm", "a/b.bcstm", "a b.bcstm", "a\\b", "sdmc:/x", "\xC3\xA4.bcstm",
                                            "0123456789012345678901234567890123456789012345678", "a.bcstm ", " off"};
    for (size_t i = 0; i < sizeof bad_music / sizeof *bad_music; i++) {
        char text[200];
        snprintf(text, sizeof text, "3DStockTheme settings 1\nmusic=%s\nsounds=off\n", bad_music[i]);
        CHECK(parsed(text, &s) == 1 && !strcmp(s.music, "default.bcstm") && s.sounds == 0);
    }
    static const char *const bad_sounds[] = {"", "1", "0", "ON", "yes", "on ", "offf"};
    for (size_t i = 0; i < sizeof bad_sounds / sizeof *bad_sounds; i++) {
        char text[200];
        snprintf(text, sizeof text, "3DStockTheme settings 1\nsounds=%s\nmusic=off\n", bad_sounds[i]);
        CHECK(parsed(text, &s) == 1 && s.sounds == 1 && !s.music[0]);
    }
    CHECK(parsed("3DStockTheme settings 1\nmusic=0123456789012345678901234567890123456789012.bcs\n", &s) == 1 &&
          strlen(s.music) == SETTINGS_NAME - 1);       /* the longest name that fits */

    /* not a settings file of this version: nothing is read from it */
    static const char *const foreign[] = {
        "", "3DStockTheme settings 1", "3DStockTheme settings 2\nmusic=off\nsounds=off\n",
        "3DStockTheme settings 10\nmusic=off\n", "3DStockTheme settings\nmusic=off\n", " 3DStockTheme settings 1\nmusic=off\n",
        "music=off\nsounds=off\n", "3dstocktheme settings 1\nmusic=off\n", "3DStockTheme settings 1 \nmusic=off\n",
        "PK\3\4", "\n3DStockTheme settings 1\nmusic=off\n", "3DStockTheme settings 1\rmusic=off\n",
    };
    for (size_t i = 0; i < sizeof foreign / sizeof *foreign; i++)
        CHECK(parsed(foreign[i], &s) == 0 && !strcmp(s.music, "default.bcstm") && s.sounds == 1);
    {
        static char large[SETTINGS_MAX + 2];        /* a settings file is never this large */
        int n = snprintf(large, sizeof large, "3DStockTheme settings 1\nmusic=off\nsounds=off\n");
        memset(large + n, '\n', sizeof large - 1 - (size_t)n);
        settings_defaults(&s, "default.bcstm");
        CHECK(settings_parse(large, SETTINGS_MAX + 1, &s) == 0 && !strcmp(s.music, "default.bcstm") && s.sounds == 1);
        CHECK(settings_parse(large, SETTINGS_MAX, &s) == 1 && !s.music[0] && s.sounds == 0);
        /* NUL bytes in a line make it a line this does not know */
        char nul[] = "3DStockTheme settings 1\nmusic=a\0b\nsounds=off\n";
        settings_defaults(&s, "default.bcstm");
        CHECK(settings_parse(nul, sizeof nul - 1, &s) == 1 && !strcmp(s.music, "default.bcstm") && s.sounds == 0);
    }

    /* written and read back, every combination; too little room writes nothing */
    static const char *const music[] = {"", "somewhere-in-the-elevator.bcstm", "a"};
    for (size_t m = 0; m < 3; m++)
        for (int sounds = 0; sounds < 2; sounds++) {
            settings a, b;
            char text[SETTINGS_MAX];
            settings_defaults(&a, music[m]);
            a.sounds = sounds;
            a.logo = (int)(m + (size_t)sounds) % SETTINGS_STYLES;
            a.pattern = (int)(2 * m + (size_t)sounds) % SETTINGS_STYLES;
            a.scheme = (int)(3 * m + 5 * (size_t)sounds) % SETTINGS_SCHEMES;
            size_t n = settings_format(&a, text, sizeof text);
            CHECK(n > 0 && n == strlen(text));
            settings_defaults(&b, "other.bcstm");
            b.sounds = !sounds;
            b.logo = (a.logo + 1) % SETTINGS_STYLES;
            b.pattern = (a.pattern + 1) % SETTINGS_STYLES;
            b.scheme = (a.scheme + 1) % SETTINGS_SCHEMES;
            CHECK(settings_parse(text, n, &b) == 1 && !strcmp(b.music, a.music) && b.sounds == sounds && b.logo == a.logo &&
                  b.pattern == a.pattern && b.scheme == a.scheme);
            CHECK_EQ(settings_format(&a, text, n), 0);
            /* cut short anywhere it is still read without harm: a settings file or not, and sane either way */
            for (size_t cut = 0; cut <= n; cut++) {
                char *part = (char *)exact_copy((const uint8_t *)text, cut);
                settings_defaults(&b, "default.bcstm");
                int r = settings_parse(part, cut, &b);
                CHECK((r == 0 || r == 1) && settings_sane(&b));
                CHECK(r == (cut >= strlen("3DStockTheme settings 1\n")));
                free(part);
            }
        }

    /* the file on the card: missing, written, read, damaged, in a folder that is not there */
    char path[1024], gone[1100];
    const char *tmp = getenv("TMPDIR");
    snprintf(path, sizeof path, "%s/test-audio-settings-%ld.txt", tmp && *tmp ? tmp : "/tmp", (long)getpid());
    snprintf(gone, sizeof gone, "%s.d/settings.txt", path);
    remove(path);
    CHECK(settings_load(path, &s, "default.bcstm") == 0 && !strcmp(s.music, "default.bcstm") && s.sounds == 1);
    snprintf(s.music, sizeof s.music, "urban-shop.bcstm");
    s.sounds = 0;
    s.logo = 2;
    s.pattern = 0;
    s.scheme = 7;
    CHECK_EQ(settings_save(path, &s), 0);
    settings back;
    CHECK(settings_load(path, &back, "default.bcstm") == 1 && !strcmp(back.music, "urban-shop.bcstm") && back.sounds == 0 &&
          back.logo == 2 && back.pattern == 0 && back.scheme == 7);
    s.music[0] = 0;
    s.sounds = 1;
    s.logo = 1;
    s.pattern = 2;
    s.scheme = 4;
    CHECK_EQ(settings_save(path, &s), 0);            /* written anew, not added to */
    CHECK(settings_load(path, &back, "default.bcstm") == 1 && !back.music[0] && back.sounds == 1 && back.logo == 1 &&
          back.pattern == 2 && back.scheme == 4);
    static const char *const damaged[] = {"", "3DStockTheme sett", "\xFF\xFE\0\0garbage", "3DStockTheme settings 3\nmusic=off\n"};
    for (size_t i = 0; i < sizeof damaged / sizeof *damaged; i++) {
        FILE *f = fopen(path, "wb");
        CHECK(f != NULL);
        if (f) {
            fwrite(damaged[i], 1, i == 2 ? 11 : strlen(damaged[i]), f);
            fclose(f);
        }
        CHECK(settings_load(path, &back, "default.bcstm") == 0 && !strcmp(back.music, "default.bcstm") && back.sounds == 1 &&
              back.logo == SETTINGS_LOGO_DEFAULT && back.pattern == SETTINGS_PATTERN_DEFAULT &&
              back.scheme == SETTINGS_SCHEME_DEFAULT);
    }
    {
        FILE *f = fopen(path, "wb");                /* larger than a settings file: not read */
        CHECK(f != NULL);
        if (f) {
            fputs("3DStockTheme settings 1\nmusic=off\nsounds=off\n", f);
            for (int i = 0; i < SETTINGS_MAX; i++)
                fputc('\n', f);
            fclose(f);
        }
        CHECK(settings_load(path, &back, "default.bcstm") == 0 && !strcmp(back.music, "default.bcstm") && back.sounds == 1);
    }
    remove(path);
    CHECK_EQ(settings_save(gone, &s), -1);
    CHECK(settings_load(gone, &back, "default.bcstm") == 0 && back.sounds == 1);

    /* mutations of a good file: never more than the choice is lost */
    int read = 0, rounds = 20000;
    for (int k = 0; k < rounds; k++) {
        char text[SETTINGS_MAX];
        settings a;
        settings_defaults(&a, "somewhere-in-the-elevator.bcstm");
        a.sounds = k & 1;
        a.logo = k % SETTINGS_STYLES;
        a.pattern = k / 3 % SETTINGS_STYLES;
        a.scheme = k / 9 % SETTINGS_SCHEMES;
        size_t n = settings_format(&a, text, sizeof text);
        int flips = 1 + (int)(next_rand() % 4);
        for (int j = 0; j < flips; j++)
            text[next_rand() % n] = (char)next_rand();
        if (next_rand() % 8 == 0)
            n = next_rand() % (n + 1);
        char *copy = (char *)exact_copy((const uint8_t *)text, n);
        settings_defaults(&a, "default.bcstm");
        int r = settings_parse(copy, n, &a);
        read += r;
        CHECK((r == 0 || r == 1) && settings_sane(&a));
        free(copy);
    }
    printf("mutations of a settings file: %d rounds, %d still read\n", rounds, read);
}

/* G1, the look (settings.h): the line design= after pattern=, one of four words exactly; another word, an empty one,
 * another case or no line: Stock. The files of 0.9.3 and of the test builds are read, a line of another build
 * skipped; glass= is read and written back as it was. */
static void test_settings_design(void)
{
    settings s;
    char text[SETTINGS_MAX];
    static const char *const words[SETTINGS_DESIGNS] = {"stock", "frost", "night", "prism"};
    CHECK(SETTINGS_DESIGNS == 4 && SETTINGS_DESIGN_DEFAULT == 0);
    settings_defaults(&s, "");
    CHECK(s.design == SETTINGS_DESIGN_DEFAULT);
    for (int d = 0; d < SETTINGS_DESIGNS; d++) {
        char file[200], last[60];
        snprintf(file, sizeof file, "3DStockTheme settings 1\ncolour=sand\ndesign=%s\nsounds=off\n", words[d]);
        CHECK(parsed(file, &s) == 1 && s.design == d && s.scheme == 2 && s.sounds == 0);
        size_t n = settings_format(&s, text, sizeof text);
        snprintf(last, sizeof last, "\npattern=houses\ndesign=%s\n", words[d]);     /* the last line, after pattern= */
        CHECK(n > strlen(last) && !strcmp(text + n - strlen(last), last));
        settings back;
        settings_defaults(&back, "");
        back.design = (d + 1) % SETTINGS_DESIGNS;
        CHECK(settings_parse(text, n, &back) == 1 && back.design == d);
    }
    static const char *const other[] = {"", "Stock", "FROST", "Night", "PRISM", "prisma", "nachtglas", "glass",
                                        "frost ", " frost", "1", "3", "stockx", "fros", "night\r\r", "prism=prism"};
    for (size_t i = 0; i < sizeof other / sizeof *other; i++) {
        char file[200];
        snprintf(file, sizeof file, "3DStockTheme settings 1\ndesign=prism\ndesign=%s\nsounds=off\n", other[i]);
        CHECK(parsed(file, &s) == 1 && s.design == SETTINGS_DESIGN_DEFAULT && s.sounds == 0);
    }
    for (int bad = -1; bad <= SETTINGS_DESIGNS; bad += SETTINGS_DESIGNS + 1) {    /* never written as a number */
        settings_defaults(&s, "");
        s.design = bad;
        CHECK(settings_format(&s, text, sizeof text) > 0 && strstr(text, "\ndesign=stock\n"));
    }
    /* 0.9.3's file: no line, Stock, the rest as it was; a test build's file with its own line, read without it */
    CHECK(parsed("3DStockTheme settings 1\ncolour=night\nmusic=off\nsounds=on\nglass=platin\npattern=plus\n", &s) == 1 &&
          s.design == 0 && s.scheme == 7 && !s.music[0] && s.logo == 2 && s.pattern == 0);
    CHECK(parsed("3DStockTheme settings 1\ncolour=mint\nmusic=off\nsounds=off\nglass=shard\npattern=bubbles\n"
                 "frost_ground=start\n", &s) == 1 && s.design == 0 && s.scheme == 3 && s.sounds == 0 && s.pattern == 2);
    CHECK(parsed("3DStockTheme settings 1\ncolour=rose\nnachtglas=fixed\nmusic=off\nsounds=on\nglass=clear\n"
                 "pattern=houses\ndesign=night\n", &s) == 1 && s.design == 2 && s.scheme == 6 && s.logo == 0);
    size_t n = settings_format(&s, text, sizeof text);
    CHECK(n > 0 && strstr(text, "\nglass=clear\npattern=houses\ndesign=night\n") && !strstr(text, "nachtglas="));
    /* the file's largest size with the line: 512 bytes are read, 513 not */
    static char large[SETTINGS_MAX + 2];
    const char *line = "design=frost\n";
    for (size_t size = SETTINGS_MAX; size <= SETTINGS_MAX + 1; size++) {
        int head = snprintf(large, sizeof large, "3DStockTheme settings 1\nsounds=off\n");
        memset(large + head, '\n', size - (size_t)head);
        memcpy(large + size - strlen(line), line, strlen(line));
        settings_defaults(&s, "default.bcstm");
        int r = settings_parse(large, size, &s);
        CHECK(size == SETTINGS_MAX ? r == 1 && s.design == 1 && s.sounds == 0 : r == 0 && s.design == 0 && s.sounds == 1);
    }
    /* the longest file this writes still fits */
    settings_defaults(&s, "0123456789012345678901234567890123456789012.bcs");
    s.scheme = 4, s.design = 3;
    n = settings_format(&s, text, sizeof text);
    CHECK(n > 0 && n < SETTINGS_MAX / 2 && strstr(text, "\ncolour=sky\n") && strstr(text, "\ndesign=prism\n"));
}

int main(void)
{
    test_body_kinds();
    test_body_texels();
    test_body_refusals();
    test_body_gradient();
    test_wall_x();
    test_media();
    test_tracks();
    test_settings();
    test_settings_design();
    return check_summary("test_audio");
}
