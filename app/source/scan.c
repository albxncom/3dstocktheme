#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE         /* DT_DIR on glibc */
#define _DARWIN_C_SOURCE        /* and on macOS */
#include "scan.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "files.h"

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

static int candidate(const char *dir, const char *name)
{
    size_t n = strlen(name);
    return !scan_hidden(dir, name) && n > 0 && n < SCAN_NAME;
}

/* By name, ignoring ASCII case; equal ignoring case: by bytes, so the order is total. */
static int names(const char *x, const char *y)
{
    for (size_t i = 0;; i++) {
        int d = lower((unsigned char)x[i]) - lower((unsigned char)y[i]);
        if (d || !x[i])
            return d ? d : strcmp(x, y);
    }
}

/* A directory entry before it is looked at: only its name, so a folder of thousands of files costs little. */
typedef struct entry {
    char *name;
    int folder;
} entry;

static int by_name(const void *a, const void *b)
{
    return names(((const entry *)a)->name, ((const entry *)b)->name);
}

static void free_entries(entry *all, size_t count)
{
    for (size_t k = 0; k < count; k++)
        free(all[k].name);
    free(all);
}

static int folders_first(const void *a, const void *b)
{
    const scan_item *x = a, *y = b;
    if (x->folder != y->folder)
        return y->folder - x->folder;
    return names(x->file, y->file);
}

void scan_path(char *out, size_t size, const char *dir, const char *file)
{
    size_t n = strlen(dir);
    snprintf(out, size, "%s%s%s", dir, n && dir[n - 1] == '/' ? "" : "/", file);
}

void scan_fallback(char *out, size_t size, const char *file)
{
    snprintf(out, size, "%s", file);
    char *dot = strrchr(out, '.');
    if (dot && dot != out)
        *dot = 0;
}

int scan_is_root(const char *dir)
{
    const char *slash = strchr(dir, '/');
    return !slash || !slash[1];
}

int scan_hidden(const char *dir, const char *name)
{
    static const char SYSTEM[] = "nintendo 3ds";
    if (name[0] == '.')
        return 1;
    if (!scan_is_root(dir) || strlen(name) != sizeof SYSTEM - 1)
        return 0;
    for (size_t i = 0; name[i]; i++)
        if (lower((unsigned char)name[i]) != SYSTEM[i])
            return 0;
    return 1;
}

int scan_up(char *dir, char *child, size_t child_size)
{
    if (scan_is_root(dir))
        return -1;
    char *last = strrchr(dir, '/');
    snprintf(child, child_size, "%s", last + 1);
    if (last == strchr(dir, '/'))
        last[1] = 0;                /* up to the root: keep its slash */
    else
        *last = 0;
    return 0;
}

const char *scan_shown(const char *dir)
{
    const char *slash = strchr(dir, '/');
    return slash ? slash : dir;
}

static int is_folder(const char *dir, const struct dirent *e)
{
    if (e->d_type == DT_DIR)
        return 1;
    if (e->d_type != DT_UNKNOWN)
        return 0;
    char path[SCAN_PATH + SCAN_NAME];
    struct stat st;
    scan_path(path, sizeof path, dir, e->d_name);
    return !stat(path, &st) && S_ISDIR(st.st_mode);
}

int scan_folder(const char *dir, const pack_view *pv, scan_list *list,
                void (*progress)(void *ctx, size_t done, size_t total), void *ctx)
{
    memset(list, 0, sizeof *list);
    DIR *d = opendir(dir);
    if (!d) {
        list->missing = 1;
        return 0;
    }
    size_t cap = 64, count = 0;
    entry *all = malloc(cap * sizeof *all);
    struct dirent *e = NULL;
    while (all && (e = readdir(d))) {
        if (!candidate(dir, e->d_name))
            continue;
        if (count == cap) {
            entry *grown = realloc(all, 2 * cap * sizeof *grown);
            if (!grown)
                break;
            all = grown;
            cap *= 2;
        }
        if (!(all[count].name = strdup(e->d_name)))
            break;
        all[count++].folder = is_folder(dir, e);
    }
    int out_of_memory = !all || e;      /* the loop left before the end of the folder */
    closedir(d);
    if (out_of_memory) {
        free_entries(all, count);
        return -1;
    }
    qsort(all, count, sizeof *all, by_name);
    size_t looked = count > SCAN_MAX ? SCAN_MAX : count;
    list->more = count > SCAN_MAX;
    list->items = calloc(looked ? looked : 1, sizeof *list->items);
    if (!list->items) {
        free_entries(all, count);
        return -1;
    }
    for (size_t k = 0; k < looked; k++) {
        snprintf(list->items[k].file, sizeof list->items[k].file, "%s", all[k].name);
        list->items[k].folder = all[k].folder;
    }
    list->count = looked;
    free_entries(all, count);

    size_t kept = 0;
    for (size_t k = 0; k < looked; k++) {
        scan_item *it = &list->items[kept];
        if (k != kept)
            memcpy(it, &list->items[k], sizeof *it);
        if (it->folder) {
            kept++;
            list->folders++;
            if (progress)
                progress(ctx, k + 1, looked);
            continue;
        }
        char path[SCAN_PATH + SCAN_NAME], fallback[SCAN_NAME];
        scan_path(path, sizeof path, dir, it->file);
        scan_fallback(fallback, sizeof fallback, it->file);
        tp_source src;
        tp_theme t;
        static tp_package package;
        const tp_source *zip;
        it->kind = TP_KIND_NONE;
        if (files_open_source(path, &src, NULL, NULL) == FILES_OK) {
            it->result = tp_theme_in(&src, 0, &package, &zip, &it->kind);
            if (it->result == TP_OK)
                it->result = tp_theme_peek(&t, zip, fallback);
            files_close_source(&src);
        } else {
            it->result = TP_E_KIND;     /* a file that does not open */
        }
        if (progress)
            progress(ctx, k + 1, looked);
        if (it->kind == TP_KIND_NONE) {
            list->other++;
            continue;
        }
        kept++;
        if (it->result == TP_OK) {
            memcpy(it->name, t.name, sizeof it->name);
            memcpy(it->description, t.description, sizeof it->description);
            memcpy(it->icon, t.icon, TP_ICON_SIZE);
            it->has_bgm = t.has_bgm;
            if (pv && pv->state == PACK_OK)
                it->in_pack = pack_find(pv, it->name, it->icon, &it->in_pack_usable);
        } else {
            snprintf(it->name, sizeof it->name, "%s", fallback);
        }
    }
    list->count = kept;
    qsort(list->items, list->count, sizeof *list->items, folders_first);
    return 0;
}

void scan_free(scan_list *list)
{
    free(list->items);
    memset(list, 0, sizeof *list);
}
