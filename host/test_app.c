/* The application's plain C (app/source: files, store, pack, install, scan, text) on the Mac.
 *
 *   test_app unit ZIP_WITH_BGM ZIP_WITHOUT_BGM
 *       unit tests: the pack's state from AM's view, the record and the catalogue; plans; the install sequence
 *       against an importer that records every call and fails each one in turn; the judgement afterwards; the
 *       zip list; every message in both languages. Works in a temporary folder.
 *   test_app install --store DIR --archive FILE --icons DIR --zip FILE --out DIR [--fail N] [--not-usable I]...
 *                    [--am-tmd FILE] [--music off]
 *       the application's install path on files: the record in DIR (installed.tmd, installed.tmd.new), the
 *       installed catalogue (ContentInfoArchive_EUR_en.bin and icons/ of the installed content 0), AM's view made
 *       from the record, or from --am-tmd (what AM would report: every content installed and owned, except
 *       --not-usable), an importer that writes
 *       OUT/tmd.bin, OUT/0.app, OUT/<index>.app and OUT/delta.json (what theme_pack.py verify-delta reads), and
 *       fails its call number N with --fail; --music off installs it as "Ohne Musik installieren" does
 *       (tp_theme_drop_bgm). FILE is a theme zip or a theme installer CIA. Prints one JSON line. Exit 0 installed, 1 refused or failed before
 *       any AM write, 2 failed after one.
 *   test_app texts
 *       every message in both languages, for reading.
 */
#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE           /* mkdtemp on macOS */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "deletion.h"
#include "designs.h"
#include "files.h"
#include "install.h"
#include "plan.h"
#include "scan.h"
#include "sha256.h"
#include "store.h"
#include "text.h"
#include "wrap.h"
#include "layout.h"
#include "scroll.h"
#include "progress.h"
#include "touch.h"
#include "popup.h"
#include "order.h"
#include "motion.h"

#define UI_POPUP_TEST 3
#include "download.h"
#include <curl/curl.h>

#include "../uikit/host/check.h"
#include "../uikit/host/ui_fakes.h"

static void put32le(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++)
        p[i] = (uint8_t)(v >> 8 * i);
}

static uint8_t *slurp(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "cannot open %s\n", path);
        exit(3);
    }
    fseek(f, 0, SEEK_END);
    *size = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *p = malloc(*size ? *size : 1);
    if (!p || fread(p, 1, *size, f) != *size)
        exit(3);
    fclose(f);
    return p;
}

static int same_file(const char *path, const uint8_t *p, size_t size)
{
    static uint8_t buf[STORE_MAX];
    size_t n;
    return files_read(path, buf, sizeof buf, &n) == FILES_OK && n == size && !memcmp(buf, p, size);
}

static int exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

/* ---------------------------------------------------------------- a pack made up in memory */

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

typedef struct fixture {
    uint8_t tmd[STORE_MAX];
    size_t tmd_size;
    uint8_t archive[PACK_ARCHIVE_MAX];
    size_t archive_size;
    uint8_t icons[TP_INDEX_MAX + 1][TP_ICON_SIZE];
    am_view am;
    int fail_archive, fail_icon;
    char names[TP_INDEX_MAX + 1][0x41];
} fixture;

static void icon_of(uint8_t icon[TP_ICON_SIZE], unsigned seed)
{
    for (size_t i = 0; i < TP_ICON_SIZE; i++)
        icon[i] = (uint8_t)(seed * 31 + i * 7);
}

/* Theme records `indices` (ascending, made-up contents), a catalogue up to `top` ("(removed)" where no record),
 * title version `version`, and AM's view of it: every content installed and owned. */
static void fixture_make(fixture *f, const uint16_t *indices, size_t n, uint16_t top, uint16_t version)
{
    memset(f, 0, sizeof *f);
    tp_content recs[TP_INDEX_MAX + 1];
    memset(recs, 0, sizeof recs);
    recs[0] = (tp_content){version ? 0x10000u + version : 0, 0, 0, 0xB000, {0}};
    memset(recs[0].sha256, 0xC0, 32);
    for (size_t k = 0; k < n; k++) {
        recs[k + 1] = (tp_content){indices[k], indices[k], 0x4000, 0x10000u + indices[k], {0}};
        memset(recs[k + 1].sha256, (int)indices[k], 32);
    }
    mem_sink m = {f->tmd, 0, sizeof f->tmd};
    tp_sink sink = {&m, mem_write};
    if (tp_tmd(TP_TITLE_ID, version, recs, n + 1, &sink) != TP_OK) {
        fprintf(stderr, "fixture TMD refused\n");
        exit(3);
    }
    f->tmd_size = m.size;
    f->archive_size = 8 + 0xC8 * ((size_t)top + 1);
    put32le(f->archive, 1);
    put32le(f->archive + 4, top);
    for (uint16_t i = 0; i <= top; i++) {
        uint8_t *e = f->archive + 8 + 0xC8 * (size_t)i;
        int theme = 0;
        for (size_t k = 0; k < n; k++)
            theme |= indices[k] == i;
        if (i == 0)
            snprintf(f->names[i], sizeof f->names[i], "Dummy");
        else if (theme)
            snprintf(f->names[i], sizeof f->names[i], "Theme %u", i);
        else
            snprintf(f->names[i], sizeof f->names[i], "(removed)");
        memcpy(e, f->names[i], strlen(f->names[i]));
        if (theme)
            snprintf((char *)e + 0x40, 0x80, "About theme %u", i);
        put32le(e + 0xC0, i);
        put32le(e + 0xC4, i != 0);
        icon_of(f->icons[i], theme ? i : 0);
    }
    f->am.ok = 1;
    f->am.exists = 1;
    f->am.version = version;
    f->am.count = n + 1;
    for (size_t k = 0; k <= n; k++) {
        f->am.list[k] = (tp_am_content){recs[k].index, recs[k].id, recs[k].size};
        f->am.usable[k] = 1;
    }
    f->am.pending = PENDING_NO;
}

static int fx_archive(void *ctx, uint8_t *buf, size_t cap, size_t *size)
{
    fixture *f = ctx;
    if (f->fail_archive || f->archive_size > cap)
        return -1;
    memcpy(buf, f->archive, f->archive_size);
    *size = f->archive_size;
    return 0;
}

static int fx_icon(void *ctx, uint16_t index, uint8_t icon[TP_ICON_SIZE])
{
    fixture *f = ctx;
    if (f->fail_icon == index)
        return -1;
    memcpy(icon, f->icons[index], TP_ICON_SIZE);
    return 0;
}

static store_record rec[2];

static void records(const fixture *f, int main_present, int staged_present)
{
    memset(rec, 0, sizeof rec);
    if (main_present) {
        rec[0].present = 1;
        rec[0].size = f->tmd_size;
        memcpy(rec[0].tmd, f->tmd, f->tmd_size);
    }
    if (staged_present) {
        rec[1].present = 1;
        rec[1].size = f->tmd_size;
        memcpy(rec[1].tmd, f->tmd, f->tmd_size);
    }
}

static uint16_t fx_mark;        /* the record's mark that assess() passes */

static int assess(pack_view *pv, fixture *f)
{
    catalogue_reader cat = {f, fx_archive, fx_icon};
    pack_free(pv);
    return pack_assess(pv, &f->am, rec, fx_mark, &cat);
}

static fixture fx, fx2;

static void test_assess(void)
{
    static const uint16_t idx[] = {1, 2, 4};
    pack_view pv = {0};
    fixture_make(&fx, idx, 3, 5, 1);
    records(&fx, 1, 0);
    CHECK_EQ(assess(&pv, &fx), PACK_OK);
    CHECK_EQ(pv.count, 3);
    CHECK_EQ(pv.usable, 3);
    CHECK_EQ(pv.top, 5);
    CHECK_EQ(pv.version, 1);
    CHECK_EQ(pv.from_staged, 0);
    CHECK(pv.tmd == rec[0].tmd);
    CHECK(!strcmp(pv.themes[2].name, "Theme 4") && !strcmp(pv.themes[2].description, "About theme 4"));
    CHECK(!memcmp(pv.themes[1].icon, fx.icons[2], TP_ICON_SIZE));

    /* AM first */
    fx.am.ok = 0;
    CHECK_EQ(assess(&pv, &fx), PACK_AM_ERROR);
    fx.am.ok = 1;
    fx.am.exists = 0;
    CHECK_EQ(assess(&pv, &fx), PACK_NOT_INSTALLED);
    fx.am.exists = 1;
    fx.am.pending = PENDING_YES;
    CHECK_EQ(assess(&pv, &fx), PACK_PENDING);
    fx.am.pending = PENDING_UNKNOWN;
    CHECK_EQ(assess(&pv, &fx), PACK_PENDING_UNKNOWN);
    fx.am.pending = PENDING_NO;

    /* the record: missing, unreadable, another title, damaged, stale; the staged one when it matches */
    records(&fx, 0, 0);
    CHECK_EQ(assess(&pv, &fx), PACK_NO_STORE);
    records(&fx, 1, 0);
    rec[0].result = FILES_ERROR;
    CHECK_EQ(assess(&pv, &fx), PACK_STORE_BAD);
    CHECK_EQ(pv.detail, TP_E_READ);
    records(&fx, 1, 0);
    rec[0].tmd[0x140 + 0x4C + 7] = 0x08;                /* variation 08: an official title */
    CHECK_EQ(assess(&pv, &fx), PACK_STORE_BAD);
    CHECK_EQ(pv.detail, TP_E_TITLE);
    records(&fx, 1, 0);
    rec[0].size -= 1;
    CHECK_EQ(assess(&pv, &fx), PACK_STORE_BAD);
    CHECK_EQ(pv.detail, TP_E_TMD);
    records(&fx, 1, 0);
    fx.am.version = 2;
    CHECK_EQ(assess(&pv, &fx), PACK_STALE);
    CHECK_EQ(pv.diff.what, TP_DIFF_VERSION);
    fx.am.version = 1;
    fx.am.list[2].size++;
    CHECK_EQ(assess(&pv, &fx), PACK_STALE);
    CHECK_EQ(pv.diff.what, TP_DIFF_SIZE);
    CHECK_EQ(pv.diff.index, 2);
    fx.am.list[2].size--;
    fx.am.count--;                                      /* AM no longer lists index 4 */
    CHECK_EQ(assess(&pv, &fx), PACK_STALE);
    CHECK_EQ(pv.diff.what, TP_DIFF_MISSING);
    fx.am.count++;

    /* installed.tmd stale, installed.tmd.new matches (power cut after the commit): the staged one counts */
    fixture_make(&fx2, idx, 2, 5, 0);
    records(&fx, 1, 1);
    memcpy(rec[0].tmd, fx2.tmd, fx2.tmd_size);
    rec[0].size = fx2.tmd_size;
    CHECK_EQ(assess(&pv, &fx), PACK_OK);
    CHECK_EQ(pv.from_staged, 1);
    CHECK(pv.tmd == rec[1].tmd);
    /* both stale: refused, the first difference named */
    memcpy(rec[1].tmd, fx2.tmd, fx2.tmd_size);
    rec[1].size = fx2.tmd_size;
    CHECK_EQ(assess(&pv, &fx), PACK_STALE);
    /* installed.tmd matches, a stale .new left by a failed install is ignored */
    records(&fx, 1, 1);
    memcpy(rec[1].tmd, fx2.tmd, fx2.tmd_size);
    rec[1].size = fx2.tmd_size;
    CHECK_EQ(assess(&pv, &fx), PACK_OK);
    CHECK_EQ(pv.from_staged, 0);
    /* only a staged one, damaged */
    records(&fx, 0, 1);
    rec[1].tmd[0x300] ^= 1;
    CHECK_EQ(assess(&pv, &fx), PACK_STORE_BAD);

    /* the catalogue */
    records(&fx, 1, 0);
    fx.am.usable[0] = 0;
    CHECK_EQ(assess(&pv, &fx), PACK_NO_CATALOGUE);
    fx.am.usable[0] = 1;
    fx.fail_archive = 1;
    CHECK_EQ(assess(&pv, &fx), PACK_CATALOGUE_BAD);
    CHECK_EQ(pv.detail, TP_E_READ);
    fx.fail_archive = 0;
    fx.fail_icon = 4;
    CHECK_EQ(assess(&pv, &fx), PACK_CATALOGUE_BAD);
    fx.fail_icon = 0;
    put32le(fx.archive + 8 + 0xC8 * 2 + 0xC0, 3);      /* entry 2 says it is 3 */
    CHECK_EQ(assess(&pv, &fx), PACK_CATALOGUE_BAD);
    CHECK_EQ(pv.detail, TP_E_CATALOGUE);
    put32le(fx.archive + 8 + 0xC8 * 2 + 0xC0, 2);
    fx.archive_size -= 0xC8;
    put32le(fx.archive + 4, 4);                         /* the catalogue ends at 4: fine */
    CHECK_EQ(assess(&pv, &fx), PACK_OK);
    CHECK_EQ(pv.top, 4);
    fx.archive_size -= 0xC8;
    put32le(fx.archive + 4, 3);                         /* ends at 3, the TMD has 4 */
    CHECK_EQ(assess(&pv, &fx), PACK_CATALOGUE_BAD);
    CHECK_EQ(pv.detail, TP_E_CATALOGUE);

    /* deleted in HOME: listed, not usable */
    fixture_make(&fx, idx, 3, 5, 1);
    records(&fx, 1, 0);
    fx.am.usable[1] = 0;
    CHECK_EQ(assess(&pv, &fx), PACK_OK);
    CHECK_EQ(pv.usable, 2);
    CHECK_EQ(pv.themes[0].usable, 0);

    /* full: 100 theme records, or index 349 handed out */
    static uint16_t many[TP_THEMES_MAX];
    for (int k = 0; k < TP_THEMES_MAX; k++)
        many[k] = (uint16_t)(k + 1);
    fixture_make(&fx, many, TP_THEMES_MAX - 1, TP_THEMES_MAX - 1, 3);
    records(&fx, 1, 0);
    CHECK_EQ(assess(&pv, &fx), PACK_OK);
    fixture_make(&fx, many, TP_THEMES_MAX, TP_THEMES_MAX, 3);
    records(&fx, 1, 0);
    CHECK_EQ(assess(&pv, &fx), PACK_FULL_THEMES);
    static const uint16_t last[] = {2, 349};
    fixture_make(&fx, last, 2, 349, 0);
    records(&fx, 1, 0);
    CHECK_EQ(assess(&pv, &fx), PACK_FULL_INDEX);
    static const uint16_t below[] = {2, 348};
    fixture_make(&fx, below, 2, 348, 0);
    records(&fx, 1, 0);
    CHECK_EQ(assess(&pv, &fx), PACK_OK);
    pack_free(&pv);
}

static void test_names(void)
{
    char out[0x41];
    pack_cut_name(out, "short");
    CHECK(!strcmp(out, "short"));
    char longer[100];
    memset(longer, 'a', 70);
    longer[70] = 0;
    pack_cut_name(out, longer);
    CHECK_EQ(strlen(out), 63);
    /* 62 ASCII bytes, then a 3-byte character across the cut: the character goes */
    memset(longer, 'b', 62);
    memcpy(longer + 62, "\xE2\x82\xAC" "x", 5);
    pack_cut_name(out, longer);
    CHECK_EQ(strlen(out), 62);
    /* 60 ASCII bytes, then the 3-byte character fits */
    memset(longer, 'c', 60);
    memcpy(longer + 60, "\xE2\x82\xAC" "yz", 6);
    pack_cut_name(out, longer);
    CHECK_EQ(strlen(out), 63);
    CHECK(!memcmp(out + 60, "\xE2\x82\xAC", 3));
}

/* ---------------------------------------------------------------- an importer that records and fails on cue */

enum { C_TITLE, C_TMD, C_CONTEXTS, C_BEGIN, C_WRITE, C_FINISH, C_COMMIT };

typedef struct fake {
    int calls, fail_at, cancels, commits;
    int kinds[256];
    uint8_t tmd[STORE_MAX];
    size_t tmd_size;
    uint16_t contexts[8];
    size_t context_count;
    int open;                   /* a content is open */
    uint16_t index;
    sha256_ctx sha;
    uint64_t size;
    struct { uint16_t index; uint64_t size; uint8_t sha256[32]; } done[4];
    int done_count;
    const char *dir;            /* file mode: write the contents there */
    FILE *f;
} fake;

static int fake_call(fake *k, int kind)
{
    if (k->calls < 256)
        k->kinds[k->calls] = kind;
    return k->calls++ == k->fail_at ? -1 : 0;
}

static int fake_title(void *ctx) { return fake_call(ctx, C_TITLE); }

static int fake_tmd(void *ctx, const uint8_t *tmd, size_t size)
{
    fake *k = ctx;
    if (fake_call(k, C_TMD))
        return -1;
    memcpy(k->tmd, tmd, size);
    k->tmd_size = size;
    if (k->dir) {
        char path[1024];
        snprintf(path, sizeof path, "%s/tmd.bin", k->dir);
        return files_write(path, tmd, size) ? -1 : 0;
    }
    return 0;
}

static int fake_contexts(void *ctx, const uint16_t *indices, size_t count)
{
    fake *k = ctx;
    if (fake_call(k, C_CONTEXTS))
        return -1;
    k->context_count = count;
    memcpy(k->contexts, indices, count * sizeof *indices);
    return 0;
}

static int fake_begin(void *ctx, uint16_t index)
{
    fake *k = ctx;
    if (fake_call(k, C_BEGIN))
        return -1;
    k->open = 1;
    k->index = index;
    k->size = 0;
    sha256_init(&k->sha);
    if (k->dir) {
        char path[1024];
        snprintf(path, sizeof path, "%s/%u.app", k->dir, index);
        if (!(k->f = fopen(path, "wb")))
            return -1;
    }
    return 0;
}

static int fake_write(void *ctx, const void *buf, size_t len)
{
    fake *k = ctx;
    if (fake_call(k, C_WRITE) || !k->open || len > 0x4000)
        return -1;
    sha256_update(&k->sha, buf, len);
    k->size += len;
    return k->f && fwrite(buf, 1, len, k->f) != len ? -1 : 0;
}

static int fake_finish(void *ctx)
{
    fake *k = ctx;
    int r = fake_call(k, C_FINISH);
    if (k->f && fclose(k->f))
        r = -1;
    k->f = NULL;
    k->open = 0;
    if (!r && k->done_count < 4) {
        k->done[k->done_count].index = k->index;
        k->done[k->done_count].size = k->size;
        sha256_final(&k->sha, k->done[k->done_count].sha256);
        k->done_count++;
    }
    return r;
}

static int fake_commit(void *ctx)
{
    fake *k = ctx;
    if (fake_call(k, C_COMMIT))
        return -1;
    k->commits++;
    return 0;
}

static void fake_cancel(void *ctx)
{
    fake *k = ctx;
    k->cancels++;
    if (k->f)
        fclose(k->f);
    k->f = NULL;
    k->open = 0;
}

static importer fake_importer(fake *k, int fail_at, const char *dir)
{
    memset(k, 0, sizeof *k);
    k->fail_at = fail_at;
    k->dir = dir;
    return (importer){k, fake_title, fake_tmd, fake_contexts, fake_begin, fake_write, fake_finish, fake_commit,
                      fake_cancel};
}

/* ---------------------------------------------------------------- plans and the install sequence */

static char tmp_dir[512];

static void reset_store(const uint8_t *tmd, size_t size)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/" STORE_FILE, tmp_dir);
    if (files_write(path, tmd, size))
        exit(3);
    snprintf(path, sizeof path, "%s/" STORE_STAGED, tmp_dir);
    remove(path);
    snprintf(path, sizeof path, "%s/" MARK_FILE, tmp_dir);
    remove(path);
    snprintf(path, sizeof path, "%s/" MARK_STAGED, tmp_dir);
    remove(path);
}

static void view_of_tmd(am_view *v, const uint8_t *tmd, size_t size)
{
    uint64_t title;
    uint16_t version;
    size_t count;
    memset(v, 0, sizeof *v);
    tp_tmd_read(tmd, size, &title, &version, &count);
    v->ok = v->exists = 1;
    v->version = version;
    v->count = count;
    for (size_t k = 0; k < count; k++) {
        tp_content c;
        tp_tmd_record(tmd, size, k, &c);
        v->list[k] = (tp_am_content){c.index, c.id, c.size};
        v->usable[k] = 1;
    }
    v->pending = PENDING_NO;
}

static void test_plan_and_install(const char *zip_bgm, const char *zip_plain)
{
    static const uint16_t idx[] = {1, 2, 4};
    pack_view pv = {0};
    fixture_make(&fx, idx, 3, 5, 1);
    records(&fx, 1, 0);
    CHECK_EQ(assess(&pv, &fx), PACK_OK);

    tp_source src;
    tp_theme theme;
    CHECK_EQ(files_open_source(zip_plain, &src, NULL, NULL), FILES_OK);
    CHECK_EQ(tp_theme_open(&theme, &src, "plain"), TP_OK);
    install_plan plan;
    CHECK_EQ(plan_install(&plan, &pv, &theme), PLAN_OK);
    CHECK_EQ(plan.index, 6);                            /* above the catalogue's 5, which was handed out */
    CHECK_EQ(plan.count, 4);
    CHECK_EQ(plan.top, 6);
    CHECK_EQ(plan.delta.version, 2);
    CHECK_EQ(plan.delta.content0_id, 0x10002);
    CHECK_EQ(plan.delta.count, 1);
    CHECK_EQ(plan.delta.kept, 3);
    tp_catalogue cat;
    plan_catalogue(&plan, &cat);
    uint8_t icon[TP_ICON_SIZE];
    CHECK_EQ(cat.icon(cat.icon_ctx, 6, icon), 0);
    CHECK(!memcmp(icon, theme.icon, TP_ICON_SIZE));
    CHECK_EQ(cat.icon(cat.icon_ctx, 2, icon), 0);
    CHECK(!memcmp(icon, fx.icons[2], TP_ICON_SIZE));
    CHECK(cat.icon(cat.icon_ctx, 3, icon) != 0);        /* a reserved index: the core draws it */

    /* the whole sequence */
    reset_store(fx.tmd, fx.tmd_size);
    fake k;
    importer imp = fake_importer(&k, -1, NULL);
    install_result res;
    CHECK_EQ(install_run(&plan, &theme, &imp, tmp_dir, NULL, &res), 0);
    CHECK(res.done && res.wrote && res.tmd_accepted && res.committed1 && res.record_ok);
    CHECK_EQ(res.failed, STEP_NONE);
    CHECK_EQ(k.commits, 2);
    CHECK_EQ(k.cancels, 0);
    CHECK_EQ(k.done_count, 2);
    CHECK_EQ(k.done[0].index, 0);
    CHECK_EQ(k.done[0].size, res.content0.size);
    CHECK(!memcmp(k.done[0].sha256, res.content0.sha256, 32));
    CHECK_EQ(k.done[1].index, 6);
    CHECK_EQ(k.done[1].size, res.theme.size);
    CHECK(!memcmp(k.done[1].sha256, res.theme.sha256, 32));
    CHECK_EQ(k.context_count, 1);
    CHECK_EQ(k.contexts[0], 6);
    /* the call order of T3: title, TMD, content 0, commit; title, context, content, commit */
    static const int order[] = {C_TITLE, C_TMD, C_BEGIN};
    for (int i = 0; i < 3; i++)
        CHECK_EQ(k.kinds[i], order[i]);
    int total = k.calls, first_commit = -1;
    for (int i = 0; i < total; i++)
        if (k.kinds[i] == C_COMMIT && first_commit < 0)
            first_commit = i;
    CHECK_EQ(k.kinds[first_commit + 1], C_TITLE);
    CHECK_EQ(k.kinds[first_commit + 2], C_CONTEXTS);
    CHECK_EQ(k.kinds[total - 1], C_COMMIT);
    /* the TMD: installed records unchanged, the new content 0 and theme, version + 1 */
    uint64_t title;
    uint16_t version;
    size_t count;
    CHECK_EQ(tp_tmd_read(k.tmd, k.tmd_size, &title, &version, &count), TP_OK);
    CHECK_EQ(version, 2);
    CHECK_EQ(count, 5);
    for (size_t r = 1; r < 4; r++) {
        tp_content a, b;
        tp_tmd_record(fx.tmd, fx.tmd_size, r, &a);
        tp_tmd_record(k.tmd, k.tmd_size, r, &b);
        CHECK(!memcmp(&a, &b, sizeof a));
    }
    char path[1024], staged[1024];
    snprintf(path, sizeof path, "%s/" STORE_FILE, tmp_dir);
    snprintf(staged, sizeof staged, "%s/" STORE_STAGED, tmp_dir);
    CHECK(same_file(path, k.tmd, k.tmd_size));
    CHECK(!exists(staged));
    uint8_t new_tmd[STORE_MAX];
    size_t new_size = k.tmd_size;
    memcpy(new_tmd, k.tmd, new_size);

    /* the judgement on AM's view afterwards */
    am_view after;
    judgement j;
    view_of_tmd(&after, new_tmd, new_size);
    install_judge(&fx.am, &after, &plan, &res, &j);
    CHECK_EQ(j.outcome, OUT_DONE);
    CHECK(j.seen && j.new_installed && !j.lost);

    /* every call failing in turn */
    am_view mid;
    view_of_tmd(&mid, new_tmd, new_size);
    mid.usable[4] = 0;                                  /* phase 1 committed, the theme not imported */
    for (int n = 0; n < total; n++) {
        reset_store(fx.tmd, fx.tmd_size);
        imp = fake_importer(&k, n, NULL);
        CHECK_EQ(install_run(&plan, &theme, &imp, tmp_dir, NULL, &res), -1);
        CHECK(!res.done);
        CHECK_EQ(k.cancels, 1);
        CHECK_EQ(res.wrote, n > 0);
        CHECK_EQ(res.tmd_accepted, n > 1);
        CHECK_EQ(res.committed1, n > first_commit);
        int kind = k.kinds[n], want;
        if (n <= first_commit)
            want = kind == C_TITLE ? STEP_TITLE1 : kind == C_TMD ? STEP_TMD : kind == C_COMMIT ? STEP_COMMIT1
                                                                           : STEP_CONTENT0;
        else
            want = kind == C_TITLE ? STEP_TITLE2 : kind == C_CONTEXTS ? STEP_CONTEXT : kind == C_COMMIT
                                                                                    ? STEP_COMMIT2 : STEP_CONTENT;
        CHECK_EQ(res.failed, want);
        if (res.committed1) {
            CHECK(same_file(path, new_tmd, new_size) && !exists(staged));
            install_judge(&fx.am, &mid, &plan, &res, &j);
            CHECK_EQ(j.outcome, OUT_PHASE2_FAILED_KEPT);
        } else {
            CHECK(same_file(path, fx.tmd, fx.tmd_size) && same_file(staged, new_tmd, new_size));
            install_judge(&fx.am, &fx.am, &plan, &res, &j);
            CHECK_EQ(j.outcome, n == 0 ? OUT_NOTHING_WRITTEN : OUT_CANCELLED_KEPT);
        }
    }

    /* the record cannot be staged: nothing is asked of AM */
    imp = fake_importer(&k, -1, NULL);
    snprintf(path, sizeof path, "%s/missing-dir", tmp_dir);
    CHECK_EQ(install_run(&plan, &theme, &imp, path, NULL, &res), -1);
    CHECK_EQ(res.failed, STEP_STAGE);
    CHECK_EQ(k.calls, 0);
    install_judge(&fx.am, &fx.am, &plan, &res, &j);
    CHECK_EQ(j.outcome, OUT_NOTHING_WRITTEN);

    tp_theme_close(&theme);
    files_close_source(&src);

    /* the same theme again: refused while installed, a new index once HOME deleted it */
    install_plan again;
    pack_view pv2 = {0};
    CHECK_EQ(files_open_source(zip_bgm, &src, NULL, NULL), FILES_OK);
    CHECK_EQ(tp_theme_open(&theme, &src, "bgm"), TP_OK);
    fixture_make(&fx, idx, 3, 5, 1);
    snprintf(fx.names[2], sizeof fx.names[2], "%s", theme.name);
    memset(fx.archive + 8 + 0xC8 * 2, 0, 0x40);
    pack_cut_name((char *)fx.archive + 8 + 0xC8 * 2, theme.name);
    memcpy(fx.icons[2], theme.icon, TP_ICON_SIZE);
    records(&fx, 1, 0);
    CHECK_EQ(assess(&pv2, &fx), PACK_OK);
    CHECK_EQ(plan_install(&again, &pv2, &theme), PLAN_DUPLICATE);
    CHECK_EQ(again.duplicate, 2);
    int usable = 0;
    CHECK_EQ(pack_find(&pv2, theme.name, theme.icon, &usable), 2);
    CHECK_EQ(usable, 1);
    fx.am.usable[2] = 0;                                /* deleted in HOME */
    CHECK_EQ(assess(&pv2, &fx), PACK_OK);
    CHECK_EQ(pack_find(&pv2, theme.name, theme.icon, &usable), 2);
    CHECK_EQ(usable, 0);
    CHECK_EQ(plan_install(&again, &pv2, &theme), PLAN_OK);
    CHECK_EQ(again.index, 6);
    uint8_t other[TP_ICON_SIZE];
    memcpy(other, theme.icon, TP_ICON_SIZE);
    other[0] ^= 1;
    CHECK_EQ(pack_find(&pv2, theme.name, other, &usable), 0);
    CHECK_EQ(pack_find(&pv2, "another name", theme.icon, &usable), 0);

    /* full packs refuse in the plan too (pack_assess refuses them first; a view made up here) */
    static pack_theme many[TP_THEMES_MAX];
    pack_view full;
    memset(&full, 0, sizeof full);
    for (int i = 0; i < TP_THEMES_MAX; i++) {
        many[i].index = (uint16_t)(i + 1);
        snprintf(many[i].name, sizeof many[i].name, "Theme %d", i + 1);
    }
    full.state = PACK_OK;
    full.themes = many;
    full.count = TP_THEMES_MAX;
    full.top = TP_THEMES_MAX;
    CHECK_EQ(plan_install(&again, &full, &theme), PLAN_FULL_THEMES);
    full.count = 3;
    full.top = TP_INDEX_MAX;
    CHECK_EQ(plan_install(&again, &full, &theme), PLAN_FULL_INDEX);
    pv2.state = PACK_STALE;
    CHECK_EQ(plan_install(&again, &pv2, &theme), PLAN_CORE);
    imp = fake_importer(&k, -1, NULL);
    CHECK_EQ(install_run(&again, &theme, &imp, tmp_dir, NULL, &res), -1);
    CHECK_EQ(res.failed, STEP_MEASURE);
    CHECK_EQ(k.calls, 0);
    tp_theme_close(&theme);
    files_close_source(&src);
    pack_free(&pv2);
    pack_free(&pv);
}

/* ---------------------------------------------------------------- the mark, and the reviewer's two cases */

static void write_in_tmp(const char *name, const char *text)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", tmp_dir, name);
    if (files_write(path, (const uint8_t *)text, strlen(text)))
        exit(3);
}

static void test_mark(void)
{
    static const struct { const char *text; uint16_t want; } cases[] = {
        {"5\n", 5}, {"5", 5}, {"349\n", 349}, {"007\n", 7}, {"0\n", 0}, {"350\n", 0}, {"12x\n", 0},
        {"\n", 0}, {"", 0}, {"5\n\n", 0}, {" 5\n", 0}, {"99999999999\n", 0},
    };
    reset_store((const uint8_t *)"", 0);
    CHECK_EQ(store_mark(tmp_dir), 0);
    for (size_t k = 0; k < sizeof cases / sizeof *cases; k++) {
        write_in_tmp(MARK_FILE, cases[k].text);
        CHECK_EQ(store_mark(tmp_dir), cases[k].want);
    }
    write_in_tmp(MARK_FILE, "5\n");
    write_in_tmp(MARK_STAGED, "9\n");
    CHECK_EQ(store_mark(tmp_dir), 9);                   /* a raise cut off before its rename */
    reset_store((const uint8_t *)"", 0);
    CHECK_EQ(store_mark_raise(tmp_dir, 4), 0);
    CHECK_EQ(store_mark(tmp_dir), 4);
    char path[1024], text[8] = {0};
    size_t n = 0;
    snprintf(path, sizeof path, "%s/" MARK_FILE, tmp_dir);
    CHECK_EQ(files_read(path, (uint8_t *)text, sizeof text - 1, &n), FILES_OK);
    CHECK(!strcmp(text, "4\n"));
    snprintf(path, sizeof path, "%s/" MARK_STAGED, tmp_dir);
    CHECK(!exists(path));
    CHECK_EQ(store_mark_raise(tmp_dir, 3), 0);          /* never lowered */
    CHECK_EQ(store_mark(tmp_dir), 4);
    CHECK_EQ(store_mark_raise(tmp_dir, 349), 0);
    CHECK_EQ(store_mark(tmp_dir), 349);
    CHECK(store_mark_raise(tmp_dir, 350) != 0);
    CHECK(store_mark_raise(tmp_dir, 0) != 0);
    snprintf(path, sizeof path, "%s/missing-dir", tmp_dir);
    CHECK(store_mark_raise(path, 5) != 0);
}

static void test_reviewer_cases(const char *zip_plain)
{
    static const uint16_t three[] = {1, 2, 3};
    pack_view pv = {0};
    install_plan plan;
    tp_source src;
    tp_theme theme;
    CHECK_EQ(files_open_source(zip_plain, &src, NULL, NULL), FILES_OK);
    CHECK_EQ(tp_theme_open(&theme, &src, "plain"), TP_OK);

    /* 1. After a recovery with a pack of three themes, while indices up to 5 were handed out: the mark keeps 4
     *    and 5 from being handed out again. Without the mark only the catalogue and the TMD count. */
    fixture_make(&fx, three, 3, 3, 0);
    records(&fx, 1, 0);
    fx_mark = 5;
    CHECK_EQ(assess(&pv, &fx), PACK_OK);
    CHECK_EQ(pv.top, 5);
    CHECK_EQ(plan_install(&plan, &pv, &theme), PLAN_OK);
    CHECK_EQ(plan.index, 6);
    CHECK_EQ(plan.top, 6);
    fx_mark = 2;                                        /* a mark below the catalogue changes nothing */
    CHECK_EQ(assess(&pv, &fx), PACK_OK);
    CHECK_EQ(plan_install(&plan, &pv, &theme), PLAN_OK);
    CHECK_EQ(plan.index, 4);
    fx_mark = 0;
    CHECK_EQ(assess(&pv, &fx), PACK_OK);
    CHECK_EQ(plan_install(&plan, &pv, &theme), PLAN_OK);
    CHECK_EQ(plan.index, 4);
    fx_mark = TP_INDEX_MAX;
    CHECK_EQ(assess(&pv, &fx), PACK_FULL_INDEX);
    fx_mark = 0;

    /* 2. Only installed.tmd.new matches AM (a power cut after a commit). An install that fails before its first
     *    commit must leave a record that still matches: the .new file is promoted before the next is staged. */
    fixture_make(&fx2, three, 3, 3, 1);                 /* what AM reports: version 1 */
    fixture_make(&fx, three, 3, 3, 0);                  /* the stale installed.tmd: version 0 */
    reset_store(fx.tmd, fx.tmd_size);
    char path[1024];
    snprintf(path, sizeof path, "%s/" STORE_STAGED, tmp_dir);
    CHECK_EQ(files_write(path, fx2.tmd, fx2.tmd_size), FILES_OK);
    store_read(tmp_dir, rec);
    CHECK_EQ(assess(&pv, &fx2), PACK_OK);
    CHECK_EQ(pv.from_staged, 1);
    CHECK_EQ(plan_install(&plan, &pv, &theme), PLAN_OK);
    CHECK_EQ(plan.index, 4);
    for (int n = 0; n < 3; n++) {                       /* title, TMD, content 0 begin */
        fake k;
        importer imp = fake_importer(&k, n, NULL);
        install_result res;
        CHECK_EQ(install_run(&plan, &theme, &imp, tmp_dir, NULL, &res), -1);
        CHECK(!res.committed1);
        pack_view after = {0};
        store_read(tmp_dir, rec);
        fx_mark = store_mark(tmp_dir);
        CHECK_EQ(assess(&after, &fx2), PACK_OK);        /* installed.tmd is the promoted record */
        fx_mark = 0;
        CHECK_EQ(after.from_staged, 0);
        CHECK_EQ(after.top, 4);                         /* the mark took the handed-out index */
        pack_free(&after);
    }
    /* 3. A whole install leaves the mark at its index. */
    reset_store(fx2.tmd, fx2.tmd_size);
    store_read(tmp_dir, rec);
    CHECK_EQ(assess(&pv, &fx2), PACK_OK);
    CHECK_EQ(plan_install(&plan, &pv, &theme), PLAN_OK);
    fake k;
    importer imp = fake_importer(&k, -1, NULL);
    install_result res;
    CHECK_EQ(install_run(&plan, &theme, &imp, tmp_dir, NULL, &res), 0);
    CHECK_EQ(store_mark(tmp_dir), plan.index);
    tp_theme_close(&theme);
    files_close_source(&src);
    pack_free(&pv);
}

/* ---------------------------------------------------------------- deleting a theme */

typedef struct fake_deleter {
    int calls;
    uint64_t title;
    uint16_t index;
    uint32_t answer;            /* what AM answers */
    am_view *am;                /* with really: the view that loses the content */
    int really;
} fake_deleter;

static uint32_t fake_delete(void *ctx, uint64_t title_id, uint16_t index)
{
    fake_deleter *d = ctx;
    d->calls++;
    d->title = title_id;
    d->index = index;
    for (size_t j = 0; d->really && j < d->am->count; j++)
        if (d->am->list[j].index == index)
            d->am->usable[j] = 0;
    return d->answer;
}

static void put_entry(uint8_t *save, size_t at, uint32_t index, uint8_t variation, uint8_t type)
{
    put32le(save + at, index);
    save[at + 4] = variation;
    save[at + 5] = type;
}

static int usable_of(const am_view *v, uint16_t index)
{
    for (size_t j = 0; j < v->count; j++)
        if (v->list[j].index == index)
            return v->usable[j];
    return -1;
}

/* The installed designs (designs.c), on the console's state after 0.8.2's run (install.log of 2026-11-13): title
 * version 6, theme records 1 to 9, theme 5 deleted (AM still lists its content, not installed and owned, so AM's
 * count is 10 with 8 themes installed), the mark at 9. */
static void test_designs(void)
{
    static const uint16_t idx[] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
    static uint8_t music[PACK_CONTENTS];
    static design_list d;
    pack_view pv = {0};
    fixture_make(&fx, idx, 9, 9, 6);
    fx.am.usable[5] = 0;                    /* list entry 5: content 5 */
    records(&fx, 1, 0);
    fx_mark = 9;
    CHECK_EQ(assess(&pv, &fx), PACK_OK);
    CHECK(designs_readable(&pv));
    memset(music, MUSIC_UNKNOWN, sizeof music);
    music[1] = MUSIC_YES;
    music[2] = MUSIC_NONE;
    designs_list(&d, &pv, music);
    CHECK_EQ(fx.am.count, 10);              /* AM's count is the TMD's, deleted contents too */
    CHECK_EQ(d.count, 8);                   /* what HOME lists: not the deleted one */
    static const uint16_t want[] = {1, 2, 3, 4, 6, 7, 8, 9};
    for (size_t i = 0; i < d.count && i < 8; i++) {
        CHECK_EQ(d.rows[i].index, want[i]);  /* index order: the order they were added */
        CHECK(d.rows[i].theme && d.rows[i].theme->index == want[i] && d.rows[i].theme->usable);
        CHECK_EQ(d.rows[i].file[0], 0);
    }
    CHECK(d.rows[0].music == MUSIC_YES && d.rows[1].music == MUSIC_NONE && d.rows[2].music == MUSIC_UNKNOWN);
    CHECK(!strcmp(d.rows[3].theme->name, "Theme 4") && !strcmp(d.rows[4].theme->name, "Theme 6"));
    /* the room: 100 records (the deleted one keeps its record) and indices never handed out twice (up to 9 now) */
    CHECK_EQ(designs_room(&pv), TP_THEMES_MAX - 9);
    CHECK_EQ(designs_room_low(&pv), 0);     /* 0.9.2: plenty left, nothing is said */

    /* the files in /Themes: each design gets the first entry the folder view marks as it, installed; a file of the
     * deleted theme and a folder do not count */
    static scan_item items[5];
    memset(items, 0, sizeof items);
    snprintf(items[0].file, sizeof items[0].file, "Themes-folder");
    items[0].folder = 1;
    items[0].in_pack = 2;
    items[0].in_pack_usable = 1;
    snprintf(items[1].file, sizeof items[1].file, "ben10-old.zip");
    items[1].in_pack = 5;
    snprintf(items[2].file, sizeof items[2].file, "two.zip");
    items[2].in_pack = 2;
    items[2].in_pack_usable = 1;
    snprintf(items[3].file, sizeof items[3].file, "nine.zip");
    items[3].in_pack = 9;
    items[3].in_pack_usable = 1;
    snprintf(items[4].file, sizeof items[4].file, "nine-again.zip");
    items[4].in_pack = 9;
    items[4].in_pack_usable = 1;
    scan_list folder = {items, 5, 1, 0, 0, 0};
    designs_files(&d, &folder);
    CHECK(!strcmp(d.rows[1].file, "two.zip") && !strcmp(d.rows[7].file, "nine.zip"));
    CHECK(!d.rows[0].file[0] && !d.rows[3].file[0] && !d.rows[4].file[0]);
    scan_list none = {NULL, 0, 0, 0, 0, 0};
    designs_files(&d, &none);
    CHECK(!d.rows[1].file[0] && !d.rows[7].file[0]);

    /* a delete from the list: the row's index is planned as on the check screen; HOME's refusals and AM's view */
    static uint8_t save[HOME_SAVE_SIZE];
    memset(save, 0, sizeof save);
    put_entry(save, HOME_THEME_AT, 7, HOME_PACK_VARIATION, HOME_TYPE_DLC);
    put_entry(save, HOME_SHUFFLE_AT + 8 * 2, 3, HOME_PACK_VARIATION, HOME_TYPE_DLC);
    home_state home;
    home_parse(&home, save, sizeof save);
    deletion_plan p;
    CHECK_EQ(deletion_plan_make(&p, &pv, &fx.am, &home, TP_TITLE_ID, d.rows[7].index), DEL_OK);
    CHECK(p.index == 9 && p.theme == d.rows[7].theme);
    CHECK_EQ(deletion_plan_make(&p, &pv, &fx.am, &home, TP_TITLE_ID, d.rows[5].index), DEL_ACTIVE);
    CHECK_EQ(deletion_plan_make(&p, &pv, &fx.am, &home, TP_TITLE_ID, d.rows[2].index), DEL_SHUFFLE);
    CHECK_EQ(deletion_plan_make(&p, &pv, &fx.am, &home, TP_TITLE_ID, 5), DEL_NOT_INSTALLED);
    home_state unread = {0};
    CHECK_EQ(deletion_plan_make(&p, &pv, &fx.am, &unread, TP_TITLE_ID, d.rows[7].index), DEL_HOME_UNKNOWN);

    /* after a delete AM lists theme 9 as not installed: the list read again has 7 rows, the room stays */
    fx.am.usable[9] = 0;
    CHECK_EQ(assess(&pv, &fx), PACK_OK);
    designs_list(&d, &pv, music);
    CHECK_EQ(d.count, 7);
    CHECK_EQ(d.rows[d.count - 1].index, 8);
    CHECK_EQ(designs_room(&pv), TP_THEMES_MAX - 9);
    fx.am.usable[9] = 1;

    /* a full pack is read whole: listed, and its themes can be deleted from the list */
    fx_mark = TP_INDEX_MAX;
    CHECK_EQ(assess(&pv, &fx), PACK_FULL_INDEX);
    CHECK(designs_readable(&pv));
    designs_list(&d, &pv, music);
    CHECK_EQ(d.count, 8);
    CHECK_EQ(designs_room(&pv), 0);
    CHECK_EQ(designs_room_low(&pv), 0);     /* a full pack: its state says so, no warning beside it */
    CHECK_EQ(deletion_plan_make(&p, &pv, &fx.am, &home, TP_TITLE_ID, d.rows[0].index), DEL_OK);
    /* 0.9.2: the room is said only when fewer than DESIGNS_ROOM_LOW are left, by indices ... */
    for (unsigned left = 1; left <= DESIGNS_ROOM_LOW + 1; left++) {
        fx_mark = (uint16_t)(TP_INDEX_MAX - left);
        CHECK_EQ(assess(&pv, &fx), PACK_OK);
        CHECK_EQ(designs_room(&pv), left);
        CHECK_EQ(designs_room_low(&pv), left < DESIGNS_ROOM_LOW ? left : 0);
    }
    fx_mark = 0;
    static uint16_t hundred[TP_THEMES_MAX];
    for (int k = 0; k < TP_THEMES_MAX; k++)
        hundred[k] = (uint16_t)(k + 1);
    fixture_make(&fx2, hundred, TP_THEMES_MAX, TP_THEMES_MAX, 3);
    records(&fx2, 1, 0);
    CHECK_EQ(assess(&pv, &fx2), PACK_FULL_THEMES);
    designs_list(&d, &pv, music);
    CHECK_EQ(d.count, TP_THEMES_MAX);
    CHECK_EQ(designs_room(&pv), 0);
    CHECK_EQ(designs_room_low(&pv), 0);
    CHECK_EQ(deletion_plan_make(&p, &pv, &fx2.am, &home, TP_TITLE_ID, d.rows[49].index), DEL_OK);
    /* ... and by records: 96 of 100 leave 4 (said), 95 leave 5 (not said) */
    for (unsigned left = DESIGNS_ROOM_LOW - 1; left <= DESIGNS_ROOM_LOW; left++) {
        fixture_make(&fx2, hundred, TP_THEMES_MAX - left, (uint16_t)(TP_THEMES_MAX - left), 3);
        records(&fx2, 1, 0);
        CHECK_EQ(assess(&pv, &fx2), PACK_OK);
        CHECK_EQ(designs_room(&pv), left);
        CHECK_EQ(designs_room_low(&pv), left < DESIGNS_ROOM_LOW ? left : 0);
    }

    /* AM and the record disagree, or the pack was not read whole: no list, no room (the screen shows why) */
    fixture_make(&fx, idx, 9, 9, 6);
    fx.am.usable[5] = 0;
    records(&fx, 1, 0);
    fx.am.version = 7;                      /* AM reports another title version than the record */
    CHECK_EQ(assess(&pv, &fx), PACK_STALE);
    CHECK(!designs_readable(&pv));
    designs_list(&d, &pv, music);
    CHECK_EQ(d.count, 0);
    CHECK_EQ(designs_room(&pv), 0);
    CHECK_EQ(designs_room_low(&pv), 0);
    fx.am.version = 6;
    fx.am.list[9].id ^= 1;                  /* a content AM lists that the record does not have */
    CHECK_EQ(assess(&pv, &fx), PACK_STALE);
    designs_list(&d, &pv, music);
    CHECK_EQ(d.count, 0);
    fx.am.list[9].id ^= 1;
    fx.am.pending = PENDING_YES;
    CHECK_EQ(assess(&pv, &fx), PACK_PENDING);
    designs_list(&d, &pv, music);
    CHECK_EQ(d.count, 0);
    fx.am.pending = PENDING_NO;
    records(&fx, 0, 0);
    CHECK_EQ(assess(&pv, &fx), PACK_NO_STORE);
    designs_list(&d, &pv, music);
    CHECK_EQ(d.count, 0);
    fx.fail_icon = 4;                       /* the catalogue lacks a record's icon */
    records(&fx, 1, 0);
    CHECK_EQ(assess(&pv, &fx), PACK_CATALOGUE_BAD);
    CHECK(!designs_readable(&pv));
    fx.fail_icon = 0;
    pack_free(&pv);
}

static void test_deletion(const char *zip_plain)
{
    static const uint16_t idx[] = {1, 2, 4};
    pack_view pv = {0};
    fixture_make(&fx, idx, 3, 5, 1);
    records(&fx, 1, 0);
    CHECK_EQ(assess(&pv, &fx), PACK_OK);

    /* HOME's data: theme 1 of the pack in use; the shuffle holds theme 4 of the pack and theme 2 of the official
     * variation 00, which is not the pack's theme 2 */
    static uint8_t save[HOME_SAVE_SIZE];
    memset(save, 0x5A, sizeof save);
    put_entry(save, HOME_THEME_AT, 1, HOME_PACK_VARIATION, HOME_TYPE_DLC);
    for (int i = 0; i < HOME_SHUFFLE; i++)
        put_entry(save, HOME_SHUFFLE_AT + 8 * (size_t)i, 0, 0, 0);
    put_entry(save, HOME_SHUFFLE_AT + 8 * 3, 4, HOME_PACK_VARIATION, HOME_TYPE_DLC);
    put_entry(save, HOME_SHUFFLE_AT + 8 * 7, 2, 0, HOME_TYPE_DLC);
    save[HOME_SHUFFLE_ON_AT] = 0;
    home_state home;
    home_parse(&home, save, sizeof save);
    CHECK(home.ok && !home.shuffle_on);
    CHECK(home.current.index == 1 && home.current.variation == 9 && home.current.type == 2);
    CHECK(home.shuffle[3].index == 4 && home.shuffle[7].variation == 0);
    home_state other;
    home_parse(&other, save, HOME_SAVE_OLD_SIZE);
    CHECK(other.ok);
    home_parse(&other, save, HOME_SAVE_SIZE - 1);
    CHECK(!other.ok);
    home_parse(&other, NULL, HOME_SAVE_SIZE);
    CHECK(!other.ok);

    /* the content a delete targets: the theme's own index, once, of this title only */
    deletion_plan p;
    CHECK_EQ(deletion_plan_make(&p, &pv, &fx.am, &home, TP_TITLE_ID, 2), DEL_OK);
    CHECK_EQ(p.index, 2);
    CHECK(p.theme && p.theme->index == 2 && !strcmp(p.theme->name, "Theme 2"));
    fake_deleter fd = {0};
    deleter d = {&fd, fake_delete};
    uint32_t error = 7;
    CHECK_EQ(deletion_run(&p, &d, &error), 0);
    CHECK(fd.calls == 1 && fd.title == TP_TITLE_ID && fd.index == 2 && error == 0);
    fd.answer = 0xD8A083FAu;
    CHECK_EQ(deletion_run(&p, &d, &error), 1);
    CHECK_EQ(error, 0xD8A083FAu);
    fd.answer = 0;

    /* the refusals: nothing reaches the deleter */
    struct { uint64_t title; uint16_t index; int want; } refused[] = {
        {TP_TITLE_ID, 0, DEL_CATALOGUE},
        {TP_TITLE_ID, 350, DEL_INDEX},
        {TP_TITLE_ID, 0xFFFF, DEL_INDEX},
        {TP_TITLE_ID, 3, DEL_NOT_IN_PACK},          /* handed out once, not in the record */
        {TP_TITLE_ID, 5, DEL_NOT_IN_PACK},
        {TP_TITLE_ID, 1, DEL_ACTIVE},
        {TP_TITLE_ID, 4, DEL_SHUFFLE},
        {0x0004008C00009800ull, 2, DEL_TITLE},
        {0x0004008C00009801ull, 2, DEL_TITLE},
        {0x0004008C00009802ull, 2, DEL_TITLE},
        {0x0004008C00009803ull, 2, DEL_TITLE},
        {0x0004008C00009808ull, 2, DEL_TITLE},
        {0x0004008C0000980Aull, 2, DEL_TITLE},
        {0x0004000000000000ull | (TP_TITLE_ID & 0xFFFFFFFFull), 2, DEL_TITLE},
        {0x0004008C00009809ull ^ 0x100000000ull, 0, DEL_TITLE},
    };
    for (size_t k = 0; k < sizeof refused / sizeof *refused; k++) {
        CHECK_EQ(deletion_plan_make(&p, &pv, &fx.am, &home, refused[k].title, refused[k].index), refused[k].want);
        fd.calls = 0;
        CHECK_EQ(deletion_run(&p, &d, &error), -1);
        CHECK_EQ(fd.calls, 0);
    }
    /* a plan that says DEL_OK for content 0 (never made by deletion_plan_make) is still refused */
    deletion_plan forged = {DEL_OK, 0, NULL};
    fd.calls = 0;
    CHECK_EQ(deletion_run(&forged, &d, &error), -1);
    forged.index = 350;
    CHECK_EQ(deletion_run(&forged, &d, &error), -1);
    CHECK_EQ(fd.calls, 0);

    /* HOME's checks as HOME compares entries: type 2 with the pack's variation and the index */
    home_state h2 = home;
    h2.shuffle_on = 1;
    CHECK_EQ(deletion_plan_make(&p, &pv, &fx.am, &h2, TP_TITLE_ID, 4), DEL_SHUFFLE);
    h2 = home;
    h2.current = (home_entry){2, 0, HOME_TYPE_DLC};     /* theme 2 of variation 00 in use */
    CHECK_EQ(deletion_plan_make(&p, &pv, &fx.am, &h2, TP_TITLE_ID, 2), DEL_OK);
    h2.current = (home_entry){2, HOME_PACK_VARIATION, 3};   /* a cache theme with that index */
    CHECK_EQ(deletion_plan_make(&p, &pv, &fx.am, &h2, TP_TITLE_ID, 2), DEL_OK);
    h2.current = (home_entry){2, HOME_PACK_VARIATION, HOME_TYPE_DLC};
    CHECK_EQ(deletion_plan_make(&p, &pv, &fx.am, &h2, TP_TITLE_ID, 2), DEL_ACTIVE);
    h2 = home;
    for (int i = 0; i < HOME_SHUFFLE; i++) {
        h2.shuffle[i] = (home_entry){2, HOME_PACK_VARIATION, HOME_TYPE_DLC};
        CHECK_EQ(deletion_plan_make(&p, &pv, &fx.am, &h2, TP_TITLE_ID, 2), DEL_SHUFFLE);
        h2.shuffle[i] = home.shuffle[i];
    }
    /* unknown states */
    h2 = home;
    h2.ok = 0;
    CHECK_EQ(deletion_plan_make(&p, &pv, &fx.am, &h2, TP_TITLE_ID, 2), DEL_HOME_UNKNOWN);
    am_view v = fx.am;
    v.ok = 0;
    CHECK_EQ(deletion_plan_make(&p, &pv, &v, &home, TP_TITLE_ID, 2), DEL_PACK);
    v = fx.am;
    v.version = 2;                                      /* the pack changed since it was read */
    CHECK_EQ(deletion_plan_make(&p, &pv, &v, &home, TP_TITLE_ID, 2), DEL_PACK);
    v = fx.am;
    v.pending = PENDING_YES;
    CHECK_EQ(deletion_plan_make(&p, &pv, &v, &home, TP_TITLE_ID, 2), DEL_PENDING);
    v.pending = PENDING_UNKNOWN;
    CHECK_EQ(deletion_plan_make(&p, &pv, &v, &home, TP_TITLE_ID, 2), DEL_PENDING);
    v = fx.am;
    v.usable[2] = 0;                                    /* index 2: deleted in HOME already */
    CHECK_EQ(deletion_plan_make(&p, &pv, &v, &home, TP_TITLE_ID, 2), DEL_NOT_INSTALLED);
    for (int st = PACK_AM_ERROR; st <= PACK_NOMEM; st++) {
        int was = pv.state;
        pv.state = st;
        int want = st == PACK_FULL_THEMES || st == PACK_FULL_INDEX ? DEL_OK : DEL_PACK;
        CHECK_EQ(deletion_plan_make(&p, &pv, &fx.am, &home, TP_TITLE_ID, 2), want);
        pv.state = was;
    }

    /* the judgement, from AM's view afterwards */
    CHECK_EQ(deletion_plan_make(&p, &pv, &fx.am, &home, TP_TITLE_ID, 2), DEL_OK);
    deletion_judgement j;
    am_view after = fx.am;
    after.usable[2] = 0;
    deletion_judge(&fx.am, &after, &pv, &p, &j);
    CHECK(j.outcome == DEL_DONE && j.lost == 0 && j.record_ok);
    deletion_judge(&fx.am, &fx.am, &pv, &p, &j);
    CHECK_EQ(j.outcome, DEL_NOT_DONE);
    after.usable[1] = 0;                                /* theme 1 went too */
    deletion_judge(&fx.am, &after, &pv, &p, &j);
    CHECK(j.outcome == DEL_LOST && j.lost == 1);
    after = fx.am;
    after.usable[0] = 0;                                /* the catalogue went */
    deletion_judge(&fx.am, &after, &pv, &p, &j);
    CHECK(j.outcome == DEL_LOST && j.lost == 1);
    after.ok = 0;
    deletion_judge(&fx.am, &after, &pv, &p, &j);
    CHECK_EQ(j.outcome, DEL_UNKNOWN);
    after = fx.am;                                      /* AM dropped the record instead of marking it */
    after.list[2] = after.list[3];
    after.usable[2] = after.usable[3];
    after.count--;
    deletion_judge(&fx.am, &after, &pv, &p, &j);
    CHECK(j.outcome == DEL_DONE && !j.record_ok);

    /* the record: a delete writes nothing; afterwards the pack reads as before with theme 2 deleted, and an install
     * gets a new index and carries record 2 on */
    reset_store(fx.tmd, fx.tmd_size);
    CHECK_EQ(store_mark_raise(tmp_dir, 5), 0);
    char tmd_path[1024], mark_path[1024], staged[1024];
    snprintf(tmd_path, sizeof tmd_path, "%s/" STORE_FILE, tmp_dir);
    snprintf(mark_path, sizeof mark_path, "%s/" MARK_FILE, tmp_dir);
    snprintf(staged, sizeof staged, "%s/" STORE_STAGED, tmp_dir);
    am_view live = fx.am;
    fake_deleter really = {0, 0, 0, 0, &live, 1};
    deleter dr = {&really, fake_delete};
    CHECK_EQ(deletion_plan_make(&p, &pv, &live, &home, TP_TITLE_ID, 2), DEL_OK);
    am_view seen = live;
    CHECK_EQ(deletion_run(&p, &dr, &error), 0);
    deletion_judge(&seen, &live, &pv, &p, &j);
    CHECK(j.outcome == DEL_DONE && j.record_ok);
    CHECK(same_file(tmd_path, fx.tmd, fx.tmd_size));
    CHECK(same_file(mark_path, (const uint8_t *)"5\n", 2));
    CHECK(!exists(staged));
    CHECK_EQ(store_mark(tmp_dir), 5);
    fx.am = live;
    CHECK_EQ(assess(&pv, &fx), PACK_OK);
    CHECK(pv.count == 3 && pv.usable == 2 && pv.themes[1].index == 2 && !pv.themes[1].usable);
    CHECK_EQ(usable_of(&fx.am, 2), 0);
    CHECK_EQ(deletion_plan_make(&p, &pv, &fx.am, &home, TP_TITLE_ID, 2), DEL_NOT_INSTALLED);
    tp_source src;
    tp_theme theme;
    CHECK_EQ(files_open_source(zip_plain, &src, NULL, NULL), FILES_OK);
    CHECK_EQ(tp_theme_open(&theme, &src, "plain"), TP_OK);
    install_plan plan;
    CHECK_EQ(plan_install(&plan, &pv, &theme), PLAN_OK);
    CHECK_EQ(plan.index, 6);
    CHECK_EQ(plan.delta.kept, 3);                       /* records 1, 2 (deleted) and 4 */
    tp_theme_close(&theme);
    files_close_source(&src);
    /* AM deleted it but answered with an error: the view decides, and the record holds as well */
    fixture_make(&fx, idx, 3, 5, 1);
    records(&fx, 1, 0);
    CHECK_EQ(assess(&pv, &fx), PACK_OK);
    live = fx.am;
    really = (fake_deleter){0, 0, 0, 0xC8A08C0Fu, &live, 1};
    CHECK_EQ(deletion_plan_make(&p, &pv, &live, &home, TP_TITLE_ID, 2), DEL_OK);
    seen = live;
    CHECK_EQ(deletion_run(&p, &dr, &error), 1);
    deletion_judge(&seen, &live, &pv, &p, &j);
    CHECK(j.outcome == DEL_DONE && j.record_ok);
    pack_free(&pv);
}

/* ---------------------------------------------------------------- what a popup and the screen under it take */

/* A tap on what lies under the popup's button opens the popup: down at `down`, lifted 60 ms later, the push shown
 * for PRESS_MS from the touch's start, the first frame 6 ms after. Returns that frame's time. */
static uint64_t open_by_tap(popup_guard *g, popup_focus *f, uint64_t down, int touching)
{
    uint64_t opened = down + PRESS_MS;
    guard_arm(g, touching);
    focus_open(f);
    guard_from(g, opened + 6);
    guard_from(g, opened + 22);                         /* later frames do not move it */
    return opened + 6;
}

/* A touch-down at `at` on a button of the popup: what touch.c makes of it once lifted on that button. */
static int tap_popup(popup_guard *g, uint64_t at, int button)
{
    touch_state t = {.target = -1};
    touch_down(&t, guard_takes(g, at) ? button : -1, 0, 0, 236, 190, at);
    guard_lift(g);
    return touch_up(&t, 1);
}

static void test_popup(void)
{
    popup_guard g;
    popup_focus f;
    enum { DELETE = 7 };                                /* the question's "Löschen", any target number */
    /* 0.8.0's double tap: "Datei löschen" tapped twice at the place where the question's "Löschen" then lies */
    uint64_t shown = open_by_tap(&g, &f, 1000, 0);
    CHECK_EQ(tap_popup(&g, 1180, DELETE), -1);
    shown = open_by_tap(&g, &f, 1000, 0);
    CHECK_EQ(tap_popup(&g, shown + POPUP_GUARD_MS - 1, DELETE), -1);    /* inside the guard time */
    CHECK_EQ(tap_popup(&g, shown + POPUP_GUARD_MS, DELETE), DELETE);    /* a deliberate tap after it */
    CHECK_EQ(tap_popup(&g, shown + 5000, DELETE), DELETE);
    /* a touch held through the opening presses nothing, however long; the next one after its lift does */
    shown = open_by_tap(&g, &f, 1000, 1);
    CHECK(!guard_takes(&g, shown + 5000));
    guard_lift(&g);
    CHECK(guard_takes(&g, shown + 5000));
    CHECK_EQ(tap_popup(&g, shown + 6000, DELETE), DELETE);
    /* a double tap stays out of it up to a gap of about 450 ms: opened PRESS_MS after the first touch-down */
    CHECK(PRESS_MS + POPUP_GUARD_MS >= 450);

    /* the screen under a popup, once it closed (0.8.1's help "Okay" over the cleanup "Löschen"): a guard that never
     * fired takes everything; after the close only a touch that came down POPUP_GUARD_MS later */
    popup_guard screen = {0};
    CHECK(guard_takes(&screen, 0) && guard_takes(&screen, 5000));
    uint64_t closed = 1000 + PRESS_MS + 2;              /* "Okay" tapped at 1000, the push shown, then the close */
    guard_arm(&screen, 0);
    guard_from(&screen, closed);
    CHECK_EQ(tap_popup(&screen, 1160, DELETE), -1);     /* the second tap of a double tap, 160 ms after the first */
    CHECK_EQ(tap_popup(&screen, 1450, DELETE), -1);
    CHECK_EQ(tap_popup(&screen, closed + POPUP_GUARD_MS - 1, DELETE), -1);
    CHECK_EQ(tap_popup(&screen, closed + POPUP_GUARD_MS, DELETE), DELETE);
    guard_arm(&screen, 1);                              /* closed with B while the stylus rests on the screen */
    guard_from(&screen, closed);
    CHECK(!guard_takes(&screen, closed + 5000));
    guard_lift(&screen);
    CHECK(guard_takes(&screen, closed + 5000));
}

static void test_judge(void)
{
    static const uint16_t idx[] = {1, 2};
    fixture_make(&fx, idx, 2, 2, 0);
    install_plan plan;
    memset(&plan, 0, sizeof plan);
    plan.index = 3;
    am_view after = fx.am;                              /* positions 0, 1, 2 hold indices 0, 1, 2 */
    after.count = 4;
    after.list[3] = (tp_am_content){3, 3, 0x1000};
    after.usable[3] = 1;
    install_result res;
    judgement j;
    memset(&res, 0, sizeof res);
    res.done = res.wrote = res.committed1 = 1;
    install_judge(&fx.am, &after, &plan, &res, &j);
    CHECK_EQ(j.outcome, OUT_DONE);
    after.pending = PENDING_YES;
    install_judge(&fx.am, &after, &plan, &res, &j);
    CHECK_EQ(j.outcome, OUT_DONE_UNCONFIRMED);
    after.pending = PENDING_NO;
    after.usable[3] = 0;
    install_judge(&fx.am, &after, &plan, &res, &j);
    CHECK_EQ(j.outcome, OUT_DONE_UNCONFIRMED);
    CHECK_EQ(j.new_installed, 0);
    after.usable[3] = 1;
    after.usable[1] = 0;
    install_judge(&fx.am, &after, &plan, &res, &j);
    CHECK_EQ(j.outcome, OUT_DONE_UNCONFIRMED);
    CHECK_EQ(j.lost, 1);
    after.ok = 0;
    install_judge(&fx.am, &after, &plan, &res, &j);
    CHECK_EQ(j.outcome, OUT_DONE_UNCONFIRMED);
    CHECK_EQ(j.seen, 0);
    res.done = 0;
    install_judge(&fx.am, &after, &plan, &res, &j);
    CHECK_EQ(j.outcome, OUT_UNKNOWN);
    after.ok = 1;
    install_judge(&fx.am, &after, &plan, &res, &j);
    CHECK_EQ(j.outcome, OUT_LOST);
    CHECK_EQ(j.lost, 1);
    res.committed1 = 0;
    after = fx.am;
    install_judge(&fx.am, &after, &plan, &res, &j);
    CHECK_EQ(j.outcome, OUT_CANCELLED_KEPT);
    res.committed1 = 1;
    install_judge(&fx.am, &after, &plan, &res, &j);
    CHECK_EQ(j.outcome, OUT_PHASE2_FAILED_KEPT);
    res.wrote = 0;
    install_judge(&fx.am, &after, &plan, &res, &j);
    CHECK_EQ(j.outcome, OUT_NOTHING_WRITTEN);
    /* a theme HOME had deleted before does not count as lost */
    fx.am.usable[1] = 0;
    after = fx.am;
    res.wrote = 1;
    res.committed1 = 0;
    install_judge(&fx.am, &after, &plan, &res, &j);
    CHECK_EQ(j.outcome, OUT_CANCELLED_KEPT);
}

/* ---------------------------------------------------------------- the zip list */

static void copy_file(const char *from, const char *to)
{
    size_t size;
    uint8_t *p = slurp(from, &size);
    if (files_write(to, p, size))
        exit(3);
    free(p);
}

static void count_tick(void *ctx, size_t done, size_t total)
{
    (void)total;
    *(size_t *)ctx = done;
}

static void test_scan(const char *zip_bgm, const char *zip_plain, const char *cia)
{
    char dir[1024], path[1200];
    snprintf(dir, sizeof dir, "%s/Themes", tmp_dir);
    scan_list list;
    CHECK_EQ(scan_folder(dir, NULL, &list, NULL, NULL), 0);
    CHECK_EQ(list.missing, 1);
    CHECK_EQ(list.count, 0);
    mkdir(dir, 0755);
    CHECK_EQ(scan_folder(dir, NULL, &list, NULL, NULL), 0);
    CHECK_EQ(list.missing, 0);
    CHECK_EQ(list.count, 0);
    scan_free(&list);
    /* the kind comes from the content: a zip named .txt is listed, a file named .zip that is none is not */
    static const char *const names[][2] = {
        {"b-plain.zip", "p"}, {"A-Music.ZIP", "m"}, {"._A-Music.ZIP", "m"}, {".hidden.zip", "p"},
        {"notes.txt", "p"}, {"not-a-zip.zip", "x"}, {"damaged.zip", "d"}, {"Pocket.cia", "c"},
        {"preview.png", "x"},
    };
    for (size_t i = 0; i < sizeof names / sizeof *names; i++) {
        snprintf(path, sizeof path, "%s/%s", dir, names[i][0]);
        char what = names[i][1][0];
        if (what == 'x')
            files_write(path, (const uint8_t *)"<html>a page</html>", 19);
        else if (what == 'd')
            files_write(path, (const uint8_t *)"PK\3\4 but nothing after it", sizeof "PK\3\4 but nothing after it" - 1);
        else
            copy_file(what == 'm' ? zip_bgm : what == 'c' ? cia : zip_plain, path);
    }
    /* folders: listed first, by name, whatever their name says; their content is not read */
    static const char *const folders[] = {"zeta", "folder.zip", ".hidden-folder", "Alpha"};
    for (size_t i = 0; i < sizeof folders / sizeof *folders; i++) {
        snprintf(path, sizeof path, "%s/%s", dir, folders[i]);
        mkdir(path, 0755);
    }
    snprintf(path, sizeof path, "%s/zeta/inner.zip", dir);
    copy_file(zip_plain, path);
    size_t ticks = 0;
    CHECK_EQ(scan_folder(dir, NULL, &list, count_tick, &ticks), 0);
    CHECK_EQ(ticks, 10);                                /* every candidate, folders included */
    CHECK_EQ(list.other, 2);                            /* the page, the png */
    CHECK_EQ(list.folders, 3);
    CHECK_EQ(list.count, 8);
    if (list.count == 8) {
        CHECK(!strcmp(list.items[0].file, "Alpha") && list.items[0].folder);
        CHECK(!strcmp(list.items[1].file, "folder.zip") && list.items[1].folder);
        CHECK(!strcmp(list.items[2].file, "zeta") && list.items[2].folder);
        CHECK(!strcmp(list.items[3].file, "A-Music.ZIP") && !list.items[3].folder);
        CHECK(!strcmp(list.items[4].file, "b-plain.zip"));
        CHECK(!strcmp(list.items[5].file, "damaged.zip"));
        CHECK(!strcmp(list.items[6].file, "notes.txt"));
        CHECK(!strcmp(list.items[7].file, "Pocket.cia"));
        CHECK_EQ(list.items[3].result, TP_OK);
        CHECK_EQ(list.items[3].kind, TP_KIND_ZIP);
        CHECK_EQ(list.items[3].has_bgm, 1);
        CHECK_EQ(list.items[4].has_bgm, 0);
        CHECK(list.items[5].result != TP_OK);
        CHECK(!strcmp(list.items[5].name, "damaged"));
        CHECK_EQ(list.items[6].result, TP_OK);
        CHECK_EQ(list.items[7].kind, TP_KIND_CIA);
        CHECK_EQ(list.items[7].result, TP_OK);
        CHECK_EQ(list.items[7].has_bgm, 1);             /* the CIA of the tests carries the music zip */
        CHECK_EQ(list.items[3].in_pack, 0);
    }
    /* one folder down and up again */
    char sub[1200], child[64];
    scan_path(sub, sizeof sub, dir, "zeta");
    scan_free(&list);
    CHECK_EQ(scan_folder(sub, NULL, &list, NULL, NULL), 0);
    CHECK_EQ(list.count, 1);
    CHECK_EQ(list.folders, 0);
    CHECK_EQ(scan_up(sub, child, sizeof child), 0);
    CHECK(!strcmp(sub, dir) && !strcmp(child, "zeta"));
    /* marks: a pack whose theme 2 is the music zip */
    tp_source src;
    tp_theme t;
    CHECK_EQ(files_open_source(zip_bgm, &src, NULL, NULL), FILES_OK);
    CHECK_EQ(tp_theme_peek(&t, &src, NULL), TP_OK);
    files_close_source(&src);
    static const uint16_t idx[] = {1, 2};
    fixture_make(&fx, idx, 2, 2, 0);
    memset(fx.archive + 8 + 0xC8 * 2, 0, 0x40);
    pack_cut_name((char *)fx.archive + 8 + 0xC8 * 2, t.name);
    memcpy(fx.icons[2], t.icon, TP_ICON_SIZE);
    records(&fx, 1, 0);
    pack_view pv = {0};
    CHECK_EQ(assess(&pv, &fx), PACK_OK);
    scan_free(&list);
    CHECK_EQ(scan_folder(dir, &pv, &list, NULL, NULL), 0);
    CHECK_EQ(list.items[3].in_pack, 2);
    CHECK_EQ(list.items[3].in_pack_usable, 1);
    CHECK_EQ(list.items[4].in_pack, 0);
    CHECK_EQ(list.items[7].in_pack, 2);                 /* the CIA's zip is the same theme */
    scan_free(&list);
    pack_free(&pv);
    char fallback[64];
    scan_fallback(fallback, sizeof fallback, "Name.zip");
    CHECK(!strcmp(fallback, "Name"));
    /* a folder of thousands of files (screenshots, say): only their names are held until the first 500 by name
     * are chosen; those are looked at, the rest only counted as "more" */
    char many[1200];
    snprintf(many, sizeof many, "%s/many", tmp_dir);
    mkdir(many, 0755);
    snprintf(path, sizeof path, "%s/a-theme.zip", many);
    copy_file(zip_plain, path);
    for (int k = 0; k < 3000; k++) {
        snprintf(path, sizeof path, "%s/shot-%04d.png", many, k);
        files_write(path, (const uint8_t *)"", 0);
    }
    ticks = 0;
    CHECK_EQ(scan_folder(many, NULL, &list, count_tick, &ticks), 0);
    CHECK_EQ(list.more, 1);
    CHECK_EQ(ticks, SCAN_MAX);
    CHECK_EQ(list.count, 1);
    CHECK_EQ(list.folders, 0);
    CHECK_EQ(list.other, SCAN_MAX - 1);
    if (list.count == 1)
        CHECK(!strcmp(list.items[0].file, "a-theme.zip") && list.items[0].result == TP_OK);
    scan_free(&list);
    /* the card's root: "sdmc:/" keeps its slash, shows as "/", has nothing above it */
    char card[64] = "sdmc:/Themes/sub", path2[80];
    CHECK(!scan_is_root(card));
    CHECK(!strcmp(scan_shown(card), "/Themes/sub"));
    CHECK_EQ(scan_up(card, child, sizeof child), 0);
    CHECK(!strcmp(card, "sdmc:/Themes") && !strcmp(child, "sub"));
    CHECK_EQ(scan_up(card, child, sizeof child), 0);
    CHECK(!strcmp(card, "sdmc:/") && !strcmp(child, "Themes"));
    CHECK(scan_is_root(card));
    CHECK(!strcmp(scan_shown(card), "/"));
    CHECK_EQ(scan_up(card, child, sizeof child), -1);
    scan_path(path2, sizeof path2, card, "Themes");
    CHECK(!strcmp(path2, "sdmc:/Themes"));
}

/* Opening a theme as the application does (check_work), with its bar: never back, and near the end at the end. */
static progress_run bar_run;
static float bar_last, bar_old_last;
static int bar_falls, bar_old_falls;
static unsigned bar_passes;

static void bar_tick(void *ctx, uint64_t read, uint64_t size)
{
    (void)ctx;
    float f = progress_keep(&bar_run, progress_passes(read, size, bar_passes));
    bar_falls += f < bar_last;
    bar_last = f;
}

/* 0.6.0's bar: the read offset, reported where the read ended (here: from the bytes the reads cover) */
static void old_tick(void *ctx, uint64_t at, uint64_t size)
{
    (void)ctx;
    float f = size ? (float)at / (float)size : 0;
    bar_old_falls += f < bar_old_last - 0.001f;
    bar_old_last = f;
}

typedef struct offset_source {
    const tp_source *in;
} offset_source;

static int offset_read(void *ctx, uint64_t offset, void *buf, size_t len)
{
    const offset_source *o = ctx;
    int r = o->in->read(o->in->ctx, offset, buf, len);
    old_tick(NULL, offset + len, o->in->size);
    return r;
}

/* The installing screen's steps (0.8.4; the ring of 0.8.3 is gone in 0.9.0): the line, the only thing that shows
 * progress, the drops and the footer's dots from one value */
static void test_step_line(void)
{
    static const float x[4] = {STEPS_X1, STEPS_X2, STEPS_X3, STEPS_X4};
    static const int first[5] = {STEP_MEASURE, STEP_TITLE1, STEP_TITLE2, STEP_COMMIT2, STEP_DONE};
    /* the steepest stretch: px of line per unit of bar, so a step of the bar moves the line at most this much */
    float steepest = 0;
    for (int k = 0; k < 4; k++) {
        float from = k ? x[k - 1] : STEPS_LINE_X, lo = progress_install(first[k], 0), hi = progress_install(first[k + 1], 0);
        if ((x[k] - from) / (hi - lo) > steepest)
            steepest = (x[k] - from) / (hi - lo);
    }
    int done_was = 0;
    float end_was = STEPS_LINE_X, most_moved = 0;
    for (int i = 0; i <= 4000; i++) {           /* every bar value from 0 to 100 % in steps of 0.025 % */
        float f = i / 4000.0f;
        int done = progress_steps_done(f);
        float end = progress_line_end(f, STEPS_LINE_X, x);
        int passed = 0;
        for (int k = 0; k < 4; k++)
            passed += end >= x[k];
        CHECK_EQ(passed, done);                 /* green exactly the drops whose centre the line has reached */
        CHECK(end >= end_was && end >= STEPS_LINE_X && end <= x[3]);
        CHECK(done >= done_was);
        CHECK(done == 4 || end < x[done]);      /* the step under way's drop is not reached yet */
        /* continuous: no jump at a drop or anywhere else, only as far as the bar moved */
        CHECK(end - end_was <= steepest / 4000.0f + 0.001f);
        if (end - end_was > most_moved)
            most_moved = end - end_was;
        done_was = done;
        end_was = end;
    }
    CHECK(most_moved > 0);
    CHECK(progress_steps_done(1.0f) == 4 && progress_line_end(1.0f, STEPS_LINE_X, x) == x[3]);
    CHECK(progress_steps_done(0) == 0 && progress_line_end(0, STEPS_LINE_X, x) == STEPS_LINE_X);
    for (int k = 1; k < 4; k++)                 /* a step begins with the line at the drop before it */
        CHECK(fabsf(progress_line_end(progress_install(first[k], 0), STEPS_LINE_X, x) - x[k - 1]) < 0.001f);
    /* over a whole install, as install_run reports it: nothing goes back */
    static const int order[] = {STEP_MEASURE, STEP_STAGE, STEP_TITLE1, STEP_TMD, STEP_CONTENT0, STEP_COMMIT1,
                                STEP_RECORD, STEP_TITLE2, STEP_CONTEXT, STEP_CONTENT, STEP_COMMIT2, STEP_DONE};
    done_was = 0;
    end_was = STEPS_LINE_X;
    for (size_t i = 0; i < sizeof order / sizeof *order; i++)
        for (int part = 0; part <= 40; part++) {
            float f = progress_install(order[i], part / 40.0f);
            int done = progress_steps_done(f);
            float end = progress_line_end(f, STEPS_LINE_X, x);
            CHECK(done >= done_was && end >= end_was);
            done_was = done;
            end_was = end;
        }
    CHECK_EQ(done_was, 4);
    /* 0.9.3, the line held off the marks (Clear and Platin): with the marks where the bar puts them, the liquid's
     * end never stands under a mark's image, moves only forward, starts at the line's start and ends past the last
     * mark; while a mark becomes done the end passes under it with the change and no further */
    const float before = STEP_TUBE_BEFORE, after = STEP_TUBE_AFTER;
    float rest[4] = {1, 0, 0, 0}, held_was = STEPS_LINE_X;
    for (int i = 0; i <= 4000; i++) {
        float f = i / 4000.0f, end = progress_line_end(f, STEPS_LINE_X, x);
        int done = progress_steps_done(f);
        for (int k = 0; k < 4; k++)
            rest[k] = k < done ? 2 : k == done ? 1 : 0;
        float held = progress_line_held(end, STEPS_LINE_X, x, rest, before, after);
        int under = 0;
        for (int k = 0; k < 4; k++)
            under += held > x[k] - before + 0.001f && held < x[k] + after - 0.001f;
        CHECK_EQ(under, 0);
        CHECK(held >= held_was - 0.001f && held <= x[3] + after);
        CHECK(progress_line_held(end, STEPS_LINE_X, x, rest, 0, 0) == end);
        held_was = held;
    }
    float all_done[4] = {2, 2, 2, 2}, none[4] = {1, 0, 0, 0};
    CHECK(progress_line_held(x[3], STEPS_LINE_X, x, all_done, before, after) == x[3] + after);
    CHECK(progress_line_held(STEPS_LINE_X, STEPS_LINE_X, x, none, before, after) == STEPS_LINE_X);
    for (int k = 0; k < 3; k++)
        for (int part = 0; part <= 10; part++) {
            float changing[4] = {0, 0, 0, 0}, way = part / 10.0f;
            for (int j = 0; j < 4; j++)
                changing[j] = j < k ? 2 : j == k ? 1 + way : j == k + 1 ? 1 : 0;
            float held = progress_line_held(x[k], STEPS_LINE_X, x, changing, before, after);
            CHECK(fabsf(held - (x[k] - before + (before + after) * way)) < 0.001f);
        }
}

/* 0.9.2, the check's time by step: what the core tells of its phases and files.c of its reads */
static int phase_told[TP_PHASE_HASH + 1], phase_last, phase_bad, read_open, read_bad, read_spans;

static void phase_tell(void *ctx, int phase)
{
    (void)ctx;
    if (phase < 0 || phase > TP_PHASE_HASH) {
        phase_bad++;
        return;
    }
    phase_told[phase]++;
    phase_last = phase;
}

static void read_around(void *ctx, int begin)
{
    (void)ctx;
    read_bad += begin ? read_open : !read_open;     /* a begin inside a read, or an end without one */
    read_open = begin;
    read_spans += begin;
}

static void test_check_times(const char *zip, const char *cia)
{
    const char *files[] = {zip, cia};
    for (int k = 0; k < 2; k++) {
        tp_source src;
        static tp_package package;
        const tp_source *inner = NULL;
        tp_theme plain, timed;
        int kind;
        CHECK_EQ(files_open_source(files[k], &src, NULL, NULL), FILES_OK);
        CHECK_EQ(tp_theme_in(&src, 1, &package, &inner, &kind), TP_OK);
        CHECK_EQ(tp_theme_open(&plain, inner, "x"), TP_OK);
        tp_theme_close(&plain);
        memset(phase_told, 0, sizeof phase_told);
        phase_bad = read_open = read_bad = read_spans = 0;
        files_around_reads(&src, read_around, NULL);
        tp_set_phase(phase_tell, NULL);
        CHECK_EQ(tp_theme_in(&src, 1, &package, &inner, &kind), TP_OK);
        /* a CIA's own parts and its hash are none of the theme's phases: the caller times that call as a whole */
        CHECK_EQ(phase_told[TP_PHASE_UNZIP] + phase_told[TP_PHASE_LZ11] + phase_told[TP_PHASE_HASH], 0);
        int reads_in = read_spans;
        CHECK(reads_in > 0);
        CHECK_EQ(tp_theme_open(&timed, inner, "x"), TP_OK);
        CHECK(phase_told[TP_PHASE_UNZIP] > 0 && phase_told[TP_PHASE_LZ11] > 0);
        CHECK(phase_told[TP_PHASE_HASH] >= phase_told[TP_PHASE_LZ11]);  /* every piece decoded is hashed too */
        CHECK_EQ(phase_last, TP_PHASE_OTHER);       /* told last: that work is over */
        CHECK(read_spans > reads_in && !read_open && !read_bad && !phase_bad);
        /* being timed changes nothing of the theme */
        CHECK(!strcmp(plain.name, timed.name) && !memcmp(plain.icon, timed.icon, TP_ICON_SIZE));
        CHECK(plain.has_bgm == timed.has_bgm && plain.use_bgm == timed.use_bgm && plain.warnings == timed.warnings);
        /* switched off again: nothing more is told */
        tp_set_phase(NULL, NULL);
        files_around_reads(&src, NULL, NULL);
        int told = phase_told[TP_PHASE_OTHER] + phase_told[TP_PHASE_UNZIP], spans = read_spans;
        tp_content record;
        CHECK_EQ(tp_theme_content(&timed, TP_TITLE_ID, 5, NULL, &record), TP_OK);
        CHECK_EQ(phase_told[TP_PHASE_OTHER] + phase_told[TP_PHASE_UNZIP], told);
        CHECK_EQ(read_spans, spans);
        printf("check times %s: %d reads, phases told: unzip %d, LZ11 %d, hash %d\n", k ? "of a CIA" : "of a zip",
               spans, phase_told[TP_PHASE_UNZIP], phase_told[TP_PHASE_LZ11], phase_told[TP_PHASE_HASH]);
        tp_theme_close(&timed);
        files_close_source(&src);
    }
}

static void test_progress(const char *zip, const char *cia)
{
    const char *files[] = {zip, cia};
    for (int k = 0; k < 2; k++) {
        tp_source src;
        static tp_package package;
        const tp_source *inner = NULL;
        tp_theme theme;
        int kind;
        progress_start(&bar_run);
        bar_last = bar_old_last = 0;
        bar_falls = bar_old_falls = 0;
        bar_passes = k ? 2 : 1;                         /* as main.c: a file named .cia twice */
        CHECK_EQ(files_open_source(files[k], &src, bar_tick, NULL), FILES_OK);
        offset_source o = {&src};
        tp_source spy = {&o, src.size, offset_read};
        CHECK_EQ(tp_theme_in(&spy, 1, &package, &inner, &kind), TP_OK);
        CHECK_EQ(tp_theme_open(&theme, inner, "x"), TP_OK);
        CHECK_EQ(bar_falls, 0);
        CHECK(bar_last > (k ? 0.25f : 0.9f) && bar_last < 1.0f);
        printf("bar opening %s: ends at %.1f %%, falls 0 times (by the read offset, as in 0.6.0: %d times)\n",
               k ? "a CIA" : "a zip", 100 * bar_last, bar_old_falls);
        tp_theme_close(&theme);
        files_close_source(&src);
    }
    /* the mapping itself: grows, never past 1, a pass more slows down instead of starting again */
    float was = 0;
    for (uint64_t r = 0; r <= 5000; r += 7) {
        float f = progress_passes(r, 1000, 2);
        CHECK(f >= was && f < 1.0f);
        was = f;
    }
    CHECK(progress_passes(1000, 1000, 2) == 0.5f && progress_passes(0, 0, 1) == 0);
    progress_run run;
    progress_start(&run);
    CHECK(progress_keep(&run, 0.5f) == 0.5f && progress_keep(&run, 0.2f) == 0.5f && progress_keep(&run, 3) == 1);
    CHECK(progress_keep(&run, -1) == -1 && run.shown == 1);
    /* the install: every step in its order, from 0 to 1, never back (0.6.0 went back to 0 after measuring) */
    static const int steps[] = {STEP_MEASURE, STEP_STAGE, STEP_TITLE1, STEP_TMD, STEP_CONTENT0, STEP_COMMIT1,
                                STEP_RECORD, STEP_TITLE2, STEP_CONTEXT, STEP_CONTENT, STEP_COMMIT2, STEP_DONE};
    was = 0;
    for (size_t k = 0; k < sizeof steps / sizeof steps[0]; k++)
        for (int part = 0; part <= 10; part++) {
            float f = progress_install(steps[k], part / 10.0f);
            CHECK(f >= was && f <= 1.0f);
            was = f;
        }
    CHECK(was == 1.0f && progress_install(STEP_MEASURE, 0) == 0);
}

/* One list behaviour for every list (scroll.h): the scroll for a selection, a drag 1 to 1 and its glide inside the
 * bounds, a tap after a drag that selects and presses nothing, the scroll bar, a short and an empty list. */
static int list_settle(const scroll_shape *s, scroll_view *v)
{
    scroll_area a = scroll_of(s);
    int frames = 0, inside = 1;
    while (scroll_moving(v) && frames < 2000) {
        scroll_frame(s, v);
        inside &= v->scroll >= 0 && v->scroll <= scroll_max(&a);
        frames++;
    }
    return inside && !scroll_moving(v) ? frames : -1;
}

static void test_list(void)
{
    /* the folder view's make: 3 folders, then 20 themes */
    scroll_shape s = {23, 3, FOLDER_H, ROW_H, LIST_GAP, LIST_INSET, LIST_BOTTOM, LIST_END_PAD, FOCUS + 1};
    scroll_area a = scroll_of(&s);
    float m = scroll_max(&a);
    CHECK(scroll_top(&s, 3) == 3 * (FOLDER_H + LIST_GAP) && scroll_row_h(&s, 2) == FOLDER_H &&
          scroll_row_h(&s, 3) == ROW_H);
    CHECK(m > 0 && LIST_INSET + scroll_top(&s, 22) + ROW_H + LIST_END_PAD - m == LIST_BOTTOM);
    scroll_view v = {0};
    /* the first move selects the first row shown whole and moves nothing; then each move glides the list until
     * the cursor's row shows whole with its ring's room, never past either end */
    CHECK(scroll_move(&s, &v, 1) && v.selected && v.cursor == 0);
    for (int k = 0; k < 22; k++) {
        CHECK(scroll_move(&s, &v, 1) && v.cursor == (size_t)k + 1);
        float before = v.scroll;
        scroll_frame(&s, &v);
        float target = scroll_target(&s, &v);
        CHECK(target == before || (v.scroll != target && (v.scroll - before) * (target - before) > 0));  /* a glide */
        CHECK(list_settle(&s, &v) >= 0);
        CHECK(v.scroll == scroll_target(&s, &v) && scroll_whole(&s, &v, v.cursor));
        float y = scroll_y(&s, &v, v.cursor);
        CHECK(y >= LIST_INSET && y == roundf(y));                   /* below the corner button, on a whole pixel */
    }
    CHECK(v.cursor == 22 && v.scroll == m);                         /* the last row: the list at its end */
    CHECK(!scroll_move(&s, &v, 5) && v.cursor == 22);               /* nothing below the last */
    CHECK(scroll_move(&s, &v, -100) && v.cursor == 0 && list_settle(&s, &v) >= 0 && v.scroll == 0);
    /* continuous: the list stands at any pixel and its rows are cut at the area's edges */
    v.scroll = 37;
    CHECK(scroll_y(&s, &v, 0) == LIST_INSET - 37 && scroll_y(&s, &v, 0) < LIST_INSET && !scroll_whole(&s, &v, 0));
    CHECK(scroll_first(&s, &v) == 1);
    /* a drag: nothing for the first DRAG_START - 1 px, then 1 to 1 from where it came down */
    v = (scroll_view){0};
    scroll_grab(&s, &v, 150, 1);
    CHECK(v.armed && !v.dragging);
    scroll_drag(&s, &v, 150 - (DRAG_START - 1));
    CHECK(!v.dragging && v.scroll == 0);
    for (int y = 150 - DRAG_START; y >= 90; y -= 6)
        scroll_drag(&s, &v, y);
    CHECK(v.dragging && v.scroll == 60 && v.fling > 0);
    CHECK(scroll_release(&v) && !v.armed && !v.dragging && scroll_moving(&v));
    int glided = list_settle(&s, &v);
    CHECK(glided > 0 && glided < 120 && v.scroll > 60 && v.scroll < m);      /* it glides out, briefly */
    printf("list: a drag of 6 px a frame glides on for %d frames, %.0f px\n", glided, v.scroll - 60);
    /* a fast drag down past the end: the glide stops at the end, not after it */
    v = (scroll_view){0};
    scroll_grab(&s, &v, 190, 1);
    for (int y = 190; y >= 10; y -= 30)
        scroll_drag(&s, &v, y);
    CHECK(v.scroll == 180);
    for (int y = 10; y >= -1100; y -= 60)
        scroll_drag(&s, &v, y);
    CHECK(v.scroll == m);
    scroll_release(&v);
    CHECK(list_settle(&s, &v) >= 0 && v.scroll == m && v.fling == 0);
    /* and back up past the top */
    scroll_grab(&s, &v, 10, 1);
    for (int y = 10; y <= 900; y += 70)
        scroll_drag(&s, &v, y);
    scroll_release(&v);
    CHECK(list_settle(&s, &v) >= 0 && v.scroll == 0);
    /* a selected row dragged out of view hands the selection to a row that shows whole */
    v = (scroll_view){.cursor = 1, .selected = 1};
    scroll_grab(&s, &v, 180, 1);
    scroll_drag(&s, &v, 40);
    CHECK(v.selected && v.cursor > 1 && scroll_whole(&s, &v, v.cursor));
    scroll_release(&v);
    list_settle(&s, &v);
    CHECK(scroll_whole(&s, &v, v.cursor));
    /* a tap after a drag: the row it began on is neither pressed nor selected; a touch while the list glides stops
     * it and presses nothing; a tap at rest presses its row */
    touch_state t;
    v = (scroll_view){0};
    touch_down(&t, 7, 1, scroll_moving(&v), 50, 120, 1000);
    scroll_grab(&s, &v, 120, 1);
    touch_move(&t, 50, 100, 1);
    scroll_drag(&s, &v, 100);
    CHECK(touch_up(&t, 1) == -1 && scroll_release(&v) && !v.selected);
    CHECK(scroll_moving(&v));
    touch_down(&t, 7, 1, scroll_moving(&v), 50, 120, 1100);
    scroll_grab(&s, &v, 120, 1);
    CHECK(!scroll_moving(&v) && touch_held(&t, 1300) == -1 && touch_up(&t, 1) == -1 && !scroll_release(&v));
    touch_down(&t, 7, 1, scroll_moving(&v), 50, 120, 1400);
    scroll_grab(&s, &v, 120, 1);
    CHECK(touch_up(&t, 1) == 7 && !scroll_release(&v) && v.fling == 0);
    /* not on the rows (the corner button): no drag */
    v = (scroll_view){0};
    scroll_grab(&s, &v, 20, 0);
    scroll_drag(&s, &v, 120);
    CHECK(!v.armed && !v.dragging && v.scroll == 0);
    /* the scroll bar: the thumb shows the place, and held it puts the list where the stylus is */
    float at, size;
    scroll_thumb(&s, &v, &at, &size);
    CHECK(at == 0 && size > 0 && size < 1);
    scroll_thumb_to(&s, &v, SCROLL_Y, SCROLL_Y, LIST_BOTTOM - SCROLL_Y);
    CHECK(v.scroll == 0);
    scroll_thumb_to(&s, &v, LIST_BOTTOM, SCROLL_Y, LIST_BOTTOM - SCROLL_Y);
    CHECK(v.scroll == m);
    scroll_thumb(&s, &v, &at, &size);
    CHECK(fabsf(at / (1 - size) - 1) < 0.001f);                   /* the thumb at the track's end */
    scroll_thumb_to(&s, &v, (SCROLL_Y + LIST_BOTTOM) / 2.0f, SCROLL_Y, LIST_BOTTOM - SCROLL_Y);
    CHECK(v.scroll > 0 && v.scroll < m);
    /* the installed designs' and the installed badges' make: rows alone, no corner button */
    scroll_shape sets = {9, 0, FOLDER_H, ROW_H, LIST_GAP, DESIGNS_INSET, LIST_BOTTOM, LIST_END_PAD, FOCUS + 1};
    v = (scroll_view){0};
    CHECK(scroll_y(&sets, &v, 1) == DESIGNS_INSET + ROW_H + LIST_GAP);
    scroll_move(&sets, &v, 0);
    scroll_move(&sets, &v, 3);
    list_settle(&sets, &v);
    CHECK(v.cursor == 3 && v.scroll > 0 && scroll_whole(&sets, &v, 3));
    CHECK(fmodf(v.scroll, ROW_H + LIST_GAP) != 0);               /* by pixels, not by pages of rows */
    /* a list that fits: no scroll, no drag */
    scroll_shape few = {2, 1, FOLDER_H, ROW_H, LIST_GAP, LIST_INSET, LIST_BOTTOM, LIST_END_PAD, FOCUS + 1};
    v = (scroll_view){0};
    scroll_grab(&few, &v, 100, 1);
    scroll_drag(&few, &v, 20);
    CHECK(!v.armed && v.scroll == 0);
    CHECK(scroll_move(&few, &v, 1) && scroll_move(&few, &v, 1) && v.cursor == 1 && list_settle(&few, &v) >= 0 &&
          v.scroll == 0);
    /* the folder browser's paths (R16): up to the card's root and no further, joined there without a second slash;
     * the root hides the system's folder, every folder names with a dot */
    char dir[64] = "sdmc:/Badges/Mehr", child[32], path[64];
    CHECK(scan_up(dir, child, sizeof child) == 0 && !strcmp(dir, "sdmc:/Badges") && !strcmp(child, "Mehr"));
    CHECK(scan_up(dir, child, sizeof child) == 0 && !strcmp(dir, "sdmc:/") && !strcmp(child, "Badges"));
    CHECK(scan_up(dir, child, sizeof child) == -1 && !strcmp(dir, "sdmc:/") && scan_is_root(dir));
    scan_path(path, sizeof path, dir, "Badges");
    CHECK(!strcmp(path, "sdmc:/Badges") && !strcmp(scan_shown(dir), "/"));
    CHECK(scan_hidden("sdmc:/", "Nintendo 3DS") && scan_hidden("sdmc:/", "NINTENDO 3DS") &&
          scan_hidden("/", "Nintendo 3DS"));
    CHECK(!scan_hidden("sdmc:/Themes", "Nintendo 3DS") && !scan_hidden("sdmc:/", "Nintendo 3DSX") &&
          !scan_hidden("sdmc:/", "Nintendo") && !scan_hidden("sdmc:/", "Badges"));
    CHECK(scan_hidden("sdmc:/", ".Trashes") && scan_hidden("sdmc:/Themes", "._a.zip") && !scan_hidden("sdmc:/", "a.b"));
    /* an empty list: nothing to select, scroll or drag */
    scroll_shape none = {0, 0, FOLDER_H, ROW_H, LIST_GAP, LIST_INSET, LIST_BOTTOM, LIST_END_PAD, FOCUS + 1};
    v = (scroll_view){0};
    a = scroll_of(&none);
    CHECK(scroll_max(&a) == 0 && a.content == 0 && scroll_first(&none, &v) == 0);
    CHECK(!scroll_move(&none, &v, 1) && !v.selected);
    scroll_grab(&none, &v, 100, 1);
    scroll_drag(&none, &v, 20);
    scroll_frame(&none, &v);
    CHECK(!v.armed && v.scroll == 0 && !scroll_release(&v));
}

/* What 0.6.0 lays out with the application's texts and shapes: the help popup's paragraphs, the folder view's scroll
 * limits, the status line's words (runs of spaces and the corner button's path: the kit's test_kit). */
static void test_spaces_and_layout(void)
{
    static ui_block para[UI_POPUP_TEST];
    static char in_words[4096], out_words[4096];
    int all;

    /* the help popup, both languages, a short and a long folder: every paragraph whole, in the panel, at
     * DLG_MIN_PX or more; the long path broken between characters where it has no space */
    const char *dirs[] = {"/Themes", "/Themes/Downloaded from the internet/2026/November/A folder with a very long "
                          "name/and_one_more_folder_without_any_spaces_in_it"};
    float max_h = DLG_H - DLG_BTN_H - 2 * DLG_PAD_Y;
    for (int lang = LANG_EN; lang <= LANG_DE; lang++)
        for (int d = 0; d < 2; d++) {
            char where[600];
            snprintf(where, sizeof where, text(lang, T_HELP_FILE), dirs[d]);
            const char *const texts[] = {where, text(lang, T_HELP_KEEP), text(lang, T_HELP_DELETE)};
            all = wrap_paragraphs(para, 3, texts, DLG_TEXT_W, DLG_PX, DLG_MIN_PX, max_h, DLG_LH, DLG_PARA, fake_width,
                                  NULL);
            CHECK(all);
            float h = 0;
            for (int i = 0; i < 3; i++) {
                check_block(&para[i], 1, DLG_TEXT_W);
                CHECK(para[i].px == para[0].px && para[i].px >= DLG_MIN_PX);
                h += para[i].count * para[i].lh + (i ? DLG_PARA * (float)para[0].px / DLG_PX : 0);
                words_of(texts[i], in_words, sizeof in_words);
                block_words(&para[i], out_words, sizeof out_words);
                if (!strchr(texts[i], '_'))
                    CHECK(!strcmp(in_words, out_words));
            }
            CHECK(h <= max_h);
            if (lang == LANG_DE)
                printf("help popup, German, %s folder: %d px, %d + %d + %d lines\n", d ? "long" : "short", para[0].px,
                       para[0].count, para[1].count, para[2].count);
        }
    /* a folder path too long for the panel: the path is cut at its beginning, every paragraph shows whole */
    for (int len = 100; len <= 511; len += 411) {
        char path[600];
        for (int i = 0; i < len; i++)
            path[i] = i % 12 == 0 ? '/' : (char)('a' + i % 26);
        path[len] = 0;
        for (int lang = LANG_EN; lang <= LANG_DE; lang++) {
            const char *const rest[] = {text(lang, T_HELP_KEEP), text(lang, T_HELP_DELETE)};
            all = wrap_with_path(para, 3, text(lang, T_HELP_FILE), path, rest, DLG_TEXT_W, DLG_PX, DLG_MIN_PX, max_h,
                                 DLG_LH, DLG_PARA, fake_width, NULL);
            CHECK(all);
            float h = 0;
            for (int i = 0; i < 3; i++) {
                CHECK(para[i].count > 0);
                check_block(&para[i], 1, DLG_TEXT_W);
                h += para[i].count * para[i].lh + (i ? DLG_PARA * (float)para[0].px / DLG_PX : 0);
            }
            CHECK(h <= max_h);
            for (int i = 1; i < 3; i++) {
                words_of(rest[i - 1], in_words, sizeof in_words);
                block_words(&para[i], out_words, sizeof out_words);
                CHECK(!strcmp(in_words, out_words));
            }
            block_words(&para[0], out_words, sizeof out_words);
            CHECK(len < 500 || strstr(out_words, "...") != NULL);              /* the long one: cut */
            CHECK(strstr(out_words, path + len - 5) != NULL);                   /* its end kept */
        }
    }
    /* too much for the panel even at DLG_MIN_PX: cut with "...", within the height */
    char lots[1500] = "";
    for (int i = 0; i < 40; i++)
        strcat(lots, "Many words to say. ");
    const char *const big[] = {lots, lots};
    all = wrap_paragraphs(para, 2, big, DLG_TEXT_W, DLG_PX, DLG_MIN_PX, max_h, DLG_LH, DLG_PARA, fake_width, NULL);
    CHECK(!all);
    CHECK(para[0].px == DLG_MIN_PX && para[0].count * para[0].lh <= max_h);
    CHECK(para[1].count == 0 || para[0].count * para[0].lh + para[1].count * para[1].lh <= max_h);

    /* the folder view's scrolling: 3 folders and 8 themes, as the application places them */
    float tops[11], hs[11], top = 0;
    for (int i = 0; i < 11; i++) {
        hs[i] = i < 3 ? FOLDER_H : ROW_H;
        tops[i] = top;
        top += hs[i] + LIST_GAP;
    }
    scroll_area a = {LIST_BOTTOM, LIST_INSET, top - LIST_GAP};
    float m = scroll_max(&a);
    CHECK(m > 0 && m == LIST_INSET + a.content - LIST_BOTTOM);
    CHECK(scroll_clamp(&a, -5) == 0 && scroll_clamp(&a, m + 5) == m && scroll_clamp(&a, m / 2) == m / 2);
    CHECK(scroll_shows(&a, 0, tops[0], hs[0]));                   /* at rest the first row is below the button */
    CHECK(LIST_INSET >= CORNER_H);
    CHECK(LIST_INSET + tops[10] + hs[10] - m == LIST_BOTTOM);     /* at the end the last row ends at the bottom */
    for (int i = 0; i < 11; i++) {
        float froms[] = {0, m / 2, m};
        for (int f = 0; f < 3; f++) {
            float at = scroll_to_show(&a, froms[f], tops[i], hs[i], FOCUS + 1);
            CHECK(at >= 0 && at <= m);
            CHECK(scroll_shows(&a, at, tops[i], hs[i]));          /* whole, never under the button */
            CHECK(LIST_INSET + tops[i] - at >= LIST_INSET);
        }
    }
    CHECK(!scroll_shows(&a, tops[4] + 10, tops[4], hs[4]));      /* scrolled past: partly under the button */
    scroll_area few = {LIST_BOTTOM, LIST_INSET, ROW_H};
    CHECK(scroll_max(&few) == 0 && scroll_to_show(&few, 0, 0, ROW_H, FOCUS + 1) == 0);

    /* the status line */
    char line[64];
    text_installed(LANG_DE, 3, line, sizeof line);
    CHECK(!strcmp(line, "3 Designs installiert"));
    text_installed(LANG_DE, 1, line, sizeof line);
    CHECK(!strcmp(line, "1 Design installiert"));
    text_installed(LANG_DE, 0, line, sizeof line);
    CHECK(!strcmp(line, "Kein Design installiert"));
    text_installed(LANG_EN, 3, line, sizeof line);
    CHECK(!strcmp(line, "3 themes installed"));
    text_installed(LANG_EN, 0, line, sizeof line);
    CHECK(!strcmp(line, "No themes installed"));
}

static void test_shown_and_wrap(void)
{
    static const size_t runs[] = {60, 120, 190, 200, 300, 1900};
    char url[2100], shown[3 * 2100 + 1], small[16];
    for (size_t r = 0; r < sizeof runs / sizeof *runs; r++) {
        size_t n = (size_t)snprintf(url, sizeof url, "http://a.example/");
        memset(url + n, 0x80, runs[r]);
        url[n + runs[r]] = 0;
        /* shown: every 0x80 as "%80", nothing dropped, valid UTF-8 */
        text_shown(shown, sizeof shown, url);
        CHECK(!strncmp(shown, "http://a.example/%80", 20));
        CHECK_EQ(strlen(shown), n + 3 * runs[r]);
        CHECK(valid_utf8_shown(shown));
        /* wrapped as on the QR screen (278 px, 22 px down to 16 px, four lines), raw and shown */
        ui_block b;
        int all = wrap_text(&b, url, 278, 22, 16, 4, 0, 25, fake_width, NULL);
        check_block(&b, all, 278);
        all = wrap_text(&b, shown, 278, 22, 16, 4, 0, 25, fake_width, NULL);
        check_block(&b, all, 278);
        /* a long text in the folder view's empty line: never wider, never silently shorter */
        all = wrap_text(&b, url, 290, 13, 10, 0, 0, 18, fake_width, NULL);
        check_block(&b, all, 290);
        if (all) {
            size_t total = 0;
            for (int i = 0; i < b.count; i++)
                total += strlen(b.line[i]);
            CHECK_EQ(total, strlen(url));
        }
    }
    /* kept: printable ASCII and valid UTF-8; shown as %XX: controls, DEL, C1, refused bytes */
    text_shown(shown, sizeof shown, "a\xc3\xbc\x01\x7f\xc2\x85\xc0\xaf\xe0\x80\x80z");
    CHECK(!strcmp(shown, "a\xc3\xbc%01%7F%C2%85%C0%AF%E0%80%80z"));
    /* too small: cut, marked */
    snprintf(url, sizeof url, "http://a.example/%s", "\x80\x80\x80\x80\x80\x80");
    text_shown(small, sizeof small, url);
    CHECK(strlen(small) < sizeof small && !strcmp(small + strlen(small) - 3, "..."));
}

/* ---------------------------------------------------------------- files and the record */

static size_t last_tick;

static void tick(void *ctx, uint64_t at, uint64_t size)
{
    (void)ctx;
    (void)size;
    last_tick = (size_t)at;
}

static void test_files_and_store(void)
{
    char path[1024];
    /* 0.9.2, for the folder /Themes: whether something is there; one folder made, never its parents; an empty file */
    snprintf(path, sizeof path, "%s/made", tmp_dir);
    CHECK(!files_exists(path));
    CHECK_EQ(files_make_dir(path), FILES_OK);
    CHECK(files_exists(path));
    CHECK_EQ(files_make_dir(path), FILES_ERROR);        /* there already */
    snprintf(path, sizeof path, "%s/none/inner", tmp_dir);
    CHECK_EQ(files_make_dir(path), FILES_ERROR);
    snprintf(path, sizeof path, "%s/none", tmp_dir);
    CHECK(!files_exists(path));
    snprintf(path, sizeof path, "%s/made/mark", tmp_dir);
    size_t mark_size = 1;
    uint8_t mark[1];
    CHECK_EQ(files_write(path, (const uint8_t *)"", 0), FILES_OK);
    CHECK(files_exists(path) && files_read(path, mark, sizeof mark, &mark_size) == FILES_OK && mark_size == 0);
    snprintf(path, sizeof path, "%s/bytes.bin", tmp_dir);
    uint8_t data[300], back[300];
    for (int i = 0; i < 300; i++)
        data[i] = (uint8_t)i;
    CHECK_EQ(files_write(path, data, sizeof data), FILES_OK);
    tp_source src;
    CHECK_EQ(files_open_source(path, &src, tick, NULL), FILES_OK);
    CHECK_EQ(src.size, 300);
    CHECK_EQ(src.read(src.ctx, 290, back, 10), 0);
    CHECK_EQ(last_tick, 10);                             /* bytes read so far, not where: the end read first */
    CHECK(!memcmp(back, data + 290, 10));
    CHECK_EQ(src.read(src.ctx, 0, back, 300), 0);       /* backwards: seeks */
    CHECK_EQ(last_tick, 310);
    CHECK(!memcmp(back, data, 300));
    CHECK_EQ(src.read(src.ctx, 300, back, 0), 0);
    CHECK(src.read(src.ctx, 299, back, 2) != 0);         /* past the end */
    files_close_source(&src);
    snprintf(path, sizeof path, "%s/none.bin", tmp_dir);
    CHECK_EQ(files_open_source(path, &src, NULL, NULL), FILES_MISSING);
    size_t n;
    CHECK_EQ(files_read(path, back, sizeof back, &n), FILES_MISSING);
    snprintf(path, sizeof path, "%s/bytes.bin", tmp_dir);
    CHECK_EQ(files_read(path, back, 299, &n), FILES_ERROR);
    CHECK_EQ(files_read(path, back, 300, &n), FILES_OK);
    CHECK_EQ(n, 300);

    static const uint16_t idx[] = {1};
    fixture_make(&fx, idx, 1, 1, 0);
    fixture_make(&fx2, idx, 1, 1, 1);
    reset_store(fx.tmd, fx.tmd_size);
    store_record r2[2];
    store_read(tmp_dir, r2);
    CHECK(r2[0].present && r2[0].result == FILES_OK && r2[0].size == fx.tmd_size);
    CHECK(!r2[1].present);
    CHECK_EQ(store_stage(tmp_dir, fx2.tmd, fx2.tmd_size), 0);
    store_read(tmp_dir, r2);
    CHECK(r2[1].present && r2[1].size == fx2.tmd_size && !memcmp(r2[1].tmd, fx2.tmd, fx2.tmd_size));
    CHECK_EQ(store_commit(tmp_dir), 0);
    store_read(tmp_dir, r2);
    CHECK(!r2[1].present && !memcmp(r2[0].tmd, fx2.tmd, fx2.tmd_size));
    CHECK(store_commit(tmp_dir) != 0);                   /* nothing staged */
}

/* ---------------------------------------------------------------- every message */

static int clean_text(const char *s)
{
    const unsigned char *p = (const unsigned char *)s;
    if (!*p)
        return 0;
    if (strstr(s, " ?") || strstr(s, ": :") || strstr(s, "::"))
        return 0;                                       /* a missing piece */
    for (; *p; p++) {
        if (*p == '%')
            return 0;                                   /* a format left unfilled */
        if (p[0] == 0xC3 && p[1] == 0x9F)
            return 0;                                   /* sharp s */
        if (p[0] == 0xC2 && (p[1] == 0xAB || p[1] == 0xBB || p[1] == 0xB7))
            return 0;                                   /* guillemets, middle dot */
        if (p[0] == 0xE2 && p[1] == 0x80 && (p[2] == 0x9C || p[2] == 0x9D || p[2] == 0x9E))
            return 0;                                   /* curly double quotes */
    }
    return 1;
}

static void each_text(void (*fn)(int lang, const char *what, const char *title, const char *body))
{
    char title[256], body[1024], what[64];
    for (int lang = LANG_EN; lang <= LANG_DE; lang++) {
        for (int id = 0; id < T_COUNT; id++) {
            snprintf(what, sizeof what, "text %d", id);
            char filled[512];
            if (id == T_INSTALLED_AS || id == T_THEMES_INSTALLED || id == T_DESIGN_IN_HOME || id == T_ROOM)
                snprintf(filled, sizeof filled, text(lang, id), 7u);
            else
                snprintf(filled, sizeof filled, text(lang, id), "sdmc:/Themes");
            fn(lang, what, "", filled);
        }
        for (int r = TP_OK; r <= TP_E_KIND + 1; r++) {
            snprintf(what, sizeof what, "result %d", r);
            fn(lang, what, "", text_result(lang, r));
        }
        for (int s = PACK_OK; s <= PACK_NOMEM; s++) {
            for (int d = TP_DIFF_VERSION; d <= TP_DIFF_SIZE; d++) {
                pack_view pv;
                memset(&pv, 0, sizeof pv);
                pv.state = s;
                pv.detail = TP_E_CATALOGUE;
                pv.diff.what = d;
                pv.diff.index = 3;
                pv.version = 2;
                pv.count = 100;
                pv.usable = 4;
                if (s != PACK_STALE && d != TP_DIFF_VERSION)
                    continue;
                text_pack(lang, &pv, title, sizeof title, body, sizeof body);
                snprintf(what, sizeof what, "pack %d/%d", s, d);
                fn(lang, what, title, body);
            }
        }
        for (int st = DL_BAD_URL; st <= DL_NOMEM; st++) {
            char body2[1024];
            text_download(lang, st, 403, TP_E_LZ11, "Could not resolve host: example", body2, sizeof body2);
            snprintf(what, sizeof what, "download %d", st);
            fn(lang, what, "", body2);
        }
        for (int st = DEL_TITLE; st <= DEL_SHUFFLE; st++) {
            text_deletion(lang, st, "T4 Name", title, sizeof title, body, sizeof body);
            snprintf(what, sizeof what, "deletion %d", st);
            fn(lang, what, title, body);
        }
        for (int o = DEL_DONE; o <= DEL_UNKNOWN; o++)
            for (int variant = 0; variant < 4; variant++) {
                deletion_judgement dj = {o, 2, variant & 1};
                text_deleted(lang, &dj, variant & 1, 0xD8A083FAu, variant >> 1, "T4 Name", title, sizeof title, body,
                             sizeof body);
                snprintf(what, sizeof what, "deleted %d/%d", o, variant);
                fn(lang, what, title, body);
            }
        for (int s = PLAN_OK; s <= PLAN_CORE; s++) {
            if (s == PLAN_DUPLICATE)
                continue;           /* the delete buttons, no text */
            install_plan plan;
            memset(&plan, 0, sizeof plan);
            plan.state = s;
            plan.index = 5;
            plan.duplicate = 2;
            plan.detail = TP_E_LZ11;
            text_plan(lang, &plan, body, sizeof body);
            snprintf(what, sizeof what, "plan %d", s);
            fn(lang, what, "", body);
        }
        for (int o = OUT_DONE; o <= OUT_UNKNOWN; o++) {
            for (int variant = 0; variant < 4; variant++) {
                judgement j = {o, 1, 0, 1, PENDING_NO};
                install_result res;
                memset(&res, 0, sizeof res);
                static const int steps[] = {STEP_CONTENT0, STEP_COMMIT1, STEP_CONTENT, STEP_TITLE2};
                res.failed = o == OUT_NOTHING_WRITTEN ? (variant == 0 ? STEP_MEASURE : variant == 1 ? STEP_STAGE
                                                                                                     : STEP_TITLE1)
                                                      : steps[variant];
                res.detail = TP_E_LZ11;
                if (o == OUT_DONE_UNCONFIRMED) {
                    j.seen = variant != 0;
                    j.lost = variant == 1 ? 2 : 0;
                    j.new_installed = variant == 3;
                    j.pending = variant == 3 ? PENDING_YES : PENDING_NO;
                }
                if (o == OUT_LOST)
                    j.lost = 2;
                if (variant == 2 && o != OUT_DONE_UNCONFIRMED)
                    j.pending = PENDING_YES;
                text_outcome(lang, &j, &res, "T4 Name", title, sizeof title, body, sizeof body);
                snprintf(what, sizeof what, "outcome %d/%d", o, variant);
                fn(lang, what, title, body);
            }
        }
    }
}

static void check_one(int lang, const char *what, const char *title, const char *body)
{
    if (!clean_text(body) || (title[0] && !clean_text(title)) || strlen(body) > 1000 || !strcmp(body, "?")) {
        failures++;
        fprintf(stderr, "text %s (%s): \"%s\" \"%s\"\n", what, lang == LANG_DE ? "de" : "en", title, body);
    }
    checks++;
}

static void print_one(int lang, const char *what, const char *title, const char *body)
{
    printf("[%s %s] %s%s%s\n", lang == LANG_DE ? "de" : "en", what, title, title[0] ? " | " : "", body);
}

/* ---------------------------------------------------------------- install on files */

typedef struct file_catalogue {
    const char *archive, *icons;
} file_catalogue;

static int file_archive(void *ctx, uint8_t *buf, size_t cap, size_t *size)
{
    return files_read(((file_catalogue *)ctx)->archive, buf, cap, size) ? -1 : 0;
}

static int file_icon(void *ctx, uint16_t index, uint8_t icon[TP_ICON_SIZE])
{
    char path[1024];
    size_t n;
    snprintf(path, sizeof path, "%s/%u.icn", ((file_catalogue *)ctx)->icons, index);
    return files_read(path, icon, TP_ICON_SIZE, &n) || n != TP_ICON_SIZE ? -1 : 0;
}

static void hex(const uint8_t *p, size_t n, char *out)
{
    for (size_t i = 0; i < n; i++)
        sprintf(out + 2 * i, "%02x", p[i]);
}

static int cmd_install(int argc, char **argv)
{
    const char *store = NULL, *archive = NULL, *icons = NULL, *zip = NULL, *out = NULL, *am_tmd = NULL;
    int fail_at = -1, not_usable[16], n_not = 0, music = 1;
    for (int i = 0; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "--store")) store = argv[i + 1];
        else if (!strcmp(argv[i], "--archive")) archive = argv[i + 1];
        else if (!strcmp(argv[i], "--icons")) icons = argv[i + 1];
        else if (!strcmp(argv[i], "--zip")) zip = argv[i + 1];
        else if (!strcmp(argv[i], "--out")) out = argv[i + 1];
        else if (!strcmp(argv[i], "--fail")) fail_at = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--am-tmd")) am_tmd = argv[i + 1];
        else if (!strcmp(argv[i], "--not-usable") && n_not < 16) not_usable[n_not++] = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--music")) music = strcmp(argv[i + 1], "off") != 0;
        else {
            fprintf(stderr, "unknown option %s\n", argv[i]);
            return 3;
        }
    }
    if (!store || !archive || !icons || !zip || !out) {
        fprintf(stderr, "install needs --store, --archive, --icons, --zip and --out\n");
        return 3;
    }
    static store_record recs[2];
    static am_view before, after;
    store_read(store, recs);
    if (am_tmd) {
        size_t size;
        uint8_t *p = slurp(am_tmd, &size);
        view_of_tmd(&before, p, size);
        free(p);
    } else {
        const store_record *base = recs[0].present ? &recs[0] : &recs[1];
        view_of_tmd(&before, base->tmd, base->size);
    }
    for (size_t k = 0; k < before.count; k++)
        for (int i = 0; i < n_not; i++)
            if (before.list[k].index == not_usable[i])
                before.usable[k] = 0;
    file_catalogue fc = {archive, icons};
    catalogue_reader cat = {&fc, file_archive, file_icon};
    pack_view pv;
    int state = pack_assess(&pv, &before, recs, store_mark(store), &cat);
    char title[256], body[2048], body_de[2048];
    if (state != PACK_OK) {
        text_pack(LANG_EN, &pv, title, sizeof title, body, sizeof body);
        printf("{\"pack\": %d, \"text\": \"%s\"}\n", state, body);
        pack_free(&pv);
        return 1;
    }
    tp_source src;
    tp_theme theme;
    static tp_package package;
    const tp_source *inner = NULL;
    int kind;
    char fallback[256];
    const char *slash = strrchr(zip, '/');
    scan_fallback(fallback, sizeof fallback, slash ? slash + 1 : zip);
    int r = files_open_source(zip, &src, NULL, NULL) ? TP_E_READ : tp_theme_in(&src, 1, &package, &inner, &kind);
    if (!r)
        r = tp_theme_open(&theme, inner, fallback);
    if (!r && !music)
        r = tp_theme_drop_bgm(&theme);
    if (r) {
        printf("{\"pack\": 0, \"theme\": \"%s\"}\n", tp_result_name(r));
        return 1;
    }
    install_plan plan;
    if (plan_install(&plan, &pv, &theme) != PLAN_OK) {
        text_plan(LANG_EN, &plan, body, sizeof body);
        printf("{\"pack\": 0, \"plan\": %d, \"duplicate\": %u, \"text\": \"%s\"}\n", plan.state, plan.duplicate, body);
        tp_theme_close(&theme);
        files_close_source(&src);
        pack_free(&pv);
        return 1;
    }
    fake k;
    importer imp = fake_importer(&k, fail_at, out);
    install_result res;
    install_run(&plan, &theme, &imp, store, NULL, &res);
    /* AM's view afterwards, as a console would report it if AM did what was committed */
    if (res.committed1) {
        view_of_tmd(&after, k.tmd, k.tmd_size);
        for (size_t i = 0; i < after.count; i++) {
            if (after.list[i].index == plan.index)
                after.usable[i] = (uint8_t)res.done;
            for (int x = 0; x < n_not; x++)
                if (after.list[i].index == not_usable[x])
                    after.usable[i] = 0;
        }
    } else {
        after = before;
    }
    judgement j;
    install_judge(&before, &after, &plan, &res, &j);
    text_outcome(LANG_EN, &j, &res, theme.name, title, sizeof title, body, sizeof body);
    char title_de[256];
    text_outcome(LANG_DE, &j, &res, theme.name, title_de, sizeof title_de, body_de, sizeof body_de);
    if (res.done) {
        char path[1024], digest[65], base_digest[65];
        uint8_t d[32];
        sha256(k.tmd, k.tmd_size, d);
        hex(d, 32, digest);
        sha256(pv.tmd, pv.tmd_size, d);
        hex(d, 32, base_digest);
        snprintf(path, sizeof path, "%s/delta.json", out);
        FILE *f = fopen(path, "w");
        if (f) {
            fprintf(f, "{\"made_by\": \"test_app\", \"base_tmd_sha256\": \"%s\", \"tmd_sha256\": \"%s\"}\n",
                    base_digest, digest);
            fclose(f);
        }
    }
    printf("{\"pack\": 0, \"plan\": 0, \"index\": %u, \"top\": %u, \"version\": %u, \"from_staged\": %d, "
           "\"done\": %d, \"failed\": %d, \"wrote\": %d, \"committed1\": %d, \"record_ok\": %d, \"calls\": %d, "
           "\"cancels\": %d, \"outcome\": %d, \"title\": \"%s\", \"title_de\": \"%s\"}\n",
           plan.index, plan.top, plan.delta.version, pv.from_staged, res.done, res.failed, res.wrote,
           res.committed1, res.record_ok, k.calls, k.cancels, j.outcome, title, title_de);
    tp_theme_close(&theme);
    files_close_source(&src);
    pack_free(&pv);
    return res.done ? 0 : res.wrote ? 2 : 1;
}

/* ---------------------------------------------------------------- downloads: names, addresses, the transfer */

static void test_download_names(void)
{
    static const char *const good[] = {"http://a.example/x.zip", "https://a.example/download/1", "HTTPS://A.EXAMPLE/"};
    static const char *const bad[] = {"ftp://a.example/x.zip", "file:///etc/passwd", "http://", "https:///x",
                                      "http://a b/x", "javascript:alert(1)", "a.example/x.zip", "http://a\tb/",
                                      ""};
    for (size_t i = 0; i < sizeof good / sizeof *good; i++)
        CHECK(download_url_ok(good[i]));
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++)
        CHECK(!download_url_ok(bad[i]));
    char longer[DOWNLOAD_URL_MAX + 32] = "http://a.example/";
    memset(longer + 17, 'a', DOWNLOAD_URL_MAX);
    longer[17 + DOWNLOAD_URL_MAX] = 0;
    CHECK(!download_url_ok(longer));

    char dir[1024], out[DOWNLOAD_NAME], path[1200];
    snprintf(dir, sizeof dir, "%s/names", tmp_dir);
    mkdir(dir, 0755);
    static const struct { const char *disposition, *url; int kind; const char *want; } cases[] = {
        {" attachment; filename=\"Fancy Theme.zip\"", "http://a/x", TP_KIND_ZIP, "Fancy Theme.zip"},
        {" attachment; filename=plain.zip", "http://a/x", TP_KIND_ZIP, "plain.zip"},
        {" attachment; filename=\"x.zip\"; filename*=UTF-8''Caf%C3%A9%20Theme.zip", "http://a/x", TP_KIND_ZIP,
         "Caf__ Theme.zip"},
        {NULL, "http://a/files/My%20Theme.zip?token=1#top", TP_KIND_ZIP, "My Theme.zip"},
        {NULL, "http://a/get/12345", TP_KIND_CIA, "12345.cia"},
        {NULL, "http://a/theme.zip", TP_KIND_CIA, "theme.cia"},          /* the content decides the extension */
        {NULL, "http://a/", TP_KIND_ZIP, "theme.zip"},
        {NULL, "http://a", TP_KIND_ZIP, "theme.zip"},
        {" attachment; filename=\"../../etc/x.zip\"", "http://a/", TP_KIND_ZIP, "_.._etc_x.zip"},
        {" attachment; filename=\"...hidden.zip\"", "http://a/", TP_KIND_ZIP, "hidden.zip"},
        {" attachment; filename=\"a/b\\c:d.zip\"", "http://a/", TP_KIND_ZIP, "a_b_c_d.zip"},
    };
    for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
        download_name(cases[i].disposition, cases[i].url, cases[i].kind, dir, out, sizeof out);
        if (strcmp(out, cases[i].want)) {
            failures++;
            fprintf(stderr, "download_name case %zu: \"%s\", want \"%s\"\n", i, out, cases[i].want);
        }
        checks++;
    }
    snprintf(path, sizeof path, "%s/theme.zip", dir);
    files_write(path, (const uint8_t *)"x", 1);
    download_name(NULL, "http://a/theme.zip", TP_KIND_ZIP, dir, out, sizeof out);
    CHECK(!strcmp(out, "theme (2).zip"));
    snprintf(path, sizeof path, "%s/theme (2).zip", dir);
    files_write(path, (const uint8_t *)"x", 1);
    download_name(NULL, "http://a/theme.zip", TP_KIND_ZIP, dir, out, sizeof out);
    CHECK(!strcmp(out, "theme (3).zip"));
    char very[400] = "http://a/";
    memset(very + 9, 'n', 300);
    download_name(NULL, very, TP_KIND_ZIP, dir, out, sizeof out);
    CHECK_EQ(strlen(out), 104);                         /* 100 bytes of name and ".zip" */
}

/* a theme's row as ui_row_theme draws it: the face, the icon, the name, the music symbol and the second line */
static void sim_row(order *o, float y, int music)
{
    sim_image(o, LIST_X, y, LIST_W, ROW_H + FACE_PAD_LOW);
    order_picture(o, (order_rect){LIST_X + ROW_ICON_X, y + (ROW_H - ROW_ICON) / 2, ROW_ICON, ROW_ICON});
    order_put('T');
    if (music) {
        sim_image(o, LIST_X + ROW_TEXT_X, y + 30, 9, 11);
        order_put('T');
    }
}

static void test_order(void)
{
    order o = {.draw = order_drawn};
    /* a list: rows with and without music, then what lies over them (the corner button with its path, the bar) */
    order_log[0] = 0;
    for (int i = 0; i < 5; i++)
        sim_row(&o, LIST_INSET + i * (ROW_H + LIST_GAP), i & 1);
    sim_image(&o, 0, 0, 120, CORNER_H);
    order_put('T');
    sim_image(&o, 0, BAR_Y, BOTTOM_W, SCREEN_H - BAR_Y);
    order_put('T');
    order_flush(&o);
    CHECK(!strstr(order_log, "PT"));            /* never letters directly after a picture */
    CHECK_EQ(order_count('P'), 5);              /* every icon is drawn, once */
    CHECK_EQ(o.count, 0);
    /* each row's texts come before its icon: by the first icon, the first row's face and name are out */
    CHECK(!strncmp(order_log, "IT", 2) && strchr(order_log, 'P') > order_log + 1);
    printf("order of a list: %s\n", order_log);

    /* a picture and what covers it: the badge on it is drawn after it, the name under it before it */
    order_log[0] = 0;
    order_picture(&o, (order_rect){GAL_PICTURE_X, GAL_PICTURE_Y, GAL_PICTURE, GAL_PICTURE});
    sim_image(&o, GAL_PICTURE_X + GAL_PICTURE - GAL_BADGE, GAL_PICTURE_Y + GAL_PICTURE - GAL_BADGE, GAL_BADGE, GAL_BADGE);
    order_put('T');
    order_flush(&o);
    CHECK(!strcmp(order_log, "PIT"));
    CHECK(order_last.x == GAL_PICTURE_X && order_last.y == GAL_PICTURE_Y && order_last.w == GAL_PICTURE);
    order_log[0] = 0;
    order_picture(&o, (order_rect){GAL_PICTURE_X, GAL_PICTURE_Y, GAL_PICTURE, GAL_PICTURE});
    order_put('T');                             /* no badge: the name, then the picture when the screen is done */
    CHECK(!strcmp(order_log, "T") && o.count == 1);
    order_flush(&o);
    CHECK(!strcmp(order_log, "TP"));
    order_flush(&o);                            /* nothing waits: nothing is drawn */
    CHECK(!strcmp(order_log, "TP"));
}

/* the bar's glide by the clock (progress.h): 18 % of what is left in a sixtieth of a second, the same way in one
 * frame or in several, nothing in no time, there after a long time */
static void test_glide(void)
{
    float one = progress_glide(0.0f, 1.0f, 18, 1000 / 60);
    CHECK(one > 0.17f && one < 0.18f);
    CHECK(progress_glide(0.4f, 1.0f, 18, 0) == 0.4f);
    CHECK(progress_glide(0.4f, 0.4f, 18, 500) == 0.4f);
    CHECK(progress_glide(0.0f, 1.0f, 18, 5000) > 0.9999f && progress_glide(0.0f, 1.0f, 18, 5000) <= 1.0001f);
    float in_two = progress_glide(progress_glide(0.2f, 0.9f, 18, 20), 0.9f, 18, 20);
    CHECK(fabsf(in_two - progress_glide(0.2f, 0.9f, 18, 40)) < 0.0001f);
    /* to within the snap (a thousandth) of the end in about 0.6 s, whatever the frames' length */
    for (unsigned frame = 8; frame <= 128; frame *= 2) {
        float shown = 0;
        unsigned ms = 0;
        while (1.0f - shown >= 0.001f && ms < 5000) {
            shown = progress_glide(shown, 1.0f, 18, frame);
            ms += frame;
        }
        CHECK(ms >= 576 && ms <= 580 + frame);
    }
    /* never past the target, also where the share of the way rounds to all of it (a very long frame, a share of
     * 100): one step too many would be shown otherwise */
    CHECK(progress_glide(0.08772f, 0.96999997f, 18, 1456) <= 0.96999997f);
    for (int from = 0; from <= 100; from += 5)
        for (int to = from; to <= 100; to += 7) {
            float a = (float)from / 100.0f, b = (float)to / 100.0f - 0.00000003f;
            CHECK(progress_glide(a, b, 18, 3000) <= b && progress_glide(a, b, 18, 3000) > b - 0.0001f);
            CHECK(progress_glide(a, b, 100, 16) <= b && progress_glide(a, b, 100, 16) > b - 0.0001f);
            CHECK(progress_glide(b, a, 100, 16) >= a && progress_glide(b, a, 100, 16) < a + 0.0001f);
        }
}

/* 0.9.2, item 25: the installing screen's four marks on the one clock (progress.h) */
static void test_marks(void)
{
    progress_marks m;
    const unsigned ms = 200;
    progress_marks_start(&m);
    /* a new install: the first step under way, the others to come, nothing moving, so they come in with the screen */
    CHECK(progress_mark(&m, 0, 0) == 1 && progress_mark(&m, 1, 0) == 0 && progress_mark(&m, 3, 99999) == 0);
    CHECK(!progress_marks_moving(&m, 0) && progress_mark(&m, -1, 0) == 0 && progress_mark(&m, 4, 0) == 0);
    progress_marks_show(&m, 0, 500, ms);        /* the bar still in its first step: nothing to do */
    CHECK(!progress_marks_moving(&m, 500) && progress_mark(&m, 0, 600) == 1);
    /* the first step is done at 1000: its mark to done, the next one under way; both there 200 ms later, no sooner */
    progress_marks_show(&m, 1, 1000, ms);
    CHECK(progress_mark(&m, 0, 1000) == 1 && progress_mark(&m, 1, 1000) == 0 && progress_marks_moving(&m, 1000));
    CHECK(progress_mark(&m, 0, 1100) == 1.5f && progress_mark(&m, 1, 1100) == 0.5f && progress_mark(&m, 2, 1100) == 0);
    CHECK(progress_mark(&m, 0, 1199) < 2 && progress_mark(&m, 0, 1200) == 2 && progress_mark(&m, 1, 1200) == 1);
    CHECK(!progress_marks_moving(&m, 1200));
    progress_marks_show(&m, 1, 1300, ms);       /* told again: nothing starts anew */
    CHECK(!progress_marks_moving(&m, 1300));
    /* the second step is done 48 ms after its mark set out: sent on from where it is, without a jump */
    progress_marks_start(&m);
    progress_marks_show(&m, 1, 1000, ms);
    float before = progress_mark(&m, 1, 1048);
    progress_marks_show(&m, 2, 1048, ms);
    CHECK(before > 0 && before < 1 && progress_mark(&m, 1, 1048) == before);
    CHECK(progress_mark(&m, 1, 1248) == 2 && progress_mark(&m, 0, 1200) == 2 && progress_mark(&m, 2, 1248) == 1);
    /* every step done at once: all four marks done one motion later; in no frame of 16 ms does a mark move more than
     * 1.5 times the even step of its way, and it never passes where it is sent */
    progress_marks_start(&m);
    progress_marks_show(&m, 4, 2000, ms);
    float last[4] = {1, 0, 0, 0};
    for (unsigned t = 2000; t <= 2208; t += 16)
        for (int k = 0; k < 4; k++) {
            float now = progress_mark(&m, k, t), way = k ? 2 : 1;
            CHECK(now >= last[k] && now <= 2 && now - last[k] <= way * 1.5f * 16 / ms + 0.0001f);
            last[k] = now;
        }
    CHECK(last[0] == 2 && last[1] == 2 && last[2] == 2 && last[3] == 2 && !progress_marks_moving(&m, 2200));
    /* never ahead of the bar: with done steps done, no mark after them is past "to come", and theirs is not past
     * "under way" */
    progress_marks_start(&m);
    for (int done = 0; done <= 4; done++) {
        progress_marks_show(&m, done, 3000 + 100 * (unsigned)done, ms);
        for (unsigned t = 3000 + 100 * (unsigned)done; t < 3100 + 100 * (unsigned)done; t += 10)
            for (int k = 0; k < 4; k++)
                CHECK(progress_mark(&m, k, t) <= (k < done ? 2 : k == done ? 1 : 0));
    }

    /* what a mark shows (item 29). At rest: to come, the flat circle alone, on the card; under way, the bead alone,
     * breathing between STEP_DROP_NOW and STEP_PULSE_PX more, in front of the card; done, the check alone at its
     * own size, on the card again */
    progress_look l = progress_mark_look(0, 0.7f);
    CHECK(l.later == 1 && l.now == 0 && l.done == 0 && l.raised == 0);
    l = progress_mark_look(1, 0);
    CHECK(l.later == 0 && l.now == 1 && l.done == 0 && l.now_px == STEP_DROP_NOW && l.raised == 1);
    l = progress_mark_look(1, 1);
    CHECK(l.now == 1 && l.now_px == STEP_DROP_NOW + STEP_PULSE_PX && l.raised == 1);
    l = progress_mark_look(2, 0.3f);
    CHECK(l.later == 0 && l.now == 0 && l.done == 1 && l.done_px == STEP_DROP_DONE && l.raised == 0);
    l = progress_mark_look(-3, 0);
    CHECK(l.later == 1 && l.now == 0);
    l = progress_mark_look(9, 0);
    CHECK(l.done == 1 && l.now == 0);
    /* on the way: the bead grows from the circle's size, the check from the bead's and has its own from
     * STEP_GROWN_PCT of the way on; the image coming in is as solid as the way is long, the one underneath goes
     * later than that (the card does not show through half way: both together cover more than either alone would
     * in a plain cross-fade) and is gone at the end */
    l = progress_mark_look(0.001f, 0);
    CHECK(l.now_px > STEP_DROP_LATER && l.now_px < STEP_DROP_LATER + 0.1f && l.now < 0.01f && l.later > 0.99f);
    l = progress_mark_look(0.5f, 0);
    CHECK(l.now == 0.5f && l.later == 0.75f && l.now_px == (STEP_DROP_LATER + STEP_DROP_NOW) / 2.0f && l.raised == 0.5f);
    l = progress_mark_look(1.5f, 0);
    CHECK(l.done == 0.5f && l.now == 0.75f && l.raised == 0.5f && l.done_px > STEP_DROP_NOW && l.done_px < STEP_DROP_DONE);
    l = progress_mark_look(1 + STEP_GROWN_PCT / 100.0f, 0);
    CHECK(l.done_px == STEP_DROP_DONE && l.done < 1);
    l = progress_mark_look(1.001f, 0);
    CHECK(l.done_px < STEP_DROP_NOW + 0.1f && l.done < 0.01f && l.now > 0.99f);
    /* nothing appears from one frame to the next: in steps of a hundredth of a level (a mark's whole way is some 12
     * frames) no opacity moves by more than 0.03, no size by more than 0.2 px, the depth by no more than 0.02; and
     * across the two levels where one image hands over to the next */
    progress_look was = progress_mark_look(0, 0.4f);
    int steady = 1;
    for (int i = 1; i <= 200; i++) {
        l = progress_mark_look(i / 100.0f, 0.4f);
        steady &= fabsf(l.later - was.later) <= 0.03f && fabsf(l.now - was.now) <= 0.03f &&
                  fabsf(l.done - was.done) <= 0.03f && fabsf(l.raised - was.raised) <= 0.02f &&
                  (l.now <= 0 || was.now <= 0 || fabsf(l.now_px - was.now_px) <= 0.2f) &&
                  (l.done <= 0 || was.done <= 0 || fabsf(l.done_px - was.done_px) <= 0.2f);
        steady &= l.later >= 0 && l.later <= 1 && l.now >= 0 && l.now <= 1 && l.done >= 0 && l.done <= 1 &&
                  l.raised >= 0 && l.raised <= 1;
        was = l;
    }
    CHECK(steady);
    /* the breath alone moves the bead by no more than STEP_PULSE_PX, whatever the level */
    for (int i = 0; i <= 20; i++) {
        progress_look a = progress_mark_look(i / 10.0f, 0), b = progress_mark_look(i / 10.0f, 1);
        CHECK(b.now_px - a.now_px >= 0 && b.now_px - a.now_px <= STEP_PULSE_PX && a.done_px == b.done_px &&
              a.now == b.now && a.raised == b.raised);
    }
}

/* 0.9.2, the log's line for a movement on the screens (motion.h) */
static void test_motion(void)
{
    motion m = {0};
    motion_result r = {0};
    CHECK(!motion_frame(&m, 1000, 16, 0, &r) && !m.active);     /* nothing watched: a frame tells nothing */
    /* 180 ms at 60 frames a second, gaps of 16 and 17 ms; its first frame came 40 ms after the one before */
    CHECK(!motion_frame(&m, 1040, 40, 180, &r) && m.active);
    uint64_t now = 1040;
    unsigned n = 1;
    int ended = 0;
    while (!ended && n < 100) {
        unsigned gap = 16 + n % 2;
        now += gap;
        n++;
        ended = motion_frame(&m, now, gap, 0, &r);
    }
    CHECK(ended && !m.active && !r.cut && r.first == 40 && r.frames == n && r.slowest == 17);
    CHECK(r.ms == now - 1040 && r.ms >= 180 && r.ms < 180 + 17);
    CHECK_EQ(r.frames, 12);
    CHECK(!motion_frame(&m, now + 16, 16, 0, &r));              /* over: the next frame tells nothing */
    /* a lost frame is the slowest gap, and one frame fewer in the same time */
    now = 5000;
    CHECK(!motion_frame(&m, now, 16, 300, &r));
    for (n = 1, ended = 0; !ended && n < 100; n++) {
        unsigned gap = n == 5 ? 33 : 16;
        now += gap;
        ended = motion_frame(&m, now, gap, 0, &r);
    }
    CHECK(ended && r.slowest == 33 && r.first == 16 && r.frames == n && r.ms >= 300 && !r.cut);
    /* the next movement cuts one short at its own first frame, and is watched from there */
    CHECK(!motion_frame(&m, 9000, 16, 300, &r));
    CHECK(!motion_frame(&m, 9016, 16, 0, &r));
    CHECK(motion_frame(&m, 9050, 34, 200, &r));
    CHECK(r.cut && r.frames == 3 && r.ms == 50 && r.slowest == 34 && r.first == 16);
    CHECK(m.active && m.began == 9050 && m.length == 200 && m.first == 34);
    CHECK(!motion_frame(&m, 9150, 100, 0, &r) && motion_frame(&m, 9250, 100, 0, &r));
    CHECK(!r.cut && r.frames == 3 && r.ms == 200 && r.slowest == 100 && r.first == 34);
    /* a movement ending in the frame the next begins with is whole, not cut */
    CHECK(!motion_frame(&m, 10000, 16, 100, &r));
    CHECK(motion_frame(&m, 10100, 100, 100, &r) && !r.cut && r.ms == 100 && m.active && m.began == 10100);
}

static void test_may_clean(void)
{
    judgement j = {OUT_DONE, 1, 0, 1, PENDING_NO};
    install_result res;
    memset(&res, 0, sizeof res);
    res.done = res.wrote = res.tmd_accepted = res.committed1 = res.record_ok = 1;
    CHECK(install_may_clean(&j, &res));
    res.record_ok = 0;                                  /* the record is only in installed.tmd.new */
    CHECK(!install_may_clean(&j, &res));
    res.record_ok = 1;
    for (int o = OUT_DONE_UNCONFIRMED; o <= OUT_UNKNOWN; o++) {
        j.outcome = o;
        CHECK(!install_may_clean(&j, &res));
    }
    j.outcome = OUT_DONE;
    res.done = 0;
    CHECK(!install_may_clean(&j, &res));
}

/* The download's bar as main.c shows it: progress_keep over done / total; full_at: the bytes when it first was full. */
typedef struct dl_watch {
    int cancel;
    progress_run run;
    uint64_t full_at;
} dl_watch;

static int watch_tick(void *ctx, uint64_t done, uint64_t total)
{
    dl_watch *w = ctx;
    if (total && progress_keep(&w->run, (float)done / (float)total) >= 1.0f && !w->full_at)
        w->full_at = done;
    return w->cancel && done > 0;
}

/* test_app download URL PART DIR [CA_FILE] [--cancel]: one JSON line with the result; the part file's state too. */
static int cmd_download(int argc, char **argv)
{
    if (argc < 3)
        return 3;
    dl_watch w = {0};
    w.cancel = argc >= 4 && !strcmp(argv[argc - 1], "--cancel");
    progress_start(&w.run);
    const char *ca = argc >= 4 + w.cancel ? argv[3] : NULL;
    if (curl_global_init(CURL_GLOBAL_DEFAULT))
        return 3;
    download_progress progress = {&w, watch_tick};
    download_result res;
    download(argv[0], argv[1], argv[2], ca, "3DStockTheme/0.0.0-test", &progress, &res);
    printf("{\"status\": %d, \"http\": %ld, \"kind\": %d, \"detail\": \"%s\", \"file\": \"%s\", "
           "\"part_left\": %d, \"bytes\": %llu, \"full_at\": %llu, \"first_ms\": %u, \"body_ms\": %u, "
           "\"pieces\": %u, \"largest\": %u, \"file_ms\": %u, \"rcvbuf_before\": %d, \"rcvbuf_after\": %d, \"file_buffer\": %u}\n",
           res.status, res.http, res.kind, tp_result_name(res.detail), res.file, exists(argv[1]),
           (unsigned long long)res.bytes, (unsigned long long)w.full_at, res.first_ms, res.body_ms, res.pieces,
           res.largest, res.file_ms, res.rcvbuf_before, res.rcvbuf_after, res.file_buffer);
    curl_global_cleanup();
    return 0;
}

int main(int argc, char **argv)
{
    if (argc >= 2 && !strcmp(argv[1], "install"))
        return cmd_install(argc - 2, argv + 2);
    if (argc == 2 && !strcmp(argv[1], "texts")) {
        each_text(print_one);
        return 0;
    }
    if (argc >= 2 && !strcmp(argv[1], "download"))
        return cmd_download(argc - 2, argv + 2);
    if (argc != 5 || strcmp(argv[1], "unit")) {
        fprintf(stderr, "usage: test_app unit ZIP_WITH_BGM ZIP_WITHOUT_BGM THEME_CIA | install ... | download ... | "
                        "texts\n");
        return 3;
    }
    const char *base = getenv("TMPDIR");
    snprintf(tmp_dir, sizeof tmp_dir, "%s/test-app-XXXXXX", base && *base ? base : "/tmp");
    if (!mkdtemp(tmp_dir)) {
        perror("mkdtemp");
        return 3;
    }
    test_names();
    test_files_and_store();
    test_assess();
    test_plan_and_install(argv[2], argv[3]);
    test_mark();
    test_reviewer_cases(argv[3]);
    test_judge();
    test_deletion(argv[3]);
    test_designs();
    test_popup();
    test_step_line();
    test_scan(argv[2], argv[3], argv[4]);
    test_progress(argv[2], argv[4]);
    test_check_times(argv[2], argv[4]);
    test_shown_and_wrap();
    test_spaces_and_layout();
    test_list();
    test_download_names();
    test_may_clean();
    test_order();
    test_motion();
    test_glide();
    test_marks();
    each_text(check_one);
    char cmd[1100];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", tmp_dir);
    if (system(cmd))
        fprintf(stderr, "could not remove %s\n", tmp_dir);
    return check_summary("test_app");
}
