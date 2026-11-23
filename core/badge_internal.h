/* Shared by the badge core's files; not part of the API. Offsets as in tools/badges/README.md and badge_sets.py. */
#ifndef BADGE_INTERNAL_H
#define BADGE_INTERNAL_H

#include "badge.h"
#include "internal.h"

/* BadgeData.dat: names and images, by set index and badge index */
#define BD_SET_TITLES      0x0u          /* 100 x 16 languages x 0x8A, UTF-16LE */
#define BD_BADGE_TITLES    0x35E80u      /* 1000 x 16 x 0x8A */
#define BD_TITLE_SIZE      0x8Au
#define BD_LANGUAGES       16u
#define BD_TITLE_BLOCK     (BD_TITLE_SIZE * BD_LANGUAGES)
#define BD_SET_ICONS       0x250F80u     /* 64x64 tiled RGB565, the icon in its top-left 48x48 */
#define BD_SET_ICON_SIZE   0x2000u
#define BD_BADGE_ICONS     0x318F80u     /* 64x64 tiled RGB565 + A4 */
#define BD_BADGE_ICON_SIZE 0x2800u
#define BD_SMALL_ICONS     0xCDCF80u     /* 32x32 tiled RGB565 + A4 */
#define BD_SMALL_ICON_SIZE 0xA00u

/* BadgeMngFile.dat: the catalogue and the live layout */
#define BM_SET_COUNT       0x4u
#define BM_UNIQUE          0x8u
#define BM_PLACED          0xCu
#define BM_VIEW            0x10u         /* selected set and selected column, up to BM_TOTAL */
#define BM_TOTAL           0x18u
#define BM_NNID            0x1Cu
#define BM_USED_BADGES     0x358u
#define BM_USED_BADGES_SIZE 0x80u
#define BM_USED_SETS       0x3D8u
#define BM_USED_SETS_SIZE  0x10u
#define BM_BADGE_INFO      0x3E8u
#define BM_BADGE_INFO_SIZE 0x28u
#define BM_SET_INFO        0xA028u
#define BM_SET_INFO_SIZE   0x30u
#define BM_LAYOUT          0xB2E8u
#define BM_LAYOUT_RECORD   0x18u

/* MngFile.dat in extdata 0000165c: eight saved layouts, each with a full copy of BadgeMngFile.dat */
#define SL_FLAGS           0x4u
#define SL_BADGES          0x2ED10u

#define B_QUANTITY         0xFFFFu       /* what Anemone3DS writes: unlimited copies */
#define B_SET_UNKNOWN_0C   0x2710u
#define B_NONE             0xFFFFFFFFu
#define B_FOLDER_ICON      0xF0FFu
#define B_TILE             64u
#define B_ICON             48u

/* A byte range an append writes: in BadgeData.dat (file 0) or BadgeMngFile.dat (file 1). */
enum { REG_DATA, REG_MNG };
#define REGIONS 12
typedef struct badge_region {
    int file;
    uint32_t offset, size;
} badge_region;

/* Every byte range an append of a set at set slot and index s with n badges from index u writes (badge_sets.py
 * regions), in its order. */
void badge_regions(uint32_t s, uint32_t u, uint32_t n, badge_region r[REGIONS]);
/* unchanged_outside for BadgeMngFile.dat: 1 if a byte outside the regions (and extra, may be NULL) differs, or a
 * used bit changes that is not one of the set's (badges [u, u + n), and set s if set_allowed). */
int badge_mng_changed(const uint8_t *before, const uint8_t *after, const badge_region *r, const badge_region *extra,
                      uint32_t s, int set_allowed, uint32_t u, uint32_t n);

/* ---------------------------------------------------------------- names (badge.c) */

/* Whether the core takes code point c in a name: U+0001..U+00FF and the blocks of badge.c's TAKEN. */
int name_accepted(uint32_t c);
/* The code points of a name of len bytes (strict UTF-8, each one taken), or BADGE_E_NAME; *n of them, *units its
 * length in UTF-16 units (may be NULL). cp may be NULL to count only. */
int name_points(const char *s, size_t len, uint32_t *cp, size_t cap, size_t *n, size_t *units);
/* str.isspace(), and str.casefold() of one code point the core takes (1 or 2 code points into out) */
int name_space(uint32_t c);
int name_fold(uint32_t c, uint32_t out[2]);
/* badge_sets.py check_name: not blank after str.strip(), at most 68 UTF-16 units, every code point taken. */
int name_check(const char *s, size_t len);
/* The 16 language copies of a name, UTF-16LE, NUL-padded (title_block); the name passed name_check. */
void name_block(const char *s, size_t len, uint8_t block[BD_TITLE_BLOCK]);
/* Python's order of (nfc(a).casefold(), nfc(a)) for names name_points takes: < 0, 0, > 0. */
int name_order(const char *a, const char *b);

/* ---------------------------------------------------------------- images (badge_png.c) */

/* The 64x64 badge image (0x2800 bytes, RGB565 + A4, tiled) and its 32x32 one (0xA00 bytes) of the 64x64 tile at
 * (x0, y0) of an RGBA image `width` pixels wide (badge_sets.py badge_images). */
void badge_tile_images(const uint8_t *rgba, uint32_t width, uint32_t x0, uint32_t y0, uint8_t *big, uint8_t *small);
/* The 0x2000-byte set icon from a side x side area at (x0, y0): 48 is taken as is, 64 is scaled to 48x48 by the
 * nearest pixel; alpha is dropped, the rest of the 64x64 is black (set_icon_image). */
void badge_icon_image(const uint8_t *rgba, uint32_t width, uint32_t x0, uint32_t y0, uint32_t side, uint8_t *out);
/* Index of pixel (x, y) in a size x size image of 8x8 tiles, Morton order inside a tile. */
uint32_t badge_tiled(uint32_t x, uint32_t y, uint32_t size);

#endif
