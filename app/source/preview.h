/* The preview of a design: its wallpapers for both screens and its music. What it shows comes from the theme's body
 * (body.h), so a file on the card and a design installed in the pack go the same way; the body and the music file
 * are read on a thread of the preview's own, so the frames go on meanwhile. The screen itself is drawn in main.c. */
#ifndef PREVIEW_H
#define PREVIEW_H

#include <stdint.h>

#include "body.h"
#include "tp.h"

enum { PREVIEW_OFF, PREVIEW_LOADING, PREVIEW_READY, PREVIEW_FAILED };

/* Starts reading an open theme's body and music (the theme must stay open and unused until preview_close), or
 * those of the pack's installed content `index` (1 to 349). */
void preview_open_theme(const tp_theme *theme);
void preview_open_content(uint16_t index);

/* Once per frame while the preview's screen shows: PREVIEW_LOADING until the reading is over, then PREVIEW_READY
 * or PREVIEW_FAILED. The call that first answers PREVIEW_READY starts the music: the theme's own when it has a
 * music file and its body switches it on, as the HOME Menu decides it; the menu music pauses either way. */
int preview_poll(void);

/* While PREVIEW_READY: the two wallpapers, and the texels of one that is a texture (bottom: 0 the top screen's). */
const tp_body *preview_walls(void);
const uint8_t *preview_texels(int bottom);

/* One line for the log about the preview as it stands (after it left PREVIEW_LOADING). */
void preview_describe(char *out, size_t size);

/* Ends the preview: waits for a reading still under way, stops the music, gives the memory back. Safe when none
 * is open. */
void preview_close(void);

#endif
