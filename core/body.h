/* The wallpapers of a decoded theme body (body_LZ.bin after LZ11), for the preview: what each screen shows and how
 * its texture moves. Format: 3dbrew, "Home Menu Themes", sections "Decompressed body_LZ.bin structure" and
 * "Textures". No allocation; the body is hostile bytes, every offset is checked against its size. */
#ifndef TP_BODY_H
#define TP_BODY_H

#include <stddef.h>
#include <stdint.h>

#define TP_WALL_PAN 24          /* texels per second a scrolling texture glides */
#define TP_WALL_FLIP_MS 900     /* how long a flipbook shows each of its frames */

enum { TP_WALL_NONE, TP_WALL_COLOR, TP_WALL_TEXTURE };                    /* what a screen shows */
enum { TP_WALL_STILL, TP_WALL_SCROLL, TP_WALL_FLIP, TP_WALL_FLIP_BACK };  /* how a texture moves */

typedef struct tp_wall {
    int kind;                   /* TP_WALL_* */
    uint8_t color[3];           /* COLOR: R, G, B at the screen's lower edge */
    uint8_t color_top[3];       /* COLOR: at its upper edge (top draw type 1 fades towards white); else as color */
    uint32_t offset;            /* TEXTURE: where the tiled RGB565 texels start in the body */
    uint16_t width, height;     /* TEXTURE: 512x256 or 1024x256 texels */
    uint16_t shown_w;           /* TEXTURE: the width HOME shows, 240 high, from the top left: 412, 1008, 320, 960 */
    int motion;                 /* TEXTURE: TP_WALL_STILL ... */
} tp_wall;

typedef struct tp_body {
    int use_bgm;                /* byte 0x5 is not 0 */
    tp_wall top, bottom;
} tp_body;

/* 0, or -1 when this is not a body the reader accepts. The squares and pattern textures of top draw type 2 are not
 * read: the preview shows its colour only. */
int tp_body_read(const uint8_t *body, size_t size, tp_body *out);
/* The left edge, in texels, of the screen_w wide part of a texture to show ms after the preview began. */
float tp_wall_x(const tp_wall *wall, int screen_w, uint64_t ms);
/* The RGB565 texel at (x, y), y = 0 at the top, of a tiled texture `width` texels wide. */
uint16_t tp_wall_texel(const uint8_t *tiled, unsigned width, unsigned x, unsigned y);

#endif
