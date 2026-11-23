/* The application's own widgets: the parts of its screens only it has (the start screen, the HOME Menu's way, the
 * gallery of a list's pictures, the install's steps and the installing screen, the QR scan, a theme's row and
 * card), drawn with the drawing layer (uikit/include/ui.h) in app_ui.c; the screens are put together in main.c. */
#ifndef APP_UI_H
#define APP_UI_H

#include "ui.h"
#include "part_art.h"   /* the application's sprites (ids from ART_ALL on), handed to the kit with ui_part */

/* ---------------------------------------------------------------- top screen */

/* Start, Designs, Badges: the big card with a symbol (or the application's icon) and a title; with home and change,
 * the way to the HOME Menu's theme list centred under it (house, home, chevron, change), else NULL, NULL. */
void ui_hero(int symbol, const char *title, const char *home, const char *change);
/* The card for one thing: a theme's icon or a symbol, its name (two lines at most), a note under it (NULL: none). */
void ui_detail(const uint8_t *smdh_icon, int symbol, const char *name, const char *note, uint32_t note_color);
/* The waits (reading a folder, checking a theme, downloading): icon or symbol and name in a row, the progress bar (a
 * sliding piece when fraction < 0), then one line. */
void ui_busy_card(const uint8_t *smdh_icon, int symbol, const char *name, float fraction, const char *line);
/* G2, after an install: the theme's picture in front in the middle pane's glass and rim (as on the check screen) with
 * the check on it, the name and the way to it in the HOME Menu under it (home NULL: no way, after a removal). */
void ui_done_card(const uint8_t *smdh_icon, const char *name, const char *home, const char *change);
/* DN, the Done screen's bottom where it asks nothing: the way to the entry in the HOME Menu (house, home, chevron,
 * change) on a pill of the card's material, centred low above the bar (PILL_*). Information only: no hit, no press. */
void ui_way_pill(const char *home, const char *change);
/* The start screen's top (0.9.3): a pattern drifting behind everything, the word "3DStock" over "Theme" in the
 * chosen glass, its two lines breathing, and beside it the house of glass turning all the way about its vertical
 * axis, all by the clock; with the 3D slider up each eye sees the house a step further or back. */
void ui_start(void);
/* G1, the list's and the check's card: the current item's picture in the middle pane and its neighbours' in the
 * panes beside it, in front of the card (an icon, or a symbol on an empty pane; icon NULL and symbol -1: none), the
 * music badge on the middle pane when music, the name below. */
typedef struct ui_pic {
    const uint8_t *icon;
    int symbol;
} ui_pic;
void ui_gallery(ui_pic now, ui_pic prev, ui_pic next, const char *name, int music);
/* The same place with nothing chosen: only an empty pane and the name's placeholder, where ui_gallery draws them. */
void ui_gallery_empty(void);
/* G1 for a list (0.9.3, round 14 F3; swarm.h): item cursor of count in the middle pane and up to SWARM_SIDE of its
 * neighbours on each side as a swarm, each further one smaller and fainter; pic(i) is item i's picture as for
 * ui_gallery. When cursor changes the panes glide to their new places; list: the list's identity (another list, or
 * one not drawn in the last frame, starts at rest). The music badge once the panes are at rest. */
void ui_swarm(ui_pic (*pic)(size_t i), size_t count, size_t cursor, const void *list, const char *name, int music);
/* F2, installing: the title in front, the name, the four steps as drops on a line filled up to x = line_end, the
 * only thing that shows progress. mark[k] is step k's state: 0 to come (an outline), 1 under way (yellow,
 * breathing, a little forward), 2 done (green with a check, further forward), and anything between while it moves
 * from one to the next (progress.h, progress_marks). Line and marks come from one value. */
void ui_steps(const char *title, const char *name, const char *const steps[4], const float mark[4], float line_end);
#include "pour.h"
/* R9 D3, the installing screen (the install and the badge write): the glass box filling from the balls on the
 * line as p says, picture (an SMDH icon; NULL: symbol, -1 none) as its sticker with name under it, the steps (as many
 * as p's table has) beside it; title while the work runs, done_title at the end, and the way home > what under the
 * footer's dots then. The header, the card and the box are drawn here; the footer's dots are the caller's. */
void ui_pour(const pour *p, const char *title, const char *done_title, const char *name, const uint8_t *picture,
             int symbol, const char *const steps[], const char *home, const char *what);
/* A QR code's address: the globe and the whole address, wrapped over up to URL_LINES lines and shrunk down to
 * URL_MIN_PX; still too long, the middle between host and file name is cut as little as fits ("..."). */
void ui_url_card(const char *url);
/* The QR scan's frame: four corner brackets over the camera picture. */
void ui_brackets(void);

/* ---------------------------------------------------------------- bottom screen */

/* A list row for a theme: icon (NULL: the warning symbol), name, the line under it. */
void ui_row_theme(rect r, int state, const uint8_t *smdh_icon, const char *name, const char *sub, int music,
                  uint32_t sub_color, int installed);

/* ---------------------------------------------------------------- the settings (the audio lead's, 0.9.2) */

/* The settings screen's top, so a change of glass shows there at once: the start screen's word at rest in the
 * chosen glass on the card's right (after ui_choices; the pattern behind the screen: ui_backdrop, first). */
void ui_choices_word(void);

/* FIX-1: under a setting's choices on the card, the credits of what it plays, n lines (who made it, the licence),
 * small and muted, each wrapped to the card's inner width, the last line's box CARD_PAD above the card's lower edge
 * (FIX-2). choices_end: the y under the choices, as ui_choices_top returned it. Returns 1 when every line stands
 * whole and at least CREDIT_ROOM_MIN below the choices, 0 when one is cut or comes closer. */
int ui_choices_credits(float choices_end, const char *const lines[], int n);

#endif
