/* What the host tests expect of Nachtglas (uikit/source/look_night.c) where it differs from Stock on purpose;
 * host/expect.h gives Stock's for everything not named here. */
#ifndef EXPECT_NIGHT_H
#define EXPECT_NIGHT_H

/* N3, F1: the settings tab is 1 px taller than Stock's (NG_TAB_H 33) and the wrench, 1 px left of CORNER_PAD, keeps
 * the place it had in the 32 px tab: the tab grew downward, so the air under the wrench, where its shadow falls, is as
 * much as above it. The wrench's element place on the tab; held adds 1 to y. */
#define EXPECT_NIGHT_WRENCH_X 12
#define EXPECT_NIGHT_WRENCH_Y 5

/* A1: the pattern is drawn once over the whole top screen, as in Stock, after the look's own ground (the gradient's
 * strip and its lights) and right before an atlas image. */
#define EXPECT_NIGHT_PATTERN 1

/* The other shared points hold for Nachtglas as for Stock (host/expect.h's defaults), checked so:
 * R4, N1, F2  the settings rows: five at SET_ROW_H(5), Nachtglas's SET_ROW stretched by face_tall, content rect r
 * R10, N5     "Datei loeschen?": its help button's element is HELP_FACE as Stock's, so test_design's 6 px hold
 * R12         the check screen's 12 px: the shared layout (test_design)
 * R8, F4      the placeholder bar: EMPTY_NAME 168 x 18, 0.9.3's drawing in Nachtglas's colours (art.py)
 * R14, R16    the lists: Nachtglas's rows, folder rows and scroll bar (TRACK, THUMB turned with the ground)
 * R11, A2     the Badges menu's single entrance: the shared flow (test_main_badges)
 * R15, A3     the installed set's box: the shared card, drawn by Nachtglas's card
 * R7          the walks: 0 texts after a picture's texture, 0 transparent pieces (test_ui, test_look)
 * D3          the shared glass tile (pour_ui.h, C44) with POUR_* from pour_art.py in the look's tones and its own
 *             material (G2-M2, C48: smoked glass, the loader's light lit from within, no sheen; look_night.py
 *             pour_material): the same names, sizes and places as Stock's; test_ui's tile checks with Nachtglas,
 *             test_look's night_tile_material */

#endif
