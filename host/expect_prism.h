/* What the host tests expect of Prisma where it differs from Stock on purpose (host/expect.h includes this file when
 * it is there; every expectation not named here is Stock's). */
#ifndef EXPECT_PRISM_H
#define EXPECT_PRISM_H

/* Prisma has no pattern: every top screen's ground is its own (the gradient, the facets' texture, their last rows
 * and the beam from the atlas), so test_ui's A1 check looks for a ground over the whole screen, not the pattern. */
#define EXPECT_PRISM_PATTERN 0

/* The wrench on the settings tab (and the folder's symbol on the corner button): PRISM_CORNER_ICON_X across and
 * PRISM_CORNER_ICON_Y down in the tab, which stands PRISM_CORNER_H high (N3, F1, kept from ds-glass-prism). */
#define EXPECT_PRISM_WRENCH_X 12
#define EXPECT_PRISM_WRENCH_Y 3

/* The installing screen's tile is cut crystal (G2-M3, C48 "geschnittene Ecken"): its corners cut POUR_PRISM_CUT px,
 * the inner shape with it (pour_ui.h cuts the liquid's surface to it), the picture's corners PRISM_ICON_CUT px per
 * 48 as everywhere in this look; test_ui's tile checks hold the liquid and the recess to these shapes. */
#define EXPECT_PRISM_POUR_CUT 11
#define EXPECT_PRISM_PICTURE_CUT 6

/* The beam of light is the ground's on every top screen (look_prism.c beam), so the installing screen draws no drop
 * behind its tile of its own (C147). */
#define EXPECT_PRISM_GROUND_SUN 1

/* The shared points (G1 plan, "The three ports"), each as Prisma has it; verified at g1-prism's step 4 against Stock
 * in the same two-look build (renderer walks, Weiss: the texts' and images' places) and by the tests named:
 * - settings rows R4/N1/F2: SET_ROW is SET_H (34) high, stretched to SET_ROW_H(n) by ui_row_setting; Prisma's four
 *   rows stand 43 high, 46 apart, their texts 4.5 px lower than in a 34 px row (centred), at x 18 and 302 as Stock's.
 * - the wrench N3/F1: in the tab at 12, 3 (EXPECT_PRISM_WRENCH_X/_Y above; test_ui "the wrench"); the tab and the
 *   corner button stand PRISM_CORNER_H (33) high, so the corner's label stands 1 px left and 2 px up of Stock's.
 * - the gap on "Datei loeschen?" R10/N5: HELP is 32 x 32 as Stock's; the question's place on the Done screen equals
 *   Stock's (x 160, y 38.2).
 * - the gap on the check screen R12: every text of the check screens (Designs and Badges) at Stock's place.
 * - the placeholder bar R8: EMPTY_NAME 168 x 18 (art.py name_box, the Stock line's height).
 * - the one list R14/R16: ROW 50 and FOLDER_ROW 38 high as Stock's, TRACK and THUMB stretched by the code; every
 *   text of the lists, the subfolder and the cut list at Stock's place (but the corner's label, above).
 * - the Badges menu's single entrance R11/A2 and the installed set's screen R15/A3: shared code; every text at
 *   Stock's place.
 * - the flicker order R7: 0 texts after a picture's texture, 0 transparent pieces in all 808 lists of the 8-scheme
 *   walks (order.py) and in test_ui's R7 checks with Prisma.
 * - a moving ground on every top screen A1: the beam wanders on ui_f.moving_ms (still in the probe build); test_ui's
 *   A1 checks find Prisma's ground over every top screen (EXPECT_PRISM_PATTERN 0).
 * - D3, the installing screen: the shared glass tile and its places (pour_ui.h, C44) with POUR_* from pour_art.py in
 *   Prisma's tones and its own material, cut crystal (look_prism.py pour_tone, pour_material; G2-M3, C48 "geschnittene
 *   Ecken"): the same images, sizes and places, the tile's corners cut POUR_PRISM_CUT px; test_ui's tile checks with
 *   Prisma, test_look's prism_tile_material.
 * - popups Q3 (done on g1-base 1adb8cd): the paragraphs are laid out in DLG_H - DLG_BTN_H whatever Prisma's panel
 *   (4 px taller); every text of the delete question (0.654, at Stock's places) and of the badge removal's question
 *   (its five lines) equals Stock's in the same build (renderer walks, Weiss).
 */

#endif
