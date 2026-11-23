/* What the host tests expect of Frost where it differs from Stock on purpose (host/expect.h gives Stock's for the
 * rest; the names are the ones expect.h reads). */
/* The shared points (G1 plan, "The three ports"), one line each, as Frost meets them; ui.c's shared layout draws them
 * through Frost's table, so test_ui's checks for every look held check Frost's too (verified 2026-11-15 on 22b7217):
 * - R4, N1, F2 settings rows: SET_ROW's element 34 px high as Stock's, its content rect less FROST_LIP_ROW below
 * - N3, F1 the wrench on the tab: EXPECT_FROST_WRENCH_X and _WRENCH_Y below (test_ui, wrench)
 * - R10, N5 "Datei loeschen?": Frost's HELP is HELP_FACE as every look's (G1-R2), the box under it follows ui_help_low
 * - R12 the check screen: GO and ALT keep Stock's 84 px element height (stretched across), the gap is the layout's
 * - R8, F4 the placeholder bar: EMPTY_NAME 168 x 18, art.py's, as Stock's
 * - R14, R16 the one list: ROW 50, FOLDER_ROW 38 high as Stock's; TRACK 10 x 16 and THUMB in the scheme's glass
 * - R11, A2 the Badges menu's single entrance, R15, A3 the installed set's screen: main.c's and badges_ui.h's flows
 * - R7 the flicker order: 0 texts after a picture's texture, 0 transparent pieces (test_ui, test_look; the walks
 *   but the base's b17/b18 restart-failed screens)
 * - A1 a moving ground on every top screen: the pattern on both screens, EXPECT_FROST_PATTERN below
 * - D3 the installing screen: the shared glass tile (pour_ui.h, C44) with POUR_* from pour_art.py in Frost's tones
 *   and its own material, frosted all over (look_frost.py, pour_material; G2-M1, C48); test_ui's tile checks with
 *   Frost, test_look's frost_tile_material */
#ifndef EXPECT_FROST_H
#define EXPECT_FROST_H

#include "../uikit/include/look_frost.h"

/* F1: the wrench on Frost's settings tab, which is FROST_TAB_GROW taller below, with even air above and below it:
 * FROST_TAB_ICON_DX from CORNER_PAD (as far left as Stock's), its top FROST_TAB_ICON_Y px down the tab (Stock's 4),
 * held one lower */
#define EXPECT_FROST_WRENCH_X (CORNER_PAD + FROST_TAB_ICON_DX)
#define EXPECT_FROST_WRENCH_Y FROST_TAB_ICON_Y
/* A1: the pattern drawn once over the whole top screen with an atlas image right after it, as Stock's */
#define EXPECT_FROST_PATTERN 1

#endif
