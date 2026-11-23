/* Scratch renderer: the sprite list of one set of generated art (record.sh builds it against a join's ui_art.c and
 * art_<look>.c and the application's part, part_art.c), free of ids, order and places: per look and scheme every
 * sprite of the atlas and the glass sheet, the kit's and the part's (each read from its own texture's blob), by
 * its name with its size, pad, cap and a hash of its pixels (row by row from the top, each A B G R as the texture
 * holds it; "-" for a name the look does not draw), the part's pictures (HOUSE, the turning house's sheet), the
 * pattern tiles and the look's own blobs (the hash of all their bytes), the colours; per look the theme icon's corners; once the selection's
 * corners. One line each: <look> <scheme> <name> <what it is>. */
#include <stdio.h>
#include <stdlib.h>
#include <zlib.h>
#include "ui_art.h"
#include "part_art.h"
#include "art_names.h"

static unsigned char buf[8 << 20];
static uLongf got;

static unsigned long long fnv(const unsigned char *p, size_t n)
{
    unsigned long long h = 1469598103934665603ull;
    while (n--)
        h = (h ^ *p++) * 1099511628211ull;
    return h;
}
static void unpack(const uint8_t *z, uint32_t size, const char *what)
{
    got = sizeof buf;
    if (uncompress(buf, &got, z, size) != Z_OK) {
        fprintf(stderr, "sprites: %s does not unpack\n", what);
        exit(1);
    }
}
static unsigned long long pixels(const art_sprite *s, int width)
{
    unsigned long long h = 1469598103934665603ull;
    for (int y = s->y; y < s->y + s->h; y++)
        for (int x = s->x; x < s->x + s->w; x++) {
            size_t k = (size_t)((y >> 3) * (width >> 3) + (x >> 3)) << 6 | (x & 1) | (y & 1) << 1 | (x & 2) << 1 |
                       (y & 2) << 2 | (x & 4) << 2 | (y & 4) << 3;
            for (int b = 0; b < 4; b++)
                h = (h ^ buf[4 * k + b]) * 1099511628211ull;
        }
    return h;
}
static void sprite(const char *look, const char *scheme, const char *name, const art_sprite *s, int width)
{
    if (!s->w) {
        printf("%s %s %s -\n", look, scheme, name);
        return;
    }
    printf("%s %s %s %d %d pad %d %d %d %d cap %d %d %d %d %016llx\n", look, scheme, name, s->w, s->h, s->pad[0],
           s->pad[1], s->pad[2], s->pad[3], s->cap[0], s->cap[1], s->cap[2], s->cap[3], pixels(s, width));
}
static void whole(const char *look, const char *scheme, const char *name, const uint8_t *z, uint32_t size)
{
    unpack(z, size, name);
    printf("%s %s %s bytes %lu %016llx\n", look, scheme, name, (unsigned long)got, fnv(buf, got));
}
static void one_look(const look_art *a, const char *look)
{
    char name[96];
    const art_part_look *part = NULL;
    for (int k = 0; k < ART_APP_PART.count; k++)
        part = ART_APP_PART.looks[k].look == a ? &ART_APP_PART.looks[k] : part;
    if (!part) {
        fprintf(stderr, "sprites: the part has no tables of %s\n", look);
        exit(1);
    }
    for (int s = 0; s < ART_SCHEMES; s++) {
        const char *sc = ART_SCHEME_NAME[s];
        unpack(a->atlas_z[s], a->atlas_z_size[s], "an atlas");
        for (int id = 0; id < ART_COUNT; id++)
            sprite(look, sc, ART_NAME[id], &a->sprites[id], ART_ATLAS_W);
        if (part->count)
            unpack(part->atlas_z[s], part->atlas_z_size[s], "the part's atlas");
        for (int k = 0; k < part->count; k++)
            sprite(look, sc, ART_NAME[ART_ALL + k], &part->sprites[k], ART_APP_PART.atlas_w);
        unpack(a->sheet_z[s], a->sheet_z_size[s], "a sheet");
        for (int id = ART_COUNT; id < ART_ALL; id++)
            sprite(look, sc, ART_NAME[id], &a->sheet_sprites[id - ART_COUNT], ART_GLASS_SHEET_W);
        if (part->sheet_count)
            unpack(part->sheet_z[s], part->sheet_z_size[s], "the part's sheet");
        for (int k = 0; k < part->sheet_count; k++)
            sprite(look, sc, ART_NAME[ART_ALL + part->count + k], &part->sheet_sprites[k], ART_APP_PART.sheet_w);
        if (part->pictures_z)
            whole(look, sc, "HOUSE", part->pictures_z[s], part->pictures_z_size[s]);
        for (int p = 0; p < ART_PATTERNS; p++) {
            snprintf(name, sizeof name, "PATTERN_%d", p);
            whole(look, sc, name, a->pattern_z[s][p], a->pattern_z_size[s][p]);
        }
        for (int c = 0; c < ART_COLOURS; c++)
            printf("%s %s COLOUR_%s %06X\n", look, sc, ART_COLOUR_NAME[c], (unsigned)a->colour[s][c]);
    }
    printf("%s - ICON_MASK %016llx\n", look, fnv(a->mask, ART_MASK_SIZE * ART_MASK_SIZE));
}

int main(void)
{
    char name[96];
    unpack(ART_CORNERS_Z, ART_CORNERS_Z_SIZE, "the corners");
    for (int k = 0; k < ART_CORNER_COUNT; k++)
        sprite("-", "-", ART_CORNER_NAME[k], &ART_CORNER_SPRITES[k], ART_CORNERS_W);
#define LOOK(art, look) one_look(&art, look);
    ART_LOOKS(LOOK)
#define BLOB(z, size, look, blob, n) \
    for (int s = 0; s < ART_SCHEMES; s++) \
        for (int k = 0; k < n; k++) { \
            snprintf(name, sizeof name, "BLOB_%s_%d", blob, k); \
            whole(look, ART_SCHEME_NAME[s], name, z[s][k], size[s][k]); \
        }
    ART_BLOBS(BLOB)
    (void)name;
    return 0;
}
