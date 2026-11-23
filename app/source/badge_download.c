#include "badge_download.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "badge_flow.h"
#include "tp.h"

static int fail(download_result *res, int status, const char *part)
{
    remove(part);
    res->status = status;
    return status;
}

/* BADGE_SRC_ZIP or BADGE_SRC_PNG by the file's first bytes; -1 neither, -2 it cannot be read. */
static int sniff(const char *path)
{
    static const unsigned char ZIP[4] = {'P', 'K', 3, 4}, PNG[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    unsigned char head[8];
    FILE *f = fopen(path, "rb");
    if (!f)
        return -2;
    size_t n = fread(head, 1, sizeof head, f);
    fclose(f);
    if (n >= sizeof ZIP && !memcmp(head, ZIP, sizeof ZIP))
        return BADGE_SRC_ZIP;
    if (n == sizeof PNG && !memcmp(head, PNG, sizeof PNG))
        return BADGE_SRC_PNG;
    return -1;
}

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

static int mentions(const char *s, const char *word)
{
    for (; s && *s; s++) {
        size_t i = 0;
        while (word[i] && lower((unsigned char)s[i]) == word[i])
            i++;
        if (!word[i])
            return 1;
    }
    return 0;
}

static int taken(const char *dir, const char *key, const char *ext)
{
    char path[640];
    struct stat st;
    snprintf(path, sizeof path, "%s/%s%s", dir, key, ext);
    return stat(path, &st) == 0;
}

void badge_download_name(const char *disposition, const char *url, int kind, const char *dir, char *out,
                         size_t size)
{
    char base[DOWNLOAD_NAME];
    /* No folder is named "\x01" (FAT does not allow the character), so download_name finds no name taken and
     * gives its first choice; the loop below looks for a set of each kind. */
    download_name(disposition, url, TP_KIND_ZIP, "\x01", base, sizeof base);
    size_t n = strlen(base);
    if (n > 4)
        base[n -= 4] = 0;       /* ".zip" */
    if (!strcmp(base, "theme") && !mentions(disposition, "theme") && !mentions(url, "theme"))
        snprintf(base, sizeof base, "Badges");
    if (strlen(base) > BADGE_DOWNLOAD_BASE)
        base[BADGE_DOWNLOAD_BASE] = 0;
    for (n = strlen(base); n && base[n - 1] == ' ';)
        base[--n] = 0;
    const char *ext = kind == BADGE_SRC_PNG ? ".png" : ".zip";
    for (int k = 1; k < 1000; k++) {
        char key[DOWNLOAD_NAME + 16];
        if (k > 1)
            snprintf(key, sizeof key, "%s (%d)", base, k);
        else
            snprintf(key, sizeof key, "%s", base);
        snprintf(out, size, "%s%s", key, ext);
        if (!taken(dir, key, "") && !taken(dir, key, ".zip") && !taken(dir, key, ".png"))
            return;
    }
}

/* The set as it will be listed under its new name: the key, and a lone PNG's own name (its badge's name). */
static int as_named(badge_source *src, const char *file)
{
    snprintf(src->key, sizeof src->key, "%s", file);
    src->key[strlen(src->key) - 4] = 0;
    if (src->kind != BADGE_SRC_PNG)
        return BADGE_OK;
    size_t n = strlen(file) + 1;
    char *name = malloc(n);
    if (!name)
        return BADGE_E_NOMEM;
    memcpy(name, file, n);
    free((void *)(uintptr_t)src->files[0].name);
    src->files[0].name = name;
    return BADGE_OK;
}

int badge_download(const char *url, const char *part_path, const char *dir, const char *ca_file,
                   const char *user_agent, const download_progress *progress, download_result *res)
{
    char disposition[512];
    int status = download_fetch(url, part_path, ca_file, user_agent, progress, res, disposition, sizeof disposition);
    if (status != DL_OK)
        return status;
    int kind = sniff(part_path);
    if (kind < 0)
        return fail(res, kind == -2 ? DL_WRITE : DL_NOT_THEME, part_path);
    mkdir(dir, 0777);
    badge_download_name(disposition, url, kind, dir, res->file, sizeof res->file);
    badge_source src;
    memset(&src, 0, sizeof src);
    badge_peek *peek = malloc(sizeof *peek);
    int r = peek ? badge_source_load(part_path, kind, &src) : BADGE_E_NOMEM;
    if (!r)
        r = as_named(&src, res->file);
    if (!r) {
        badge_source_peek(&src, peek);
        r = peek->result;
    }
    badge_source_free(&src);
    free(peek);
    if (r) {
        res->detail = r;
        res->file[0] = 0;
        return fail(res, r == BADGE_E_NOMEM ? DL_NOMEM : DL_BAD_THEME, part_path);
    }
    res->kind = kind;
    char final[640];
    snprintf(final, sizeof final, "%s/%s", dir, res->file);
    if (rename(part_path, final)) {
        res->file[0] = 0;
        return fail(res, DL_WRITE, part_path);
    }
    return res->status = DL_OK;
}
