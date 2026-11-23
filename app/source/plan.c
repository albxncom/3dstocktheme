#include "plan.h"

#include <stdlib.h>
#include <string.h>

#include "files.h"

typedef struct mem_source {
    const uint8_t *p;
} mem_source;

static int mem_read(void *ctx, uint64_t offset, void *buf, size_t len)
{
    memcpy(buf, ((mem_source *)ctx)->p + offset, len);
    return 0;
}

static tp_source source_of(mem_source *m, const uint8_t *p, size_t size)
{
    m->p = p;
    return (tp_source){m, size, mem_read};
}

static int usable_in(const am_view *am, uint16_t index)
{
    for (size_t j = 0; j < am->count; j++)
        if (am->list[j].index == index)
            return am->usable[j];
    return 0;
}

/* installed.tmd first, then installed.tmd.new: the first that AM's list confirms. */
static int pick_record(pack_view *pv, const am_view *am, const store_record rec[2])
{
    int any = 0, bad = 0, stale = 0;
    for (int i = 0; i < 2; i++) {
        if (!rec[i].present)
            continue;
        any = 1;
        if (rec[i].result != FILES_OK) {
            bad = bad ? bad : TP_E_READ;
            continue;
        }
        uint64_t title = 0;
        uint16_t version;
        size_t count;
        int r = tp_tmd_read(rec[i].tmd, rec[i].size, &title, &version, &count);
        if (!r && title != TP_TITLE_ID)
            r = TP_E_TITLE;
        tp_difference d = {0};
        if (!r)
            r = tp_tmd_check(rec[i].tmd, rec[i].size, am->version, am->list, am->count, &d);
        if (r == TP_OK) {
            pv->tmd = rec[i].tmd;
            pv->tmd_size = rec[i].size;
            pv->from_staged = i;
            return PACK_OK;
        }
        if (r == TP_E_STALE && !stale) {
            pv->diff = d;
            stale = 1;
        } else if (r != TP_E_STALE && !bad) {
            bad = r;
        }
    }
    if (!any)
        return PACK_NO_STORE;
    if (stale)
        return PACK_STALE;
    pv->detail = bad;
    return PACK_STORE_BAD;
}

static int read_catalogue(pack_view *pv, const am_view *am, const catalogue_reader *cat)
{
    uint8_t *archive = malloc(PACK_ARCHIVE_MAX);
    if (!archive)
        return PACK_NOMEM;
    int state = PACK_CATALOGUE_BAD;
    size_t size = 0, records = 0;
    uint16_t top = 0, version;
    uint64_t title;
    mem_source ma, mi;
    uint8_t icon[TP_ICON_SIZE];
    if (cat->archive(cat->ctx, archive, PACK_ARCHIVE_MAX, &size)) {
        pv->detail = TP_E_READ;
        goto out;
    }
    tp_source asrc = source_of(&ma, archive, size);
    if ((pv->detail = tp_installed_top(&asrc, &top)) != TP_OK)
        goto out;
    tp_tmd_read(pv->tmd, pv->tmd_size, &title, &version, &records);
    pv->themes = calloc(records ? records : 1, sizeof *pv->themes);
    if (!pv->themes) {
        state = PACK_NOMEM;
        goto out;
    }
    pv->top = top;
    for (size_t k = 1; k < records; k++) {
        tp_content rec;
        tp_tmd_record(pv->tmd, pv->tmd_size, k, &rec);
        if (rec.index > top) {
            pv->detail = TP_E_CATALOGUE;    /* a theme the catalogue does not list */
            goto out;
        }
        if (cat->icon(cat->ctx, rec.index, icon)) {
            pv->detail = TP_E_READ;
            goto out;
        }
        tp_source isrc = source_of(&mi, icon, TP_ICON_SIZE);
        tp_installed entry;
        if ((pv->detail = tp_installed_entry(&asrc, &isrc, rec.index, &entry)) != TP_OK)
            goto out;
        pack_theme *t = &pv->themes[pv->count++];
        t->index = rec.index;
        t->usable = usable_in(am, rec.index);
        memcpy(t->name, entry.name, sizeof t->name);
        memcpy(t->description, entry.description, sizeof t->description);
        memcpy(t->icon, entry.icon, TP_ICON_SIZE);
        pv->usable += t->usable != 0;
    }
    pv->detail = TP_OK;
    state = PACK_OK;
out:
    free(archive);
    return state;
}

int pack_assess(pack_view *pv, const am_view *am, const store_record rec[2], uint16_t mark,
                const catalogue_reader *cat)
{
    memset(pv, 0, sizeof *pv);
    pv->mark = mark;
    pv->error = am->error;
    if (!am->ok)
        return pv->state = PACK_AM_ERROR;
    /* before the title: an import pending of a title AM does not know (a first install not cleaned up) refuses too */
    if (am->pending != PENDING_NO)
        return pv->state = am->pending == PENDING_YES ? PACK_PENDING : PACK_PENDING_UNKNOWN;
    if (!am->exists) {
        /* no title: an earlier pack's record files do not count, its mark does (the first install's index) */
        pv->top = mark;
        pv->ticket = am->ticket;
        return pv->state = PACK_NOT_INSTALLED;
    }
    pv->version = am->version;
    if ((pv->state = pick_record(pv, am, rec)) != PACK_OK)
        return pv->state;
    if (!usable_in(am, 0))
        return pv->state = PACK_NO_CATALOGUE;
    if ((pv->state = read_catalogue(pv, am, cat)) != PACK_OK)
        return pv->state;
    uint16_t used[TP_INDEX_MAX], next;
    if (mark > pv->top)
        pv->top = mark;
    for (size_t k = 0; k < pv->count; k++) {
        used[k] = pv->themes[k].index;
        if (used[k] > pv->top)
            pv->top = used[k];
    }
    if (pv->count >= TP_THEMES_MAX)
        return pv->state = PACK_FULL_THEMES;
    if (tp_next_index(used, pv->count, pv->top, &next) != TP_OK)
        return pv->state = PACK_FULL_INDEX;
    return pv->state = PACK_OK;
}

int pack_takes(const pack_view *pv)
{
    return pv->state == PACK_OK || pv->state == PACK_NOT_INSTALLED;
}

void pack_free(pack_view *pv)
{
    free(pv->themes);
    pv->themes = NULL;
    pv->count = 0;
}

void pack_cut_name(char out[0x41], const char *name)
{
    size_t n = strlen(name);
    if (n > 0x3F) {
        n = 0x3F;
        while (n && ((unsigned char)name[n] & 0xC0) == 0x80)
            n--;
    }
    memcpy(out, name, n);
    out[n] = 0;
}

uint16_t pack_find(const pack_view *pv, const char *name, const uint8_t icon[TP_ICON_SIZE], int *usable)
{
    char cut[0x41];
    uint16_t deleted = 0;
    pack_cut_name(cut, name);
    for (size_t k = 0; k < pv->count; k++) {
        const pack_theme *t = &pv->themes[k];
        if (strcmp(t->name, cut) || memcmp(t->icon, icon, TP_ICON_SIZE))
            continue;
        if (t->usable) {
            *usable = 1;
            return t->index;
        }
        if (!deleted)
            deleted = t->index;
    }
    *usable = 0;
    return deleted;
}

static int plan_icon(void *ctx, uint16_t index, uint8_t icon[TP_ICON_SIZE])
{
    const install_plan *p = ctx;
    if (index == p->index) {
        memcpy(icon, p->theme->icon, TP_ICON_SIZE);
        return 0;
    }
    for (size_t k = 0; k < p->pv->count; k++)
        if (p->pv->themes[k].index == index) {
            memcpy(icon, p->pv->themes[k].icon, TP_ICON_SIZE);
            return 0;
        }
    return -1;
}

void plan_catalogue(const install_plan *plan, tp_catalogue *cat)
{
    *cat = (tp_catalogue){plan->entries, plan->count, plan->top, plan_icon, (void *)plan};
}

/* The first install: AM knows no index, so only the mark counts; the catalogue holds the one theme, up to its index
 * (an index below it that an earlier pack handed out is listed as "(removed)"). */
static int plan_first(install_plan *p, const pack_view *pv, const tp_theme *theme)
{
    p->first = 1;
    p->new_ticket = !pv->ticket;
    int r = tp_next_index(NULL, 0, pv->top, &p->index);
    if (r) {
        p->detail = r;
        return p->state = r == TP_E_INDEX ? PLAN_FULL_INDEX : PLAN_CORE;
    }
    p->entries[0] = (tp_entry){p->index, theme->name, theme->description};
    p->count = 1;
    p->top = p->index;
    return p->state = PLAN_OK;
}

int plan_install(install_plan *p, const pack_view *pv, const tp_theme *theme)
{
    memset(p, 0, sizeof *p);
    p->pv = pv;
    p->theme = theme;
    if (pv->state == PACK_NOT_INSTALLED)
        return plan_first(p, pv, theme);
    if (pv->state != PACK_OK) {
        p->detail = TP_E_ARG;
        return p->state = PLAN_CORE;
    }
    int usable;
    uint16_t same = pack_find(pv, theme->name, theme->icon, &usable);
    if (same && usable) {
        p->duplicate = same;
        return p->state = PLAN_DUPLICATE;
    }
    if (pv->count >= TP_THEMES_MAX)
        return p->state = PLAN_FULL_THEMES;
    uint16_t used[TP_INDEX_MAX];
    for (size_t k = 0; k < pv->count; k++) {
        used[k] = pv->themes[k].index;
        p->entries[k] = (tp_entry){pv->themes[k].index, pv->themes[k].name, pv->themes[k].description};
    }
    int r = tp_next_index(used, pv->count, pv->top, &p->index);
    if (r) {
        p->detail = r;
        return p->state = r == TP_E_INDEX ? PLAN_FULL_INDEX : PLAN_CORE;
    }
    p->entries[pv->count] = (tp_entry){p->index, theme->name, theme->description};
    p->count = pv->count + 1;
    p->top = p->index > pv->top ? p->index : pv->top;
    tp_catalogue cat;
    plan_catalogue(p, &cat);
    if ((r = tp_delta_plan(pv->tmd, pv->tmd_size, TP_TITLE_ID, &cat, &p->delta)) != TP_OK) {
        p->detail = r;
        return p->state = PLAN_CORE;
    }
    if (p->delta.count != 1 || p->delta.add[0] != p->index) {
        p->detail = TP_E_ARG;
        return p->state = PLAN_CORE;
    }
    return p->state = PLAN_OK;
}
