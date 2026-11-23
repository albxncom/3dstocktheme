/* The Badges part's work (app/source/badge_flow.c) on the host, against a fake of HOME's two badge files: a folder
 * with BadgeData.dat and BadgeMngFile.dat that can be told to fail (an open, a read, the n-th write, a write that
 * is lost, a byte that changes behind the application's back, a close), and a card that can be told to fail (a
 * write, a card that is full, a copy that reads back differently). Every case checks what is left in the two files
 * byte for byte. The collection, the PNGs and the zips are generated (host/test_badge.c's helpers, included); no
 * badge art from anywhere.
 *
 *   test_badgeflow unit                          prints "test_badgeflow: N checks, M failed"; exit 1 if any failed
 *   test_badgeflow download URL PART DIR [CA]    one JSON line with badge_download's result (host/test_badgeflow.py)
 *   test_badgeflow fixture DIR                   a card folder for pictures, made anew (DIR is removed first):
 *                                                DIR/Badges with generated sets, DIR/extdata with a collection
 */
#define _DARWIN_C_SOURCE
#define main badge_cli_main
#include "test_badge.c"
#undef main
#include <unistd.h>

#include <curl/curl.h>

#include "badge_dir_store.h"
#include "badge_download.h"
#include "badge_flow.h"
#include "badge_text.h"

/* ---------------------------------------------------------------- the fake store: a folder with the two files */

#define TRACE 512
typedef struct fake_store {
    char dir[400];
    FILE *f[BADGE_FILE_COUNT];
    int opens, reads, writes, closes;           /* calls so far */
    int opens_read, opens_write;                /* ... of them by mode */
    int mode;                                   /* the last open's */
    int refused;                                /* writes through an open to read only: refused, nothing written */
    int fail_open;                              /* the code every open returns (0: opens) */
    int fail_open_write;                        /* the code every open to read and write returns (0: opens) */
    int fail_read_at, fail_write_at;            /* that call fails (counted from 1; 0: none) */
    int dead_from_write;                        /* from that write on every call fails: the power is gone */
    int lose_write_at;                          /* that write says yes and writes nothing */
    int flip_after_write, flip_file;            /* after that write a byte of flip_file at flip_offset changes */
    uint32_t flip_offset;
    int fail_close_at;
    int writes_fail_from;                       /* from that write on every write fails (reads go on) */
    void (*on_open)(struct fake_store *s);
    struct {
        int file;
        uint32_t offset, size;
    } trace[TRACE];
    int traced;
} fake_store;

#define CODE_OPEN 0xC8804478    /* not found (FS, summary 4): what a missing file answers */
#define CODE_IO 0xC8A04555      /* any other code: the flow only passes it on */
#define CODE_LOCKED 0xC92044E6  /* in use (FS, description 230): the console's answer while HOME holds the files */
#define CODE_READ_ONLY 0xE0C04556       /* the fake's answer to a write through an open to read only */

static const char *const STORE_FILE[BADGE_FILE_COUNT] = {"BadgeData.dat", "BadgeMngFile.dat"};

static int fs_dead(const fake_store *s)
{
    return s->dead_from_write && s->writes >= s->dead_from_write;
}

static int fs_open(void *ctx, int mode)
{
    fake_store *s = ctx;
    s->opens++;
    ++*(mode == BADGE_OPEN_WRITE ? &s->opens_write : &s->opens_read);
    s->mode = mode;
    if (s->on_open)
        s->on_open(s);
    if (s->fail_open || fs_dead(s))
        return s->fail_open ? s->fail_open : (int)CODE_IO;
    if (s->fail_open_write && mode == BADGE_OPEN_WRITE)
        return s->fail_open_write;
    for (int i = 0; i < BADGE_FILE_COUNT; i++) {
        char *path = join(s->dir, STORE_FILE[i]);
        s->f[i] = fopen(path, mode == BADGE_OPEN_WRITE ? "r+b" : "rb");
        free(path);
        if (!s->f[i])
            return (int)CODE_OPEN;
    }
    return 0;
}

static int fs_size(void *ctx, int file, uint64_t *size)
{
    fake_store *s = ctx;
    if (fs_dead(s) || fseek(s->f[file], 0, SEEK_END))
        return (int)CODE_IO;
    *size = (uint64_t)ftell(s->f[file]);
    return 0;
}

static int fs_read(void *ctx, int file, uint32_t offset, uint8_t *buf, uint32_t size)
{
    fake_store *s = ctx;
    if (++s->reads == s->fail_read_at || fs_dead(s))
        return (int)CODE_IO;
    if (fseek(s->f[file], (long)offset, SEEK_SET) || fread(buf, 1, size, s->f[file]) != size)
        return (int)CODE_IO;
    return 0;
}

static int fs_write(void *ctx, int file, uint32_t offset, const uint8_t *buf, uint32_t size)
{
    fake_store *s = ctx;
    if (s->mode != BADGE_OPEN_WRITE) {
        s->refused++;
        return (int)CODE_READ_ONLY;
    }
    s->writes++;
    if (s->writes == s->fail_write_at || fs_dead(s) || (s->writes_fail_from && s->writes >= s->writes_fail_from))
        return (int)CODE_IO;
    if (s->traced < TRACE) {
        s->trace[s->traced].file = file;
        s->trace[s->traced].offset = offset;
        s->trace[s->traced++].size = size;
    }
    if (s->writes != s->lose_write_at &&
        (fseek(s->f[file], (long)offset, SEEK_SET) || fwrite(buf, 1, size, s->f[file]) != size ||
         fflush(s->f[file])))
        return (int)CODE_IO;
    if (s->writes == s->flip_after_write) {
        uint8_t b;
        FILE *f = s->f[s->flip_file];
        fseek(f, (long)s->flip_offset, SEEK_SET);
        if (fread(&b, 1, 1, f) != 1)
            return (int)CODE_IO;
        b ^= 0x40;
        fseek(f, (long)s->flip_offset, SEEK_SET);
        fwrite(&b, 1, 1, f);
        fflush(f);
    }
    return 0;
}

static int fs_close(void *ctx)
{
    fake_store *s = ctx;
    int bad = 0;
    for (int i = 0; i < BADGE_FILE_COUNT; i++) {
        if (s->f[i])
            bad |= fclose(s->f[i]) != 0;
        s->f[i] = NULL;
    }
    return ++s->closes == s->fail_close_at || bad || fs_dead(s) ? (int)CODE_IO : 0;
}

/* ---------------------------------------------------------------- the card that can fail */

typedef struct fake_card {
    const char *fail_put;       /* a put to a path with this in it fails */
    const char *flip_get;       /* a get from such a path returns a changed byte */
    long room;                  /* bytes the card still takes (-1: any): a put over it writes what fits and fails */
    int puts, gets;
} fake_card;

static int fc_put(void *ctx, const char *path, uint64_t offset, const uint8_t *buf, size_t size)
{
    fake_card *c = ctx;
    c->puts++;
    if (c->fail_put && strstr(path, c->fail_put))
        return -1;
    if (c->room >= 0 && (long)size > c->room) {
        BADGE_CARD_STDIO.put(NULL, path, offset, buf, (size_t)c->room);
        c->room = 0;
        return -1;
    }
    if (c->room >= 0)
        c->room -= (long)size;
    return BADGE_CARD_STDIO.put(NULL, path, offset, buf, size);
}

static int fc_get(void *ctx, const char *path, uint64_t offset, uint8_t *buf, size_t size, size_t *got)
{
    fake_card *c = ctx;
    c->gets++;
    int r = BADGE_CARD_STDIO.get(NULL, path, offset, buf, size, got);
    if (!r && *got && c->flip_get && strstr(path, c->flip_get))
        buf[*got / 2] ^= 1;
    return r;
}

/* ---------------------------------------------------------------- one test's world */

static char root[300];          /* a temporary folder: extdata/ (the store), card/ (the application's), Badges/ */
static fake_store store;
static fake_card card;
static badge_store the_store = {&store, fs_open, fs_size, fs_read, fs_write, fs_close};
static badge_card the_card = {&card, fc_put, fc_get};
static char card_dir[400], badges_dir[400];
static char log_text[1 << 16];
static int steps_seen[BSTEP_DONE + 1], step_back;
static uint64_t step_done[BSTEP_DONE + 1];

static void log_line(void *ctx, const char *line)
{
    (void)ctx;
    size_t n = strlen(log_text);
    snprintf(log_text + n, sizeof log_text - n, "%s\n", line);
}

static void step_seen(void *ctx, int step, uint64_t done, uint64_t total)
{
    (void)ctx;
    steps_seen[step]++;
    if (done > total || (steps_seen[step] > 1 && done < step_done[step]))
        step_back++;            /* a bar that goes back or past its end */
    step_done[step] = done;
}

static badge_env env = {&the_store, &the_card, card_dir, log_line, step_seen, NULL};

static void rm_rf(const char *path)
{
    char cmd[700];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", path);
    if (system(cmd))
        failures++;
}

static void put_file(const char *dir, const char *name, const void *buf, size_t size)
{
    char *path = join(dir, name);
    if (write_file(path, buf, size))
        failures++;
    free(path);
}

static uint8_t *get_file(const char *dir, const char *name, size_t *size)
{
    char *path = join(dir, name);
    uint8_t *buf = NULL;
    *size = 0;
    if (read_file(path, &buf, size))
        buf = NULL;
    free(path);
    return buf;
}

static int exists(const char *dir, const char *name)
{
    char *path = join(dir, name);
    struct stat st;
    int there = stat(path, &st) == 0;
    free(path);
    return there;
}

/* How many entries of dir have suffix in their names (NULL: all, without the dot entries). */
static int count_in(const char *dir, const char *suffix)
{
    DIR *d = opendir(dir);
    int n = 0;
    if (!d)
        return 0;
    for (struct dirent *e; (e = readdir(d)) != NULL;)
        if (e->d_name[0] != '.' && (!suffix || strstr(e->d_name, suffix)))
            n++;
    closedir(d);
    return n;
}

/* A PNG of w x h generated pixels into dir. */
static void put_png(const char *dir, const char *name, uint32_t w, uint32_t h, uint32_t seed)
{
    size_t size;
    uint8_t *rgba = pattern(w, h, seed), *png = make_png(w, h, rgba, &size);
    put_file(dir, name, png, size);
    free(rgba);
    free(png);
}

static tree original;           /* the collection the store started with */

/* A new world: the store holds a generated collection of n_sets sets with these sizes; the card is empty; Badges/
 * holds the set folder "Tiere" (three badges from two files, and a set icon), "Blumen.zip" (two badges) and
 * "Stern.png" (one). */
static void world(const uint32_t *sizes, uint32_t n_sets)
{
    char *ext = join(root, "extdata"), *tiere;
    rm_rf(root);
    mkdir(root, 0755);
    mkdir(ext, 0755);
    snprintf(card_dir, sizeof card_dir, "%s/card", root);
    snprintf(badges_dir, sizeof badges_dir, "%s/Badges", root);
    mkdir(badges_dir, 0755);
    free(original.data);
    free(original.mng);
    make_collection(&original, sizes, n_sets, 2);
    put_file(ext, STORE_FILE[0], original.data, original.data_size);
    put_file(ext, STORE_FILE[1], original.mng, original.mng_size);
    memset(&store, 0, sizeof store);
    snprintf(store.dir, sizeof store.dir, "%s", ext);
    memset(&card, 0, sizeof card);
    card.room = -1;
    log_text[0] = 0;
    memset(steps_seen, 0, sizeof steps_seen);
    step_back = 0;
    tiere = join(badges_dir, "Tiere");
    mkdir(tiere, 0755);
    put_png(tiere, "Fuchs.png", 64, 64, 1);
    put_png(tiere, "Voegel.png", 128, 64, 2);
    put_png(tiere, "_seticon.png", 48, 48, 3);
    put_file(tiere, "._Fuchs.png", "x", 1);     /* what macOS leaves on a card: passed over */
    size_t za, zb, zsize;
    uint8_t *a = pattern(64, 64, 4), *b = pattern(64, 64, 5), *pa = make_png(64, 64, a, &za),
            *pb = make_png(64, 64, b, &zb);
    const char *names[2] = {"Rose.png", "Tulpe.png"};
    const uint8_t *datas[2] = {pa, pb};
    size_t sizes_z[2] = {za, zb};
    uint8_t *zip = make_zip(names, datas, sizes_z, 2, 0, &zsize);
    put_file(badges_dir, "Blumen.zip", zip, zsize);
    put_png(badges_dir, "Stern.png", 64, 64, 6);
    free(a);
    free(b);
    free(pa);
    free(pb);
    free(zip);
    free(tiere);
    free(ext);
}

/* The store's two files as they are now. */
static void store_now(tree *t)
{
    t->data = get_file(store.dir, STORE_FILE[0], &t->data_size);
    t->mng = get_file(store.dir, STORE_FILE[1], &t->mng_size);
}

static int store_is(const tree *want)
{
    tree now;
    store_now(&now);
    int same = now.data && now.mng && now.data_size == want->data_size && now.mng_size == want->mng_size &&
               !memcmp(now.data, want->data, want->data_size) && !memcmp(now.mng, want->mng, want->mng_size);
    free(now.data);
    free(now.mng);
    return same;
}

static void free_tree(tree *t)
{
    free(t->data);
    free(t->mng);
    memset(t, 0, sizeof *t);
}

/* A copy in new buffers (test_badge.c's copy_tree fills buffers that are there). */
static void dup_tree(tree *dst, const tree *src)
{
    dst->data_size = BADGE_DATA_SIZE;
    dst->mng_size = BADGE_MNG_SIZE;
    dst->data = malloc(BADGE_DATA_SIZE);
    dst->mng = malloc(BADGE_MNG_SIZE);
    copy_tree(dst, src);
}

static int load(const char *name, int kind, badge_source *src)
{
    char *path = join(badges_dir, name);
    int r = badge_source_load(path, kind, src);
    free(path);
    return r;
}

/* Appends the set `name` of Badges/ through the flow. */
static int append(const char *name, int kind, badge_flow_result *res)
{
    badge_source src;
    int r = load(name, kind, &src);
    CHECK_EQ(r, BADGE_OK);
    int out = badge_flow_append(&env, &src, res);
    CHECK_EQ(out, res->outcome);
    badge_source_free(&src);
    return out;
}

/* What the core alone makes of the same append with the next IDs set_id and badge_id (this application's first:
 * 0x7800 and 0x5800), on copies: the bytes the store must hold afterwards. */
static void expected_append(const tree *from, const char *name, int kind, uint32_t set_id, uint32_t badge_id,
                            tree *want, badge_region regs[REGIONS])
{
    badge_source src;
    badge_append_result ar;
    const badge_manifest first = {set_id, badge_id, NULL, 0};
    dup_tree(want, from);
    CHECK_EQ(load(name, kind, &src), BADGE_OK);
    CHECK_EQ(badge_append(want->data, want->data_size, want->mng, want->mng_size, src.files, src.count, &first,
                          src.key, NULL, &ar), BADGE_OK);
    badge_regions(ar.slot, ar.start, ar.count, regs);
    badge_append_free(&ar);
    badge_source_free(&src);
}

/* Every write the store saw is BadgeMngFile.dat whole or lies inside one of the set's areas of BadgeData.dat. */
static int writes_within(const badge_region *regs)
{
    for (int i = 0; i < store.traced; i++) {
        if (store.trace[i].file == BADGE_FILE_MNG) {
            if (store.trace[i].offset || store.trace[i].size != BADGE_MNG_SIZE)
                return 0;
            continue;
        }
        int inside = 0;
        for (int k = 0; k < REGIONS; k++)
            inside |= regs[k].file == REG_DATA && store.trace[i].offset >= regs[k].offset &&
                      store.trace[i].offset + store.trace[i].size <= regs[k].offset + regs[k].size;
        if (!inside)
            return 0;
    }
    return 1;
}

static char *backup_root(void) { return join(card_dir, "badge-backup"); }

/* A refusal: nothing was written to the store, its files are the original ones, no backup is left. */
static void check_refused(const badge_flow_result *res, int stage, int badge)
{
    char *backups = backup_root();
    CHECK_EQ(res->outcome, BFLOW_REFUSED);
    CHECK_EQ(res->stage, stage);
    CHECK_EQ(res->badge, badge);
    CHECK_EQ(res->wrote, 0);
    CHECK_EQ(store.writes, 0);
    CHECK(store_is(&original));
    CHECK_EQ(res->backup[0], 0);
    CHECK_EQ(count_in(backups, NULL), 0);
    CHECK(!store.f[0] && !store.f[1]);          /* the store is closed again */
    free(backups);
}

static const uint32_t THREE[3] = {4, 2, 5};

/* ---------------------------------------------------------------- the cases */

static void test_scan_and_peek(void)
{
    world(THREE, 3);
    char *plaza = join(badges_dir, "Plaza"), *kaputt = join(badges_dir, "Kaputt"), *leer = join(badges_dir, "leer");
    mkdir(plaza, 0755);                         /* a folder with a zip in it: no set, a folder to open */
    put_file(plaza, "x.zip", "PK", 2);
    mkdir(kaputt, 0755);                        /* a folder of PNG files of which one is damaged: a set that does not read */
    put_png(kaputt, "gut.png", 64, 64, 7);
    put_file(kaputt, "schlecht.png", "\x89PNG\r\n\x1a\nxxxx", 12);
    mkdir(leer, 0755);
    put_file(badges_dir, "notiz.txt", "x", 1);
    put_file(badges_dir, ".versteckt.png", "x", 1);
    put_png(badges_dir, "schief.png", 60, 64, 8);       /* sides that are no multiple of 64 */
    badge_entries list;
    CHECK_EQ(badge_scan(badges_dir, &list, NULL, NULL), 0);
    CHECK_EQ(list.count, 7);
    CHECK_EQ(list.folders, 2);
    CHECK_EQ(list.other, 1);
    CHECK_EQ(list.missing, 0);
    static const char *const order[7] = {"leer", "Plaza", "Blumen.zip", "Kaputt", "schief.png", "Stern.png", "Tiere"};
    static const int kinds[7] = {BADGE_SRC_FOLDER, BADGE_SRC_FOLDER, BADGE_SRC_ZIP, BADGE_SRC_FOLDER, BADGE_SRC_PNG,
                                 BADGE_SRC_PNG, BADGE_SRC_FOLDER};
    static const int results[7] = {BADGE_E_FOLDER, BADGE_E_FOLDER, BADGE_OK, BADGE_E_PNG, BADGE_E_IMAGE_SIZE, BADGE_OK,
                                   BADGE_OK};
    static const uint32_t counts[7] = {0, 0, 2, 0, 0, 1, 3};
    for (size_t i = 0; i < 7 && i < list.count; i++) {
        CHECK(!strcmp(list.items[i].file, order[i]));
        CHECK_EQ(list.items[i].kind, kinds[i]);
        CHECK_EQ(list.items[i].folder, i < 2);
        CHECK_EQ(list.items[i].peek.result, results[i]);
        CHECK_EQ(list.items[i].peek.count, counts[i]);
    }
    /* the icon a row shows is the icon the append writes: _seticon.png for Tiere, the first badge for Stern.png */
    for (int which = 0; which < 2; which++) {
        tree want;
        badge_region regs[REGIONS];
        badge_collection *col = malloc(sizeof *col);
        uint8_t icon[BADGE_ICON_BYTES];
        expected_append(&original, which ? "Stern.png" : "Tiere", which ? BADGE_SRC_PNG : BADGE_SRC_FOLDER, 0x7800u,
                        0x5800u, &want, regs);
        CHECK_EQ(badge_read(want.data, want.data_size, want.mng, want.mng_size, NULL, col), BADGE_OK);
        CHECK_EQ(badge_set_icon(want.data, want.data_size, &col->set[3], icon), BADGE_OK);
        CHECK(list.count == 7 && !memcmp(icon, list.items[which ? 5 : 6].peek.icon, sizeof icon));
        free(col);
        free_tree(&want);
    }
    badge_entries_free(&list);
    CHECK_EQ(badge_scan(plaza, &list, NULL, NULL), 0);  /* the folder opened: its zip is listed, and does not read */
    CHECK_EQ(list.count, 1);
    CHECK(list.count == 1 && list.items[0].kind == BADGE_SRC_ZIP && list.items[0].peek.result == BADGE_E_ZIP);
    badge_entries_free(&list);
    CHECK_EQ(badge_scan(leer, &list, NULL, NULL), 0);
    CHECK_EQ(list.count, 0);
    badge_entries_free(&list);
    char *missing = join(root, "nirgends");
    CHECK_EQ(badge_scan(missing, &list, NULL, NULL), 0);
    CHECK_EQ(list.missing, 1);
    badge_entries_free(&list);
    CHECK_EQ(badge_source_kind("a.ZIP", 0), BADGE_SRC_ZIP);
    CHECK_EQ(badge_source_kind("a.Png", 0), BADGE_SRC_PNG);
    CHECK_EQ(badge_source_kind("a.txt", 0), -1);
    CHECK_EQ(badge_source_kind("a.txt", 1), BADGE_SRC_FOLDER);
    /* a name with a control character would break a line of the manifest: no set */
    badge_source src;
    badge_peek peek;
    CHECK_EQ(load("Stern.png", BADGE_SRC_PNG, &src), BADGE_OK);
    snprintf(src.key, sizeof src.key, "Ste\trn");
    badge_source_peek(&src, &peek);
    CHECK_EQ(peek.result, BADGE_E_NAME);
    badge_source_free(&src);
    free(plaza);
    free(kaputt);
    free(leer);
    free(missing);
}

static void test_listing_and_plan(void)
{
    world(THREE, 3);
    badge_listing *l = malloc(sizeof *l);
    badge_source src;
    badge_peek peek;
    CHECK_EQ(badge_flow_list(&env, l), BLIST_OK);
    CHECK_EQ(l->col.sets, 3);
    CHECK_EQ(l->col.badges, 11);
    CHECK_EQ(l->undo, 0);
    CHECK_EQ(l->manifest_bad, 0);
    for (uint32_t k = 0; k < 3 && k < l->col.sets; k++) {
        uint8_t icon[BADGE_ICON_BYTES];
        char name[16];
        snprintf(name, sizeof name, "Set %u", (unsigned)k);
        CHECK(!strcmp(l->col.set[k].name, name));
        CHECK_EQ(l->col.set[k].count, THREE[k]);
        CHECK_EQ(l->col.set[k].mine, 0);
        CHECK_EQ(l->col.set[k].removable, 0);
        badge_set_icon(original.data, original.data_size, &l->col.set[k], icon);
        CHECK(!memcmp(icon, l->icons[k], sizeof icon));
    }
    CHECK_EQ(store.writes, 0);
    CHECK(store.reads < 12);                    /* a listing reads the catalogue, the names and the icons, not 16 MiB */
    CHECK_EQ(load("Tiere", BADGE_SRC_FOLDER, &src), BADGE_OK);
    badge_source_peek(&src, &peek);
    CHECK_EQ(badge_flow_plan(l, &src, &peek), BPLAN_OK);
    snprintf(src.key, sizeof src.key, "Set 1");
    CHECK_EQ(badge_flow_plan(l, &src, &peek), BPLAN_PRESENT);
    snprintf(src.key, sizeof src.key, "Tiere");
    peek.count = 990;
    CHECK_EQ(badge_flow_plan(l, &src, &peek), BPLAN_FULL);
    peek.count = 3;
    peek.result = BADGE_E_PNG;
    CHECK_EQ(badge_flow_plan(l, &src, &peek), BPLAN_SET);
    peek.result = BADGE_OK;
    l->manifest_bad = 1;
    CHECK_EQ(badge_flow_plan(l, &src, &peek), BPLAN_MANIFEST);
    badge_listing_free(l);
    /* no badge data, a file of the wrong size, a read that fails, a collection the core does not read */
    store.fail_open = (int)CODE_OPEN;
    CHECK_EQ(badge_flow_list(&env, l), BLIST_NO_DATA);
    CHECK_EQ(l->code, (int)CODE_OPEN);
    CHECK_EQ(badge_flow_plan(l, &src, &peek), BPLAN_COLLECTION);
    store.fail_open = 0;
    store.fail_read_at = store.reads + 2;
    CHECK_EQ(badge_flow_list(&env, l), BLIST_READ);
    store.fail_read_at = 0;
    put_file(store.dir, STORE_FILE[1], original.mng, original.mng_size - 1);
    CHECK_EQ(badge_flow_list(&env, l), BLIST_NO_DATA);
    uint8_t *bent = malloc(original.mng_size);
    memcpy(bent, original.mng, original.mng_size);
    bent[BM_USED_BADGES + 100] = 0xFF;          /* used bits that are not one block */
    put_file(store.dir, STORE_FILE[1], bent, original.mng_size);
    CHECK_EQ(badge_flow_list(&env, l), BLIST_FORMAT);
    CHECK_EQ(l->badge, BADGE_E_FORMAT);
    CHECK(!store.f[0] && !store.f[1]);
    free(bent);
    badge_source_free(&src);
    free(l);
}

static void test_append_and_remove(void)
{
    world(THREE, 3);
    badge_flow_result res;
    tree want, after_first;
    badge_region regs[REGIONS];
    char *backups = backup_root(), *undo = join(card_dir, "badge-undo");
    badge_listing *l = malloc(sizeof *l);

    /* a set from a folder */
    expected_append(&original, "Tiere", BADGE_SRC_FOLDER, 0x7800u, 0x5800u, &want, regs);
    CHECK_EQ(append("Tiere", BADGE_SRC_FOLDER, &res), BFLOW_DONE);
    CHECK_EQ(res.stage, BSTAGE_NONE);
    CHECK_EQ(res.wrote, 1);
    CHECK(!strcmp(res.name, "Tiere"));
    CHECK_EQ(res.set_id, 0x7800u);
    CHECK_EQ(res.count, 3);
    CHECK_EQ(res.start, 11);
    CHECK_EQ(res.slot, 3);
    CHECK_EQ(res.sets, 4);
    CHECK_EQ(res.badges, 14);
    CHECK(store_is(&want));                     /* byte for byte what the core makes of it */
    CHECK(writes_within(regs));
    CHECK_EQ(store.trace[store.traced - 1].file, BADGE_FILE_MNG);       /* the catalogue last */
    CHECK_EQ(store.opens, 2);                   /* read and written; then opened again to read back */
    CHECK_EQ(step_back, 0);
    for (int s = BSTEP_READ; s <= BSTEP_VERIFY; s++)
        CHECK(steps_seen[s] > 0);
    CHECK_EQ(steps_seen[BSTEP_RESTORE], 0);
    /* the backup: both files as they were, their sums, in folder 0001 */
    size_t size;
    uint8_t *copy;
    char *first = join(backups, "0001");
    CHECK(!strcmp(res.backup, first));
    copy = get_file(first, STORE_FILE[0], &size);
    CHECK(copy && size == original.data_size && !memcmp(copy, original.data, size));
    free(copy);
    copy = get_file(first, STORE_FILE[1], &size);
    CHECK(copy && size == original.mng_size && !memcmp(copy, original.mng, size));
    free(copy);
    copy = get_file(first, "sha256.txt", &size);
    CHECK(copy && size == 2 * 64 + 2 * 2 + strlen("BadgeData.dat\nBadgeMngFile.dat\n"));
    free(copy);
    CHECK_EQ(count_in(undo, ".bin"), 1);
    CHECK(exists(card_dir, "badge-manifest.txt"));
    CHECK(!exists(card_dir, "badge-manifest.txt.new"));
    CHECK(strstr(log_text, "== Append badge set \"Tiere\"") && strstr(log_text, "written and read back as meant"));
    dup_tree(&after_first, &want);
    free_tree(&want);

    /* the listing knows it as this application's, removable, with its undo record */
    CHECK_EQ(badge_flow_list(&env, l), BLIST_OK);
    CHECK_EQ(l->col.sets, 4);
    CHECK(l->col.sets == 4 && l->col.set[3].mine && l->col.set[3].removable && !strcmp(l->col.set[3].name, "Tiere"));
    CHECK_EQ(l->undo, 1);
    badge_listing_free(l);

    /* the same set again: it is there; nothing is written, no second backup */
    store.writes = store.traced = 0;
    CHECK_EQ(append("Tiere", BADGE_SRC_FOLDER, &res), BFLOW_REFUSED);
    CHECK_EQ(res.stage, BSTAGE_PRESENT);
    CHECK_EQ(store.writes, 0);
    CHECK(store_is(&after_first));
    CHECK_EQ(count_in(backups, NULL), 1);

    /* a zip and a single PNG after it: each as the core makes it, the IDs going on */
    expected_append(&after_first, "Blumen.zip", BADGE_SRC_ZIP, 0x7801u, 0x5803u, &want, regs);
    free_tree(&want);
    CHECK_EQ(append("Blumen.zip", BADGE_SRC_ZIP, &res), BFLOW_DONE);
    CHECK_EQ(res.set_id, 0x7800u + 1);
    CHECK_EQ(res.count, 2);
    CHECK(!strcmp(res.name, "Blumen"));
    CHECK_EQ(append("Stern.png", BADGE_SRC_PNG, &res), BFLOW_DONE);
    CHECK_EQ(res.set_id, 0x7800u + 2);
    CHECK_EQ(res.sets, 6);
    CHECK_EQ(res.badges, 17);
    CHECK_EQ(count_in(backups, NULL), 3);
    tree with_three;
    store_now(&with_three);
    badge_collection *col = malloc(sizeof *col);
    CHECK_EQ(badge_read(with_three.data, with_three.data_size, with_three.mng, with_three.mng_size, NULL, col),
             BADGE_OK);
    CHECK(col->sets == 6 && !strcmp(col->set[4].name, "Blumen") && !strcmp(col->set[5].name, "Stern"));
    CHECK(!memcmp(with_three.mng + BM_LAYOUT, original.mng + BM_LAYOUT, BADGE_PLACED_MAX * BM_LAYOUT_RECORD));
    free(col);
    free_tree(&with_three);

    /* remove the last set, three times: each time the files are byte for byte what they were before its append */
    store.writes = store.traced = 0;
    CHECK_EQ(badge_flow_remove_last(&env, &res), BFLOW_DONE);
    CHECK(!strcmp(res.name, "Stern"));
    CHECK_EQ(res.identical, 1);
    CHECK_EQ(res.sets, 5);
    CHECK_EQ(store.trace[0].file, BADGE_FILE_MNG);      /* the catalogue first */
    CHECK_EQ(count_in(undo, ".used"), 1);
    CHECK_EQ(count_in(backups, NULL), 3);               /* four were made: the oldest went */
    CHECK(!exists(backups, "0001") && exists(backups, "0004"));
    CHECK_EQ(badge_flow_remove_last(&env, &res), BFLOW_DONE);
    CHECK(!strcmp(res.name, "Blumen"));
    CHECK(store_is(&after_first));
    CHECK_EQ(badge_flow_remove_last(&env, &res), BFLOW_DONE);
    CHECK(!strcmp(res.name, "Tiere"));
    CHECK_EQ(res.identical, 1);
    CHECK(store_is(&original));
    CHECK_EQ(count_in(undo, ".bin"), 3);                /* three ".bin.used" */
    CHECK_EQ(count_in(undo, ".used"), 3);

    /* nothing of this application's is left: the last set is another tool's */
    store.writes = 0;
    CHECK_EQ(badge_flow_remove_last(&env, &res), BFLOW_REFUSED);
    CHECK_EQ(res.stage, BSTAGE_COLLECTION);
    CHECK_EQ(res.badge, BADGE_E_NOT_OURS);
    CHECK_EQ(store.writes, 0);

    /* appended again, the set has the IDs it had (the manifest stays), and the bytes are the same */
    CHECK_EQ(append("Tiere", BADGE_SRC_FOLDER, &res), BFLOW_DONE);
    CHECK_EQ(res.set_id, 0x7800u);
    CHECK(store_is(&after_first));

    /* HOME changed its file since (the badge case's view, as on the console after test B4): the removal still goes,
     * the set's areas get their bytes back, HOME's change to the view is undone with the set, and it says that the
     * files are not what the append wrote */
    tree touched;
    dup_tree(&touched, &after_first);
    put_le32(touched.mng + BM_VIEW, 3);
    put_le32(touched.mng + BM_PLACED + 0, get_le32(touched.mng + BM_PLACED));
    put_file(store.dir, STORE_FILE[1], touched.mng, touched.mng_size);
    CHECK_EQ(badge_flow_remove_last(&env, &res), BFLOW_DONE);
    CHECK(store_is(&original));
    free_tree(&touched);

    /* a badge of the last set placed in HOME: it stays */
    CHECK_EQ(append("Tiere", BADGE_SRC_FOLDER, &res), BFLOW_DONE);
    tree placed;
    store_now(&placed);
    put_le16(placed.mng + BM_BADGE_INFO + 11 * BM_BADGE_INFO_SIZE + 0x10, 1);
    put_file(store.dir, STORE_FILE[1], placed.mng, placed.mng_size);
    store.writes = 0;
    CHECK_EQ(badge_flow_remove_last(&env, &res), BFLOW_REFUSED);
    CHECK_EQ(res.stage, BSTAGE_COLLECTION);
    CHECK_EQ(res.badge, BADGE_E_PLACED);
    CHECK_EQ(store.writes, 0);
    free_tree(&placed);
    free_tree(&after_first);
    free(first);
    free(backups);
    free(undo);
    free(l);
}

/* Refusals before anything is written. */
static void test_refusals(void)
{
    badge_flow_result res;

    world(THREE, 3);                            /* the store does not open */
    store.fail_open = (int)CODE_OPEN;
    append("Tiere", BADGE_SRC_FOLDER, &res);
    store.fail_open = 0;
    CHECK_EQ(res.code, (int)CODE_OPEN);
    check_refused(&res, BSTAGE_OPEN, BADGE_OK);

    world(THREE, 3);                            /* a file of the wrong size */
    put_file(store.dir, STORE_FILE[0], original.data, original.data_size - 16);
    append("Tiere", BADGE_SRC_FOLDER, &res);
    CHECK_EQ(res.outcome, BFLOW_REFUSED);
    CHECK_EQ(res.stage, BSTAGE_SIZE);
    CHECK_EQ(store.writes, 0);

    world(THREE, 3);                            /* a read fails half way */
    store.fail_read_at = 20;
    append("Tiere", BADGE_SRC_FOLDER, &res);
    CHECK_EQ(res.code, (int)CODE_IO);
    check_refused(&res, BSTAGE_READ, BADGE_OK);

    world(THREE, 3);                            /* a set with a damaged PNG */
    char *tiere = join(badges_dir, "Tiere");
    put_file(tiere, "Fuchs.png", "\x89PNG\r\n\x1a\nxxxx", 12);
    free(tiere);
    append("Tiere", BADGE_SRC_FOLDER, &res);
    check_refused(&res, BSTAGE_SET, BADGE_E_PNG);

    world(THREE, 3);                            /* a set named like one that is there (another tool's) */
    char *from = join(badges_dir, "Stern.png"), *to = join(badges_dir, "Set 1.png");
    CHECK_EQ(rename(from, to), 0);
    free(from);
    free(to);
    append("Set 1.png", BADGE_SRC_PNG, &res);
    check_refused(&res, BSTAGE_PRESENT, BADGE_E_PRESENT);

    uint32_t hundred[100], many[2] = {600, 398};
    for (int i = 0; i < 100; i++)
        hundred[i] = 1;
    world(hundred, 100);                        /* 100 sets */
    append("Stern.png", BADGE_SRC_PNG, &res);
    check_refused(&res, BSTAGE_FULL, BADGE_E_FULL);
    world(many, 2);                             /* 998 badges and three more */
    append("Tiere", BADGE_SRC_FOLDER, &res);
    check_refused(&res, BSTAGE_FULL, BADGE_E_FULL);
    CHECK_EQ(append("Blumen.zip", BADGE_SRC_ZIP, &res), BFLOW_DONE);    /* two more fit: 1000 */
    CHECK_EQ(res.badges, 1000);

    world(THREE, 3);                            /* the backup cannot be written */
    card.fail_put = "BadgeData.dat";
    append("Tiere", BADGE_SRC_FOLDER, &res);
    check_refused(&res, BSTAGE_BACKUP, BADGE_OK);

    world(THREE, 3);                            /* the card is full after 5 MiB */
    card.room = 5 << 20;
    append("Tiere", BADGE_SRC_FOLDER, &res);
    check_refused(&res, BSTAGE_BACKUP, BADGE_OK);
    card.room = (long)BADGE_DATA_SIZE + 100;    /* ... or just before the second file ends */
    append("Tiere", BADGE_SRC_FOLDER, &res);
    check_refused(&res, BSTAGE_BACKUP, BADGE_OK);

    world(THREE, 3);                            /* the backup's folder cannot be made: a file stands in its place */
    mkdir(card_dir, 0755);
    put_file(card_dir, "badge-backup", "x", 1);
    append("Tiere", BADGE_SRC_FOLDER, &res);
    CHECK_EQ(res.outcome, BFLOW_REFUSED);
    CHECK_EQ(res.stage, BSTAGE_BACKUP);
    CHECK_EQ(store.writes, 0);
    CHECK(store_is(&original));

    world(THREE, 3);                            /* the copy on the card reads back differently */
    card.flip_get = "BadgeMngFile.dat";
    append("Tiere", BADGE_SRC_FOLDER, &res);
    check_refused(&res, BSTAGE_BACKUP_CHECK, BADGE_OK);
    card.flip_get = "BadgeData.dat";
    append("Tiere", BADGE_SRC_FOLDER, &res);
    check_refused(&res, BSTAGE_BACKUP_CHECK, BADGE_OK);

    world(THREE, 3);                            /* the undo record, or the manifest, cannot be written */
    card.fail_put = "badge-undo";
    append("Tiere", BADGE_SRC_FOLDER, &res);
    check_refused(&res, BSTAGE_RECORD, BADGE_OK);
    card.fail_put = "badge-manifest";
    append("Tiere", BADGE_SRC_FOLDER, &res);
    check_refused(&res, BSTAGE_RECORD, BADGE_OK);
    card.fail_put = NULL;
    card.flip_get = "badge-undo";               /* ... or reads back differently */
    append("Tiere", BADGE_SRC_FOLDER, &res);
    check_refused(&res, BSTAGE_RECORD, BADGE_OK);
    card.flip_get = NULL;
    char *undo = join(card_dir, "badge-undo");
    CHECK_EQ(count_in(undo, ".bin"), 0);        /* no record of a set that is not there */
    free(undo);
    CHECK_EQ(append("Tiere", BADGE_SRC_FOLDER, &res), BFLOW_DONE);      /* and after all that it goes through */
}

/* The manifest on the card: strict, and a damaged one stops everything. */
static void test_manifest_file(void)
{
    badge_flow_result res;
    badge_listing *l = malloc(sizeof *l);
    size_t size;
    world(THREE, 3);
    CHECK_EQ(append("Tiere", BADGE_SRC_FOLDER, &res), BFLOW_DONE);
    tree with_set;
    store_now(&with_set);
    uint8_t *text = get_file(card_dir, "badge-manifest.txt", &size);
    CHECK(text && size > 100 && !memcmp(text, "3DStockTheme badges 1\nnext_set_id 30721\nnext_badge_id 22531\n"
                                              "set 30720\tTiere\tTiere\nbadge 0\t22528\tFuchs.png\n", 100));
    /* one changed byte anywhere, a missing end, a cut file, lines after the end: damaged */
    static const size_t at_byte[] = {0, 30, 70, 95};
    for (int k = 0; k < 7 && text; k++) {
        uint8_t *bent = malloc(size + 8);
        size_t n = size;
        memcpy(bent, text, size);
        if (k < 4)
            bent[at_byte[k]] ^= 1;
        else if (k == 4)
            n = size - 66;                      /* without the end line */
        else if (k == 5)
            n = size - 1;
        else
            memcpy(bent + n, "x 1\n", 4), n += 4;
        put_file(card_dir, "badge-manifest.txt", bent, n);
        free(bent);
        CHECK_EQ(badge_flow_list(&env, l), BLIST_OK);
        CHECK_EQ(l->manifest_bad, 1);
        CHECK(l->col.sets == 4 && !l->col.set[3].mine && !l->col.set[3].removable);
        badge_listing_free(l);
        store.writes = 0;
        CHECK_EQ(append("Stern.png", BADGE_SRC_PNG, &res), BFLOW_REFUSED);
        CHECK_EQ(res.stage, BSTAGE_MANIFEST);
        CHECK_EQ(badge_flow_remove_last(&env, &res), BFLOW_REFUSED);
        CHECK_EQ(res.stage, BSTAGE_MANIFEST);
        CHECK_EQ(store.writes, 0);
        CHECK(store_is(&with_set));
    }
    /* a save cut off after the old file went: the new one under its temporary name counts */
    char *path = join(card_dir, "badge-manifest.txt");
    remove(path);
    put_file(card_dir, "badge-manifest.txt.new", text, size);
    CHECK_EQ(badge_flow_list(&env, l), BLIST_OK);
    CHECK(l->col.sets == 4 && l->col.set[3].mine && l->col.set[3].removable && !l->manifest_bad);
    badge_listing_free(l);
    CHECK_EQ(badge_flow_remove_last(&env, &res), BFLOW_DONE);
    CHECK(store_is(&original));
    /* no manifest at all: nothing is this application's, a new set starts at the first IDs */
    remove(path);
    path[strlen(path)] = 0;
    char *fresh = join(card_dir, "badge-manifest.txt.new");
    remove(fresh);
    CHECK_EQ(append("Stern.png", BADGE_SRC_PNG, &res), BFLOW_DONE);
    CHECK_EQ(res.set_id, 0x7800u);
    free(fresh);
    free(path);
    free(text);
    free_tree(&with_set);
    free(l);
}

/* The undo record missing or damaged: the last set stays, nothing is written. */
static void test_undo_records(void)
{
    badge_flow_result res;
    world(THREE, 3);
    CHECK_EQ(append("Tiere", BADGE_SRC_FOLDER, &res), BFLOW_DONE);
    tree with_set;
    store_now(&with_set);
    char *undo = join(card_dir, "badge-undo"), name[64] = "", *backups = backup_root();
    DIR *d = opendir(undo);
    for (struct dirent *e; d && (e = readdir(d)) != NULL;)
        if (strstr(e->d_name, ".bin"))
            snprintf(name, sizeof name, "%s", e->d_name);
    if (d)
        closedir(d);
    CHECK(!strncmp(name, "7800-", 5) && strlen(name) == 5 + 16 + 4);
    size_t size;
    uint8_t *record = get_file(undo, name, &size);
    CHECK(record && size > 0xE8);
    static const size_t at_byte[] = {0, 8, 0x10, 0x20, 0xC8, 0xE8 + 5};  /* magic, version, slot, view, sum, body */
    static const int why[] = {BADGE_E_UNDO, BADGE_E_UNDO, BADGE_E_NO_UNDO, BADGE_E_UNDO, BADGE_E_UNDO, BADGE_E_UNDO};
    for (int k = 0; k < 6 && record; k++) {
        uint8_t *bent = malloc(size);
        memcpy(bent, record, size);
        bent[at_byte[k]] ^= 1;
        put_file(undo, name, bent, size);
        free(bent);
        store.writes = 0;
        CHECK_EQ(badge_flow_remove_last(&env, &res), BFLOW_REFUSED);
        CHECK_EQ(res.stage, BSTAGE_UNDO);
        CHECK_EQ(res.badge, why[k]);
        CHECK_EQ(res.wrote, 0);
        CHECK_EQ(store.writes, 0);
        CHECK(store_is(&with_set));
        CHECK_EQ(count_in(backups, NULL), 1);   /* the append's; the refused removal left none */
    }
    put_file(undo, name, record, size - 1);     /* cut short */
    CHECK_EQ(badge_flow_remove_last(&env, &res), BFLOW_REFUSED);
    CHECK_EQ(res.badge, BADGE_E_UNDO);
    char *path = join(undo, name);              /* gone */
    remove(path);
    CHECK_EQ(badge_flow_remove_last(&env, &res), BFLOW_REFUSED);
    CHECK_EQ(res.stage, BSTAGE_UNDO);
    CHECK_EQ(res.badge, BADGE_E_NO_UNDO);
    badge_listing *l = malloc(sizeof *l);
    CHECK_EQ(badge_flow_list(&env, l), BLIST_OK);
    CHECK(l->col.sets == 4 && l->col.set[3].removable && !l->undo);
    badge_listing_free(l);
    free(l);
    CHECK(store_is(&with_set));
    put_file(undo, name, record, size);         /* back: it goes */
    CHECK_EQ(badge_flow_remove_last(&env, &res), BFLOW_DONE);
    CHECK(store_is(&original));
    free(path);
    free(record);
    free(undo);
    free(backups);
    free_tree(&with_set);
}

/* A write to HOME's files that fails or reads back differently: the backup goes back, and the files are what they
 * were. For an append (before 0) and for a removal (before: the files with the set). */
static void test_failed_writes(void)
{
    badge_flow_result res;
    for (int removal = 0; removal < 2; removal++)
        for (int fault = 0; fault < 7; fault++) {
            world(THREE, 3);
            tree before;
            if (removal)
                CHECK_EQ(append("Tiere", BADGE_SRC_FOLDER, &res), BFLOW_DONE);
            store_now(&before);
            int writes = store.writes;          /* an append of Tiere writes 5 areas and the catalogue */
            store.traced = 0;
            switch (fault) {
            case 0: store.fail_write_at = writes + 1; break;            /* the first write fails */
            case 1: store.fail_write_at = writes + 3; break;            /* one in the middle */
            case 2: store.fail_write_at = writes + 6; break;            /* the last */
            case 3: store.lose_write_at = writes + 4; break;            /* a write that says yes and is lost */
            case 4: store.lose_write_at = removal ? writes + 1 : writes + 6; break;     /* ... the catalogue */
            case 5:                             /* a byte far from the set changes while it is written */
                store.flip_after_write = writes + 2;
                store.flip_file = BADGE_FILE_DATA;
                store.flip_offset = BD_BADGE_ICONS + 3 * BD_BADGE_ICON_SIZE + 77;
                break;
            case 6: store.fail_close_at = store.closes + 1; break;      /* the close after the writes fails */
            }
            int out = removal ? badge_flow_remove_last(&env, &res) : append("Tiere", BADGE_SRC_FOLDER, &res);
            CHECK_EQ(out, BFLOW_RESTORED);
            CHECK_EQ(res.wrote, 1);
            CHECK_EQ(res.stage, fault < 3 || fault == 6 ? BSTAGE_WRITE : BSTAGE_VERIFY);
            if (fault < 3)
                CHECK_EQ(res.code, (int)CODE_IO);
            CHECK(store_is(&before));           /* byte for byte */
            CHECK(steps_seen[BSTEP_RESTORE] > 0);
            CHECK(res.backup[0] && exists(res.backup, "BadgeData.dat") && exists(res.backup, "BadgeMngFile.dat"));
            CHECK(strstr(log_text, "the backup is back"));
            CHECK(!store.f[0] && !store.f[1]);
            /* an append that was taken back leaves no undo record; a removal that was taken back keeps its one */
            char *undo = join(card_dir, "badge-undo");
            CHECK_EQ(count_in(undo, ".bin"), removal);
            CHECK_EQ(count_in(undo, ".used"), 0);
            free(undo);
            /* and the same work goes through afterwards */
            store.fail_write_at = store.lose_write_at = store.flip_after_write = store.fail_close_at = 0;
            out = removal ? badge_flow_remove_last(&env, &res) : append("Tiere", BADGE_SRC_FOLDER, &res);
            CHECK_EQ(out, BFLOW_DONE);
            if (removal)
                CHECK(store_is(&original));
            free_tree(&before);
        }
}

/* The power goes during a write (every call fails from the n-th write on): whatever n, the two files read as a
 * collection whose old sets and badges are untouched; and the backup on the card holds the files from before. */
static void test_power_cut(void)
{
    badge_flow_result res;
    for (int removal = 0; removal < 2; removal++)
        for (int n = 1; n <= 6; n++) {
            world(THREE, 3);
            tree before, now;
            if (removal)
                CHECK_EQ(append("Tiere", BADGE_SRC_FOLDER, &res), BFLOW_DONE);
            store_now(&before);
            store.dead_from_write = store.writes + n;
            int out = removal ? badge_flow_remove_last(&env, &res) : append("Tiere", BADGE_SRC_FOLDER, &res);
            CHECK_EQ(out, BFLOW_UNKNOWN);
            CHECK_EQ(res.wrote, 1);
            CHECK(strstr(log_text, "COULD NOT BE CONFIRMED"));
            store_now(&now);
            badge_collection *col = malloc(sizeof *col), *old = malloc(sizeof *old);
            CHECK_EQ(badge_read(original.data, original.data_size, original.mng, original.mng_size, NULL, old),
                     BADGE_OK);
            int r = badge_read(now.data, now.data_size, now.mng, now.mng_size, NULL, col);
            CHECK_EQ(r, BADGE_OK);
            CHECK(col->sets == 3 || col->sets == 4);
            for (uint32_t k = 0; k < 3; k++)
                CHECK(!memcmp(&col->set[k], &old->set[k], sizeof col->set[k]));
            CHECK(!memcmp(now.mng + BM_BADGE_INFO, original.mng + BM_BADGE_INFO, 11 * BM_BADGE_INFO_SIZE));
            CHECK(!memcmp(now.mng + BM_LAYOUT, original.mng + BM_LAYOUT, BADGE_PLACED_MAX * BM_LAYOUT_RECORD));
            /* the old badges' names and images */
            CHECK(!memcmp(now.data + BD_BADGE_TITLES, original.data + BD_BADGE_TITLES, 11 * BD_TITLE_BLOCK));
            CHECK(!memcmp(now.data + BD_BADGE_ICONS, original.data + BD_BADGE_ICONS, 11 * BD_BADGE_ICON_SIZE));
            CHECK(!memcmp(now.data + BD_SMALL_ICONS, original.data + BD_SMALL_ICONS, 11 * BD_SMALL_ICON_SIZE));
            CHECK(!memcmp(now.data + BD_SET_ICONS, original.data + BD_SET_ICONS, 3 * BD_SET_ICON_SIZE));
            /* the backup is the way back */
            size_t size;
            uint8_t *copy = get_file(res.backup, STORE_FILE[0], &size);
            CHECK(copy && size == before.data_size && !memcmp(copy, before.data, size));
            free(copy);
            copy = get_file(res.backup, STORE_FILE[1], &size);
            CHECK(copy && size == before.mng_size && !memcmp(copy, before.mng, size));
            free(copy);
            free(col);
            free(old);
            free_tree(&before);
            free_tree(&now);
        }
    /* the writes fail for good but the files can be read: the backup cannot go back either, and it says so */
    world(THREE, 3);
    store.writes_fail_from = 3;
    CHECK_EQ(append("Tiere", BADGE_SRC_FOLDER, &res), BFLOW_UNKNOWN);
    CHECK_EQ(res.stage, BSTAGE_WRITE);
    CHECK(res.backup[0] && exists(res.backup, "sha256.txt"));
}

/* B2 (the console, 2026-11-15): HOME changed its file after the append (host/test_badge.c home_changes: the placed
 * counts lower, the case's view on the new set, set record + 0x28 on the last two sets, a layout record freed). */

/* A world with Tiere appended onto a collection whose case shows its last set (view 2, as on the console), and then
 * HOME's changes `which` in the store; regs: Tiere's areas. */
static void home_world(unsigned which, tree *home, badge_region regs[REGIONS])
{
    badge_flow_result res;
    world(THREE, 3);
    put_le32(original.mng + BM_VIEW, 2);
    put_file(store.dir, STORE_FILE[1], original.mng, original.mng_size);
    CHECK_EQ(append("Tiere", BADGE_SRC_FOLDER, &res), BFLOW_DONE);
    CHECK(res.slot == 3 && res.start == 11 && res.count == 3);
    badge_regions(3, 11, 3, regs);
    store_now(home);
    home_changes(home, which, 3);
    put_file(store.dir, STORE_FILE[1], home->mng, home->mng_size);
    store.writes = store.traced = 0;
    card.puts = 0;
    log_text[0] = 0;
}

/* A removal refused before anything was written: not to HOME's files, and not to the card (no backup, no record) */
static void check_untouched(const badge_flow_result *res, int stage, int badge, const tree *home)
{
    CHECK_EQ(res->outcome, BFLOW_REFUSED);
    CHECK_EQ(res->stage, stage);
    CHECK_EQ(res->badge, badge);
    CHECK_EQ(res->wrote, 0);
    CHECK_EQ(store.writes, 0);
    CHECK(store_is(home));
    CHECK_EQ(card.puts, 0);
    CHECK_EQ(res->backup[0], 0);
    CHECK(!strstr(log_text, "badges: backup "));
    CHECK(strstr(log_text, "nothing was written to HOME's files") != NULL);
    CHECK(!store.f[0] && !store.f[1]);
}

static void test_flow_after_home(void)
{
    badge_flow_result res;
    badge_region regs[REGIONS];
    tree home, want;

    /* each of HOME's changes alone, and all of them: the removal goes, byte for byte */
    for (int round = 0; round < 5; round++) {
        unsigned which = round < 4 ? 1u << round : HOME_ALL;
        home_world(which, &home, regs);
        dup_tree(&want, &home);
        expect_removed(&home, &original, 3, 11, 3, &want);
        CHECK_EQ(badge_flow_remove_last(&env, &res), BFLOW_DONE);
        CHECK(store_is(&want));
        CHECK(writes_within(regs));
        CHECK_EQ(store.trace[0].file, BADGE_FILE_MNG);
        CHECK_EQ(res.identical, which == HOME_VIEW);
        CHECK(res.sets == 3 && res.badges == 11 && !strcmp(res.name, "Tiere"));
        CHECK(strstr(log_text, "not what (HOME changed them since)") != NULL);
        CHECK_EQ(strstr(log_text, "the badge case's view named the set") != NULL, (which & HOME_VIEW) != 0);
        CHECK(card.puts > 0 && res.backup[0] && exists(res.backup, "BadgeData.dat"));    /* the backup before it */
        free_tree(&home);
        free_tree(&want);
    }

    /* the refusals, on files HOME changed: before the backup, nothing written anywhere */
    home_world(HOME_ALL, &home, regs);          /* a badge of the set placed */
    tree t;
    dup_tree(&t, &home);
    put_le16(t.mng + BM_BADGE_INFO + 12 * BM_BADGE_INFO_SIZE + 0x10, 1);
    put_file(store.dir, STORE_FILE[1], t.mng, t.mng_size);
    CHECK_EQ(badge_flow_remove_last(&env, &res), BFLOW_REFUSED);
    check_untouched(&res, BSTAGE_COLLECTION, BADGE_E_PLACED, &t);
    free_tree(&t);
    free_tree(&home);

    home_world(HOME_ALL, &home, regs);          /* a byte the append wrote changed: an image of the set */
    home.data[BD_BADGE_ICONS + 12 * BD_BADGE_ICON_SIZE + 100] ^= 0x10;
    put_file(store.dir, STORE_FILE[0], home.data, home.data_size);
    CHECK_EQ(badge_flow_remove_last(&env, &res), BFLOW_REFUSED);
    check_untouched(&res, BSTAGE_UNDO, BADGE_E_SET_CHANGED, &home);
    free_tree(&home);

    home_world(HOME_ALL, &home, regs);          /* ... the set record's own last word */
    home.mng[BM_SET_INFO + 3 * BM_SET_INFO_SIZE + 0x2C] ^= 1;
    put_file(store.dir, STORE_FILE[1], home.mng, home.mng_size);
    CHECK_EQ(badge_flow_remove_last(&env, &res), BFLOW_REFUSED);
    check_untouched(&res, BSTAGE_UNDO, BADGE_E_SET_CHANGED, &home);
    free_tree(&home);

    char *undo = join(card_dir, "badge-undo");
    home_world(HOME_ALL, &home, regs);          /* the undo record damaged on the card */
    DIR *d = opendir(undo);
    char name[300] = "";
    for (struct dirent *e; d && (e = readdir(d)) != NULL;)
        if (strstr(e->d_name, ".bin"))
            snprintf(name, sizeof name, "%s", e->d_name);
    if (d)
        closedir(d);
    size_t size;
    uint8_t *rec = get_file(undo, name, &size);
    CHECK(rec && size > 0x100);
    if (rec) {
        rec[size - 1] ^= 1;
        put_file(undo, name, rec, size);
    }
    CHECK_EQ(badge_flow_remove_last(&env, &res), BFLOW_REFUSED);
    check_untouched(&res, BSTAGE_UNDO, BADGE_E_UNDO, &home);
    if (rec) {                                  /* ... a second record that fits as well */
        rec[size - 1] ^= 1;
        put_file(undo, name, rec, size);
        put_file(undo, "7800-0000000000000000.bin", rec, size);
    }
    card.puts = 0;
    CHECK_EQ(badge_flow_remove_last(&env, &res), BFLOW_REFUSED);
    check_untouched(&res, BSTAGE_UNDO, BADGE_E_AMBIGUOUS, &home);
    char *second = join(undo, "7800-0000000000000000.bin"), *first = join(undo, name), *away = join(root, "away.bin");
    remove(second);
    rename(first, away);                        /* ... no record */
    CHECK_EQ(badge_flow_remove_last(&env, &res), BFLOW_REFUSED);
    check_untouched(&res, BSTAGE_UNDO, BADGE_E_NO_UNDO, &home);
    rename(away, first);
    CHECK_EQ(badge_flow_remove_last(&env, &res), BFLOW_DONE);   /* with its record back it goes */
    free(second);
    free(first);
    free(away);
    free(rec);
    free_tree(&home);

    home_world(HOME_ALL, &home, regs);          /* HOME's data not understood: a count that does not add up */
    put_le32(home.mng + BM_SET_INFO + 3 * BM_SET_INFO_SIZE + 0x1C, 2);
    put_file(store.dir, STORE_FILE[1], home.mng, home.mng_size);
    CHECK_EQ(badge_flow_remove_last(&env, &res), BFLOW_REFUSED);
    check_untouched(&res, BSTAGE_COLLECTION, BADGE_E_FORMAT, &home);
    free_tree(&home);

    /* a write that fails after HOME's changes: the backup (HOME's files as they were at the start) goes back */
    home_world(HOME_ALL, &home, regs);
    store.fail_write_at = 3;
    CHECK_EQ(badge_flow_remove_last(&env, &res), BFLOW_RESTORED);
    CHECK(store_is(&home));
    store.fail_write_at = 0;
    free_tree(&home);

    /* the power goes at each write: the files read as a collection, with the set or without it, HOME's fields as
     * HOME left them; the backup is HOME's files from before the removal */
    for (int n = 1; n <= 6; n++) {
        home_world(HOME_ALL, &home, regs);
        dup_tree(&want, &home);
        expect_removed(&home, &original, 3, 11, 3, &want);
        store.dead_from_write = n;
        CHECK_EQ(badge_flow_remove_last(&env, &res), BFLOW_UNKNOWN);
        CHECK_EQ(res.wrote, 1);
        store.dead_from_write = 0;
        tree now;
        store_now(&now);
        badge_collection *col = malloc(sizeof *col), *was = malloc(sizeof *was);
        CHECK_EQ(badge_read(want.data, want.data_size, want.mng, want.mng_size, NULL, was), BADGE_OK);
        CHECK_EQ(badge_read(now.data, now.data_size, now.mng, now.mng_size, NULL, col), BADGE_OK);
        CHECK_EQ(col->sets, n == 1 ? 4 : 3);
        CHECK(!memcmp(now.mng, n == 1 ? home.mng : want.mng, BADGE_MNG_SIZE));
        for (uint32_t k = 0; k < 3; k++)
            CHECK(!memcmp(&col->set[k], &was->set[k], sizeof col->set[k]));
        CHECK(!memcmp(now.data, want.data, BD_BADGE_TITLES + 11 * BD_TITLE_BLOCK - 0) ||
              !memcmp(now.data, home.data, BD_BADGE_TITLES + 11 * BD_TITLE_BLOCK));
        CHECK(!memcmp(now.data + BD_BADGE_ICONS, home.data + BD_BADGE_ICONS, 11 * BD_BADGE_ICON_SIZE));
        CHECK(!memcmp(now.data + BD_SMALL_ICONS, home.data + BD_SMALL_ICONS, 11 * BD_SMALL_ICON_SIZE));
        CHECK_EQ(get_le32(now.mng + BM_PLACED), get_le32(home.mng + BM_PLACED));
        CHECK(!memcmp(now.mng + BM_LAYOUT, home.mng + BM_LAYOUT, BADGE_PLACED_MAX * BM_LAYOUT_RECORD));
        CHECK_EQ(get_le32(now.mng + BM_SET_INFO + 2 * BM_SET_INFO_SIZE + 0x28), 0x11);
        uint8_t *copy = get_file(res.backup, STORE_FILE[1], &size);
        CHECK(copy && size == home.mng_size && !memcmp(copy, home.mng, size));
        free(copy);
        free(col);
        free(was);
        free_tree(&now);
        free_tree(&want);
        free_tree(&home);
    }
    free(undo);
}

/* Never two writes at once: one asked for while another runs is refused at once. */
static badge_flow_result inner;
static int inner_ran;
static void second_write(fake_store *s)
{
    if (s->opens != 1 || inner_ran)
        return;
    inner_ran = 1;
    badge_flow_remove_last(&env, &inner);
}

static void test_busy(void)
{
    badge_flow_result res;
    world(THREE, 3);
    inner_ran = 0;
    store.on_open = second_write;
    CHECK_EQ(append("Tiere", BADGE_SRC_FOLDER, &res), BFLOW_DONE);
    store.on_open = NULL;
    CHECK_EQ(inner_ran, 1);
    CHECK_EQ(inner.outcome, BFLOW_REFUSED);
    CHECK_EQ(inner.stage, BSTAGE_BUSY);
    CHECK_EQ(badge_flow_remove_last(&env, &res), BFLOW_DONE);   /* the guard is open again afterwards */
}

#include "test_badgeflow_more.h"

static int flow_unit(void)
{
    const char *base = getenv("TMPDIR");
    snprintf(root, sizeof root, "%s/badgeflow-XXXXXX", base && *base ? base : "/tmp");
    if (!mkdtemp(root))
        return 3;
    test_scan_and_peek();
    test_listing_and_plan();
    test_append_and_remove();
    test_refusals();
    test_manifest_file();
    test_undo_records();
    test_failed_writes();
    test_power_cut();
    test_flow_after_home();
    test_busy();
    test_id_ranges();
    test_dir_store();
    test_open_modes();
    test_texts();
    test_download_names();
    rm_rf(root);
    free_tree(&original);
    printf("test_badgeflow: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}

int main(int argc, char **argv)
{
    if (argc >= 2 && !strcmp(argv[1], "unit"))
        return flow_unit();
    if (argc >= 5 && !strcmp(argv[1], "download"))
        return cmd_badge_download(argc - 2, argv + 2);
    if (argc == 3 && !strcmp(argv[1], "fixture"))
        return cmd_fixture(argv[2]);
    fprintf(stderr, "usage: test_badgeflow unit | download URL PART DIR [CA] | fixture DIR\n");
    return 2;
}
