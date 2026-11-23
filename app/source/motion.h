/* How a movement on the screens went, for the log (0.9.2: "the fade in animation is not smooth at all and the fade
 * out of the alert is even worse"): a change of screen, a popup opening or closing. Told every frame that is drawn,
 * it watches a movement from the frame that first shows it and says, when its time is over, how long that first
 * frame took to come (the work between the input and the frame is in it), how many frames were drawn and the longest
 * gap between two of them. At 60 frames a second a gap is 16 or 17 ms; 33 ms is a lost frame. Plain C in this header
 * alone, no clock of its own, so the host tests run it. */
#ifndef MOTION_H
#define MOTION_H

#include <stdint.h>

typedef struct motion {
    int active;
    uint64_t began;             /* when its first frame was drawn, ms */
    unsigned length;            /* how long it is watched, ms */
    unsigned first, frames, slowest;
} motion;

typedef struct motion_result {
    unsigned first;             /* ms from the frame before it to the movement's first frame */
    unsigned frames;            /* frames drawn, the first included */
    unsigned ms;                /* from the first to the last of them */
    unsigned slowest;           /* the longest gap between two of them, ms */
    int cut;                    /* the next movement began before its time was over */
} motion_result;

/* A frame was drawn at now, gap ms after the frame before it. begins: this frame is the first of a movement to be
 * watched for that many ms (0: none begins). Returns 1 and fills done when the movement watched so far ended with
 * this frame: its time is over, or the new one cuts it short. */
static inline int motion_frame(motion *m, uint64_t now, unsigned gap, unsigned begins, motion_result *done)
{
    int ended = 0;
    if (m->active) {
        m->frames++;
        if (gap > m->slowest)
            m->slowest = gap;
        unsigned ms = (unsigned)(now - m->began);
        if (ms >= m->length || begins) {
            *done = (motion_result){m->first, m->frames, ms, m->slowest, ms < m->length};
            m->active = 0;
            ended = 1;
        }
    }
    if (begins)
        *m = (motion){1, now, begins, gap, 1, 0};
    return ended;
}

#endif
