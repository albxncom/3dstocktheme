/* A badge_store (badge_flow.h) over a folder that holds the two files BadgeData.dat and BadgeMngFile.dat: the
 * stand-in for HOME's extdata in the DEMO=1 build and on the host. With create, a folder without the files gets them
 * as an empty collection (both zero-filled, which the core reads as no sets), whatever the open's mode. A read-only
 * open takes no write (stdio's "rb"). Codes: BADGE_CODE_MISSING for a file that is not there and not made, -1 for
 * any other failure. */
#ifndef BADGE_DIR_STORE_H
#define BADGE_DIR_STORE_H

#include <stdio.h>

#include "badge_flow.h"

typedef struct badge_dir {
    char dir[BADGE_PATH];
    int create;
    FILE *f[BADGE_FILE_COUNT];
} badge_dir;

void badge_dir_store(badge_store *s, badge_dir *d, const char *dir, int create);

#endif
