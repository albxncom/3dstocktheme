#include "install.h"

#include <stdlib.h>
#include <string.h>

#include "store.h"

typedef struct mem_sink {
    uint8_t *p;
    size_t size, cap;
} mem_sink;

static int mem_write(void *ctx, const void *buf, size_t len)
{
    mem_sink *m = ctx;
    if (len > m->cap - m->size)
        return -1;
    memcpy(m->p + m->size, buf, len);
    m->size += len;
    return 0;
}

typedef struct am_sink {
    const importer *imp;
    const install_progress *progress;
    int step;
    uint64_t done, total;
} am_sink;

static void show(const install_progress *progress, int step, uint64_t done, uint64_t total)
{
    if (progress && progress->show)
        progress->show(progress->ctx, step, done, total);
}

static int am_write(void *ctx, const void *buf, size_t len)
{
    am_sink *s = ctx;
    if (s->imp->content_write(s->imp->ctx, buf, len))
        return -1;
    s->done += len;
    show(s->progress, s->step, s->done, s->total);
    return 0;
}

static int same_record(const tp_content *a, const tp_content *b)
{
    return a->id == b->id && a->index == b->index && a->type == b->type && a->size == b->size &&
           !memcmp(a->sha256, b->sha256, 32);
}

/* One content through the importer; the bytes written must be the ones measured for the TMD. */
static int write_content(const importer *imp, const install_progress *progress, int step, const tp_catalogue *cat,
                         const tp_theme *theme, const tp_content *measured, install_result *res)
{
    if (imp->content_begin(imp->ctx, measured->index))
        return -1;
    am_sink s = {imp, progress, step, 0, measured->size};
    tp_sink sink = {&s, am_write};
    tp_content got;
    int r = theme ? tp_theme_content(theme, TP_TITLE_ID, measured->index, &sink, &got)
                  : tp_catalogue_content(cat, TP_TITLE_ID, measured->id, &sink, &got);
    if (!r && !same_record(&got, measured))
        r = TP_E_CHANGED;
    if (r) {
        res->detail = r;
        return -1;
    }
    return imp->content_finish(imp->ctx) ? -1 : 0;
}

int install_run(const install_plan *plan, const tp_theme *theme, const importer *imp, const char *store_dir,
                const install_progress *progress, install_result *res)
{
    memset(res, 0, sizeof *res);
    const pack_view *pv = plan->pv;
    tp_catalogue cat;
    plan_catalogue(plan, &cat);
    uint64_t title;
    uint16_t version;
    size_t installed = 0, size = 0;
    uint8_t *tmd = NULL;
    int r = plan->state == PLAN_OK ? TP_OK : TP_E_ARG;

    /* Everything the core can refuse happens here, before the first AM write. */
    res->failed = STEP_MEASURE;
    show(progress, STEP_MEASURE, 0, 1);
    if (!r)
        r = tp_tmd_read(pv->tmd, pv->tmd_size, &title, &version, &installed);
    if (!r)
        r = tp_catalogue_content(&cat, TP_TITLE_ID, plan->delta.content0_id, NULL, &res->content0);
    if (!r)
        r = tp_theme_content(theme, TP_TITLE_ID, plan->index, NULL, &res->theme);
    if (!r)
        size = tp_delta_tmd_size(&plan->delta, installed);
    if (!r && !(tmd = malloc(size)))
        r = TP_E_NOMEM;
    mem_sink m = {tmd, 0, size};
    tp_sink tmd_sink = {&m, mem_write};
    if (!r)
        r = tp_delta_tmd(pv->tmd, pv->tmd_size, &plan->delta, &res->content0, &res->theme, &tmd_sink);
    uint64_t new_title = 0;
    uint16_t new_version = 0;
    size_t new_count = 0;
    if (!r && m.size != size)
        r = TP_E_ARG;
    if (!r)
        r = tp_tmd_read(tmd, size, &new_title, &new_version, &new_count);
    if (!r && (new_title != TP_TITLE_ID || new_version != plan->delta.version || new_count != installed + 1))
        r = TP_E_TMD;
    /* The new TMD carries every installed theme record as installed, then the new theme (its index is above all). */
    for (size_t k = 1; !r && k <= installed; k++) {
        tp_content old, now;
        tp_tmd_record(tmd, size, k, &now);
        if (k < installed)
            tp_tmd_record(pv->tmd, pv->tmd_size, k, &old);
        if (k < installed ? !same_record(&old, &now) : !same_record(&res->theme, &now))
            r = TP_E_TMD;
    }
    if (r) {
        res->detail = r;
        free(tmd);
        return -1;
    }

    /* The record and the mark, before the first AM write. A .new file that is the only record matching AM becomes
     * installed.tmd first, so staging the next TMD cannot leave the card without a matching record. The mark is
     * raised to the new index now: an index is never handed out twice, even if this install fails. */
    res->failed = STEP_STAGE;
    if ((pv->from_staged && store_commit(store_dir)) || store_stage(store_dir, tmd, size) ||
        store_mark_raise(store_dir, plan->index)) {
        free(tmd);
        return -1;
    }

    /* Phase 1: the new TMD and content 0. */
    res->failed = STEP_TITLE1;
    show(progress, STEP_TITLE1, 0, 1);
    if (imp->title_begin(imp->ctx))
        goto fail;
    res->wrote = 1;
    res->failed = STEP_TMD;
    if (imp->tmd(imp->ctx, tmd, size))
        goto fail;
    res->tmd_accepted = 1;
    res->failed = STEP_CONTENT0;
    if (write_content(imp, progress, STEP_CONTENT0, &cat, NULL, &res->content0, res))
        goto fail;
    res->failed = STEP_COMMIT1;
    show(progress, STEP_COMMIT1, 0, 1);
    if (imp->commit(imp->ctx))
        goto fail;
    res->committed1 = 1;
    res->record_ok = !store_commit(store_dir);

    /* Phase 2: the theme content, into the title that now lists it. */
    res->failed = STEP_TITLE2;
    show(progress, STEP_TITLE2, 0, 1);
    if (imp->title_begin(imp->ctx))
        goto fail;
    res->failed = STEP_CONTEXT;
    if (imp->contexts(imp->ctx, &plan->index, 1))
        goto fail;
    res->failed = STEP_CONTENT;
    if (write_content(imp, progress, STEP_CONTENT, &cat, theme, &res->theme, res))
        goto fail;
    res->failed = STEP_COMMIT2;
    show(progress, STEP_COMMIT2, 0, 1);
    if (imp->commit(imp->ctx))
        goto fail;
    res->failed = STEP_NONE;
    res->done = 1;
    show(progress, STEP_DONE, 1, 1);
    free(tmd);
    return 0;

fail:
    imp->cancel(imp->ctx);
    free(tmd);
    return -1;
}

static int usable_at(const am_view *v, uint16_t index)
{
    for (size_t j = 0; j < v->count; j++)
        if (v->list[j].index == index)
            return v->usable[j];
    return 0;
}

void install_judge(const am_view *before, const am_view *after, const install_plan *plan,
                   const install_result *res, judgement *j)
{
    memset(j, 0, sizeof *j);
    j->seen = after->ok;
    j->pending = after->ok ? after->pending : PENDING_UNKNOWN;
    if (!res->done && !res->wrote) {
        j->outcome = OUT_NOTHING_WRITTEN;
        return;
    }
    if (!after->ok) {
        j->outcome = res->done ? OUT_DONE_UNCONFIRMED : OUT_UNKNOWN;
        return;
    }
    for (size_t k = 0; k < before->count; k++)
        if (before->list[k].index && before->usable[k] && !usable_at(after, before->list[k].index))
            j->lost++;
    j->new_installed = usable_at(after, plan->index);
    if (res->done)
        j->outcome = j->new_installed && !j->lost && after->pending == PENDING_NO ? OUT_DONE : OUT_DONE_UNCONFIRMED;
    else if (j->lost)
        j->outcome = OUT_LOST;
    else
        j->outcome = res->committed1 ? OUT_PHASE2_FAILED_KEPT : OUT_CANCELLED_KEPT;
}

int install_may_clean(const judgement *j, const install_result *res)
{
    return j->outcome == OUT_DONE && res->done && res->committed1 && res->record_ok;
}
