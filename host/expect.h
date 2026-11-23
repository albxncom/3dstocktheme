/* What the host tests (host/test_ui.c) expect of each look where it differs from Stock on purpose (G1 plan, B4).
 * Every host/expect_<name>.h there is read (each port owns its own, expect_fake.h is the test-only look's); a file
 * defines only what differs, with the look in the name, and Stock's value stands in for every name it leaves out:
 *   EXPECT_<NAME>_WRENCH_X, _WRENCH_Y   the wrench's element place on the settings tab (ui_settings_rect), px from
 *                                       its left and top; held, it is 1 px lower
 *   EXPECT_<NAME>_PATTERN               1: the pattern is drawn once over the whole top screen with an atlas image
 *                                       right after it (Stock); 0: the top screen's first image is a ground over the
 *                                       whole screen (y 0, the screen's height) with no text before it
 *   EXPECT_<NAME>_PICTURE_CUT           the picture on that tile: 0, rounded corners (DETAIL_ICON_R per 48 px); else
 *                                       cut off at 45 degrees, this many px along each edge per 48 px
 *   EXPECT_<NAME>_GROUND_SUN            the drop behind that tile (ART_DROP_SUN): 0, the installing screen draws
 *                                       it (Stock); 1, the look's ground draws it on every top screen and the
 *                                       installing screen none (C147)
 * The order check on every top screen (no text after a picture's texture, no transparent piece) holds either way. */
#ifndef EXPECT_H
#define EXPECT_H

#include <string.h>

#include "layout.h"

#if __has_include("expect_frost.h")
#include "expect_frost.h"
#endif
#if __has_include("expect_night.h")
#include "expect_night.h"
#endif
#if __has_include("expect_prism.h")
#include "expect_prism.h"
#endif
#include "expect_fake.h"

#define EXPECT_STOCK_WRENCH_X (CORNER_PAD - 1)     /* R2: one pixel left of and above the folder's symbol */
#define EXPECT_STOCK_WRENCH_Y ((CORNER_H - CORNER_ICON) / 2 - 1)
#define EXPECT_STOCK_PATTERN 1
#define EXPECT_STOCK_PICTURE_CUT 0
#define EXPECT_STOCK_GROUND_SUN 0

#ifndef EXPECT_FROST_WRENCH_X
#define EXPECT_FROST_WRENCH_X EXPECT_STOCK_WRENCH_X
#endif
#ifndef EXPECT_FROST_WRENCH_Y
#define EXPECT_FROST_WRENCH_Y EXPECT_STOCK_WRENCH_Y
#endif
#ifndef EXPECT_FROST_PATTERN
#define EXPECT_FROST_PATTERN EXPECT_STOCK_PATTERN
#endif
#ifndef EXPECT_NIGHT_WRENCH_X
#define EXPECT_NIGHT_WRENCH_X EXPECT_STOCK_WRENCH_X
#endif
#ifndef EXPECT_NIGHT_WRENCH_Y
#define EXPECT_NIGHT_WRENCH_Y EXPECT_STOCK_WRENCH_Y
#endif
#ifndef EXPECT_NIGHT_PATTERN
#define EXPECT_NIGHT_PATTERN EXPECT_STOCK_PATTERN
#endif
#ifndef EXPECT_PRISM_WRENCH_X
#define EXPECT_PRISM_WRENCH_X EXPECT_STOCK_WRENCH_X
#endif
#ifndef EXPECT_PRISM_WRENCH_Y
#define EXPECT_PRISM_WRENCH_Y EXPECT_STOCK_WRENCH_Y
#endif
#ifndef EXPECT_PRISM_PATTERN
#define EXPECT_PRISM_PATTERN EXPECT_STOCK_PATTERN
#endif
#ifndef EXPECT_FAKE_WRENCH_X
#define EXPECT_FAKE_WRENCH_X EXPECT_STOCK_WRENCH_X
#endif
#ifndef EXPECT_FAKE_WRENCH_Y
#define EXPECT_FAKE_WRENCH_Y EXPECT_STOCK_WRENCH_Y
#endif
#ifndef EXPECT_FAKE_PATTERN
#define EXPECT_FAKE_PATTERN EXPECT_STOCK_PATTERN
#endif

#ifndef EXPECT_FROST_PICTURE_CUT
#define EXPECT_FROST_PICTURE_CUT EXPECT_STOCK_PICTURE_CUT
#endif
#ifndef EXPECT_NIGHT_PICTURE_CUT
#define EXPECT_NIGHT_PICTURE_CUT EXPECT_STOCK_PICTURE_CUT
#endif
/* The installing screen's tile under Prisma's number (the application's cut, pour_ui.h by ui_f.look, whatever look
 * stands under it): 0, rounded as under every other number (the inner shape rounded by POUR_INNER_R); else its
 * corners are cut off at 45 degrees, this many px along each edge of its outline, the inner shape POUR_RIM px
 * inside that. */
#ifndef EXPECT_PRISM_POUR_CUT
#define EXPECT_PRISM_POUR_CUT 0
#endif
#ifndef EXPECT_PRISM_PICTURE_CUT
#define EXPECT_PRISM_PICTURE_CUT EXPECT_STOCK_PICTURE_CUT
#endif
#ifndef EXPECT_FAKE_PICTURE_CUT
#define EXPECT_FAKE_PICTURE_CUT EXPECT_STOCK_PICTURE_CUT
#endif
#ifndef EXPECT_FROST_GROUND_SUN
#define EXPECT_FROST_GROUND_SUN EXPECT_STOCK_GROUND_SUN
#endif
#ifndef EXPECT_NIGHT_GROUND_SUN
#define EXPECT_NIGHT_GROUND_SUN EXPECT_STOCK_GROUND_SUN
#endif
#ifndef EXPECT_PRISM_GROUND_SUN
#define EXPECT_PRISM_GROUND_SUN EXPECT_STOCK_GROUND_SUN
#endif
#ifndef EXPECT_FAKE_GROUND_SUN
#define EXPECT_FAKE_GROUND_SUN EXPECT_STOCK_GROUND_SUN
#endif

typedef struct expect {
    const char *look;           /* the look's name (look.name) */
    int wrench_x, wrench_y, pattern, picture_cut, ground_sun;
} expect;

#define EXPECT_ROW(name, NAME) {#name, EXPECT_##NAME##_WRENCH_X, EXPECT_##NAME##_WRENCH_Y, EXPECT_##NAME##_PATTERN, \
                                EXPECT_##NAME##_PICTURE_CUT, EXPECT_##NAME##_GROUND_SUN},
static const expect EXPECTS[] = {EXPECT_ROW(stock, STOCK) EXPECT_ROW(frost, FROST) EXPECT_ROW(night, NIGHT)
                                 EXPECT_ROW(prism, PRISM) EXPECT_ROW(fake, FAKE)};
#undef EXPECT_ROW

/* What the tests expect of the look of that name (by name: the test-only look stands under another's number);
 * Stock's for a name without a row. */
static const expect *expect_of(const char *look)
{
    for (size_t k = 0; k < sizeof EXPECTS / sizeof *EXPECTS; k++)
        if (look && !strcmp(EXPECTS[k].look, look))
            return &EXPECTS[k];
    return &EXPECTS[0];
}

#endif
