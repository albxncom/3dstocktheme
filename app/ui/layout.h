/* Every colour, size and position of the interface, in console pixels. Layout from the approved prototype (each
 * board there is the top screen, 400x240, above the bottom screen, 320x240; values inside a board are console
 * pixels); colours, gradients, edges, shadows, radii and the bar from measurements of screenshots of the console's
 * System Settings (two pairs, 2026-11-12 and 2026-11-13, measured pixel by pixel; nothing taken from them). uikit/art/art.py reads the lines of the form "#define NAME <integer>" to draw the sprites at these sizes and in
 * these colours, so a value changes here and nowhere else. Lines with an expression are for the C code only.
 *
 * Text: the console's system font, whose size is given as the prototype's font size in px (ui_text). Baselines
 * are worked out from the prototype's line boxes: the middle of the box plus half the cap height. */
#ifndef LAYOUT_H
#define LAYOUT_H

#include "ui_layout.h"        /* the kit's: every colour, what uikit/ uses and what the kit's sprites are drawn by */

/* ---------------------------------------------------------------- top screen */

/* A symbol in a line of text (the status line's house, the way's house and chevron, the step dots, the music note
 * and the check in a row) stands on the text's baseline and reaches its cap height; on the top screen's plain
 * background it has no light line under it. Cap heights in px, measured on a console screenshot of 0.6.0 (the
 * status line's digit: rows 213 to 222) and from FONT_CAP for the other sizes. */
#define CAP_WAY 10                  /* WAY_PX */
#define CAP_ROW_SUB 8               /* ROW_SUB_PX */

/* Start, Designs, Badges: a large card between two dotted lines, the footer below */
#define HERO_CARD_Y 33
#define HERO_CARD_H 151
#define HERO_FOOT_DOTS_Y 194
#define HERO_ICON 64                /* the start screen's logo: a picture of the turning house */
#define HERO_GAP 4
#define HERO_PX 30
#define HERO_LH 33

/* the card's content: a theme (icon, name, tags, source line) or a folder (symbol, name, "Ordner") */
#define DETAIL_GAP 14
#define NAME_PX 20
#define NAME_LH 23
#define NAME_MIN_PX 15              /* a long name shrinks down to this, then is cut */
#define DETAIL_SPACE 6              /* between name and note */
#define TAG_ICON_GAP 4              /* between a small symbol and its text (the steps' title, a row's music) */
#define NOTE_PX 13
#define NOTE_LH 19
#define NOTE_LINES 2                /* the source line wraps to at most this many lines */
/* nothing chosen: the same card with dashed placeholders for the picture and the name */
#define EMPTY_NAME_W 168           /* about a typical name; its height is the name's cap height (art.py, app_ui.c) */
#define EMPTY_NAME_MORE 4          /* ... and this much more: EMPTY_NAME_BELOW of it under the baseline, the rest
                                      above the capitals (0.9.2: 2 above; R8: 1 px more each way) */
#define EMPTY_NAME_BELOW 1
#define EMPTY_DASH 4                /* dashes of 4 px with 3 px between, 1.5 px wide */
#define EMPTY_DASH_GAP 3
#define EMPTY_STROKE_10 15

/* Installing, Downloading: icon and name in a row, the progress bar, the steps */
#define SMALL_ICON 40
#define SMALL_GAP 12
#define CARD_SPACE 12
#define PROGRESS_GLIDE_PCT 18       /* the bar glides this share of the way to the real value in each sixtieth of a
                                       second, by the clock (progress_glide) */
#define PROGRESS_SNAP 0.001f        /* ... and takes it once this close, so a finished bar is full */
#define DONE_HOLD_MS 400            /* the installing screen with all four steps green, before "Installiert" */
#define STEP_PX 12
#define STEP_LH 18

/* Done: the check in a circle, the name, the way to the theme in the HOME Menu */
#define DONE_BADGE 34
#define DONE_CHECK 20
#define WAY_LH 18                   /* the way's line box */
#define WAY_PX 13
#define WAY_GAP 6

/* QR-Code erkannt: globe and the whole address, wrapped over up to URL_LINES lines (shortened in the middle when it
 * does not fit), centred in the card; the target folder in the footer */
#define GLOBE 44
#define HOST_PX 22
#define HOST_LH 25
#define URL_MIN_PX 16
#define URL_LINES 4

/* problems: a warning symbol beside the title */
#define PROBLEM_TITLE_LINES 2

/* QR scan: the corner brackets over the camera picture, 150 px square, centred */
#define BRACKET 150
#define BRACKET_ARM 30
#define BRACKET_INSET 4
#define BRACKET_STROKE 4

/* ---------------------------------------------------------------- bottom screen */

/* Start, Designs, Badges: two tiles. 0.9.2: lower and a little shorter (were 156 high from y 16), so that the start
 * screen's settings button in the top left corner (CORNER_W x CORNER_H) meets neither the first tile nor the cursor's
 * corners round it at their outermost (SELECT_OUT + SELECT_AMP + SELECT_W = 11 px beyond the tile), and those corners
 * end above the bar's shadow (BAR_Y - BAR_SHADOW = 200) */
#define TILE_Y 44
#define TILE_W 142
#define TILE_X2 166
#define TILE_GAP 8
/* the rest of this group and its comment: uikit/include/ui_layout.h */
#define TILE_SYMBOL_DROP 3          /* a glass symbol's shadow (MAIN_ICON) reaches this many px below it */
#define SWATCH_FAN 35
#define SWATCH_CLEAR_TINT 80
#define MAIN_PX 20
#define MAIN_LH 30
/* the Designs screen: two tiles (TILE_X1, TILE_X2, TILE_W) in a first row, under them one wide tile across both
 * ("Installierte Designs": its symbol left of the label); the tiles' material drawn at these heights */
#define DTILE_Y 10
#define WIDE_Y 130
#define DESIGNS_PX 15
#define DESIGNS_LH 18
#define DESIGNS_GAP 6               /* between symbol and label, in a tile and in the wide one */
/* the rest of this group and its comment: uikit/include/ui_layout.h */
#define FOCUS 9                    /* how far the selection reaches out (SELECT_OUT + SELECT_AMP + its haze): a list
                                       keeps it in view */
#define PRESS_MS 110                /* A shows the push this long before it acts */

/* the rest of this group and its comment: uikit/include/ui_layout.h */
#define LIST_X 8
#define LIST_BOTTOM 200
#define LIST_W 290
#define LIST_GAP 4
/* At scroll 0 the first row starts this far down: below the button by FOCUS + 1 (0.9.2; it was LIST_GAP, and the
 * button covered the first row's selection where it reaches out), so no row is hidden at rest; scrolled, rows pass
 * under the button. A row the cursor moves to always shows whole below this line. */
#define LIST_INSET 42
#define DESIGNS_INSET 15            /* the installed designs have no corner button: their first row leaves the
                                       selection its whole reach below the screen's edge (SELECT_OUT, SELECT_AMP and
                                       SELECT_FADE; 0.9.2: it was 8, the track's top) */
#define LIST_END_PAD 8              /* at a list's end the last row stops this far above LIST_BOTTOM, so its
                                       selection stays clear of the tabs' shadow (0.9.2) */
#define ROW_ICON 40
#define ROW_ICON_X 5
#define ROW_TEXT_X 55
#define ROW_NAME_LH 17
#define ROW_SUB_LH 16
#define ROW_SUB_GAP 2

#define ROW_WARN 24
#define SCROLL_X 304
#define SCROLL_Y 8                  /* the track, from here down to LIST_BOTTOM */
#define SCROLL_HIT_X 298            /* the scroll bar takes touches from here to the screen's edge */

/* one big green button: Install, Download */
#define GO_X 12
#define GO_Y 62
#define GO_W 296
#define GO_NOTE_Y 156               /* a warning below it (music the theme does not play) */
/* a theme with music: the green button and under it one as large in the tiles' cream, "Ohne Musik installieren" */
#define GO_TWO_Y1 18
#define GO_TWO_Y2 114
/* under a box of two facts at ACT_FACTS_Y (a badge set's check, from the card or after a download): as far below the
 * box as the tiles of a theme's check (R12; at GO_Y the button covered the box's lower 8 px) */
#define GO_FACTS_Y ACT_TILE_Y

/* a theme that is installed already: the check with "Schon installiert als Design N" and the file with its folder
 * ("/Themes/name.zip") in that box, then "Design löschen" and "Datei löschen" (both symbols in COL_ORANGE, the
 * colour of deleting) as the Done screen's two tiles, above the tabs */
#define ACT_FACTS_Y 10
#define ACT_TILE_Y 82
/* one installed design: the house with where HOME lists it in that box; under it the one tile, the two centred
 * above the tab. With its file still in /Themes the box has that row too (the file symbol, "/Themes/name.zip"), and
 * box and tile stand at ACT_FACTS_Y and ACT_TILE_Y */
#define DESIGN_FACTS_Y 20
#define DESIGN_TILE_Y 70

/* Installing: two symbols, what not to do, side by side in one box of the top screen's card material (information,
 * not a button), centred on the bottom screen with even padding */
#define SAFE_BOX_W 288
#define SAFE_BOX_H 132
#define SAFE_BOX_PAD 12
#define SAFE_ICON 52
#define SAFE_GAP 10
#define SAFE_PX 14
#define SAFE_LH 18

/* Done: the question, the file with its folder in the box of facts, Keep and Delete. A long name makes the box as
 * wide as the screen allows, under the help button's right half: question and box stand HELP_AIR px lower than
 * 0.9.3's (R10: the box touched the button); the box DONE_FACTS_AIR below the help button's lower edge in every
 * look (ui_help_low: a look's button may be larger, G1-R1 Q1); both 4 px lower than G1's (G1-R1 Q2) */
#define HELP_AIR 4                  /* at least between the help button and a box under it */
#define ASK_Y 26
#define ASK_PX 17
#define ASK_LH 20
#define DONE_FACTS_AIR 10
#define CHOICE_Y 114
#define CHOICE_GAP 6
#define CHOICE_PX 16
#define CHOICE_LH 24
/* Done without the question (DN, C70): the way to the entry in the HOME Menu on a pill of the card's material,
 * centred, PILL_AIR above the bar's shadow; as wide as the way, at most the screen less TILE_X1 on each side */
#define PILL_H 30
#define PILL_PAD 14
#define PILL_AIR 12
#define PILL_Y (BAR_Y - BAR_SHADOW - PILL_AIR - PILL_H)

/* QR scan: symbol and hint centred above the bar */
#define SCAN_ICON 64
#define SCAN_GAP 12
#define SCAN_PX 17
#define SCAN_LH 21

/* the rest of this group and its comment: uikit/include/ui_layout.h */
#define DEPTH_TITLE 2               /* the big title on the Designs and Badges screens, the installing screen's */
#define DEPTH_HINT 0                /* the line under that title ("HOME-Menü > Design ändern") */
#define DEPTH_ICON 4                /* the solid symbols: the start screen's turning house, a screen's big symbol,
                                     * a theme's icon, the folder, the warning triangle, the check, the globe */
#define DEPTH_NAME 0                /* the name in a card: theme, folder, file, a problem's title */
#define DEPTH_FRAME 2               /* the scan frame's corners over it */
/* 0.7.0, the concepts chosen on the canvas (boards E1, F1, F2, G1, G2): behind the screen's plane at -2 */
#define DEPTH_BADGE 4               /* installed: the check over the picture */
#define DEPTH_STEP_NOW DEPTH_BADGE  /* installing: the step under way stands in front of the card as a badge does; the
                                       steps to come and the done ones lie on the card (item 29) */

/* the rest of this group and its comment: uikit/include/ui_layout.h */
#define PLATIN_CUT_BOOST 10

/* The start screen (0.9.2, as chosen on the canvas from the logo sketches' rounds R, S and B): the logo is a house of
 * glass that turns all the way about its vertical axis; beside it the word in glass (0.9.3, below); behind
 * everything a tile of soft shapes that repeats and drifts. The glass (GLASS_*, ui.h) and the pattern
 * (PATTERN_*) are choices of the settings.
 *
 * The turn: the application has no 3D code, so art.py draws LOGO_FRAMES pictures of the slab, HERO_ICON px square
 * and LOGO_STEP degrees apart, from edge-on over face-on to edge-on. The slab looks the same after half a turn, so
 * half a turn is stored and a whole turn shows it twice; a picture stays LOGO_TICKS sixtieths of a second (8 s a
 * turn, by the clock). One texture LOGO_SHEET px square per scheme, the part's pictures (ui_part_pictures). With the
 * 3D slider up each eye takes a picture LOGO_EYE steps before or after the flat view's (the slider scales it,
 * rounded), in front of the screen by DEPTH_ICON.
 * The pattern: a tile PATTERN_TILE px square, light and dark shapes at PATTERN_ALPHA % over the background, read
 * with linear filtering and repeated; it moves PATTERN_DRIFT px a second, so a tile's width in one whole turn. */
#define LOGO_SHEET 512
#define LOGO_STEP 3
#define LOGO_FRAMES 60
#define LOGO_TICKS 4
#define LOGO_EYE 1
#define LOGO_THICK_10 30            /* half the slab's thickness, in tenths of a px */
#define LOGO_SWEEP 70               /* how far its highlight bands travel over the face from face-on to edge-on */
#define DEPTH_WORD 2                /* the word, between the screen's plane and the house (DEPTH_ICON) */

/* The start screen's top (0.9.3, round 13, stage B): no box; the turning house (HERO_ICON px) at START_HOUSE_X,
 * START_HOUSE_Y and beside it the word "3DStock" over "Theme" in glass letters of the chosen style, its image
 * WORD_W x WORD_H at START_WORD_X, START_WORD_Y. The two lines are sprites of the glass sheet of their own, rows
 * WORD_TOP_ROW and WORD_LOW_ROW of that image down (they overlap: the first line's shadow reaches over the second's
 * top). They breathe: each moves down by WORD_TRAVEL px and back on a raised cosine, one breath per half turn of
 * the house (LOGO_FRAMES x LOGO_TICKS sixtieths, so house and word loop together), the lower line WORD_LAG_MS
 * behind the upper one; drawn at fractional places with linear filtering, never rounded to whole pixels. */
#define START_HOUSE_X 65
#define START_HOUSE_Y 65
#define START_WORD_X 139
#define START_WORD_Y 43
#define WORD_W 201
#define WORD_H 108
#define WORD_TOP_ROW 4
#define WORD_TOP_H 49
#define WORD_LOW_ROW 45
#define WORD_LOW_H 59
#define WORD_TRAVEL 2
#define WORD_LAG_MS 500

/* G1, the list and the check: the current item's picture in front over the card, its neighbours beside it (the
 * carousel's block at the end) */
#define GAL_DOTS_Y 34
#define GAL_CARD_Y 100
#define GAL_CARD_H 86
#define GAL_PICTURE 80
#define GAL_PICTURE_X 160
#define GAL_PICTURE_Y (48 + GAL_LOWER)  /* 4 + GAL_LOWER px under the dotted line above it (rows 41 to 43) */
/* a theme with music: a round badge of glass with the note on the picture's lower right corner, its centre
 * GAL_BADGE_IN px in from the corner, one step in front in 3D (DEPTH_BADGE), the same make as the check on
 * "Installiert"; both in the three glass styles, each image BADGE_PAD px larger than its disc all round: the room
 * the glass's shadow needs below it to fade out inside the image (4 px cut it at up to 56 of 255) */
#define GAL_BADGE 24
#define GAL_BADGE_IN 5
#define GAL_BADGE_NOTE 14
#define BADGE_PAD 7
#define GAL_NAME_PX 19              /* the name, centred to the eye between the picture and the card's lower edge */
#define GAL_NAME_UP 2               /* ... then this far up, for more room under it (0.9.2); its placeholder too */
#define GAL_FOLDER 64               /* a folder's symbol in the picture's place */

/* F2, installing: a title in front, the name, the four steps as drops along a line */
#define STEPS_TITLE_Y 10
#define STEPS_TITLE_PX 20
#define STEPS_TITLE_ICON 20
#define STEPS_DOTS_Y 44
#define STEPS_CARD_Y 56
#define STEPS_CARD_H 126
#define STEPS_NAME_Y 66
#define STEPS_NAME_PX 15
#define STEPS_LINE_X 32            /* the line from the card's inner edge to the last drop's centre: the stretch up to
                                    * the first drop is that step's, so every drop has a stretch that ends at it */
#define STEPS_X1 77                 /* the drops' centres */
#define STEPS_X2 157
#define STEPS_X3 241
#define STEPS_X4 321
#define STEPS_CY 117
#define STEP_DROP_DONE 38
#define STEP_DROP_NOW 34
#define STEP_DROP_LATER 26
/* the step under way: its bead breathes (STEP_PULSE_MS a cycle, from STEP_DROP_NOW to STEP_PULSE_PX more across, by
 * scaling its image); only the line shows progress */
#define STEP_PULSE_MS 1000
#define STEP_PULSE_PX 2
/* Shard's two marks as glass (0.9.2; 0.9.3: Clear's and Platin's are round 12's, layout.h "installing"): the step
 * under way a raised bead, STEP_DROP_NOW px, no yellow (Shard's in the tone of the
 * mark to come, COL_DOTS, its ring), with a soft light on its upper left (STEP_BEAD_LIGHT % at most) and a shade
 * towards its lower right (STEP_BEAD_SHADE %). A finished step: the check badge's make at STEP_DROP_DONE px. Both
 * images BADGE_PAD px larger than the mark all round. */
#define STEP_BEAD_LIGHT 55
#define STEP_BEAD_SHADE 22
#define STEPS_LABEL_Y 146
#define STEPS_LABEL_PX 11

/* G2, installed: the picture in front over the card, the check on it, the text and the way under it */
#define DONE_CARD_Y 96
#define DONE_CARD_H 94
#define DONE_PICTURE 64
#define DONE_PICTURE_X 168
#define DONE_PICTURE_Y 54
#define DONE_BADGE_X 214
#define DONE_BADGE_Y 100
#define DONE_TEXT_Y 134
#define DONE_TEXT_GAP 6
#define DONE_DOTS_Y 198

/* What moves, moves alike (uikit/include/ease.h: one clock, two curves). Coming in eases out (ease_out: a screen's
 * content, the loader's cover and ring); changing in place has no speed at either end (ease_both: a popup opening and
 * closing, a step mark). The times are related: TRANSITION_MS for a screen, the loader's cover and a step mark;
 * POPUP_IN_MS for a popup opening; POPUP_OUT_MS for what goes away (a popup closing, the loader's ring).
 * Screen changes: uikit/include/ui_layout.h (TRANSITION_*). */
#define FRAME_MS 16                 /* a frame at 60 a second, where a clock is started one frame back */
/* The start (ST-B): its first frame is the start screen's first one coming in, under a black over both screens
 * that is gone within START_BLACK_MS, eased as the screen comes in, on a clock of its own from the second frame. */
#define START_BLACK_MS 250
/* A popup: the screen behind dims and the panel grows from POPUP_ZOOM_PCT % about its middle and fades in, in
 * POPUP_IN_MS; closing, the same way back in POPUP_OUT_MS, and what its button starts (a delete) waits for that.
 * Both by the clock, with no speed at either end. */
#define POPUP_IN_MS 180
#define POPUP_OUT_MS 150

/* the rest of this group and its comment: uikit/include/ui_layout.h */
#define BODY_PX 13
#define BODY_LH_PCT 140             /* line height in percent of the font size */

/* the rest of this group and its comment: uikit/include/ui_layout.h */
#define LOADER_FADE_MS TRANSITION_MS    /* the content leaves, and comes back, within this, eased as a screen */
#define LOADER_OUT_MS POPUP_OUT_MS  /* the ring fades within this once the job is done, as a popup goes */
/* The installing screen's step marks in motion (items 25 and 29): a mark whose step begins or is done moves into its
 * new state within STEP_MOVE_MS, with no speed at either end (ease.h, ease_both; a popup's curve, a screen's time).
 * A step to come is the flat small circle (STEP_DROP_LATER); the step under way is a raised bead in that circle's
 * tone (STEP_DROP_NOW, breathing by STEP_PULSE_PX once in STEP_PULSE_MS), which comes in over the circle while it
 * grows from the circle's size, and stands DEPTH_STEP_NOW in front of the card in stereo; a done step is the check
 * (STEP_DROP_DONE), which comes in over the bead, growing from the bead's size, and lies on the card again. The
 * label's ink goes along. What exactly shows at each moment: progress_mark_look (progress.h). The images and the line
 * are the glass style's (0.9.3: layout.h "installing", app_ui.c STEP_ART). The screen itself comes in as every screen does, its marks with it.
 * The end, by the clock: once the install's work is over the bar may take INSTALL_GLIDE_MS to glide to its end, and
 * "Installiert" comes when all four steps have shown done for DONE_HOLD_MS (the last mark's motion is within that),
 * so never later than INSTALL_GLIDE_MS + DONE_HOLD_MS after the work, whatever the marks do. */
#define STEP_MOVE_MS TRANSITION_MS
#define STEP_GROWN_PCT 80           /* of the way to done: the check has its own size from here on (drawn on whole
                                       pixels again); the rest of the way is its opacity alone */
#define INSTALL_GLIDE_MS 800

/* ---------------------------------------------------------------- the audio lead's, 0.9.2 */

/* the settings screen: a row a setting, of the folder view's make, its name over the choice that holds, the two
 * lines 3 px closer than in a folder's row, and "2 / 3" on the right. The rows share the room above the bar's
 * shadow (SET_ROW_H): as tall as they fit with SET_AIR px left above the shadow (200), but no taller than SET_H_MAX
 * (R4: Stock's four rows had the pitch of 0.9.3's five and looked cramped; four are 43 px now, three and two
 * SET_H_MAX). More than SET_SHOWN rows (five with the row "Stil", G1 0.7) keep the four's height and scroll, by the
 * row on the D-pad and by the stylus as a list's (main.c, scroll.h; FIX-1): SET_SHOWN show whole at scroll 0, the
 * next one cut at the screen's edge or showing round the tab. The
 * face's image is SET_H tall (Stock's 1 px less than its lowest row's height, look_stock.py set_row_face_h); a
 * taller row stretches its middle (ui.c, ui_row_setting). On the top screen the card lists the choices of the row
 * the cursor is on: one column of six at most (CARD_H / SET_LIST_LH), more in two columns */
#define SET_X 8
#define SET_W 304
#define SET_ROOM (BAR_Y - BAR_SHADOW - SET_AIR - SET_Y)
#define SET_ROW_H(n) ((SET_ROOM - ((n) - 1) * SET_GAP) / (n) < SET_H_MAX ? (SET_ROOM - ((n) - 1) * SET_GAP) / (n) \
                                                                        : SET_H_MAX)
/* FIX-1: under the sounds' two choices the credits of what plays (ui_choices_credits): a line each, small and muted
 * as the rows' notes (SET_NOTE_PX). FIX-2: the choices stand at the card's top (ui_choices_top), the credits at its
 * lower end, the last line's box CARD_PAD above the edge as the choices' first is below it, at least
 * CREDIT_ROOM_MIN between the two (main.c pins it) */
#define CREDIT_PX 11
#define CREDIT_LH 12
#define CREDIT_ROOM_MIN 8

/* ---- carousel (0.9.3, round 14 F3); the rest in uikit/include/ui_layout.h ---- */
/* A pane: the picture under glass in a rim of glass in the chosen style, one sprite of the glass sheet drawn into
 * the same box as the picture and scaled with it. Two classes, each drawn at its own size: big (PANE_BIG px, its rim
 * PANE_BIG_RIM px) for a pane higher than PANE_CLASS_AT px, small below. The rim's inner edge is the picture's own:
 * the class's size less the rim on each side, its corners DETAIL_ICON_R per 48 px as the icons' mask (look_art's mask);
 * the outer edge that radius plus the rim. PANE_PAD: what the image holds round the box for the glass's hairline and
 * shadow (2 px more below). */
#define PANE_BIG 80
#define PANE_SMALL 56
#define PANE_CLASS_AT 68
#define PANE_PAD 5
#define PANE_EMPTY_PCT 26           /* white in an empty pane's hole under its glass (a folder, an unreadable file,
                                       nothing selected): with the glass's own 5 %, round 8's 30 % */
/* the rest of this group and its comment: uikit/include/ui_layout.h */
#define PANE_LIGHT_PCT 34
#define PANE_LIGHT_WIDE_PCT 16
#define PANE_LIGHT 36

/* ---------------------------------------------------------------- installing, 0.9.3 (round 12, liquid glass) */

/* The installing screen's line by glass style (art.py "installing", app_ui.c STEP_ART). Clear (C1) and Platin (P2): a
 * glass tube STEP_TUBE_H across, centred on STEPS_CY, from STEPS_LINE_X to STEP_TUBE_AFTER + STEP_TUBE_TAIL past
 * the last mark's centre, the teal liquid in it at least STEP_FILL_LEAST wide; a step's stretch of liquid ends
 * STEP_TUBE_BEFORE left of its own mark's centre and starts STEP_TUBE_AFTER right of the mark before (the half
 * widths of the mark images under way and done), and waits under a mark while it changes (progress_line_held). The
 * marks are chambers (Clear) or platinum settings (Platin) round the tube, their images reaching along it; a step
 * becoming done fills from the middle (the liquid core, scaled from STEP_CORE_FROM_PCT %, solid at
 * STEP_CORE_SOLID_PCT % of the change, grown at STEP_CORE_GROWN_PCT %). Shard (S1): today's marks, the line a rod
 * of Shard glass STEP_TUBE_H across to the last mark's centre, the teal rod in it at least STEP_ROD_LEAST wide and
 * STEP_ROD_FRONT longer than the line's end (its front is cut on a slant) */
#define STEP_TUBE_H 8
#define STEP_TUBE_BEFORE 22
#define STEP_TUBE_AFTER 24
#define STEP_TUBE_TAIL 3
#define STEP_FILL_LEAST 11
#define STEP_ROD_LEAST 18
#define STEP_ROD_FRONT 2
#define STEP_CORE_FROM_PCT 30
#define STEP_CORE_SOLID_PCT 40
#define STEP_CORE_GROWN_PCT 80
/* The loader (LS1): eight beads of Shard glass LOADER_BEAD across, LOADER_BEAD_R from the middle, the teal one over
 * the pale one as bright as the dot's glow */
/* the rest of this group and its comment: uikit/include/ui_layout.h */


#include "pour_layout.h"      /* the installing screen D3 (R9) */

#endif
