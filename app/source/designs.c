#include "designs.h"

#include <stdio.h>
#include <string.h>

int designs_readable(const pack_view *pv)
{
    return pv->state == PACK_OK || pv->state == PACK_FULL_THEMES || pv->state == PACK_FULL_INDEX ||
           pv->state == PACK_NOT_INSTALLED;
}

void designs_list(design_list *d, const pack_view *pv, const uint8_t music_of[PACK_CONTENTS])
{
    d->count = 0;
    if (!designs_readable(pv))
        return;
    for (size_t k = 0; k < pv->count && d->count < TP_THEMES_MAX; k++) {
        const pack_theme *t = &pv->themes[k];
        if (!t->usable)
            continue;
        design_row *r = &d->rows[d->count++];
        r->index = t->index;
        r->theme = t;
        r->music = t->index < PACK_CONTENTS ? music_of[t->index] : MUSIC_UNKNOWN;
        r->file[0] = 0;
    }
}

void designs_files(design_list *d, const scan_list *folder)
{
    for (size_t i = 0; i < d->count; i++) {
        d->rows[i].file[0] = 0;
        for (size_t j = 0; j < folder->count; j++) {
            const scan_item *it = &folder->items[j];
            if (!it->folder && it->in_pack == d->rows[i].index && it->in_pack_usable) {
                snprintf(d->rows[i].file, sizeof d->rows[i].file, "%s", it->file);
                break;
            }
        }
    }
}

unsigned designs_room(const pack_view *pv)
{
    if (!designs_readable(pv))
        return 0;
    unsigned records = pv->count < TP_THEMES_MAX ? (unsigned)(TP_THEMES_MAX - pv->count) : 0;
    unsigned indices = pv->top < TP_INDEX_MAX ? (unsigned)(TP_INDEX_MAX - pv->top) : 0;
    return records < indices ? records : indices;
}

unsigned designs_room_low(const pack_view *pv)
{
    unsigned room = designs_room(pv);
    return pack_takes(pv) && room < DESIGNS_ROOM_LOW ? room : 0;
}
