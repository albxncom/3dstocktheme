#include "store.h"

#include <stdio.h>
#include <string.h>

#include "files.h"

static void path_of(char *out, size_t size, const char *dir, const char *name)
{
    snprintf(out, size, "%s/%s", dir, name);
}

void store_read(const char *dir, store_record rec[2])
{
    const char *names[2] = {STORE_FILE, STORE_STAGED};
    for (int i = 0; i < 2; i++) {
        char path[512];
        path_of(path, sizeof path, dir, names[i]);
        rec[i].size = 0;
        rec[i].result = files_read(path, rec[i].tmd, sizeof rec[i].tmd, &rec[i].size);
        rec[i].present = rec[i].result != FILES_MISSING;
    }
}

int store_stage(const char *dir, const uint8_t *tmd, size_t size)
{
    char path[512];
    path_of(path, sizeof path, dir, STORE_STAGED);
    if (files_write(path, tmd, size))
        return -1;
    /* Read it back: a card that drops writes must not leave a record that differs from what is imported. */
    static uint8_t back[STORE_MAX];
    size_t n = 0;
    if (size > sizeof back || files_read(path, back, sizeof back, &n) || n != size)
        return -1;
    for (size_t i = 0; i < size; i++)
        if (back[i] != tmd[i])
            return -1;
    return 0;
}

int store_commit(const char *dir)
{
    char main_path[512], staged[512];
    path_of(main_path, sizeof main_path, dir, STORE_FILE);
    path_of(staged, sizeof staged, dir, STORE_STAGED);
    /* The card's rename does not replace an existing file. Between the two calls only the .new file exists, and
     * pack_assess accepts it while it matches AM. */
    remove(main_path);
    return rename(staged, main_path) ? -1 : 0;
}

static uint16_t mark_in(const char *path)
{
    uint8_t buf[16];
    size_t n = 0;
    if (files_read(path, buf, sizeof buf - 1, &n) || n == 0)
        return 0;
    unsigned value = 0;
    size_t i = 0;
    for (; i < n && buf[i] >= '0' && buf[i] <= '9' && value <= TP_INDEX_MAX; i++)
        value = value * 10 + (buf[i] - '0');
    if (i == 0 || (i < n && !(buf[i] == '\n' && i + 1 == n)) || value < 1 || value > TP_INDEX_MAX)
        return 0;
    return (uint16_t)value;
}

uint16_t store_mark(const char *dir)
{
    char path[512];
    path_of(path, sizeof path, dir, MARK_FILE);
    uint16_t a = mark_in(path);
    path_of(path, sizeof path, dir, MARK_STAGED);
    uint16_t b = mark_in(path);
    return a > b ? a : b;
}

int store_mark_raise(const char *dir, uint16_t index)
{
    if (index < 1 || index > TP_INDEX_MAX)
        return -1;
    if (store_mark(dir) >= index)
        return 0;
    char staged[512], main_path[512], text[8];
    path_of(staged, sizeof staged, dir, MARK_STAGED);
    path_of(main_path, sizeof main_path, dir, MARK_FILE);
    snprintf(text, sizeof text, "%u\n", index);
    if (files_write(staged, (const uint8_t *)text, strlen(text)) || mark_in(staged) != index)
        return -1;
    /* As for the TMD: between the two calls only the .new file holds the mark, and store_mark reads both. */
    remove(main_path);
    return rename(staged, main_path) ? -1 : 0;
}
