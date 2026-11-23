/* The start screen's turning house, drifting pattern and breathing word as plain numbers (layout.h, LOGO_*,
 * PATTERN_* and WORD_*), so the host tests check them: which stored picture shows at a time and for which eye, how
 * far the pattern has moved (drift.h, the kit's: the pattern is drawn behind every screen), how far the word is
 * down.
 * All by the clock: a late frame skips a picture, it does not slow the turn. */
#ifndef TURN_H
#define TURN_H

#include <math.h>
#include <stdint.h>

#include "drift.h"
#include "layout.h"

/* The picture of the house ms after it started face-on: LOGO_FRAMES pictures are half a turn, one every LOGO_TICKS
 * sixtieths of a second, and a whole turn shows them twice. eye: 0 the flat view, 1 the left eye, -1 the right eye;
 * with the 3D slider (0 to 1) up, an eye's picture is LOGO_EYE pictures on (left) or back (right), the slider
 * scaling that and rounding it: one picture from half-way up, none below. The left eye leads because the near side
 * moves to the right as the pictures go on. */
static inline int turn_picture(uint64_t ms, int eye, float slider)
{
    long step = (long)(ms * 60 / (1000 * LOGO_TICKS) % LOGO_FRAMES);
    long at = (LOGO_FRAMES / 2 + step + eye * (long)(LOGO_EYE * slider + 0.5f)) % LOGO_FRAMES;
    return (int)(at < 0 ? at + LOGO_FRAMES : at);
}

/* Whether the start screen opens with this frame, so that its clock starts and the house stands face-on: it was not
 * drawn in the frame before. frame counts from 1; seen: the frame it was drawn in last, 0 never. A frame draws it
 * once per eye. */
static inline int turn_opens(uint32_t frame, uint32_t seen)
{
    return !seen || frame - seen > 1;
}

/* How far a line of the word is down ms after the start screen opened (0.9.3): it breathes WORD_TRAVEL px down and
 * back on a raised cosine, one breath per half turn of the house, so house and word loop together; lag_ms behind
 * the clock (the lower line follows the upper one). A fraction of a px: the line is drawn where it is, not rounded. */
static inline float turn_breath(uint64_t ms, int lag_ms)
{
    const uint64_t period = (uint64_t)LOGO_FRAMES * LOGO_TICKS * 1000 / 60;
    uint64_t at = (ms + period - (uint64_t)lag_ms % period) % period;
    return WORD_TRAVEL / 2.0f * (1.0f - cosf(6.2831853f * (float)at / (float)period));
}

#endif
