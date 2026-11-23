/* The wallpapers of a decoded theme body (body.h). */
#include "body.h"
#include "internal.h"

#define HEADER_MIN 0xC4u

/* Whether size bytes at offset lie inside the body; offsets come from the body itself. */
static int inside(size_t body_size, uint32_t offset, uint64_t size)
{
    return (uint64_t)offset + size <= body_size;
}

static int texture(const uint8_t *body, size_t size, uint32_t at, unsigned width, unsigned shown_w, int motion,
                   tp_wall *w)
{
    uint32_t offset = get_le32(body + at);
    if (!inside(size, offset, (uint64_t)width * 256 * 2))
        return -1;
    w->kind = TP_WALL_TEXTURE;
    w->offset = offset;
    w->width = (uint16_t)width;
    w->height = 256;
    w->shown_w = (uint16_t)shown_w;
    w->motion = motion;
    return 0;
}

static int top_wall(const uint8_t *body, size_t size, tp_wall *w)
{
    uint32_t draw = get_le32(body + 0xC), frame = get_le32(body + 0x10);
    if (draw == 0)
        return 0;
    if (draw == 3) {
        if (frame == 0 || frame == 3)
            return texture(body, size, 0x18, 1024, 1008, TP_WALL_SCROLL, w);
        if (frame == 1)
            return texture(body, size, 0x18, 512, 412, TP_WALL_STILL, w);
        return -1;
    }
    uint32_t offset = get_le32(body + 0x14);
    if (!inside(size, offset, draw == 1 ? 5 : 7))
        return -1;
    /* Draw type 1: R, G, B, then how far the colour fades to white at the top (0 none, 255 all the way). Draw type
     * 2's fourth byte is documented as the colour's alpha, not as a gradient: the plain colour. */
    const uint8_t *c = body + offset;
    unsigned gradient = draw == 1 ? c[3] : 0;
    w->kind = TP_WALL_COLOR;
    for (int i = 0; i < 3; i++) {
        w->color[i] = c[i];
        w->color_top[i] = (uint8_t)(c[i] + (255 - c[i]) * gradient / 255);
    }
    return 0;
}

static int bottom_wall(const uint8_t *body, size_t size, tp_wall *w)
{
    uint32_t draw = get_le32(body + 0x20), frame = get_le32(body + 0x24);
    if (draw == 0)
        return 0;
    if (draw == 2)
        return -1;
    if (draw == 3) {
        switch (frame) {
        case 0:
        case 3:
            return texture(body, size, 0x28, 1024, 1008, TP_WALL_SCROLL, w);
        case 1:
            return texture(body, size, 0x28, 512, 320, TP_WALL_STILL, w);
        case 2:
            return texture(body, size, 0x28, 1024, 960, TP_WALL_FLIP, w);
        case 4:
            return texture(body, size, 0x28, 1024, 960, TP_WALL_FLIP_BACK, w);
        default:
            return -1;
        }
    }
    /* Solid colour: the outer area's 9-byte block (bottom stripes, main colour, edge glow) is used only when its
     * flag at 0x88 is 1; without it the body names no colour for the screen. */
    if (get_le32(body + 0x88) != 1)
        return 0;
    uint32_t offset = get_le32(body + 0x8C);
    if (!inside(size, offset, 9))
        return -1;
    w->kind = TP_WALL_COLOR;
    memcpy(w->color, body + offset + 3, 3);
    memcpy(w->color_top, body + offset + 3, 3);
    return 0;
}

int tp_body_read(const uint8_t *body, size_t size, tp_body *out)
{
    memset(out, 0, sizeof *out);
    if (size < HEADER_MIN || get_le32(body) != 1 || get_le32(body + 0xC) >= 4 || get_le32(body + 0x20) >= 4)
        return -1;
    out->use_bgm = body[5] != 0;
    if (top_wall(body, size, &out->top) || bottom_wall(body, size, &out->bottom)) {
        memset(out, 0, sizeof *out);
        return -1;
    }
    return 0;
}

float tp_wall_x(const tp_wall *wall, int screen_w, uint64_t ms)
{
    if (wall->kind != TP_WALL_TEXTURE || wall->width <= screen_w)
        return 0;
    long max = wall->width - screen_w, range = (long)wall->shown_w - screen_w;
    float x = 0;
    if (wall->motion == TP_WALL_STILL) {
        x = (float)range / 2;
    } else if (wall->motion == TP_WALL_SCROLL) {
        /* In thousandths of a texel, so a long preview neither overflows nor loses the position to float
         * rounding: the path there and back is 2 * range texels, and only ms modulo its length matters. */
        if (range > 0) {
            uint64_t trip = (uint64_t)range * 2 * 1000, at = ms % trip * TP_WALL_PAN % trip;
            uint64_t half = trip / 2;
            x = (float)(at <= half ? at : trip - at) / 1000;
        }
    } else {
        static const int back[4] = {0, 1, 2, 1};
        uint64_t step = ms / TP_WALL_FLIP_MS;
        x = (float)(320 * (wall->motion == TP_WALL_FLIP ? (int)(step % 3) : back[step % 4]));
    }
    if (x < 0)
        return 0;
    return x > (float)max ? (float)max : x;
}

uint16_t tp_wall_texel(const uint8_t *tiled, unsigned width, unsigned x, unsigned y)
{
    /* 8x8 tiles row after row; inside a tile the x and y bits interleave, x in the even bits (Z order). */
    unsigned tile = (y >> 3) * (width >> 3) + (x >> 3);
    unsigned z = (x & 1) | (y & 1) << 1 | (x & 2) << 1 | (y & 2) << 2 | (x & 4) << 2 | (y & 4) << 3;
    return get_le16(tiled + ((size_t)tile * 64 + z) * 2);
}
