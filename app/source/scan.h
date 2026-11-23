/* One folder of the SD card as the folder view lists it: its folders first, then every file whose content is a
 * theme zip or a theme installer CIA (tp_kind; the name does not count), each group sorted by name; a theme is read
 * with tp_theme_peek for the list. Only this folder is read, not the ones in it. Hidden entries (scan_hidden) are
 * passed over; files of neither kind are counted, not listed. */
#ifndef SCAN_H
#define SCAN_H

#include <stddef.h>
#include <stdint.h>

#include "paths.h"
#include "plan.h"
#include "tp.h"

#define SCAN_MAX 500
#define SCAN_NAME 256
#define SCAN_PATH 512

typedef struct scan_item {
    char file[SCAN_NAME];       /* the name in the folder */
    int folder;                 /* a folder; nothing below is set */
    int kind;                   /* TP_KIND_ZIP or TP_KIND_CIA */
    int result;                 /* tp_theme_in's and then tp_theme_peek's result */
    char name[TP_NAME_BYTES];
    char description[TP_DESC_BYTES];
    uint8_t icon[TP_ICON_SIZE];
    int has_bgm;
    uint16_t in_pack;           /* the index of the installed theme it already is, 0 if none */
    int in_pack_usable;         /* that theme is installed (else HOME deleted it) */
} scan_item;

typedef struct scan_list {
    scan_item *items;           /* the folders (items[0] to items[folders - 1]), then the themes */
    size_t count;
    size_t folders;
    int missing;                /* the folder does not exist */
    int more;                   /* more than SCAN_MAX entries; the first SCAN_MAX by name were looked at */
    size_t other;               /* files of neither kind */
} scan_list;

/* pv may be NULL (no marks). progress, if set, is told after each entry. 0, or -1 when out of memory. */
int scan_folder(const char *dir, const pack_view *pv, scan_list *list,
                void (*progress)(void *ctx, size_t done, size_t total), void *ctx);
void scan_free(scan_list *list);

/* The file name without its extension: the name tp_theme_open falls back to when info.smdh has none. */
void scan_fallback(char *out, size_t size, const char *file);

#endif
