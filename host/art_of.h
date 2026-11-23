/* The host tests' way to a look's sprite by its id, in any look built and without the drawing layer (ui.c's
 * ui_sprite_info reads the look drawn and the part handed over): the kit's tables (ui_art.h), from ART_ALL on the
 * application's part's (part_art.h, ART_APP_PART); NULL for none. */
#ifndef ART_OF_H
#define ART_OF_H

#include <stddef.h>

#include "part_art.h"

static inline const art_sprite *art_of(const look_art *a, int id)
{
    if (id >= 0 && id < ART_COUNT)
        return &a->sprites[id];
    if (id >= ART_COUNT && id < ART_ALL)
        return &a->sheet_sprites[id - ART_COUNT];
    for (int k = 0; id >= ART_ALL && k < ART_APP_PART.count; k++) {
        const art_part_look *p = &ART_APP_PART.looks[k];
        if (p->look != a)
            continue;
        if (id - ART_ALL < p->count)
            return &p->sprites[id - ART_ALL];
        if (id - ART_ALL - p->count < p->sheet_count)
            return &p->sheet_sprites[id - ART_ALL - p->count];
    }
    return NULL;
}

/* The texture a look's sprite lies in, in a scheme: the blob and the size of the kit's atlas or sheet or of the
 * part's (ART_APP_PART's sizes); z NULL for none. */
typedef struct art_tex {
    const uint8_t *z;
    uint32_t z_size;
    int w, h;
} art_tex;

static inline art_tex art_tex_of(const look_art *a, int id, int scheme)
{
    if (id >= 0 && id < ART_COUNT)
        return (art_tex){a->atlas_z[scheme], a->atlas_z_size[scheme], ART_ATLAS_W, ART_ATLAS_H};
    if (id >= ART_COUNT && id < ART_ALL)
        return (art_tex){a->sheet_z[scheme], a->sheet_z_size[scheme], ART_GLASS_SHEET_W, ART_GLASS_SHEET_H};
    for (int k = 0; id >= ART_ALL && k < ART_APP_PART.count; k++) {
        const art_part_look *p = &ART_APP_PART.looks[k];
        if (p->look != a)
            continue;
        if (id - ART_ALL < p->count && p->atlas_z)
            return (art_tex){p->atlas_z[scheme], p->atlas_z_size[scheme], ART_APP_PART.atlas_w, ART_APP_PART.atlas_h};
        if (id - ART_ALL - p->count < p->sheet_count && p->sheet_z)
            return (art_tex){p->sheet_z[scheme], p->sheet_z_size[scheme], ART_APP_PART.sheet_w, ART_APP_PART.sheet_h};
    }
    return (art_tex){NULL, 0, 0, 0};
}

/* The part's pictures in a look and a scheme (ART_APP_PART's size); z NULL for none. */
static inline art_tex art_pictures_of(const look_art *a, int scheme)
{
    for (int k = 0; k < ART_APP_PART.count; k++) {
        const art_part_look *p = &ART_APP_PART.looks[k];
        if (p->look == a && p->pictures_z)
            return (art_tex){p->pictures_z[scheme], p->pictures_z_size[scheme], ART_APP_PART.pictures_w,
                             ART_APP_PART.pictures_h};
    }
    return (art_tex){NULL, 0, 0, 0};
}

/* The sheet's sprites, the kit's and then the part's: ART_SHEET_SPRITES of them, the k-th's id sheet_id(k). */
#define ART_SHEET_SPRITES (ART_ALL - ART_COUNT + ART_APP_GLASS_COUNT)
static inline int sheet_id(int k)
{
    return k < ART_ALL - ART_COUNT ? ART_COUNT + k : ART_ALL + ART_APP_COUNT + k - (ART_ALL - ART_COUNT);
}

#endif
