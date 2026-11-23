#include "deletion.h"

#include <string.h>

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static home_entry entry_at(const uint8_t *p)
{
    return (home_entry){le32(p), p[4], p[5]};
}

void home_parse(home_state *h, const uint8_t *save, size_t size)
{
    memset(h, 0, sizeof *h);
    if (!save || (size != HOME_SAVE_SIZE && size != HOME_SAVE_OLD_SIZE))
        return;
    h->current = entry_at(save + HOME_THEME_AT);
    for (int i = 0; i < HOME_SHUFFLE; i++)
        h->shuffle[i] = entry_at(save + HOME_SHUFFLE_AT + 8 * i);
    h->shuffle_on = save[HOME_SHUFFLE_ON_AT] != 0;
    h->ok = 1;
}

int home_names(const home_entry *e, uint16_t index)
{
    return e->type == HOME_TYPE_DLC && e->variation == HOME_PACK_VARIATION && e->index == index;
}

static int am_usable(const am_view *am, uint16_t index)
{
    for (size_t j = 0; j < am->count; j++)
        if (am->list[j].index == index)
            return am->usable[j];
    return 0;
}

int deletion_plan_make(deletion_plan *p, const pack_view *pv, const am_view *am, const home_state *home,
                       uint64_t title_id, uint16_t index)
{
    memset(p, 0, sizeof *p);
    p->index = index;
    if (title_id != TP_TITLE_ID)
        return p->state = DEL_TITLE;
    if (index == 0)
        return p->state = DEL_CATALOGUE;
    if (index > TP_INDEX_MAX)
        return p->state = DEL_INDEX;
    /* A full pack was read whole too; every other state left something unread or unmatched. */
    if (pv->state != PACK_OK && pv->state != PACK_FULL_THEMES && pv->state != PACK_FULL_INDEX)
        return p->state = DEL_PACK;
    for (size_t k = 0; k < pv->count && !p->theme; k++)
        if (pv->themes[k].index == index)
            p->theme = &pv->themes[k];
    if (!p->theme)
        return p->state = DEL_NOT_IN_PACK;
    tp_difference d;
    if (!am->ok || !am->exists || tp_tmd_check(pv->tmd, pv->tmd_size, am->version, am->list, am->count, &d))
        return p->state = DEL_PACK;
    if (am->pending != PENDING_NO)
        return p->state = DEL_PENDING;
    if (!am_usable(am, index))
        return p->state = DEL_NOT_INSTALLED;
    if (!home->ok)
        return p->state = DEL_HOME_UNKNOWN;
    if (home_names(&home->current, index))
        return p->state = DEL_ACTIVE;
    for (int i = 0; i < HOME_SHUFFLE; i++)
        if (home_names(&home->shuffle[i], index))
            return p->state = DEL_SHUFFLE;
    return p->state = DEL_OK;
}

int deletion_run(const deletion_plan *p, const deleter *d, uint32_t *error)
{
    *error = 0;
    if (p->state != DEL_OK || p->index == 0 || p->index > TP_INDEX_MAX)
        return -1;
    *error = d->delete_content(d->ctx, TP_TITLE_ID, p->index);
    return *error ? 1 : 0;
}

void deletion_judge(const am_view *before, const am_view *after, const pack_view *pv, const deletion_plan *p,
                    deletion_judgement *j)
{
    memset(j, 0, sizeof *j);
    if (!after->ok) {
        j->outcome = DEL_UNKNOWN;
        return;
    }
    for (size_t k = 0; k < before->count; k++) {
        uint16_t index = before->list[k].index;
        if (index != p->index && before->usable[k] && !am_usable(after, index))
            j->lost++;
    }
    tp_difference d;
    j->record_ok = after->exists &&
                   tp_tmd_check(pv->tmd, pv->tmd_size, after->version, after->list, after->count, &d) == TP_OK;
    j->outcome = j->lost ? DEL_LOST : am_usable(after, p->index) ? DEL_NOT_DONE : DEL_DONE;
}
