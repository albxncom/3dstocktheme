/* What a progress bar shows, in plain C so the host tests run it: one run from 0 to 1 that never goes back.
 *
 * Reading a file counts every byte read, wherever it is in the file: a zip is read from its directory at the end
 * first and then from the start, and a CIA is read once to hash it and once more for the zip inside, so a bar made
 * from the read offset jumped to the end and started again. */
#ifndef PROGRESS_RUN_H
#define PROGRESS_RUN_H

#include <stdint.h>

#include "ease.h"

/* The bar of one piece of work: what it shows so far. */
typedef struct progress_run {
    float shown;
} progress_run;

void progress_start(progress_run *r);
/* f, or what the bar shows already if that is more: the bar never goes back. f < 0 (size unknown) is passed on
 * and leaves the run as it is. */
float progress_keep(progress_run *r, float f);
/* read bytes of passes reads over size bytes: grows with every byte; past the passes expected it slows down
 * towards 1 instead of starting again. */
float progress_passes(uint64_t read, uint64_t size, unsigned passes);
/* The install's bar from its step (install.h, STEP_*) and how far that step is (0 to 1): measuring 0 to 15 %, the
 * catalogue to 25 %, the theme to 97 %, the commits between. */
float progress_install(int step, float within);

/* The installing screen's four steps, all from the install's bar (overall, progress_install) and nothing else, so the
 * line, the drops and the footer's dots never disagree. A step is done when the bar reaches its end;
 * progress_steps_done counts those (0 to 4). progress_line_end is where the line's filled part ends: from start
 * (left of the first drop) through the drops' centres x[0..3], step k filling the stretch from x[k - 1] (start for
 * k = 0) to x[k] in proportion to how far the bar is within that step, so the line reaches a drop's centre exactly
 * when its step is done. */
int progress_steps_done(float overall);
float progress_line_end(float overall, float start, const float x[4]);
/* 0.9.3, the line where a mark's image covers the tube on both sides (Clear and Platin, round 12): end
 * (progress_line_end, mark centre to mark centre) mapped so that step k's stretch runs from after px right of mark
 * k - 1's centre (start for k = 0) to before px left of its own, x[3] + after once all are done; and held under the
 * first mark not yet done (mark[k] < 2, levels as ui_steps takes them) at before px left of its centre until it
 * changes, passing under it as it becomes done. before = after = 0: end as it is. */
float progress_line_held(float end, float start, const float x[4], const float mark[4], float before, float after);

/* The bar on screen gliding after the real value: where a bar at shown is ms later on its way to target, when it
 * covers share percent of what is left in each sixtieth of a second. By the clock, so frames far apart change
 * nothing of how long the glide takes. */
float progress_glide(float shown, float target, int share, unsigned ms);

/* The four steps' marks in motion (0.9.2, item 25: no mark changes from one frame to the next). A mark is at 0 while
 * its step is to come, at 1 while it is under way and at 2 once it is done. When the bar says otherwise it moves
 * there, by the clock, within the time given; sent on before it has arrived (a step done sooner than its mark's
 * motion takes) it goes on from where it is and takes only that time again. So a mark is never ahead of the bar and
 * never more than one motion behind it. */
typedef struct progress_marks {
    eased mark[4];
} progress_marks;

/* A new install: the first step under way, the others to come, all at rest, so they come in with their screen. */
void progress_marks_start(progress_marks *m);
/* The bar says done steps are done (progress_steps_done) at now: each mark is sent to its place, to arrive ms later. */
void progress_marks_show(progress_marks *m, int done, uint64_t now, unsigned ms);
/* Where mark k (0 to 3) is at now: 0 to 2. */
float progress_mark(const progress_marks *m, int k, uint64_t now);
/* Whether any mark is still on its way at now. */
int progress_marks_moving(const progress_marks *m, uint64_t now);

/* What a step mark shows (item 29), from its level (progress_mark: 0 to come, 1 under way, 2 done, between two on
 * its way) and the breath of the step under way (pulse, 0 to 1). Three images lie over one another: the flat small
 * circle of a step to come, the raised bead of the step under way, the check of a done step. To come to under way:
 * the bead comes in over the circle, growing from the circle's size to its own, and the circle goes; under way to
 * done: the check comes in over the bead, growing from the bead's size to its own over the first STEP_GROWN_PCT of
 * the way, and the bead goes. The image underneath goes late (with the square of the way), so the card does not
 * show through the mark half way. raised: how far the mark stands in front of the card in stereo, up with the bead
 * and down again with the check. Nothing here changes from one level to a near one by more than its share. */
typedef struct progress_look {
    float later, now, done;     /* how solid each image is, 0 to 1 */
    float now_px, done_px;      /* how large the bead and the check are, px across */
    float raised;               /* 0 to 1 */
} progress_look;
progress_look progress_mark_look(float level, float pulse);

#endif
