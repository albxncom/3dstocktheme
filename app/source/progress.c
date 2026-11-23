#include "progress.h"

#include <math.h>

#include "install.h"
#include "layout.h"

void progress_start(progress_run *r)
{
    r->shown = 0;
}

float progress_keep(progress_run *r, float f)
{
    if (f < 0)
        return f;
    if (f > 1)
        f = 1;
    if (f > r->shown)
        r->shown = f;
    return r->shown;
}

float progress_passes(uint64_t read, uint64_t size, unsigned passes)
{
    if (!size || !passes)
        return 0;
    float p = (float)((double)read / ((double)size * passes));
    if (p <= 0.9f)
        return p;
    /* beyond nine tenths: half of what is left for each further tenth of the expected reading */
    float left = 0.1f;
    for (p -= 0.9f; p > 0.1f && left > 1e-6f; p -= 0.1f)
        left /= 2;
    return 1.0f - left + left / 2 * (p / 0.1f);
}

float progress_install(int step, float within)
{
    if (within < 0)
        within = 0;
    if (within > 1)
        within = 1;
    switch (step) {
    case STEP_MEASURE: return 0.15f * within;
    case STEP_STAGE: case STEP_TITLE1: case STEP_TMD: return 0.15f;
    case STEP_CONTENT0: return 0.15f + 0.10f * within;
    case STEP_COMMIT1: case STEP_RECORD: case STEP_TITLE2: case STEP_CONTEXT: return 0.25f;
    case STEP_CONTENT: return 0.25f + 0.72f * within;
    case STEP_COMMIT2: return 0.97f;
    case STEP_DONE: return 1.0f;
    default: return 0;
    }
}

/* The first install step of each of the screen's steps, and the end: their bar values bound the steps. */
static const int FIRST[5] = {STEP_MEASURE, STEP_TITLE1, STEP_TITLE2, STEP_COMMIT2, STEP_DONE};

int progress_steps_done(float overall)
{
    int done = 0;
    while (done < 4 && overall >= progress_install(FIRST[done + 1], 0))
        done++;
    return done;
}

/* How far step at is (0 to 1, rising with the bar). */
static float step_within(int at, float overall)
{
    if (at < 0 || at > 3)
        return 0;
    float lo = progress_install(FIRST[at], 0), hi = progress_install(FIRST[at + 1], 0);
    float f = (overall - lo) / (hi - lo);
    return f < 0 ? 0 : f > 1 ? 1 : f;
}

float progress_line_end(float overall, float start, const float x[4])
{
    float end = start;
    for (int k = 0; k < 4; k++) {
        float from = k ? x[k - 1] : start;
        end = from + (x[k] - from) * step_within(k, overall);
        if (end < x[k])
            break;
    }
    return end;
}

float progress_line_held(float end, float start, const float x[4], const float mark[4], float before, float after)
{
    if (!before && !after)
        return end;
    int k = 0;
    while (k < 4 && end >= x[k])
        k++;
    float held = x[3] + after;
    if (k < 4) {
        float from = k ? x[k - 1] : start, a = k ? x[k - 1] + after : start, b = x[k] - before;
        held = a + (b - a) * (end - from) / (x[k] - from);
    }
    for (k = 0; k < 4; k++)
        if (mark[k] < 2) {
            float way = mark[k] > 1 ? mark[k] - 1 : 0, most = x[k] - before + (before + after) * way;
            return held < most ? held : most;
        }
    return held;
}

float progress_glide(float shown, float target, int share, unsigned ms)
{
    float next = shown + (target - shown) * (1.0f - powf(1.0f - (float)share / 100.0f, (float)ms * 60.0f / 1000.0f));
    /* where the share rounds to the whole way, the sum can come out one float step past the target */
    if (target >= shown ? next > target : next < target)
        return target;
    return next;
}

progress_look progress_mark_look(float level, float pulse)
{
    float m = level < 0 ? 0 : level > 2 ? 2 : level, p = pulse < 0 ? 0 : pulse > 1 ? 1 : pulse;
    float breath = STEP_DROP_NOW + STEP_PULSE_PX * p;
    progress_look l = {0, 0, 0, breath, STEP_DROP_DONE, 0};
    if (m <= 1) {
        l.later = 1 - m * m;
        l.now = m;
        l.now_px = STEP_DROP_LATER + (breath - STEP_DROP_LATER) * m;
        l.raised = m;
    } else {
        float way = m - 1, grown = way * 100 / STEP_GROWN_PCT;
        l.now = 1 - way * way;
        l.done = way;
        l.done_px = grown >= 1 ? STEP_DROP_DONE : STEP_DROP_NOW + (STEP_DROP_DONE - STEP_DROP_NOW) * grown;
        l.raised = 1 - way;
    }
    return l;
}

void progress_marks_start(progress_marks *m)
{
    for (int k = 0; k < 4; k++)
        eased_rest(&m->mark[k], k ? 0 : 1);
}

void progress_marks_show(progress_marks *m, int done, uint64_t now, unsigned ms)
{
    for (int k = 0; k < 4; k++)
        eased_to(&m->mark[k], k < done ? 2 : k == done ? 1 : 0, now, ms);
}

float progress_mark(const progress_marks *m, int k, uint64_t now)
{
    return k < 0 || k > 3 ? 0 : eased_at(&m->mark[k], now);
}

int progress_marks_moving(const progress_marks *m, uint64_t now)
{
    for (int k = 0; k < 4; k++)
        if (eased_moving(&m->mark[k], now))
            return 1;
    return 0;
}
