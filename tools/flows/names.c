/* Scratch renderer: the names of what an image call samples, for rec.c's second form. A call on the kit's atlas or
 * glass sheet, or on the application's part's atlas or sheet, is named by the sprite of the look in use whose place
 * in that texture holds the texels (its name in ui_art.h, or in part_art.h for the part's; and "+dx,dy" where the
 * call starts inside it), one on the selection's corners by its corner, one on the part's pictures (the turning
 * house's sheet) "HOUSE", any other "-": the camera picture, a wallpaper, a theme's icon, a look's own texture. A
 * region of one of the four that no sprite's place holds is "-atlas", "-sheet", "-part-atlas" or "-part-sheet". The
 * part's three textures have a word of their own in the texture events too (rec_texture_word). */
#include <stdio.h>
#include <string.h>
#include <zlib.h>
#include "kit.h"
#include "ui_art.h"
#include "part_art.h"
#include "art_names.h"

extern C3D_Tex ui_part_pictures_tex;      /* ui.c's, known by its memory */

static const char *named(const char *name, int sx, int sy, int x0, int y0)
{
    static char buf[96];
    if (x0 == sx && y0 == sy)
        snprintf(buf, sizeof buf, "%s", name);
    else
        snprintf(buf, sizeof buf, "%s+%d,%d", name, x0 - sx, y0 - sy);
    return buf;
}
static int holds(const art_sprite *s, int x0, int y0, int x1, int y1)
{
    return s && s->w && x0 >= s->x && y0 >= s->y && x1 <= s->x + s->w && y1 <= s->y + s->h;
}
/* the corners' texture is known by its pixels: ui.c keeps it to itself */
static int is_corners(const void *data)
{
    static const void *known;
    static unsigned char want[ART_CORNERS_W * ART_CORNERS_H * 4];
    static int unpacked;
    if (data == known)
        return 1;
    if (!unpacked) {
        uLongf size = sizeof want;
        unpacked = uncompress(want, &size, ART_CORNERS_Z, ART_CORNERS_Z_SIZE) == Z_OK ? 1 : -1;
    }
    if (unpacked < 0 || memcmp(data, want, sizeof want))
        return 0;
    known = data;
    return 1;
}
/* the textures of sprites, each with its ids and its word: the kit's atlas and sheet, the part's */
static void sprite_texes(C3D_Tex *tex[4])
{
    tex[0] = ui_sprite_tex(0, 0), tex[1] = ui_sprite_tex(ART_COUNT, 0);
    tex[2] = ui_sprite_tex(ART_ALL, 0), tex[3] = ui_sprite_tex(ART_ALL + ART_APP_COUNT, 0);
}
static const int first_id[5] = {0, ART_COUNT, ART_ALL, ART_ALL + ART_APP_COUNT,
                                 ART_ALL + ART_APP_COUNT + ART_APP_GLASS_COUNT};
static const char *const tex_word[4] = {"atlas", "sheet", "part-atlas", "part-sheet"};

/* the word of the part's texture whose memory data is, for its texture events; NULL for any other */
const char *rec_texture_word(const void *data)
{
    C3D_Tex *tex[4];
    sprite_texes(tex);
    for (int t = 2; t < 4; t++)
        if (tex[t] && data == tex[t]->data)
            return tex_word[t];
    if (data && data == ui_part_pictures_tex.data)
        return "part-pictures";
    return NULL;
}
const char *rec_sprite_name(const void *data, int w, int h, int x0, int y0, int x1, int y1)
{
    static char none[16];
    C3D_Tex *tex[4];
    sprite_texes(tex);
    for (int t = 0; t < 4; t++) {
        if (!tex[t] || data != tex[t]->data)
            continue;
        for (int id = first_id[t]; id < first_id[t + 1]; id++) {
            const art_sprite *s = ui_sprite_info(id);
            if (s && holds(s, x0, y0, x1, y1))
                return named(ART_NAME[id], s->x, s->y, x0, y0);
        }
        snprintf(none, sizeof none, "-%s", tex_word[t]);
        return none;                /* past every sprite's place: worth a look, never hidden */
    }
    if (data && data == ui_part_pictures_tex.data)
        return named("HOUSE", 0, 0, x0, y0);
    if (w == ART_CORNERS_W && h == ART_CORNERS_H && is_corners(data))
        for (int k = 0; k < ART_CORNER_COUNT; k++)
            if (holds(&ART_CORNER_SPRITES[k], x0, y0, x1, y1))
                return named(ART_CORNER_NAME[k], ART_CORNER_SPRITES[k].x, ART_CORNER_SPRITES[k].y, x0, y0);
    return "-";
}
