#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE         /* DT_DIR on glibc */
#define _DARWIN_C_SOURCE        /* and on macOS */
#include "badge_flow.h"

#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "badge_internal.h"
#include "files.h"
#include "paths.h"
#include "sha256.h"

#define PIECE (256u << 10)              /* one read or write of HOME's files, one read of a card file */
#define CARD_PIECE (1u << 20)           /* one write to a card file */
#define MANIFEST_MAX (2u << 20)
#define UNDO_MAX (20u << 20)            /* an undo record: 0x3AC8 bytes per badge, 15 MiB for 1000 */
#define DIR_BACKUP "badge-backup"
#define DIR_UNDO "badge-undo"
#define FILE_MANIFEST "badge-manifest.txt"
#define MANIFEST_HEAD "3DStockTheme badges 1\n"
/* The upper half of each of the core's ID ranges: the Mac tool counts upward from the lower half (its manifest
 * keeps set ID 0x7000 and badge IDs 0x4000 and 0x4001 for good), so the two never hand out the same ID. */
#define SET_ID_FIRST 0x7800u
#define SET_ID_END BADGE_SET_ID_END
#define BADGE_ID_START 0x5800u
#define BADGE_ID_STOP BADGE_ID_END

static const char *const FILE_NAME[BADGE_FILE_COUNT] = {"BadgeData.dat", "BadgeMngFile.dat"};
static const uint32_t FILE_SIZE[BADGE_FILE_COUNT] = {BADGE_DATA_SIZE, BADGE_MNG_SIZE};

/* ---------------------------------------------------------------- the card through stdio */

static int stdio_put(void *ctx, const char *path, uint64_t offset, const uint8_t *buf, size_t size)
{
    (void)ctx;
    FILE *f = fopen(path, offset ? "ab" : "wb");
    if (!f)
        return -1;
    int bad = 0;
    if (offset)
        bad = fseek(f, 0, SEEK_END) != 0 || (uint64_t)ftell(f) != offset;
    bad = bad || fwrite(buf, 1, size, f) != size;
    bad |= fflush(f) != 0;
    bad |= fclose(f) != 0;
    return bad ? -1 : 0;
}

static int stdio_get(void *ctx, const char *path, uint64_t offset, uint8_t *buf, size_t size, size_t *got)
{
    (void)ctx;
    FILE *f = fopen(path, "rb");
    if (!f)
        return -1;
    int bad = fseek(f, (long)offset, SEEK_SET) != 0;
    size_t n = bad ? 0 : fread(buf, 1, size, f);
    bad |= ferror(f) != 0;
    fclose(f);
    if (bad)
        return -1;
    *got = n;
    return 0;
}

const badge_card BADGE_CARD_STDIO = {NULL, stdio_put, stdio_get};

static const badge_card *card_of(const badge_env *env)
{
    return env->card ? env->card : &BADGE_CARD_STDIO;
}

static void say(const badge_env *env, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void say(const badge_env *env, const char *fmt, ...)
{
    if (!env->log)
        return;
    char line[400];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    env->log(env->ctx, line);
}

static void at(const badge_env *env, int step, uint64_t done, uint64_t total)
{
    if (env->step)
        env->step(env->ctx, step, done, total);
}

/* A whole card file, in pieces: 0, or -1. */
static int card_put_all(const badge_env *env, const char *path, const uint8_t *buf, size_t size, int step,
                        uint64_t before, uint64_t total)
{
    const badge_card *c = card_of(env);
    size_t off = 0;
    do {
        size_t n = size - off < CARD_PIECE ? size - off : CARD_PIECE;
        if (c->put(c->ctx, path, off, buf + off, n))
            return -1;
        off += n;
        if (step >= 0)
            at(env, step, before + off, total);
    } while (off < size);
    return 0;
}

/* Whether the card file at path holds exactly these bytes. */
static int card_same(const badge_env *env, const char *path, const uint8_t *buf, size_t size, uint8_t *scratch,
                     int step, uint64_t before, uint64_t total)
{
    const badge_card *c = card_of(env);
    size_t got = 0;
    for (size_t off = 0; off < size; off += got) {
        size_t n = size - off < PIECE ? size - off : PIECE;
        if (c->get(c->ctx, path, off, scratch, n, &got) || got != n || memcmp(scratch, buf + off, n))
            return 0;
        if (step >= 0)
            at(env, step, before + off + n, total);
    }
    return c->get(c->ctx, path, size, scratch, 1, &got) == 0 && got == 0;       /* and nothing after them */
}

/* A whole card file of at most cap bytes: 0 with *buf allocated (also for an empty file), 1 it is not there, -1 it
 * cannot be read or is larger, -2 no memory. */
static int card_load(const badge_env *env, const char *path, size_t cap, uint8_t **buf, size_t *size)
{
    const badge_card *c = card_of(env);
    struct stat st;
    *buf = NULL;
    *size = 0;
    if (stat(path, &st))
        return 1;
    if (!S_ISREG(st.st_mode) || (uint64_t)st.st_size > cap)
        return -1;
    size_t want = (size_t)st.st_size, got = 0;
    uint8_t *p = malloc(want ? want : 1);
    if (!p)
        return -2;
    for (size_t off = 0; off < want; off += got) {
        size_t n = want - off < PIECE ? want - off : PIECE;
        if (c->get(c->ctx, path, off, p + off, n, &got) || got != n) {
            free(p);
            return -1;
        }
    }
    *buf = p;
    *size = want;
    return 0;
}

static void in_dir(char *out, size_t size, const badge_env *env, const char *rest)
{
    snprintf(out, size, "%s/%s", env->dir, rest);
}

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

static int ends_with(const char *s, const char *tail)
{
    size_t a = strlen(s), b = strlen(tail);
    if (a < b)
        return 0;
    for (size_t i = 0; i < b; i++)
        if (lower((unsigned char)s[a - b + i]) != tail[i])
            return 0;
    return 1;
}

/* A name the manifest's lines can hold: no control character. */
static int plain(const char *s)
{
    for (; *s; s++)
        if ((unsigned char)*s < 0x20 || *s == 0x7F)
            return 0;
    return 1;
}

static char *copy_of(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p)
        memcpy(p, s, n);
    return p;
}

/* ---------------------------------------------------------------- a set on the card */

int badge_source_kind(const char *name, int is_dir)
{
    if (is_dir)
        return BADGE_SRC_FOLDER;
    return ends_with(name, ".zip") ? BADGE_SRC_ZIP : ends_with(name, ".png") ? BADGE_SRC_PNG : -1;
}

void badge_source_free(badge_source *src)
{
    if (!src)
        return;
    if (src->kind == BADGE_SRC_ZIP) {
        badge_files_free(src->files, src->count);
    } else {
        for (size_t i = 0; i < src->count; i++) {
            free((void *)(uintptr_t)src->files[i].name);
            free((void *)(uintptr_t)src->files[i].data);
        }
        free(src->files);
    }
    memset(src, 0, sizeof *src);
}

static int is_folder(const char *dir, const struct dirent *e)
{
#ifdef DT_DIR
    if (e->d_type == DT_DIR)
        return 1;
    if (e->d_type != DT_UNKNOWN)
        return 0;
#endif
    char path[BADGE_PATH + BADGE_ENTRY_NAME];
    struct stat st;
    scan_path(path, sizeof path, dir, e->d_name);
    return !stat(path, &st) && S_ISDIR(st.st_mode);
}

/* The file's bytes, counted against the set's limit: BADGE_OK, BADGE_E_FOLDER, BADGE_E_NOMEM. */
static int read_whole(const char *path, size_t *budget, const uint8_t **data, size_t *size)
{
    struct stat st;
    if (stat(path, &st) || !S_ISREG(st.st_mode))
        return BADGE_E_FOLDER;
    if ((uint64_t)st.st_size > *budget)
        return BADGE_E_NOMEM;
    size_t want = (size_t)st.st_size, got = 0;
    uint8_t *p = malloc(want ? want : 1);
    if (!p)
        return BADGE_E_NOMEM;
    if (files_read(path, p, want, &got) != FILES_OK || got != want) {
        free(p);
        return BADGE_E_FOLDER;
    }
    *budget -= want;
    *data = p;
    *size = want;
    return BADGE_OK;
}

static int load_folder(const char *path, badge_source *src)
{
    DIR *d = opendir(path);
    if (!d)
        return BADGE_E_FOLDER;
    size_t cap = 0, budget = BADGE_SOURCE_MAX;
    int r = BADGE_OK;
    for (struct dirent *e; !r && (e = readdir(d)) != NULL;) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;
        if (src->count == cap) {
            size_t more = cap ? 2 * cap : 16;
            badge_file *grown = realloc(src->files, more * sizeof *grown);
            if (!grown) {
                r = BADGE_E_NOMEM;
                break;
            }
            src->files = grown;
            cap = more;
        }
        badge_file *f = &src->files[src->count];
        memset(f, 0, sizeof *f);
        if (!(f->name = copy_of(e->d_name))) {
            r = BADGE_E_NOMEM;
            break;
        }
        src->count++;
        f->is_dir = is_folder(path, e);
        /* only what the core reads: a name it passes over, a folder and a file that is no PNG by its name are
         * judged by the name alone */
        if (!f->is_dir && e->d_name[0] != '.' && ends_with(e->d_name, ".png")) {
            char full[BADGE_PATH + BADGE_ENTRY_NAME];
            if ((size_t)snprintf(full, sizeof full, "%s/%s", path, e->d_name) >= sizeof full)
                r = BADGE_E_FOLDER;
            else
                r = read_whole(full, &budget, &f->data, &f->size);
        }
    }
    closedir(d);
    return r;
}

static int load_zip(const char *path, badge_source *src)
{
    tp_source zip;
    struct stat st;
    if (stat(path, &st) || !S_ISREG(st.st_mode))
        return BADGE_E_FOLDER;
    if ((uint64_t)st.st_size > BADGE_SOURCE_MAX)
        return BADGE_E_NOMEM;
    if (files_open_source(path, &zip, NULL, NULL) != FILES_OK)
        return BADGE_E_FOLDER;
    int r = badge_zip_files(&zip, &src->files, &src->count);
    files_close_source(&zip);
    return r;
}

int badge_source_load(const char *path, int kind, badge_source *src)
{
    memset(src, 0, sizeof *src);
    src->kind = kind;
    char copy[BADGE_PATH];
    if ((size_t)snprintf(copy, sizeof copy, "%s", path) >= sizeof copy)
        return BADGE_E_FOLDER;
    size_t len = strlen(copy);
    while (len > 1 && copy[len - 1] == '/')
        copy[--len] = 0;
    const char *base = strrchr(copy, '/') ? strrchr(copy, '/') + 1 : copy;
    if ((size_t)snprintf(src->key, sizeof src->key, "%s", base) >= sizeof src->key)
        return BADGE_E_NAME;
    if (kind == BADGE_SRC_FOLDER)
        return load_folder(copy, src);
    size_t key = strlen(src->key);
    if (key > 4 && (ends_with(src->key, ".zip") || ends_with(src->key, ".png")))
        src->key[key - 4] = 0;
    if (kind == BADGE_SRC_ZIP)
        return load_zip(copy, src);
    if (!(src->files = calloc(1, sizeof *src->files)) || !(src->files[0].name = copy_of(base)))
        return BADGE_E_NOMEM;
    src->count = 1;
    size_t budget = BADGE_SOURCE_MAX;
    return read_whole(copy, &budget, &src->files[0].data, &src->files[0].size);
}

/* Every name of the set fits a line of the manifest. */
static int names_plain(const badge_source *src)
{
    if (!plain(src->key))
        return 0;
    for (size_t i = 0; i < src->count; i++)
        if (!plain(src->files[i].name))
            return 0;
    return 1;
}

void badge_source_peek(const badge_source *src, badge_peek *peek)
{
    memset(peek, 0, sizeof *peek);
    badge_folder folder = {NULL, 0, -1};
    int r = badge_folder_read(src->files, src->count, &folder);
    if (!r)
        r = name_check(src->key, strlen(src->key));
    if (!r && !names_plain(src))
        r = BADGE_E_NAME;
    if (!r) {
        const badge_tile *first = &folder.tiles[0];
        const badge_file *f = &src->files[folder.icon >= 0 ? (size_t)folder.icon : first->file];
        uint32_t w, h, x0 = folder.icon >= 0 ? 0 : first->x0, y0 = folder.icon >= 0 ? 0 : first->y0;
        uint8_t *rgba, *big = malloc(BD_SET_ICON_SIZE);
        r = big ? badge_png_decode(f->data, f->size, &w, &h, &rgba) : BADGE_E_NOMEM;
        if (!r) {
            badge_icon_image(rgba, w, x0, y0, folder.icon >= 0 ? B_ICON : B_TILE, big);
            for (uint32_t y = 0; y < B_ICON; y++)
                for (uint32_t x = 0; x < B_ICON; x++)
                    memcpy(peek->icon + 2 * badge_tiled(x, y, B_ICON), big + 2 * badge_tiled(x, y, 64), 2);
            tp_free(rgba);
            peek->count = (uint32_t)folder.count;
        }
        free(big);
    }
    badge_folder_free(&folder);
    peek->result = r;
}

typedef struct found {
    char name[BADGE_ENTRY_NAME];
    int is_dir;
} found;

/* By name, ignoring ASCII case; equal ignoring case: by bytes, so the order is total (as scan.c lists themes). */
static int by_name(const void *a, const void *b)
{
    const char *x = ((const found *)a)->name, *y = ((const found *)b)->name;
    for (size_t i = 0;; i++) {
        int d = lower((unsigned char)x[i]) - lower((unsigned char)y[i]);
        if (d || !x[i])
            return d ? d : strcmp(x, y);
    }
}

void badge_entries_free(badge_entries *list)
{
    if (!list)
        return;
    free(list->items);
    memset(list, 0, sizeof *list);
}

int badge_scan(const char *dir, badge_entries *list, void (*progress)(void *ctx, size_t done, size_t total),
               void *ctx)
{
    memset(list, 0, sizeof *list);
    DIR *d = opendir(dir);
    if (!d) {
        list->missing = 1;
        return 0;
    }
    found *all = NULL;
    size_t count = 0, cap = 0;
    int bad = 0;
    for (struct dirent *e; !bad && (e = readdir(d)) != NULL;) {
        if (scan_hidden(dir, e->d_name) || strlen(e->d_name) >= BADGE_ENTRY_NAME)
            continue;
        if (count == cap) {
            size_t more = cap ? 2 * cap : 32;
            found *grown = realloc(all, more * sizeof *grown);
            if (!grown) {
                bad = 1;
                break;
            }
            all = grown;
            cap = more;
        }
        snprintf(all[count].name, sizeof all[count].name, "%s", e->d_name);
        all[count++].is_dir = is_folder(dir, e);
    }
    closedir(d);
    if (!bad && count) {
        qsort(all, count, sizeof *all, by_name);
        if (count > BADGE_ENTRIES_MAX) {
            count = BADGE_ENTRIES_MAX;
            list->more = 1;
        }
        bad = !(list->items = calloc(count, sizeof *list->items));
    }
    for (size_t i = 0; !bad && i < count; i++) {
        int kind = badge_source_kind(all[i].name, all[i].is_dir);
        if (progress)
            progress(ctx, i + 1, count);
        if (kind < 0) {
            list->other++;
            continue;
        }
        char path[BADGE_PATH + BADGE_ENTRY_NAME];
        badge_source src;
        badge_entry *e = &list->items[list->count++];
        scan_path(path, sizeof path, dir, all[i].name);     /* at the card's root without a second slash */
        snprintf(e->file, sizeof e->file, "%s", all[i].name);
        e->kind = kind;
        int r = badge_source_load(path, kind, &src);
        if (r)
            e->peek.result = r;
        else
            badge_source_peek(&src, &e->peek);
        badge_source_free(&src);
        /* a folder with other things than PNG files in it is no set: it is listed as a folder, to be opened */
        e->folder = kind == BADGE_SRC_FOLDER && e->peek.result == BADGE_E_FOLDER;
        list->folders += (size_t)e->folder;
    }
    if (!bad && list->folders && list->folders < list->count) {     /* the folders in front, both groups in order */
        badge_entry *sorted = malloc(list->count * sizeof *sorted);
        bad = !sorted;
        size_t n = 0;
        for (int folders = 1; sorted && folders >= 0; folders--)
            for (size_t i = 0; i < list->count; i++)
                if (list->items[i].folder == folders)
                    sorted[n++] = list->items[i];
        if (sorted) {
            free(list->items);
            list->items = sorted;
        }
    }
    free(all);
    if (bad) {
        badge_entries_free(list);
        return -1;
    }
    return 0;
}

/* ---------------------------------------------------------------- the manifest on the card
 * Which sets this application appended and the IDs it gave (badge.h). A text file:
 *   3DStockTheme badges 1
 *   next_set_id <n>
 *   next_badge_id <n>
 *   set <set id> TAB <key> TAB <name>          then that set's badges:
 *   badge <tile> TAB <badge id> TAB <file>
 *   end <SHA-256 of every byte before this line>
 * Read strictly: anything else is a damaged manifest, and nothing is appended or removed with one. */

typedef struct manifest {
    badge_manifest m;
    badge_manifest_set *sets;
    size_t cap;
} manifest;

static void manifest_free(manifest *mf)
{
    for (size_t k = 0; k < mf->m.count; k++) {
        badge_manifest_set *s = &mf->sets[k];
        for (size_t i = 0; i < s->count; i++)
            free((void *)(uintptr_t)s->badges[i].file);
        free((void *)(uintptr_t)s->badges);
        free((void *)(uintptr_t)s->folder);
        free((void *)(uintptr_t)s->name);
    }
    free(mf->sets);
    memset(mf, 0, sizeof *mf);
}

static void manifest_new(manifest *mf)
{
    memset(mf, 0, sizeof *mf);
    mf->m.next_set_id = SET_ID_FIRST;
    mf->m.next_badge_id = BADGE_ID_START;
}

/* Every ID of the manifest and its counters in this application's halves of the ranges. */
static int manifest_in_range(const badge_manifest *m)
{
    if (m->next_set_id < SET_ID_FIRST || m->next_set_id > SET_ID_END || m->next_badge_id < BADGE_ID_START ||
        m->next_badge_id > BADGE_ID_STOP)
        return 0;
    for (size_t k = 0; k < m->count; k++) {
        if (m->sets[k].set_id < SET_ID_FIRST)
            return 0;
        for (size_t i = 0; i < m->sets[k].count; i++)
            if (m->sets[k].badges[i].badge_id < BADGE_ID_START)
                return 0;
    }
    return 1;
}

static int manifest_add_set(manifest *mf, uint32_t set_id, const char *folder, const char *name)
{
    if (mf->m.count == mf->cap) {
        size_t more = mf->cap ? 2 * mf->cap : 8;
        badge_manifest_set *grown = realloc(mf->sets, more * sizeof *grown);
        if (!grown)
            return -1;
        mf->sets = grown;
        mf->cap = more;
        mf->m.sets = grown;
    }
    badge_manifest_set *s = &mf->sets[mf->m.count];
    *s = (badge_manifest_set){copy_of(folder), copy_of(name), set_id, NULL, 0};
    mf->m.count++;              /* counted first: manifest_free then frees what was copied */
    return s->folder && s->name ? 0 : -1;
}

static int manifest_add_badge(manifest *mf, size_t set, const char *file, uint32_t tile, uint32_t badge_id)
{
    badge_manifest_set *s = &mf->sets[set];
    badge_known *grown = realloc((void *)(uintptr_t)s->badges, (s->count + 1) * sizeof *grown);
    if (!grown)
        return -1;
    s->badges = grown;
    grown[s->count] = (badge_known){copy_of(file), tile, badge_id};
    s->count++;
    return grown[s->count - 1].file ? 0 : -1;
}

static void hex_of(const uint8_t *bytes, size_t n, char *out)
{
    for (size_t i = 0; i < n; i++)
        snprintf(out + 2 * i, 3, "%02x", bytes[i]);
}

/* A decimal number of a line: 0, or -1 when it is not one. */
static int number(const char *s, uint32_t *out)
{
    uint64_t v = 0;
    if (!*s)
        return -1;
    for (; *s; s++) {
        if (*s < '0' || *s > '9' || (v = v * 10 + (uint64_t)(*s - '0')) > 0xFFFFFFFFu)
            return -1;
    }
    *out = (uint32_t)v;
    return 0;
}

/* 0, -1 damaged, -2 no memory. text is changed (its lines are cut); its sum is not looked at here. */
static int manifest_parse(char *text, size_t size, manifest *mf)
{
    manifest_new(mf);
    size_t head = strlen(MANIFEST_HEAD);
    if (size < head || memcmp(text, MANIFEST_HEAD, head) || memchr(text, 0, size))
        return -1;
    int counters = 0, ended = 0;
    for (char *line = text + head, *next; line < text + size; line = next) {
        char *nl = memchr(line, '\n', (size_t)(text + size - line));
        if (!nl || ended)
            return -1;          /* a last line without its end, or lines after the end */
        *nl = 0;
        next = nl + 1;
        if (!strncmp(line, "end ", 4)) {
            ended = 1;          /* its sum was compared by manifest_read, on the bytes as they were */
            continue;
        }
        char *value = strchr(line, ' ');
        if (!value)
            return -1;
        *value++ = 0;
        if (!strcmp(line, "next_set_id") || !strcmp(line, "next_badge_id")) {
            uint32_t *to = line[5] == 's' ? &mf->m.next_set_id : &mf->m.next_badge_id;
            if (number(value, to))
                return -1;
            counters++;
            continue;
        }
        char *second = strchr(value, '\t'), *third = second ? strchr(second + 1, '\t') : NULL;
        uint32_t a, b;
        if (!third || strchr(third + 1, '\t'))
            return -1;
        *second++ = 0;
        *third++ = 0;
        if (!strcmp(line, "set")) {
            if (number(value, &a) || !*second || !*third)
                return -1;
            if (manifest_add_set(mf, a, second, third))
                return -2;
        } else if (!strcmp(line, "badge") && mf->m.count) {
            if (number(value, &a) || number(second, &b) || !*third)
                return -1;
            if (manifest_add_badge(mf, mf->m.count - 1, third, a, b))
                return -2;
        } else {
            return -1;
        }
    }
    mf->m.sets = mf->sets;
    return ended && counters == 2 ? 0 : -1;
}

/* The file's text, allocated; NULL when out of memory. */
static char *manifest_text(const manifest *mf, size_t *size)
{
    size_t cap = 256;
    for (size_t k = 0; k < mf->m.count; k++) {
        cap += 32 + strlen(mf->sets[k].folder) + strlen(mf->sets[k].name);
        for (size_t i = 0; i < mf->sets[k].count; i++)
            cap += 40 + strlen(mf->sets[k].badges[i].file);
    }
    char *text = malloc(cap);
    if (!text)
        return NULL;
    size_t n = (size_t)snprintf(text, cap, MANIFEST_HEAD "next_set_id %u\nnext_badge_id %u\n",
                                (unsigned)mf->m.next_set_id, (unsigned)mf->m.next_badge_id);
    for (size_t k = 0; k < mf->m.count; k++) {
        const badge_manifest_set *s = &mf->sets[k];
        n += (size_t)snprintf(text + n, cap - n, "set %u\t%s\t%s\n", (unsigned)s->set_id, s->folder, s->name);
        for (size_t i = 0; i < s->count; i++)
            n += (size_t)snprintf(text + n, cap - n, "badge %u\t%u\t%s\n", (unsigned)s->badges[i].tile,
                                  (unsigned)s->badges[i].badge_id, s->badges[i].file);
    }
    uint8_t sum[32];
    char hex[65];
    sha256(text, n, sum);
    hex_of(sum, 32, hex);
    n += (size_t)snprintf(text + n, cap - n, "end %s\n", hex);
    *size = n;
    return text;
}

/* One manifest file: 0 read, 1 not there, -1 damaged or unreadable, -2 no memory. */
static int manifest_read(const badge_env *env, const char *path, manifest *mf)
{
    uint8_t *buf;
    size_t size;
    manifest_new(mf);
    int r = card_load(env, path, MANIFEST_MAX, &buf, &size);
    if (r)
        return r;
    /* the sum is over every byte before the line "end <sum>" */
    char want[65] = "", got[65];
    size_t end = size;
    while (end >= 5 && memcmp(buf + end - 5, "\nend ", 5))
        end--;
    if (end >= 5 && size - end == 65 && buf[size - 1] == '\n') {
        uint8_t sum[32];
        sha256(buf, end - 4, sum);
        hex_of(sum, 32, want);
        memcpy(got, buf + end, 64);
        got[64] = 0;
    }
    r = !want[0] || strcmp(want, got) ? -1 : manifest_parse((char *)buf, size, mf);
    free(buf);
    if (!r && (badge_manifest_check(&mf->m) != BADGE_OK || !manifest_in_range(&mf->m)))
        r = -1;
    if (r) {
        manifest_free(mf);
        manifest_new(mf);
    }
    return r;
}

/* The manifest as the card has it; a card without one has handed out nothing. A save cut off between its two last
 * steps leaves only the new file under its temporary name: that one counts then. BADGE_OK, BADGE_E_MANIFEST,
 * BADGE_E_NOMEM. */
static int manifest_load(const badge_env *env, manifest *mf)
{
    char path[BADGE_PATH], fresh[BADGE_PATH];
    in_dir(path, sizeof path, env, FILE_MANIFEST);
    in_dir(fresh, sizeof fresh, env, FILE_MANIFEST ".new");
    int r = manifest_read(env, path, mf);
    if (r == 1)
        r = manifest_read(env, fresh, mf);
    return r == 0 || r == 1 ? BADGE_OK : r == -2 ? BADGE_E_NOMEM : BADGE_E_MANIFEST;
}

/* Under a temporary name, read back, then in the old one's place. 0, or -1. */
static int manifest_save(const badge_env *env, const manifest *mf, uint8_t *scratch)
{
    char path[BADGE_PATH], fresh[BADGE_PATH];
    size_t size;
    char *text = manifest_text(mf, &size);
    if (!text)
        return -1;
    in_dir(path, sizeof path, env, FILE_MANIFEST);
    in_dir(fresh, sizeof fresh, env, FILE_MANIFEST ".new");
    int bad = card_put_all(env, fresh, (const uint8_t *)text, size, -1, 0, 0) ||
              !card_same(env, fresh, (const uint8_t *)text, size, scratch, -1, 0, 0);
    free(text);
    if (bad) {
        remove(fresh);
        return -1;
    }
    remove(path);               /* a card's rename does not replace a file */
    return rename(fresh, path) ? -1 : 0;
}

/* ---------------------------------------------------------------- undo records on the card
 * <dir>/badge-undo/<set ID, four hex digits>-<the record's SHA-256, first sixteen hex digits>.bin, as the core gave
 * it; after the removal that used it the same name with ".used". */

static void undo_path(const badge_env *env, uint32_t set_id, const uint8_t *record, size_t size, char *out,
                      size_t out_size)
{
    uint8_t sum[32];
    char hex[17];
    sha256(record, size, sum);
    hex_of(sum, 8, hex);
    snprintf(out, out_size, "%s/" DIR_UNDO "/%04x-%s.bin", env->dir, (unsigned)set_id, hex);
}

static int undo_named(const char *name, uint32_t set_id)
{
    char prefix[8];
    snprintf(prefix, sizeof prefix, "%04x-", (unsigned)set_id);
    return !strncmp(name, prefix, 5) && ends_with(name, ".bin");
}

typedef struct undo_set {
    badge_undo *records;
    char (*paths)[BADGE_PATH];
    size_t count;
} undo_set;

static void undo_free(undo_set *u)
{
    for (size_t i = 0; i < u->count; i++)
        free((void *)(uintptr_t)u->records[i].data);
    free(u->records);
    free(u->paths);
    memset(u, 0, sizeof *u);
}

/* Every record of the set on the card; load 0: only counted. 0, or -1 when one cannot be read or memory is short. */
static int undo_find(const badge_env *env, uint32_t set_id, undo_set *u, int load)
{
    char dir[BADGE_PATH];
    memset(u, 0, sizeof *u);
    in_dir(dir, sizeof dir, env, DIR_UNDO);
    DIR *d = opendir(dir);
    if (!d)
        return 0;
    int bad = 0;
    for (struct dirent *e; !bad && (e = readdir(d)) != NULL;) {
        if (!undo_named(e->d_name, set_id))
            continue;
        if (!load) {
            u->count++;
            continue;
        }
        badge_undo *records = realloc(u->records, (u->count + 1) * sizeof *records);
        if (records)
            u->records = records;
        char (*paths)[BADGE_PATH] = realloc(u->paths, (u->count + 1) * sizeof *paths);
        if (paths)
            u->paths = paths;
        if (!records || !paths) {
            bad = 1;
            break;
        }
        uint8_t *buf;
        size_t size;
        if ((size_t)snprintf(paths[u->count], sizeof paths[0], "%s/%s", dir, e->d_name) >= sizeof paths[0] ||
            card_load(env, paths[u->count], UNDO_MAX, &buf, &size)) {
            bad = 1;
            break;
        }
        records[u->count++] = (badge_undo){buf, size};
    }
    closedir(d);
    if (bad)
        undo_free(u);
    return bad ? -1 : 0;
}

/* ---------------------------------------------------------------- the collection, for the screens */

static int store_read(const badge_env *env, int file, uint32_t offset, uint8_t *buf, uint32_t size, int step,
                      uint64_t before, uint64_t total)
{
    const badge_store *s = env->store;
    for (uint32_t off = 0; off < size;) {
        uint32_t n = size - off < PIECE ? size - off : PIECE;
        int code = s->read(s->ctx, file, offset + off, buf + off, n);
        if (code)
            return code;
        off += n;
        if (step >= 0)
            at(env, step, before + off, total);
    }
    return 0;
}

#define CODE_SUMMARY(c) (((uint32_t)(c) >> 21) & 0x3Fu)
#define CODE_MODULE(c) (((uint32_t)(c) >> 10) & 0xFFu)
#define CODE_DESCRIPTION(c) ((uint32_t)(c) & 0x3FFu)
#define SUMMARY_NOT_FOUND 4u
#define MODULE_FS 17u
#define FS_IN_USE 230u          /* the file or archive is held open by another process */

int badge_open_kind(int code)
{
    if (CODE_MODULE(code) == MODULE_FS && CODE_DESCRIPTION(code) == FS_IN_USE)
        return BOPEN_LOCKED;
    return CODE_SUMMARY(code) == SUMMARY_NOT_FOUND ? BOPEN_MISSING : BOPEN_OTHER;
}

/* Opens the store in mode and checks both sizes: 0, or the stage it failed at with *code. */
static int store_open(const badge_env *env, int mode, int *code)
{
    const badge_store *s = env->store;
    if ((*code = s->open(s->ctx, mode)) != 0)
        return BSTAGE_OPEN;
    for (int f = 0; f < BADGE_FILE_COUNT; f++) {
        uint64_t size = 0;
        *code = s->size(s->ctx, f, &size);
        if (*code || size != FILE_SIZE[f]) {
            say(env, "badges: %s is %llu bytes (code %08X), expected %u", FILE_NAME[f], (unsigned long long)size,
                (unsigned)*code, (unsigned)FILE_SIZE[f]);
            s->close(s->ctx);
            return BSTAGE_SIZE;
        }
    }
    return 0;
}

void badge_listing_free(badge_listing *l)
{
    if (!l)
        return;
    free(l->icons);
    l->icons = NULL;
}

int badge_flow_list(const badge_env *env, badge_listing *l)
{
    memset(l, 0, sizeof *l);
    const badge_store *s = env->store;
    int stage = store_open(env, BADGE_OPEN_READ, &l->code);
    if (stage == BSTAGE_SIZE)
        return l->state = BLIST_NO_DATA;
    if (stage) {
        int kind = badge_open_kind(l->code);
        return l->state = kind == BOPEN_LOCKED ? BLIST_LOCKED : kind == BOPEN_MISSING ? BLIST_NO_DATA : BLIST_OPEN;
    }
    uint8_t *data = calloc(1, BADGE_DATA_SIZE), *mng = malloc(BADGE_MNG_SIZE);
    manifest mf;
    manifest_new(&mf);
    l->state = data && mng ? BLIST_OK : BLIST_NOMEM;
    if (!l->state) {
        l->code = store_read(env, BADGE_FILE_MNG, 0, mng, BADGE_MNG_SIZE, -1, 0, 0);
        if (!l->code)
            l->code = store_read(env, BADGE_FILE_DATA, BD_SET_TITLES, data + BD_SET_TITLES,
                                 BADGE_SETS_MAX * BD_TITLE_BLOCK, -1, 0, 0);
        if (l->code)
            l->state = BLIST_READ;
    }
    if (!l->state) {
        int m = manifest_load(env, &mf);
        l->manifest_bad = m == BADGE_E_MANIFEST;
        if (m == BADGE_E_NOMEM)
            l->state = BLIST_NOMEM;
    }
    if (!l->state) {
        l->badge = badge_read(data, BADGE_DATA_SIZE, mng, BADGE_MNG_SIZE, &mf.m, &l->col);
        if (l->badge)
            l->state = l->badge == BADGE_E_NOMEM ? BLIST_NOMEM : BLIST_FORMAT;
    }
    if (!l->state && l->col.sets && !(l->icons = malloc(l->col.sets * sizeof *l->icons)))
        l->state = BLIST_NOMEM;
    for (uint32_t k = 0; !l->state && k < l->col.sets; k++) {
        uint32_t at_icon = BD_SET_ICONS + l->col.set[k].index * BD_SET_ICON_SIZE;
        l->code = store_read(env, BADGE_FILE_DATA, at_icon, data + at_icon, BD_SET_ICON_SIZE, -1, 0, 0);
        if (l->code)
            l->state = BLIST_READ;
        else
            badge_set_icon(data, BADGE_DATA_SIZE, &l->col.set[k], l->icons[k]);
    }
    if (!l->state && l->col.sets && l->col.set[l->col.sets - 1].mine) {
        undo_set u;
        undo_find(env, l->col.set[l->col.sets - 1].set_id, &u, 0);
        l->undo = u.count > 0;
    }
    if (l->state) {
        badge_listing_free(l);
        memset(&l->col, 0, sizeof l->col);
    }
    s->close(s->ctx);
    manifest_free(&mf);
    free(data);
    free(mng);
    return l->state;
}

int badge_flow_plan(const badge_listing *l, const badge_source *src, const badge_peek *peek)
{
    if (peek->result)
        return BPLAN_SET;
    if (l->state != BLIST_OK)
        return BPLAN_COLLECTION;
    if (l->manifest_bad)
        return BPLAN_MANIFEST;
    for (uint32_t k = 0; k < l->col.sets; k++)
        if (!strcmp(l->col.set[k].name, src->key))
            return BPLAN_PRESENT;
    if (l->col.sets + 1 > BADGE_SETS_MAX || l->col.badges + peek->count > BADGE_BADGES_MAX)
        return BPLAN_FULL;
    return BPLAN_OK;
}

/* ---------------------------------------------------------------- a write */

typedef struct work {
    const badge_env *env;
    badge_flow_result *res;
    uint8_t *data, *mng, *mng_before, *scratch;         /* scratch: two chunks */
    badge_collection *col, *now;
    manifest mf;
    int open;
} work;

static int writing;             /* a write is under way: never two */

/* The work stops here: 0, for "ok = stop(...)". */
static int stop(work *w, int stage, int badge, int code)
{
    w->res->stage = stage;
    w->res->badge = badge;
    w->res->code = code;
    say(w->env, "badges: stopped at stage %d (%s, code %08X); %s", stage, badge_result_name(badge), (unsigned)code,
        w->res->wrote ? "HOME's files were being written" : "nothing was written to HOME's files");
    return 0;
}

static int work_start(work *w, const badge_env *env, badge_flow_result *res)
{
    memset(w, 0, sizeof *w);
    memset(res, 0, sizeof *res);
    res->outcome = BFLOW_REFUSED;
    w->env = env;
    w->res = res;
    manifest_new(&w->mf);
    if (writing) {
        res->stage = BSTAGE_BUSY;
        return 0;
    }
    writing = 1;
    return 1;
}

static void work_finish(work *w)
{
    if (w->open)
        w->env->store->close(w->env->store->ctx);
    manifest_free(&w->mf);
    free(w->data);
    free(w->mng);
    free(w->mng_before);
    free(w->scratch);
    free(w->col);
    at(w->env, BSTEP_DONE, 1, 1);
    writing = 0;
}

/* Step 1 and the start of step 2: both files whole, the manifest, the collection as the core reads it. */
static int work_read(work *w)
{
    const badge_env *env = w->env;
    int code = 0, stage = store_open(env, BADGE_OPEN_WRITE, &code);
    if (stage)
        return stop(w, stage, BADGE_OK, code);
    w->open = 1;
    w->data = malloc(BADGE_DATA_SIZE);
    w->mng = malloc(BADGE_MNG_SIZE);
    w->mng_before = malloc(BADGE_MNG_SIZE);
    w->scratch = malloc(2 * PIECE);
    w->col = malloc(2 * sizeof *w->col);
    w->now = w->col ? w->col + 1 : NULL;
    if (!w->data || !w->mng || !w->mng_before || !w->scratch || !w->col)
        return stop(w, BSTAGE_MEMORY, BADGE_E_NOMEM, 0);
    uint64_t total = (uint64_t)BADGE_DATA_SIZE + BADGE_MNG_SIZE;
    at(env, BSTEP_READ, 0, total);
    code = store_read(env, BADGE_FILE_MNG, 0, w->mng, BADGE_MNG_SIZE, BSTEP_READ, 0, total);
    if (!code)
        code = store_read(env, BADGE_FILE_DATA, 0, w->data, BADGE_DATA_SIZE, BSTEP_READ, BADGE_MNG_SIZE, total);
    if (code)
        return stop(w, BSTAGE_READ, BADGE_OK, code);
    int r = manifest_load(env, &w->mf);
    if (r)
        return stop(w, r == BADGE_E_NOMEM ? BSTAGE_MEMORY : BSTAGE_MANIFEST, r, 0);
    r = badge_read(w->data, BADGE_DATA_SIZE, w->mng, BADGE_MNG_SIZE, &w->mf.m, w->col);
    if (r)
        return stop(w, r == BADGE_E_NOMEM ? BSTAGE_MEMORY : BSTAGE_COLLECTION, r, 0);
    say(env, "badges: read %u sets, %u badges, %u placed; the manifest knows %u sets", (unsigned)w->col->sets,
        (unsigned)w->col->badges, (unsigned)w->col->placed, (unsigned)w->mf.m.count);
    return 1;
}

static void backup_file(const work *w, int file, char *out, size_t size)
{
    snprintf(out, size, "%s/%s", w->res->backup, file < BADGE_FILE_COUNT ? FILE_NAME[file] : "sha256.txt");
}

/* The backup's folder and what is in it, off the card again: nothing was written, so it is of no use. */
static void backup_drop(work *w)
{
    char path[BADGE_PATH + 32];
    if (!w->res->backup[0])
        return;
    for (int f = 0; f <= BADGE_FILE_COUNT; f++) {
        backup_file(w, f, path, sizeof path);
        remove(path);
    }
    remove(w->res->backup);
    w->res->backup[0] = 0;
}

static int numbered(const char *name, unsigned *n)
{
    uint32_t v;
    if (strlen(name) != 4 || number(name, &v))
        return 0;
    *n = (unsigned)v;
    return 1;
}

/* Step 3: both files as read into a new numbered folder, the copies read back and compared with memory. */
static int backup(work *w)
{
    const badge_env *env = w->env;
    char root[BADGE_PATH], path[BADGE_PATH + 32];
    unsigned top = 0, n;
    in_dir(root, sizeof root, env, DIR_BACKUP);
    mkdir(env->dir, 0777);
    mkdir(root, 0777);
    DIR *d = opendir(root);
    if (!d)
        return stop(w, BSTAGE_BACKUP, BADGE_OK, 0);
    for (struct dirent *e; (e = readdir(d)) != NULL;)
        if (numbered(e->d_name, &n) && n > top)
            top = n;
    closedir(d);
    if (top >= 9999 || (size_t)snprintf(w->res->backup, sizeof w->res->backup, "%s/%04u", root, top + 1) >=
                           sizeof w->res->backup || mkdir(w->res->backup, 0777)) {
        w->res->backup[0] = 0;
        return stop(w, BSTAGE_BACKUP, BADGE_OK, 0);
    }
    const uint8_t *bytes[BADGE_FILE_COUNT] = {w->data, w->mng};
    uint64_t total = (uint64_t)BADGE_DATA_SIZE + BADGE_MNG_SIZE, done = 0;
    char sums[2 * (64 + 2 + 20)] = "";
    at(env, BSTEP_BACKUP, 0, total);
    for (int f = 0; f < BADGE_FILE_COUNT; f++) {
        backup_file(w, f, path, sizeof path);
        if (card_put_all(env, path, bytes[f], FILE_SIZE[f], BSTEP_BACKUP, done, total)) {
            backup_drop(w);
            return stop(w, BSTAGE_BACKUP, BADGE_OK, 0);
        }
        done += FILE_SIZE[f];
        uint8_t sum[32];
        char hex[65];
        sha256(bytes[f], FILE_SIZE[f], sum);
        hex_of(sum, 32, hex);
        snprintf(sums + strlen(sums), sizeof sums - strlen(sums), "%s  %s\n", hex, FILE_NAME[f]);
    }
    backup_file(w, BADGE_FILE_COUNT, path, sizeof path);
    if (card_put_all(env, path, (const uint8_t *)sums, strlen(sums), -1, 0, 0)) {
        backup_drop(w);
        return stop(w, BSTAGE_BACKUP, BADGE_OK, 0);
    }
    done = 0;
    at(env, BSTEP_BACKUP_CHECK, 0, total);
    for (int f = 0; f < BADGE_FILE_COUNT; f++) {
        backup_file(w, f, path, sizeof path);
        if (!card_same(env, path, bytes[f], FILE_SIZE[f], w->scratch, BSTEP_BACKUP_CHECK, done, total)) {
            backup_drop(w);
            return stop(w, BSTAGE_BACKUP_CHECK, BADGE_OK, 0);
        }
        done += FILE_SIZE[f];
    }
    say(env, "badges: backup %s written and read back equal (%s)", w->res->backup, sums);
    return 1;
}

static int by_number(const void *a, const void *b)
{
    unsigned x = *(const unsigned *)a, y = *(const unsigned *)b;
    return x < y ? -1 : x > y;
}

/* After a write that read back as meant: the newest BADGE_BACKUPS_KEPT folders stay, older ones go. */
static void backup_prune(work *w)
{
    char root[BADGE_PATH], path[BADGE_PATH + 48];
    unsigned found_n[64], n;
    size_t count = 0;
    in_dir(root, sizeof root, w->env, DIR_BACKUP);
    DIR *d = opendir(root);
    if (!d)
        return;
    for (struct dirent *e; (e = readdir(d)) != NULL;)
        if (numbered(e->d_name, &n) && count < sizeof found_n / sizeof *found_n)
            found_n[count++] = n;
    closedir(d);
    qsort(found_n, count, sizeof *found_n, by_number);
    for (size_t i = 0; i + BADGE_BACKUPS_KEPT < count; i++) {
        for (int f = 0; f <= BADGE_FILE_COUNT; f++) {
            snprintf(path, sizeof path, "%s/%04u/%s", root, found_n[i], f < BADGE_FILE_COUNT ? FILE_NAME[f]
                                                                                              : "sha256.txt");
            remove(path);
        }
        snprintf(path, sizeof path, "%s/%04u", root, found_n[i]);
        remove(path);
    }
}

/* This file's own look at BadgeMngFile.dat after the core's work: every byte that differs lies in one of the set's
 * areas (or, for a removal, in the badge case's view). */
static int mng_within(const uint8_t *before, const uint8_t *after, const badge_region *regs, int view_too)
{
    for (uint32_t i = 0; i < BADGE_MNG_SIZE; i++) {
        if (before[i] == after[i])
            continue;
        int inside = view_too && i >= BM_VIEW && i < BM_TOTAL;
        for (int k = 0; !inside && k < REGIONS; k++)
            inside = regs[k].file == REG_MNG && i >= regs[k].offset && i - regs[k].offset < regs[k].size;
        if (!inside)
            return 0;
    }
    return 1;
}

static int same_set(const badge_set_info *a, const badge_set_info *b)
{
    return a->slot == b->slot && a->set_id == b->set_id && a->index == b->index && a->count == b->count &&
           a->start == b->start && a->total == b->total && a->placed == b->placed && !strcmp(a->name, b->name);
}

/* ... and at the result as a collection: it reads, with `sets` sets of which the first `kept` are what they were. */
static int result_reads(work *w, uint32_t sets, uint32_t badges, uint32_t kept)
{
    if (badge_read(w->data, BADGE_DATA_SIZE, w->mng, BADGE_MNG_SIZE, NULL, w->now) != BADGE_OK)
        return 0;
    if (w->now->sets != sets || w->now->badges != badges)
        return 0;
    for (uint32_t k = 0; k < kept; k++)
        if (!same_set(&w->col->set[k], &w->now->set[k]))
            return 0;
    return 1;
}

/* Step 6. Append: the set's areas of BadgeData.dat, which nothing names yet, then BadgeMngFile.dat whole in one
 * call, the write that makes the set exist. Removal: the other way round. 0, or the store's code. */
static int write_changes(work *w, const badge_region *regs, int mng_first)
{
    const badge_store *s = w->env->store;
    uint64_t total = BADGE_MNG_SIZE, done = 0;
    for (int k = 0; k < REGIONS; k++)
        total += regs[k].file == REG_DATA ? regs[k].size : 0;
    at(w->env, BSTEP_WRITE, 0, total);
    for (int pass = 0; pass < 2; pass++) {
        if ((pass == 0) == (mng_first != 0)) {
            int code = s->write(s->ctx, BADGE_FILE_MNG, 0, w->mng, BADGE_MNG_SIZE);
            if (code)
                return code;
            done += BADGE_MNG_SIZE;
            at(w->env, BSTEP_WRITE, done, total);
            continue;
        }
        for (int k = 0; k < REGIONS; k++) {
            if (regs[k].file != REG_DATA)
                continue;
            for (uint32_t off = 0; off < regs[k].size;) {
                uint32_t n = regs[k].size - off < PIECE ? regs[k].size - off : PIECE;
                int code = s->write(s->ctx, BADGE_FILE_DATA, regs[k].offset + off, w->data + regs[k].offset + off, n);
                if (code)
                    return code;
                off += n;
                done += n;
                at(w->env, BSTEP_WRITE, done, total);
            }
        }
    }
    return 0;
}

/* Whether the store's file holds exactly these bytes; a read that fails counts as different (*code). */
static int store_same(work *w, int file, const uint8_t *expected, int step, uint64_t before, uint64_t total, int *code)
{
    const badge_store *s = w->env->store;
    for (uint32_t off = 0; off < FILE_SIZE[file];) {
        uint32_t n = FILE_SIZE[file] - off < PIECE ? FILE_SIZE[file] - off : PIECE;
        if ((*code = s->read(s->ctx, file, off, w->scratch, n)) != 0)
            return 0;
        if (memcmp(w->scratch, expected + off, n)) {
            uint32_t i = 0;
            while (w->scratch[i] == expected[off + i])
                i++;
            say(w->env, "badges: %s read back differs at offset 0x%X", FILE_NAME[file], (unsigned)(off + i));
            return 0;
        }
        off += n;
        at(w->env, step, before + off, total);
    }
    return 1;
}

/* Step 8: HOME's files made equal to the backup again, chunk by chunk (only chunks that differ are written), in the
 * order that keeps the catalogue from naming areas that are not whole; then closed, opened and compared with the
 * backup once more. 1 when they are equal to it. */
static int restore(work *w, int mng_first)
{
    const badge_env *env = w->env;
    const badge_store *s = env->store;
    const badge_card *c = card_of(env);
    uint8_t *want = w->scratch, *have = w->scratch + PIECE;
    uint64_t total = 2 * ((uint64_t)BADGE_DATA_SIZE + BADGE_MNG_SIZE), done = 0;
    int code = 0;
    if (w->open)
        s->close(s->ctx);
    w->open = 0;
    at(env, BSTEP_RESTORE, 0, total);
    for (int round = 0; round < 2; round++) {   /* 0: write what differs; 1: compare */
        if (store_open(env, BADGE_OPEN_WRITE, &code)) {
            say(env, "badges: restore: the store did not open (code %08X)", (unsigned)code);
            return 0;
        }
        w->open = 1;
        for (int pass = 0; pass < 2; pass++) {
            int file = (pass == 0) == (mng_first != 0) ? BADGE_FILE_MNG : BADGE_FILE_DATA;
            char path[BADGE_PATH + 32];
            unsigned written = 0;
            backup_file(w, file, path, sizeof path);
            for (uint32_t off = 0; off < FILE_SIZE[file];) {
                uint32_t n = FILE_SIZE[file] - off < PIECE ? FILE_SIZE[file] - off : PIECE;
                size_t got = 0;
                if (c->get(c->ctx, path, off, want, n, &got) || got != n) {
                    say(env, "badges: restore: the backup %s cannot be read at 0x%X", path, (unsigned)off);
                    return 0;
                }
                if ((code = s->read(s->ctx, file, off, have, n)) != 0) {
                    say(env, "badges: restore: %s cannot be read at 0x%X (code %08X)", FILE_NAME[file],
                        (unsigned)off, (unsigned)code);
                    return 0;
                }
                if (memcmp(want, have, n)) {
                    if (round == 1) {
                        say(env, "badges: restore: %s still differs from the backup at 0x%X", FILE_NAME[file],
                            (unsigned)off);
                        return 0;
                    }
                    if ((code = s->write(s->ctx, file, off, want, n)) != 0) {
                        say(env, "badges: restore: %s cannot be written at 0x%X (code %08X)", FILE_NAME[file],
                            (unsigned)off, (unsigned)code);
                        return 0;
                    }
                    written++;
                }
                off += n;
                done += n;
                at(env, BSTEP_RESTORE, done, total);
            }
            if (round == 0)
                say(env, "badges: restore: %s: %u chunks written back", FILE_NAME[file], written);
        }
        code = s->close(s->ctx);
        w->open = 0;
        if (code) {
            say(env, "badges: restore: closing failed (code %08X)", (unsigned)code);
            return 0;
        }
    }
    return 1;
}

/* Steps 6 to 8, from the first write to HOME's files to the outcome. */
static void commit(work *w, const badge_region *regs, int mng_first)
{
    const badge_env *env = w->env;
    const badge_store *s = env->store;
    w->res->wrote = 1;
    int code = write_changes(w, regs, mng_first), ok = !code;
    if (!ok)
        stop(w, BSTAGE_WRITE, BADGE_OK, code);
    int closed = s->close(s->ctx);
    w->open = 0;
    if (ok && closed)
        ok = stop(w, BSTAGE_WRITE, BADGE_OK, closed);
    if (ok) {                   /* step 7: opened again, both files whole against what was meant */
        uint64_t total = (uint64_t)BADGE_DATA_SIZE + BADGE_MNG_SIZE;
        at(env, BSTEP_VERIFY, 0, total);
        if (store_open(env, BADGE_OPEN_WRITE, &code)) {
            ok = stop(w, BSTAGE_VERIFY, BADGE_OK, code);
        } else {
            w->open = 1;
            code = 0;
            if (!store_same(w, BADGE_FILE_MNG, w->mng, BSTEP_VERIFY, 0, total, &code) ||
                !store_same(w, BADGE_FILE_DATA, w->data, BSTEP_VERIFY, BADGE_MNG_SIZE, total, &code))
                ok = stop(w, BSTAGE_VERIFY, BADGE_OK, code);
        }
    }
    if (ok) {
        w->res->outcome = BFLOW_DONE;
        w->res->stage = BSTAGE_NONE;
        say(env, "badges: written and read back as meant");
        return;
    }
    say(env, "badges: writing the backup %s back", w->res->backup);
    w->res->outcome = restore(w, !mng_first) ? BFLOW_RESTORED : BFLOW_UNKNOWN;
    say(env, w->res->outcome == BFLOW_RESTORED
                 ? "badges: the backup is back: HOME's files read back equal to it"
                 : "badges: THE BACKUP COULD NOT BE CONFIRMED TO BE BACK; it is in %s", w->res->backup);
}

static uint32_t le32_at(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* The manifest's counters moved past every ID the collection holds, whoever handed it out: the next new set's ID is
 * free, and so are the next `badges` badge IDs (the core gives new badges the IDs from the counter on, one after the
 * other). BADGE_OK, or BADGE_E_IDS when this application's halves of the ranges have no room left. */
static int ids_free(work *w, uint32_t badges)
{
    uint8_t taken[(BADGE_ID_STOP - BADGE_ID_START) / 8] = {0};
    for (uint32_t i = 0; i < w->col->badges; i++) {
        uint32_t id = le32_at(w->mng + BM_BADGE_INFO + i * BM_BADGE_INFO_SIZE + 4);
        if (id >= BADGE_ID_START && id < BADGE_ID_STOP)
            taken[(id - BADGE_ID_START) / 8] |= (uint8_t)(1u << ((id - BADGE_ID_START) % 8));
    }
    uint32_t set = w->mf.m.next_set_id;
    for (int moved = 1; moved;) {
        moved = 0;
        for (uint32_t k = 0; k < w->col->sets; k++)
            if (w->col->set[k].set_id == set) {
                set++;
                moved = 1;
            }
    }
    uint32_t first = w->mf.m.next_badge_id, run = 0;
    for (uint32_t id = first; run < badges; id++) {
        if (id >= BADGE_ID_STOP)
            return BADGE_E_IDS;
        if (taken[(id - BADGE_ID_START) / 8] >> ((id - BADGE_ID_START) % 8) & 1) {
            first = id + 1;
            run = 0;
        } else {
            run++;
        }
    }
    if (set >= SET_ID_END)
        return BADGE_E_IDS;
    if (set != w->mf.m.next_set_id || first != w->mf.m.next_badge_id)
        say(w->env, "badges: IDs in the collection passed over: next set ID 0x%X, next badge ID 0x%X", (unsigned)set,
            (unsigned)first);
    w->mf.m.next_set_id = set;
    w->mf.m.next_badge_id = first;
    return BADGE_OK;
}

int badge_flow_append(const badge_env *env, const badge_source *src, badge_flow_result *res)
{
    work w;
    if (!work_start(&w, env, res))
        return res->outcome;
    badge_append_result ar;
    badge_region regs[REGIONS];
    badge_peek *peek = malloc(sizeof *peek);
    char record[BADGE_PATH] = "";
    memset(&ar, 0, sizeof ar);
    say(env, "== Append badge set \"%s\" (%u files)", src->key, (unsigned)src->count);
    int ok = work_read(&w);
    /* step 2: the set, and whether the collection takes it */
    if (ok && !peek)
        ok = stop(&w, BSTAGE_MEMORY, BADGE_E_NOMEM, 0);
    if (ok) {
        badge_source_peek(src, peek);
        if (peek->result)
            ok = stop(&w, BSTAGE_SET, peek->result, 0);
    }
    for (uint32_t k = 0; ok && k < w.col->sets; k++)
        if (!strcmp(w.col->set[k].name, src->key))
            ok = stop(&w, BSTAGE_PRESENT, BADGE_E_PRESENT, 0);
    if (ok && (w.col->sets + 1 > BADGE_SETS_MAX || w.col->badges + peek->count > BADGE_BADGES_MAX))
        ok = stop(&w, BSTAGE_FULL, BADGE_E_FULL, 0);
    if (ok && ids_free(&w, peek->count))
        ok = stop(&w, BSTAGE_CHANGE, BADGE_E_IDS, 0);
    if (ok)
        ok = backup(&w);
    if (ok) {                   /* step 4 */
        at(env, BSTEP_CHANGE, 0, 1);
        memcpy(w.mng_before, w.mng, BADGE_MNG_SIZE);
        int r = badge_append(w.data, BADGE_DATA_SIZE, w.mng, BADGE_MNG_SIZE, src->files, src->count, &w.mf.m,
                             src->key, NULL, &ar);
        if (r)
            ok = stop(&w, r == BADGE_E_PRESENT ? BSTAGE_PRESENT : r == BADGE_E_FULL ? BSTAGE_FULL : BSTAGE_CHANGE, r,
                      0);
    }
    if (ok) {
        badge_regions(ar.slot, ar.start, ar.count, regs);
        if (!mng_within(w.mng_before, w.mng, regs, 0) ||
            !result_reads(&w, w.col->sets + 1, w.col->badges + ar.count, w.col->sets) ||
            w.now->set[w.col->sets].set_id != ar.set_id || w.now->set[w.col->sets].count != ar.count ||
            w.now->set[w.col->sets].start != ar.start || strcmp(w.now->set[w.col->sets].name, ar.name))
            ok = stop(&w, BSTAGE_CHECK, BADGE_E_REFUSED, 0);
    }
    if (ok) {                   /* step 5: the IDs are handed out for good, the way back is on the card */
        size_t k = 0;
        int bad = 0;
        while (k < w.mf.m.count && strcmp(w.mf.sets[k].folder, src->key))
            k++;
        if (ar.new_set) {
            bad = manifest_add_set(&w.mf, ar.set_id, src->key, ar.name);
        } else {
            char *name = copy_of(ar.name);
            bad = !name;
            if (name) {
                free((void *)(uintptr_t)w.mf.sets[k].name);
                w.mf.sets[k].name = name;
            }
        }
        for (uint32_t i = 0; !bad && i < ar.count; i++)
            if (ar.badges[i].is_new)
                bad = manifest_add_badge(&w.mf, k, src->files[ar.badges[i].file].name, ar.badges[i].tile,
                                         ar.badges[i].badge_id);
        w.mf.m.next_set_id = ar.next_set_id;
        w.mf.m.next_badge_id = ar.next_badge_id;
        char dir[BADGE_PATH];
        in_dir(dir, sizeof dir, env, DIR_UNDO);
        mkdir(dir, 0777);
        undo_path(env, ar.set_id, ar.undo, ar.undo_size, record, sizeof record);
        bad = bad || badge_manifest_check(&w.mf.m) != BADGE_OK || manifest_save(env, &w.mf, w.scratch) ||
              card_put_all(env, record, ar.undo, ar.undo_size, -1, 0, 0) ||
              !card_same(env, record, ar.undo, ar.undo_size, w.scratch, -1, 0, 0);
        if (bad)
            ok = stop(&w, BSTAGE_RECORD, BADGE_OK, 0);
        else
            say(env, "badges: set ID 0x%X, %u badges at index %u, set slot %u; undo record %s", (unsigned)ar.set_id,
                (unsigned)ar.count, (unsigned)ar.start, (unsigned)ar.slot, record);
    }
    if (ok) {
        snprintf(res->name, sizeof res->name, "%s", ar.name);
        res->set_id = ar.set_id;
        res->slot = ar.slot;
        res->start = ar.start;
        res->count = ar.count;
        commit(&w, regs, 0);
    }
    if (res->outcome == BFLOW_DONE) {
        res->sets = w.col->sets + 1;
        res->badges = w.col->badges + ar.count;
        backup_prune(&w);
    } else if (!res->wrote) {
        backup_drop(&w);
    }
    if (res->outcome != BFLOW_DONE && res->outcome != BFLOW_UNKNOWN && record[0])
        remove(record);         /* the set is not there: its record would only stand in a later one's way */
    badge_append_free(&ar);
    free(peek);
    work_finish(&w);
    return res->outcome;
}

/* Where a refusal of the core's removal stops: the undo record's answers at BSTAGE_UNDO, the rest at BSTAGE_CHANGE */
static int remove_stage(int r)
{
    return r == BADGE_E_NO_UNDO || r == BADGE_E_UNDO || r == BADGE_E_AMBIGUOUS || r == BADGE_E_SET_CHANGED ||
                   r == BADGE_E_CHANGED
               ? BSTAGE_UNDO
               : BSTAGE_CHANGE;
}

int badge_flow_remove_last(const badge_env *env, badge_flow_result *res)
{
    work w;
    if (!work_start(&w, env, res))
        return res->outcome;
    undo_set undo;
    badge_remove_result rr;
    badge_region regs[REGIONS];
    memset(&undo, 0, sizeof undo);
    memset(&rr, 0, sizeof rr);
    say(env, "== Remove the last badge set");
    int ok = work_read(&w);
    if (ok && w.col->remove != BADGE_OK)
        ok = stop(&w, BSTAGE_COLLECTION, w.col->remove, 0);
    if (ok) {
        const badge_set_info *last = &w.col->set[w.col->sets - 1];
        snprintf(res->name, sizeof res->name, "%s", last->name);
        if (undo_find(env, last->set_id, &undo, 1))
            ok = stop(&w, BSTAGE_UNDO, BADGE_E_UNDO, 0);
        else if (!undo.count)
            ok = stop(&w, BSTAGE_UNDO, BADGE_E_NO_UNDO, 0);
    }
    if (ok) {                   /* all the core decides, before anything is written: a refusal leaves no backup */
        int r = badge_remove_check(w.data, BADGE_DATA_SIZE, w.mng, BADGE_MNG_SIZE, &w.mf.m, undo.records, undo.count,
                                   1, &rr);
        if (r)
            ok = stop(&w, remove_stage(r), r, 0);
    }
    if (ok)
        ok = backup(&w);
    if (ok) {
        at(env, BSTEP_CHANGE, 0, 1);
        memcpy(w.mng_before, w.mng, BADGE_MNG_SIZE);
        int r = badge_remove_last(w.data, BADGE_DATA_SIZE, w.mng, BADGE_MNG_SIZE, &w.mf.m, undo.records, undo.count,
                                  1, &rr);
        if (r)
            ok = stop(&w, remove_stage(r), r, 0);
    }
    if (ok) {
        badge_regions(rr.slot, rr.start, rr.count, regs);
        if (!mng_within(w.mng_before, w.mng, regs, 1) ||
            !result_reads(&w, w.col->sets - 1, w.col->badges - rr.count, w.col->sets - 1))
            ok = stop(&w, BSTAGE_CHECK, BADGE_E_REFUSED, 0);
    }
    if (ok) {
        res->set_id = rr.set_id;
        res->slot = rr.slot;
        res->start = rr.start;
        res->count = rr.count;
        res->identical = rr.identical;
        say(env, "badges: removing set ID 0x%X, %u badges from index %u, with %s; the files were %s the append "
            "wrote, the result is %s the files before it%s", (unsigned)rr.set_id, (unsigned)rr.count,
            (unsigned)rr.start, undo.paths[rr.record], rr.exact ? "what" : "not what (HOME changed them since)",
            rr.identical ? "byte for byte" : "not byte for byte (HOME's own changes stay)",
            rr.view_reset ? "; the badge case's view named the set and gets its value from before the append" : "");
        commit(&w, regs, 1);
    }
    if (res->outcome == BFLOW_DONE) {
        char used[BADGE_PATH + 8];
        res->sets = w.col->sets - 1;
        res->badges = w.col->badges - rr.count;
        snprintf(used, sizeof used, "%s.used", undo.paths[rr.record]);
        remove(used);
        rename(undo.paths[rr.record], used);
        backup_prune(&w);
    } else if (!res->wrote) {
        backup_drop(&w);
    }
    undo_free(&undo);
    work_finish(&w);
    return res->outcome;
}
