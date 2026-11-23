/* The installing screen's picture (app/ui/pour.c, R9 D3) against kinds of work, frame by frame on an uneven clock:
 * the level is never ahead of the work, a step's marks never ahead of it either; every piece lands inside its step or
 * within POUR_TRAIL_MS after it (a work the line keeps up with; POUR_CATCH_MS otherwise); never more than two pieces
 * in the air at once; each landing pressed into the surface
 * once, never early; no piece falls into a full tile; holders only move to the left and are all gone at the end; the
 * glow only once the last step is over and the level has caught up; a failed step freezes everything; both step
 * tables. The pieces (SR-TD3a): each falls and turns as the canvas's piece b and lands on the canvas's frame. The
 * holders (SR-TD3b): their poses in order on a release; their swing as wide as the canvas's, at rest within 1.5 s of
 * a stop, by the clock alone (any frames, to the step), not stepped at rest, bounded over a jump of an hour, an
 * emptied one lighter and faster. The line (SR-TD3c): where it is and the level by the clock alone, at any frames.
 * The groups (AN): the holders hang in groups with a wider gap between them, the line brings a group, stands, and
 * gives its pieces one after the other, unevenly, never faster than its speeds.
 * The landing (SR-TD4a): from its touch each piece plays its frames in order, on the floor or at the surface, which it
 * follows, then nothing; none while the tile shows full; by the clock alone; an hour's jump ends every one.
 *
 *   test_pour   prints one line per check and "N failed"; exit 1 if any failed */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "pour.h"

static int failed;

static void check(int ok, const char *what, const char *kind)
{
    printf("%s %s: %s\n", ok ? "ok  " : "FAIL", what, kind);
    failed += !ok;
}

#define START 100000u
#define EPS 1e-4f

/* A kind of work: its level at t ms after the start, and from when it is over (success), or failed. */
typedef struct work {
    const char *name;
    const pour_table *t;
    float (*level)(unsigned t);
    unsigned over;              /* ms: the work succeeded from here */
    unsigned trail;             /* ms the picture may trail: POUR_TRAIL_MS for a step done at once with the line caught
                                   up before it; a work faster than the line, or done at once, POUR_CATCH_MS */
} work;

static int pieces_of(const pour_table *t)
{
    int n = 0;
    for (int k = 0; k < t->steps; k++)
        n += t->balls[k];
    return n;
}

/* Piece i of a table: the work's level that lets it go (pour.c ball_level; 1 past the last) and its part of the box
 * (ball_part). */
static float piece_level(const pour_table *t, int i)
{
    float below = 0;
    for (int k = 0; k < t->steps; below += t->share[k], i -= t->balls[k], k++)
        if (i < t->balls[k])
            return below + t->share[k] * (float)(i + 1) / (float)t->balls[k];
    return 1;
}

static float piece_part(const pour_table *t, int i)
{
    for (int k = 0; k < t->steps; i -= t->balls[k], k++)
        if (i < t->balls[k])
            return 1.0f / ((float)t->steps * (float)t->balls[k]);
    return 0;
}

static float cum(const pour_table *t, int k)
{
    float f = 0;
    for (int s = 0; s < k && s < t->steps; s++)
        f += t->share[s];
    return f;
}

/* The box's level for the work's level: each step an equal part of the box, filled as far as the step has gone. */
static float steps_done(const pour_table *t, float real)
{
    float f = 0;
    for (int s = 0; s < t->steps; s++) {
        float in = (real - cum(t, s)) / t->share[s];
        f += in >= 1 ? 1 : in > 0 ? in : 0;
    }
    return f / (float)t->steps;
}

static float even(unsigned t) { return t >= 8000 ? 1 : t / 8000.0f; }
static float slow(unsigned t) { return t >= 24000 ? 1 : t / 24000.0f; }
/* each step's work done at once, 6 s apart, after a stretch that shows nothing */
static float install_jumps(unsigned t) { return t >= 24000 ? 1 : (float)(t / 6000) * 0.25f; }
static float at_once(unsigned t) { return t >= 16 ? 1 : 0; }
/* as the demonstration build: measuring, a pause, the catalogue, the theme, then the commit reports nothing */
static float demo(unsigned t)
{
    if (t < 1300) return 0.25f * t / 1300;
    if (t < 1800) return 0.25f;
    if (t < 2600) return 0.25f + 0.25f * (t - 1800) / 800;
    if (t < 3000) return 0.5f;
    if (t < 4500) return 0.5f + 0.25f * (t - 3000) / 1500;
    return t < 5900 ? 0.75f : 1;
}
/* each of the write's six stages done at once, 6 s apart (badges_ui.h badges_step: TO) */
static float badge_jumps(unsigned t)
{
    static const float ENDS[6] = {0.08f, 0.45f, 0.78f, 0.82f, 0.9f, 1};
    int k = (int)(t / 6000);
    return k < 1 ? 0 : k >= 6 ? 1 : ENDS[k - 1];
}

static const work WORKS[] = {
    {"install, slowly and evenly", &POUR_INSTALL, slow, 24000, POUR_TRAIL_MS},
    {"install, evenly, faster than the line", &POUR_INSTALL, even, 8000, POUR_CATCH_MS},
    {"install, each step at once", &POUR_INSTALL, install_jumps, 24000, POUR_TRAIL_MS},
    {"install, all at once from the start", &POUR_INSTALL, at_once, 16, POUR_CATCH_MS},
    {"install, as the demo", &POUR_INSTALL, demo, 5900, POUR_CATCH_MS},
    {"badge write, slowly and evenly", &POUR_BADGES, slow, 24000, POUR_TRAIL_MS},
    {"badge write, each stage at once", &POUR_BADGES, badge_jumps, 36000, POUR_TRAIL_MS},
    {"badge write, all at once from the start", &POUR_BADGES, at_once, 16, POUR_CATCH_MS},
    {"badge write, read back in full 3 s before its outcome", &POUR_BADGES, even, 11000, POUR_CATCH_MS},
};
#define N_WORKS ((int)(sizeof WORKS / sizeof WORKS[0]))
#define RUN_MS(w) ((w)->over + POUR_CATCH_MS + POUR_END_MS + 2000)     /* a run to the picture's end and past it */

static const unsigned FRAMES[] = {16, 17, 16, 33, 16, 16, 50, 16, 100, 16, 16, 5};

static void run(const work *w)
{
    pour p;
    pour_start(&p, w->t, START);
    int ahead = 0, marks_ahead = 0, glow_early = 0, backwards = 0, drops = 1, ended = 0, gone = 1, crowd = 0;
    int pressed = 0, into_full = 0, melt_off = 0, melt_full = 0;
    float real = 0, last_x[POUR_BALLS];
    unsigned begun[POUR_STEPS + 1] = {0}, ended_at[POUR_STEPS] = {0};
    for (int i = 0; i < POUR_BALLS; i++)
        last_x[i] = 1e9f;
    unsigned t = 0, stop = RUN_MS(w);
    for (int f = 0; t < stop; f++) {
        t += FRAMES[f % (sizeof FRAMES / sizeof FRAMES[0])];
        float l = w->level(t);
        int how = t >= w->over ? POUR_SUCCEEDED : POUR_WORKING;
        real = fmaxf(real, how == POUR_SUCCEEDED ? 1 : l);
        for (int k = 0; k < w->t->steps; k++) {
            if (!begun[k] && real >= cum(w->t, k) - EPS)
                begun[k] = t;
            if (!ended_at[k] && real >= cum(w->t, k + 1) - EPS)
                ended_at[k] = t;
        }
        pour_work(&p, START + t, l, how);
        float shown = pour_level(&p);
        ahead += shown > steps_done(w->t, real) + EPS;
        for (int k = 0; k < w->t->steps; k++) {
            float m = pour_mark(&p, k);
            marks_ahead += (m > 0 && real < cum(w->t, k) - EPS) ||
                           (m > 1 && (real < cum(w->t, k + 1) - EPS || shown < (float)(k + 1) / w->t->steps - EPS));
        }
        glow_early += pour_glow(&p) > 0 && (how != POUR_SUCCEEDED || shown < 1 - EPS);
        int air = 0, down = 0, soon = 0, turn;
        for (int i = 0; i < p.gone; i++) {
            float way;
            int falling = pour_piece(&p, i, &way, &turn) == 1;
            air += falling;
            into_full += falling && (pour_full(&p) || shown >= 1 - EPS);
            down += p.now >= pour_lands(&p, i);
            soon += p.now >= pour_lands(&p, i) + (2000 + POUR_WAVE_HZ - 1) / POUR_WAVE_HZ;
        }
        crowd += air > 2;
        for (int i = 0; i < p.gone; i++) {     /* SR-TD4a: from its touch a piece plays its frames, then nothing */
            uint64_t land = pour_lands(&p, i);
            int frame, m = pour_melt(&p, i, &frame, &(float){0}, &(float){0});
            int on = p.now >= land && p.now < land + POUR_MELT_MS;
            melt_off += !m != !on || (m && frame != (int)((p.now - land) * POUR_MELT_FRAMES / POUR_MELT_MS));
            melt_full += m && pour_full(&p);
        }
        /* pressed by the wave's steps: never before it landed, at the latest two wave steps after */
        pressed += p.landed > down || p.landed < soon;
        for (int i = 0; i < p.total; i++) {
            float x;
            int ball;
            if (pour_holder(&p, i, &x, &ball)) {
                backwards += x > last_x[i] + EPS;
                last_x[i] = x;
            }
        }
    }
    /* each piece: let go after its step began, risen at most POUR_TRAIL_MS after the step was over */
    for (int i = 0, k = 0, n = 0; i < p.total; i++, n++) {
        while (n >= w->t->balls[k])
            n -= w->t->balls[k++];
        uint64_t land = pour_lands(&p, i);
        drops &= i < p.gone && p.let_go[i] >= START + begun[k] &&
                 land + POUR_RISE_MS <= START + ended_at[k] + w->trail;
    }
    ended = pour_full(&p) && p.full_at <= START + w->over + w->trail && pour_over(&p) && pour_glow(&p) > 0.99f;
    for (int i = 0; i < p.total; i++) {
        float x;
        int ball;
        gone &= !pour_holder(&p, i, &x, &ball) && x <= POUR_LEAVE_X && !ball;
    }
    check(!ahead, "the level is never ahead of the work's steps (each an equal part of the box, full only as the last "
          "ends)", w->name);
    check(!marks_ahead, "the list is never ahead of the work nor of the level", w->name);
    check(drops, "every piece lands inside its step or within the trailing time after it", w->name);
    check(!crowd, "never more than two pieces in the air at once", w->name);
    check(!pressed && p.landed == p.total, "each piece's landing pressed into the surface once (pour_land), never "
          "before it landed, within two wave steps after", w->name);
    check(!into_full, "no piece falls into a full tile", w->name);
    check(!melt_off && !melt_full, "every landed piece plays its frames from the frame of its touch, one each "
          "POUR_MELT_MS / POUR_MELT_FRAMES, then nothing; none while the tile shows full", w->name);
    check(!backwards, "holders only move to the left", w->name);
    check(gone, "every holder has left to the left at the end", w->name);
    check(!glow_early, "the glow only after the last step with the level caught up", w->name);
    check(ended, "the end: full within the trailing time after the work, then glowing for POUR_END_MS", w->name);
}

/* A write that fails half way: from then on nothing moves, whatever is reported, and nothing glows. */
static void frozen(void)
{
    pour p, at;
    pour_start(&p, &POUR_BADGES, START);
    unsigned t = 0;
    for (; t < 5000; t += 16)
        pour_work(&p, START + t, even(t), POUR_WORKING);
    pour_work(&p, START + t, even(t), POUR_FAILED);
    at = p;
    int same = 1;
    for (; t < 12000; t += 16) {
        pour_work(&p, START + t, 1, t < 8000 ? POUR_FAILED : POUR_SUCCEEDED);
        same &= !memcmp(&p, &at, sizeof p) && pour_glow(&p) == 0 && !pour_full(&p);
    }
    check(same && at.gone > 0 && at.gone < at.total, "a failed step freezes everything, no glow", "badge write");
}

static void tables(void)
{
    const float stages[6] = {0.08f, 0.37f, 0.33f, 0.04f, 0.08f, 0.10f};   /* badges_ui.h badges_step FROM, TO */
    const int held[4] = {1, 2, 2, 1};      /* BS: the stages each shown step holds, in order */
    const pour_table *t[2] = {&POUR_INSTALL, &POUR_BADGES};
    int ok = POUR_INSTALL.steps == 4 && POUR_BADGES.steps == 6 && POUR_INSTALL.rows == 4 && POUR_BADGES.rows == 4;
    for (int k = 0; k < 4; k++)
        ok &= POUR_INSTALL.share[k] == 0.25f && POUR_INSTALL.held[k] == 1 && POUR_BADGES.held[k] == held[k];
    for (int k = 0; k < 6; k++)
        ok &= fabsf(POUR_BADGES.share[k] - stages[k]) < EPS;
    for (int n = 0; n < 2; n++) {
        int left = pieces_of(t[n]);
        ok &= fabsf(cum(t[n], t[n]->steps) - 1) < EPS && left <= POUR_BALLS && t[n]->steps <= POUR_STEPS;
        for (int k = 0; k < t[n]->steps; k++)
            ok &= t[n]->balls[k] >= 1 && t[n]->balls[k] <= POUR_STEP_MOST;
        for (int g = 0; left > 0; g++)     /* whole groups */
            left -= g % 2 ? POUR_GROUP_B : POUR_GROUP_A;
        ok &= left == 0;
    }
    check(ok, "both step tables: four quarters; six steps by the write's shares, listed in four rows (one, two, two, "
          "one); each adds up to the full box; each step at most POUR_STEP_MOST pieces, the pieces whole groups",
          "tables");
}

/* SR-TD2, the surface "Antwort" (the canvas's): one landing in the middle pressed into a surface at rest. Over the
 * steps that follow: the deflection's largest (within the canvas's: about 4.4 px at the dip) and at the walls (one soft
 * swell climbing them, the canvas's 1.0 to 2.2 px); between the dip (7 px round the landing, where the surface refills)
 * and each wall at most one crest at any step: no train of waves; from POUR_REST_MS on nothing left, at rest. */
static void surface_answer(void)
{
    pour_wave s;
    memset(&s, 0, sizeof s);
    pour_land(&s, POUR_COLS / 2.0f, 1, 1, 1);
    float most = 0, walls = 0;
    int train = 0, steps = POUR_REST_MS * POUR_WAVE_HZ / 1000, settled = 1;
    for (int n = 0; n < steps; n++) {
        pour_wave_step(&s);
        for (int i = 0; i < POUR_COLS; i++)
            most = fmaxf(most, fabsf(s.w[i]));
        walls = fmaxf(walls, fmaxf(s.w[0], s.w[POUR_COLS - 1]));
        for (int side = 0; side < 2; side++) {
            int crests = 0;
            for (int i = side ? POUR_COLS / 2 + 7 : 1; i < (side ? POUR_COLS - 1 : POUR_COLS / 2 - 7); i++)
                crests += s.w[i] > s.w[i - 1] && s.w[i] >= s.w[i + 1] && s.w[i] > 0.25f;
            train += crests > 1;
        }
    }
    for (int i = 0; i < POUR_COLS; i++)
        settled &= s.w[i] == 0 && s.v[i] == 0;
    char what[200];
    snprintf(what, sizeof what, "the surface's answer to a landing: largest %.2f px (at most 5.6), at the walls %.2f "
             "(0.8 to 2.2), %d steps with more than one crest on a side, at rest after %d ms: %s", most, walls, train,
             POUR_REST_MS, settled && !s.moving ? "yes" : "no");
    check(most <= 5.6f && walls >= 0.8f && walls <= 2.2f && !train && settled && !s.moving, what, "surface");
}

/* The level alone makes no wave (the review's T1): the install run through pour_work from empty to full with every
 * landing's press left out (the model told all its landings were pressed already), so only the level's follow moves
 * the surface. In every frame (16 ms) the surface (pour_heights) is the calm curve under the level, lifted whole: no
 * crest anywhere between the walls, the wave at rest; the level has risen from empty to full meanwhile. */
static void surface_level(void)
{
    pour p;
    pour_start(&p, &POUR_INSTALL, START);
    p.landed = POUR_BALLS;
    float h[POUR_COLS], calm[POUR_COLS], most = 0;
    int crests = 0, moving = 0, frames = 0;
    pour_wave rest;
    memset(&rest, 0, sizeof rest);
    for (unsigned t = 16; t <= 8000 + POUR_CATCH_MS; t += 16, frames++) {
        pour_work(&p, START + t, t >= 8000 ? 1 : t / 8000.0f, t >= 8000 ? POUR_SUCCEEDED : POUR_WORKING);
        pour_heights(&p, h);
        pour_wave_heights(&rest, pour_level(&p), 0, calm);
        for (int i = 1; i + 1 < POUR_COLS; i++)
            crests += h[i] > h[i - 1] + 1e-4f && h[i] >= h[i + 1] - 1e-4f;
        for (int i = 0; i < POUR_COLS; i++)
            most = fmaxf(most, fabsf(h[i] - calm[i]));
        moving += p.wave.moving;
    }
    char what[200];
    snprintf(what, sizeof what, "a rising level lifts the calm curve whole: over %d frames from empty to full (%.2f) "
             "%d crests, %d frames with the wave moving, %.4f px off the calm curve", frames, pour_level(&p), crests,
             moving, most);
    check(!crests && !moving && most < 1e-3f && pour_level(&p) > 1 - 1e-6f, what, "surface");
}

/* The canvas's fall (S/sr/tile4/gen.py timeline, the piece b): from rest at the hanging place, each frame of 30 a
 * second the speed by 600 px/s^2, then the place; it lands on the first frame its foot (8 px under its middle) is 2 px
 * into the surface over the middle (its six middle columns at rest), and is never drawn more than 1 px into the floor.
 * The frame it lands on, j frames after it let go (0 the first), and how far it has fallen on frame j. In the layout C
 * (G2-L) the tile stands 14 px further under the line than on that canvas: the hanging place is at y 68, the inner
 * shape's floor at 170, the fall and its rule as they were. */
static int canvas_landing(float level, float fell[64])
{
    pour_wave rest;
    float h[POUR_COLS], mid = 0, y = 68, v = 0;
    memset(&rest, 0, sizeof rest);
    pour_wave_heights(&rest, level, 0, h);
    for (int c = POUR_COLS / 2 - 3; c < POUR_COLS / 2 + 3; c++)
        mid += h[c] / 6;
    const float surf = 170 - fmaxf(0, mid);
    for (int j = 0; j < 64; j++) {
        v += 600.0f / 30;
        y += v / 30;
        fell[j] = fminf(y, 170 - 8 + 1) - 68;
        if (y + 8 >= surf + 2)
            return j;
    }
    return 64;
}

/* The pieces fall as the canvas's piece b: for both tables and every piece (a work at an even pace, frames of 1 ms),
 * it lands (pour_lands) on the canvas's frame for the tile as full as the pieces before it make it; on each canvas
 * frame of its fall its place is the canvas's (0.01 px); its turn frames go in order, from the first (or the next
 * one), one at a time, never back, up to POUR_LAND_TURN (piece_turn). */
static void piece_fall(void)
{
    const pour_table *t[2] = {&POUR_INSTALL, &POUR_BADGES};
    int frame = 0, place = 0, order = 0, pieces = 0, most = 0;
    for (int n = 0; n < 2; n++) {
        pour p;
        pour_start(&p, t[n], START);
        int last[POUR_BALLS];
        for (int i = 0; i < POUR_BALLS; i++)
            last[i] = -1;
        for (unsigned ms = 1; ms <= 30000; ms++) {
            pour_work(&p, START + ms, even(ms / 2), ms >= 16000 ? POUR_SUCCEEDED : POUR_WORKING);
            for (int i = 0; i < p.gone; i++) {
                float down;
                int turn;
                if (!pour_piece(&p, i, &down, &turn))
                    continue;
                order += last[i] >= 0 && turn != last[i] && turn != last[i] + 1;
                order += last[i] < 0 && turn > 1;
                last[i] = turn;
                most = turn > most ? turn : most;
            }
        }
        float below = 0;
        for (int i = 0, k = 0, b = 0; i < p.total; i++, b++) {
            while (b >= t[n]->balls[k])
                b -= t[n]->balls[k++];
            float fell[64];
            int j = canvas_landing(below, fell);
            uint64_t at = p.let_go[i] + ((uint64_t)j * 1000 + 29) / 30;
            frame += pour_lands(&p, i) != at;
            pieces++;
            for (int f = 0; f < j; f++) {   /* the place and the turn frame on the canvas's frames, replayed */
                pour q = p;
                q.now = p.let_go[i] + (uint64_t)f * 1000 / 30;
                float down;
                int turn;
                if (f * 1000 % 30)          /* off the ms grid: the place a hair later */
                    continue;
                place += !pour_piece(&q, i, &down, &turn) || fabsf(down - fell[f]) > 0.01f;
            }
            below += 1.0f / ((float)t[n]->steps * (float)t[n]->balls[k]);
        }
    }
    char what[240];
    snprintf(what, sizeof what, "the pieces fall as the canvas's: %d pieces, %d not landing on the canvas's frame, %d "
             "places off on its frames, %d turn frames out of order (up to frame %d)", pieces, frame, place, order, most);
    check(pieces == pieces_of(&POUR_INSTALL) + pieces_of(&POUR_BADGES) && !frame && !place && !order &&
          most == POUR_LAND_TURN, what, "pieces");
}

/* SR-TD5, the turn at the touch: every piece touches down in POUR_LAND_TURN, the turn frame its landing is bent from,
 * whatever the length of its fall (counted back from the touch, as the canvas's LAND_TURN). On every work above, both
 * tables, on frames of 5, 16 and 33 ms: at the first frame of a piece's landing the last falling frame drawn showed
 * POUR_LAND_TURN if it was drawn within POUR_TURN_HOLD_MS of the touch (on 5 and 16 ms frames always so), otherwise
 * the turn frame before it; the turn frames never go back, and on 5 and 16 ms frames they step one at a time at most
 * and the first falling frame is the hanging piece's frame 0 or the next one. */
static void piece_turn(void)
{
    static const unsigned DT[3] = {5, 16, 33};
    int pieces = 0, off = 0, before = 0, back = 0, jumps = 0, letgo = 0, unseen = 0;
    for (int f = 0; f < 3; f++)
        for (size_t k = 0; k < sizeof WORKS / sizeof WORKS[0]; k++) {
            const work *w = &WORKS[k];
            unsigned d = DT[f];
            pour p;
            pour_start(&p, w->t, START);
            int last[POUR_BALLS];
            uint64_t drawn[POUR_BALLS] = {0};
            for (int i = 0; i < POUR_BALLS; i++)
                last[i] = -1;
            for (unsigned t = d; t <= w->over + POUR_CATCH_MS + 2000; t += d) {
                pour_work(&p, START + t, w->level(t), t >= w->over ? POUR_SUCCEEDED : POUR_WORKING);
                for (int i = 0; i < p.gone; i++) {
                    float down;
                    int turn;
                    if (pour_piece(&p, i, &down, &turn)) {
                        if (last[i] < 0 && d <= 16)
                            letgo = turn > letgo ? turn : letgo;
                        if (last[i] >= 0) {
                            back += turn < last[i];
                            jumps += d <= 16 && turn > last[i] + 1;
                        }
                        last[i] = turn, drawn[i] = p.now;
                    } else if (last[i] != -2 && p.now >= pour_lands(&p, i)) {   /* the first frame of its landing */
                        pieces++;
                        unseen += last[i] < 0;
                        if (last[i] >= 0 && last[i] != POUR_LAND_TURN) {
                            int ok = last[i] == POUR_LAND_TURN - 1 && pour_lands(&p, i) - drawn[i] > POUR_TURN_HOLD_MS;
                            before += ok, off += !ok;
                        }
                        last[i] = -2;
                    }
                }
            }
        }
    char what[300];
    snprintf(what, sizeof what, "the turn at the touch: %d landings on 5, 16 and 33 ms frames, %d not drawn falling, "
             "%d with the last falling frame off POUR_LAND_TURN, %d the frame before it drawn more than "
             "POUR_TURN_HOLD_MS early; turn frames %d times back, %d steps of more than one, the let-go up to %d",
             pieces, unseen, off, before, back, jumps, letgo);
    int all = 0;
    for (int k = 0; k < N_WORKS; k++)
        all += pieces_of(WORKS[k].t);
    check(pieces == 3 * all && !unseen && !off && !back && !jumps && letgo <= 1, what, "pieces");
}

/* By the clock, not by the frames: one work run on two cadences from the moment a ball is let go (the landing's time
 * fixed) shows the same surface at the same time, to the bit. */
static void surface_clock(void)
{
    pour a, b;
    pour_start(&a, &POUR_INSTALL, START);
    unsigned t = 0;
    while (a.gone < 3)
        pour_work(&a, START + (t += 16), piece_level(&POUR_INSTALL, 2), POUR_WORKING);
    b = a;
    for (unsigned ta = t, tb = t; ta < t + 1600;) {
        pour_work(&a, START + (ta += 16), piece_level(&POUR_INSTALL, 2), POUR_WORKING);
        while (tb < ta)
            pour_work(&b, START + (tb += 5 < ta - tb ? 5 : ta - tb), piece_level(&POUR_INSTALL, 2), POUR_WORKING);
    }
    float ha[POUR_COLS], hb[POUR_COLS];
    pour_heights(&a, ha);
    pour_heights(&b, hb);
    check(a.landed == 3 && !memcmp(ha, hb, sizeof ha) && a.wave.step == b.wave.step,
          "the surface by the clock: frames of 16 ms and of 5 ms, the same surface to the bit", "surface");
}

#define DEG (180 / 3.14159265f)

/* SR-TD3b: on every kind of work, frame by frame, each holder's pose only goes on (holds, opens, lets go, springs back,
 * empty) and a holder let go long enough has shown every one of them; it holds as long as it carries its piece. */
static void holder_poses(void)
{
    int back = 0, skipped = 0, wrong = 0, released = 0;
    for (size_t k = 0; k < sizeof WORKS / sizeof WORKS[0]; k++) {
        const work *w = &WORKS[k];
        pour p;
        int last[POUR_BALLS], seen[POUR_BALLS];
        memset(last, 0, sizeof last), memset(seen, 0, sizeof seen);
        pour_start(&p, w->t, START);
        for (unsigned t = 16; t <= RUN_MS(w); t += 16) {
            pour_work(&p, START + t, w->level(t), t >= w->over ? POUR_SUCCEEDED : POUR_WORKING);
            for (int i = 0; i < p.total; i++) {
                int pose = pour_holder_pose(&p, i);
                float x;
                int ball = 1;
                pour_holder(&p, i, &x, &ball);
                back += pose < last[i];
                wrong += pose < 0 || pose >= POUR_POSES || ball != (pose == POUR_HOLDS);
                last[i] = pose, seen[i] |= 1 << pose;
            }
        }
        for (int i = 0; i < p.gone; i++)
            if (p.now - p.let_go[i] >= POUR_EMPTY_MS)
                released++, skipped += seen[i] != (1 << POUR_POSES) - 1;
    }
    char what[200];
    int all = 0;
    for (int k = 0; k < N_WORKS; k++)
        all += pieces_of(WORKS[k].t);
    snprintf(what, sizeof what, "%d holders let go on %d kinds of work: %d poses going back, %d holders that left one "
             "out, %d frames where it did not answer whether it carries its piece", released, N_WORKS, back, skipped,
             wrong);
    check(released == all && !back && !skipped && !wrong, what, "holder poses");
}

/* One kind of work on frames of ms (16: the console's), from its start for ms_end: the widest angle any holder showed,
 * the widest of each, and the longest it took from a stop of the line until every holder stood still (when the line
 * stayed stopped that long), with how many stops; whether every angle stayed finite. */
typedef struct swing_seen {
    float widest, each[POUR_BALLS], still;
    int stops, finite;
} swing_seen;

static swing_seen swing_run(const work *w, unsigned frame, unsigned ms_end)
{
    swing_seen o = {.finite = 1};
    pour p;
    pour_start(&p, w->t, START);
    unsigned stop = 0;
    int was = 0;
    for (unsigned t = frame; t <= ms_end; t += frame) {
        pour_work(&p, START + t, w->level(t), t >= w->over ? POUR_SUCCEEDED : POUR_WORKING);
        int moving = p.speed > 0, swinging = 0;
        for (int i = 0; i < p.total; i++) {
            float a = fabsf(pour_holder_angle(&p, i)) * DEG;
            o.finite &= isfinite(a);
            o.each[i] = fmaxf(o.each[i], a), o.widest = fmaxf(o.widest, a);
            swinging |= a != 0;
        }
        if (was && !moving)
            stop = t;
        if (moving)
            stop = 0;
        if (stop && !swinging) {
            o.still = fmaxf(o.still, (float)(t - stop) / 1000), o.stops++;
            stop = 0;
        } else if (stop && t - stop > 3000) {    /* never still */
            o.still = 99, o.stops++;
            stop = 0;
        }
        was = moving;
    }
    return o;
}

/* SR-TD3b: the swing on every kind of work on the console's frames: the widest angle within the canvas's 8 to 11
 * degrees with a margin of 1.5 (AN: the take's own POUR_SWING_MOST, pour.h), each holder a little differently,
 * every holder still within 1.5 s of every stop. */
static void swing_bounds(void)
{
    float widest = 0, least = 1e9f, still = 0;
    int stops = 0, finite = 1;
    for (size_t k = 0; k < sizeof WORKS / sizeof WORKS[0]; k++) {
        swing_seen o = swing_run(&WORKS[k], 16, RUN_MS(&WORKS[k]));
        widest = fmaxf(widest, o.widest), still = fmaxf(still, o.still);
        stops += o.stops, finite &= o.finite;
        for (int i = 0; i < POUR_BALLS; i++)
            if (o.each[i] > 1)
                least = fminf(least, o.each[i]);
    }
    char what[200];
    snprintf(what, sizeof what, "the widest angle %.1f degrees (6.5 to %.1f), a holder's widest from %.1f, every holder "
             "still at the latest %.2f s after each of %d stops of the line (1.5)", widest, POUR_SWING_MOST, least, still,
             stops);
    check(finite && widest >= 6.5f && widest <= POUR_SWING_MOST && least < widest - 1 && stops >= 8 && still <= 1.5f, what,
          "swing");
}

/* The install's work reported every 400 ms (evenly, succeeded from 8 s): the level at t, as the frame at t knows it. */
static float reported(unsigned t)
{
    return even(t / 400 * 400);
}

/* SR-TD3b: the swing by the clock: the same work (reported every 400 ms) on frames of 16 ms twice, of 5 ms, 33 ms and
 * uneven ones, each also at the reports' moments: every holder's angle and speed the same to the bit at every 16 ms
 * frame. */
static void swing_clock(void)
{
    static const unsigned STEPS[4] = {16, 5, 33, 0};
    pour c[4];
    for (int n = 0; n < 4; n++)
        pour_start(&c[n], &POUR_INSTALL, START);
    unsigned tc[4] = {0, 0, 0, 0}, f = 0;
    int differ = 0, frames = 0;
    float widest = 0;
    for (unsigned t = 16; t <= 12000; t += 16) {
        for (int n = 0; n < 4; n++)
            while (tc[n] < t) {
                unsigned d = STEPS[n] ? STEPS[n] : FRAMES[f++ % (sizeof FRAMES / sizeof FRAMES[0])];
                unsigned next = tc[n] + d < t ? tc[n] + d : t, report = (tc[n] / 400 + 1) * 400;
                tc[n] = report < next ? report : next;
                pour_work(&c[n], START + tc[n], reported(tc[n]), tc[n] >= 8000 ? POUR_SUCCEEDED : POUR_WORKING);
            }
        pour b = c[0];
        pour_work(&b, START + t, reported(t), t >= 8000 ? POUR_SUCCEEDED : POUR_WORKING);
        for (int n = 1; n < 4; n++)
            differ += memcmp(c[n].swing, b.swing, sizeof b.swing) != 0;
        for (int i = 0; i < b.total; i++)
            widest = fmaxf(widest, fabsf(pour_holder_angle(&b, i)) * DEG);
        frames++;
    }
    char what[200];
    snprintf(what, sizeof what, "the install reported every 400 ms, swinging up to %.1f degrees: of %d frames on 5 ms, "
             "33 ms and uneven frames %d with a holder not the same to the bit as on 16 ms frames (twice)", widest,
             frames, differ);
    check(!differ && widest > 5, what, "swing");
}

/* SR-TD3c: the line by the clock alone (SR-TD3b's two changes to it: it moves in whole ticks counted from the work's
 * start, and a reported level counts from its own frame on): the install reported every 400 ms, run on frames of
 * 16 ms, 5 ms, 33 ms and uneven ones, each also at the reports' moments: at every report the line's place and speed,
 * the pieces let go and when, the pieces landed, the level and every holder's swing the same to the bit. */
static void line_clock(void)
{
    static const unsigned STEPS[4] = {16, 5, 33, 0};
    pour c[4];
    unsigned f = 0;
    for (int n = 0; n < 4; n++)
        pour_start(&c[n], &POUR_INSTALL, START);
    int differ = 0, moments = 0, moving = 0;
    for (unsigned t = 400; t <= 8000 + POUR_CATCH_MS; t += 400) {
        for (int n = 0; n < 4; n++)
            for (unsigned tc = c[n].now - START; tc < t;) {
                unsigned d = STEPS[n] ? STEPS[n] : FRAMES[f++ % (sizeof FRAMES / sizeof FRAMES[0])];
                tc = tc + d < t ? tc + d : t;
                pour_work(&c[n], START + tc, reported(tc), tc >= 8000 ? POUR_SUCCEEDED : POUR_WORKING);
            }
        for (int n = 1; n < 4; n++)
            differ += c[n].way != c[0].way || c[n].speed != c[0].speed || c[n].gone != c[0].gone ||
                      memcmp(c[n].let_go, c[0].let_go, sizeof c[0].let_go) != 0 || c[n].landed != c[0].landed ||
                      pour_level(&c[n]) != pour_level(&c[0]) || memcmp(c[n].swing, c[0].swing, sizeof c[0].swing) != 0;
        moving += c[0].speed > 0;
        moments++;
    }
    char what[200];
    snprintf(what, sizeof what, "reports every 400 ms, %d of %d with the line moving, %d pieces let go: on 5 ms, 33 ms "
             "and uneven frames %d with the line, level or swing not as on 16 ms frames to the bit", moving, moments,
             c[0].gone, differ);
    check(!differ && moving > 3 && c[0].gone == c[0].total, what, "line");
}

/* AN: the pieces come in groups. Where the holders hang: inside a group POUR_GAP px apart, each gap up to
 * POUR_GAP_OFF more or less on whole px, from a group's last to the next group's first POUR_GROUP_GAP; the groups
 * POUR_GROUP_A and POUR_GROUP_B in turn. With the work done at once the line goes at its own pace (frames of 4 ms):
 * never faster than POUR_SPEED, between a group's first and last let-go never faster than POUR_GIVE_SPEED; it stands
 * when a group's first piece is let go, at the earliest POUR_BEAT_MS after it came to stand; inside a group two
 * let-gos are POUR_SPACE_MS to POUR_GIVE_MS apart and not all alike (the longest at least 40 ms more than the
 * shortest); from a group's last to the next group's first at least twice the longest inside a group and at most
 * POUR_BRING_MS; the first piece falls within POUR_BRING_MS of the start. */
static void line_groups(void)
{
    const pour_table *t[2] = {&POUR_INSTALL, &POUR_BADGES};
    int places = 0, fast = 0, moving = 0, early = 0, groups = 0, late = 0;
    unsigned in_least = ~0u, in_most = 0, between_least = ~0u, between_most = 0;
    for (int n = 0; n < 2; n++) {
        pour p;
        int first[POUR_BALLS + 1] = {1};        /* piece i is its group's first */
        pour_start(&p, t[n], START);
        for (int i = 1, at = 0, g = 0; i < p.total; i++) {
            float gap = p.at[i] - p.at[i - 1];
            if (++at == (g % 2 ? POUR_GROUP_B : POUR_GROUP_A))
                at = 0, g++;
            first[i] = !at;
            places += first[i] ? gap != POUR_GROUP_GAP : fabsf(gap - POUR_GAP) > POUR_GAP_OFF || gap != floorf(gap);
        }
        first[p.total] = 1;
        uint64_t stood = 0;
        for (unsigned ms = 4; ms <= POUR_CATCH_MS; ms += 4) {
            int gone = p.gone;
            float was = p.speed;
            pour_work(&p, START + ms, 1, POUR_SUCCEEDED);
            fast += p.speed > POUR_SPEED + EPS || (!first[gone] && p.speed > POUR_GIVE_SPEED + EPS);
            if (was > 0 && p.speed == 0)
                stood = p.now;
            if (p.gone > gone && first[gone])
                groups++, moving += was != 0, early += p.now < stood + POUR_BEAT_MS;
        }
        late += p.gone != p.total || p.let_go[0] > START + POUR_BRING_MS;
        for (int i = 1; i < p.gone; i++) {
            unsigned d = (unsigned)(p.let_go[i] - p.let_go[i - 1]);
            if (first[i])
                between_least = d < between_least ? d : between_least, between_most = d > between_most ? d : between_most;
            else
                in_least = d < in_least ? d : in_least, in_most = d > in_most ? d : in_most;
        }
    }
    char what[400];
    snprintf(what, sizeof what, "%d holders off their places; with the work ahead %d frames of 4 ms with the line too "
             "fast; %d groups, %d with the line moving as their first piece is let go, %d let go before the beat; two "
             "let-gos inside a group %u to %u ms apart (%d to %d), from a group to the next %u to %u ms (at least %u, at "
             "most %d); %d tables with the first piece late or pieces left", places, fast, groups, moving, early,
             in_least, in_most, POUR_SPACE_MS, POUR_GIVE_MS, between_least, between_most, 2 * in_most, POUR_BRING_MS,
             late);
    check(!places && !fast && groups >= 6 && !moving && !early && in_least >= POUR_SPACE_MS && in_most <= POUR_GIVE_MS &&
          in_most >= in_least + 40 && between_least >= 2 * in_most && between_most <= POUR_BRING_MS && !late, what,
          "groups");
}

/* SR-TD3b: at rest nothing is stepped and nothing turns; a clock going back changes nothing; a jump of the clock by an
 * hour while the line is to move (a step's pieces: at most while it gives them, POUR_TRAIL_MS, and
 * POUR_SWING_STILL_MS after) computes a bounded number of steps and leaves every holder still. */
static void swing_rest(void)
{
    pour p;
    pour_start(&p, &POUR_INSTALL, START);
    unsigned t = 0;
    while (t < POUR_CATCH_MS)   /* half the work's pieces let go, the line standing with the next over the mouth */
        pour_work(&p, START + (t += 16), 0.5f, POUR_WORKING);
    uint32_t work = p.swing_work;
    int turned = 0;
    for (unsigned end = t + 2000; t < end;) {
        pour_work(&p, START + (t += 16), 0.5f, POUR_WORKING);
        for (int i = 0; i < p.total; i++)
            turned += pour_holder_angle(&p, i) != 0;
    }
    int rest = p.swing_work == work && !turned && !p.swinging;
    pour q = p;
    pour_work(&q, START + t - 5000, 0.5f, POUR_WORKING);
    int back = !memcmp(&q, &p, sizeof p);
    pour_work(&p, START + (t += 16), 0.75f, POUR_WORKING);     /* the work goes on: the line is to move */
    work = p.swing_work;
    pour_work(&p, START + t + 3600000u, 0.75f, POUR_WORKING);
    uint32_t jump = p.swing_work - work;
    for (int i = 0; i < p.total; i++)
        turned += pour_holder_angle(&p, i) != 0;
    char what[200];
    snprintf(what, sizeof what, "at rest for 2 s %s; the clock back 5 s %s; an hour's jump computes %u steps (at most "
             "%d) and leaves %d holders turned", rest ? "nothing stepped or turned" : "STEPPED OR TURNED",
             back ? "changes nothing" : "CHANGES IT", (unsigned)jump,
             (POUR_TRAIL_MS + POUR_SWING_STILL_MS) * POUR_SWING_HZ / 1000, turned);
    check(rest && back && jump > 0 && jump <= (POUR_TRAIL_MS + POUR_SWING_STILL_MS) * POUR_SWING_HZ / 1000 && !turned,
          what, "swing");
}

/* SR-TD3b: an emptied holder swings lighter and faster: from the same push it turns less far, and given the same speed
 * it comes back through upright sooner; it springs back once when emptied, not again. */
static void swing_light(void)
{
    pour_swing full, light;
    pour_swing_init(&full, 1, 1, 1, 1), pour_swing_init(&light, 1, 1, 1, 1);
    light.light = 1;
    float wide[2] = {0, 0};
    for (int k = 0; k < 2 * POUR_SWING_HZ; k++) {
        float a = k < 7 ? 480 : 0;                          /* a short push */
        pour_swing_step(&full, a), pour_swing_step(&light, a);
        wide[0] = fmaxf(wide[0], fabsf(full.th)), wide[1] = fmaxf(wide[1], fabsf(light.th));
    }
    int cross[2] = {0, 0};
    pour_swing_init(&full, 1, 1, 1, 1), pour_swing_init(&light, 1, 1, 1, 1);
    pour_swing_empty(&light);
    full.om = light.om;
    for (int k = 1; k <= POUR_SWING_HZ && !(cross[0] && cross[1]); k++) {
        float was[2] = {full.th, light.th};
        pour_swing_step(&full, 0), pour_swing_step(&light, 0);
        if (!cross[0] && was[0] * full.th < 0)
            cross[0] = k;
        if (!cross[1] && was[1] * light.th < 0)
            cross[1] = k;
    }
    float om = light.om;
    pour_swing_empty(&light);
    char what[200];
    snprintf(what, sizeof what, "from the line's start a loaded holder %.1f degrees, an emptied one %.1f; from its "
             "spring back upright again after %d steps, a loaded one after %d; a second release %s", wide[0] * DEG,
             wide[1] * DEG, cross[1], cross[0], light.om == om ? "does nothing" : "PUSHES AGAIN");
    check(wide[1] < wide[0] && cross[1] && cross[0] && cross[1] < cross[0] && light.om == om, what, "swing");
}

/* SR-TD4a: the install on frames of 5 ms. On the dry floor (its first piece) and on liquid (every later one) a piece
 * plays its POUR_MELT_FRAMES frames in order from the frame of its touch (frame 0 there, none left out, none again
 * after its last), each landing pressed into the surface once. On the floor its ground is the floor; on liquid it is
 * the surface over the vessel's middle (its six middle columns, at most 1.5 px from where the surface stood in the
 * frame before the touch, 2.2 px under it for frame 0's dive), from there it sinks (at least 4 px by its last frame or
 * with the surface or the floor) and it never lies over the surface: at least 1 px under it (it follows the surface
 * down), and never deeper than its own dive otherwise; the frames it follows the surface are counted. */
static void melt_landing(void)
{
    pour p;
    pour_start(&p, &POUR_INSTALL, START);
    int state[POUR_BALLS] = {0}, last[POUR_BALLS], order = 0, kind = 0, floor_off = 0, over = 0, touch = 0, deep = 0;
    int sunk = 0, follow = 0, frames = 0, presses = 0;
    float before = 0, at_touch[POUR_BALLS], least[POUR_BALLS];
    for (unsigned ms = 5; ms <= 20000; ms += 5) {
        int landed = p.landed;
        pour_work(&p, START + ms, even(ms), ms >= 8000 ? POUR_SUCCEEDED : POUR_WORKING);
        presses += p.landed - landed > 1;
        float h[POUR_COLS], s = 0;
        pour_heights(&p, h);
        for (int c = POUR_COLS / 2 - 3; c < POUR_COLS / 2 + 3; c++)
            s += h[c] / 6;
        for (int i = 0; i < p.gone; i++) {
            int frame, m;
            float g, a;
            if (!(m = pour_melt(&p, i, &frame, &g, &a))) {
                if (state[i] == 1)
                    state[i] = 2, order += last[i] != POUR_MELT_FRAMES - 1, sunk += least[i] > 0 && at_touch[i] - least[i] < 4;
                continue;
            }
            frames++;
            order += state[i] == 2 || (state[i] == 0 && frame != 0) || (state[i] == 1 && frame != last[i] &&
                                                                         frame != last[i] + 1);
            if (state[i] == 0)
                at_touch[i] = least[i] = before, state[i] = 1;
            last[i] = frame;
            kind += m != (i ? 2 : 1);
            if (m == 1) {
                floor_off += g != 0;
                continue;
            }
            least[i] = fminf(least[i], g);
            over += g > fmaxf(s - 1, 0) + EPS;      /* where the blow's dip reaches the floor, on it */
            touch += frame == 0 && fabsf(g - (at_touch[i] - 2.2f)) > 1.5f && g < s - 1 - EPS;
            deep += g < at_touch[i] - 7.5f - 1.5f && g < s - 1 - EPS;
            follow += g >= s - 1 - EPS;
        }
        before = s;
    }
    for (int i = 0; i < p.total; i++)
        order += state[i] != 2;
    char what[320];
    snprintf(what, sizeof what, "%d pieces landed, %d frames melting: %d out of order, %d on the wrong ground kind, on "
             "the floor %d off it; on liquid %d over the surface, %d not at the touch's height, %d deeper than the dive "
             "under it, %d sunk less than 4 px, %d frames following the surface down; %d landings pressed more than once",
             p.landed, frames, order, kind, floor_off, over, touch, deep, sunk, follow, presses);
    check(p.landed == p.total && frames > 0 && !order && !kind && !floor_off && !over && !touch && !deep && !sunk &&
          !presses, what, "melt");
}

/* SR-TD4a: by the clock alone: the install (reported every 400 ms) on frames of 16, 5 and 33 ms and uneven ones, each
 * also at the moments compared (every 20 ms): every piece's melt (whether, its frame, its ground, its alpha) the same
 * to the bit. */
static void melt_clock(void)
{
    static const unsigned STEPS[4] = {16, 5, 33, 0};
    pour c[4];
    unsigned f = 0;
    for (int n = 0; n < 4; n++)
        pour_start(&c[n], &POUR_INSTALL, START);
    int differ = 0, melting = 0, wet = 0;
    for (unsigned t = 20; t <= 12000; t += 20) {
        for (int n = 0; n < 4; n++)
            for (unsigned tc = c[n].now - START; tc < t;) {
                unsigned d = STEPS[n] ? STEPS[n] : FRAMES[f++ % (sizeof FRAMES / sizeof FRAMES[0])];
                tc = tc + d < t ? tc + d : t;
                pour_work(&c[n], START + tc, reported(tc), tc >= 8000 ? POUR_SUCCEEDED : POUR_WORKING);
            }
        for (int i = 0; i < c[0].total; i++) {
            int k0 = 0;
            float g0 = 0, a0 = 0;
            int m0 = pour_melt(&c[0], i, &k0, &g0, &a0);
            melting += m0 != 0, wet += m0 == 2;
            for (int n = 1; n < 4; n++) {
                int k = k0, m;
                float g = g0, a = a0;
                m = pour_melt(&c[n], i, &k, &g, &a);
                differ += m != m0 || k != k0 || g != g0 || a != a0;
            }
        }
    }
    char what[200];
    snprintf(what, sizeof what, "every 20 ms, %d melting (%d on liquid): on 5 ms, 33 ms and uneven frames %d not as on "
             "16 ms frames to the bit", melting, wet, differ);
    check(!differ && melting > 100 && wet > 0, what, "melt");
}

/* SR-TD4a: a jump of the clock by an hour while pieces melt: every melt has ended at once (a melt is the clock's alone,
 * nothing of it is stepped); the rest is caught up within POUR_CATCH_MS and the box shows full and glows. */
static void melt_jump(void)
{
    pour p;
    pour_start(&p, &POUR_INSTALL, START);
    int before = 0, after = 0;
    unsigned t = 0;
    while (!before && t < 20000) {
        t += 16;
        pour_work(&p, START + t, even(t), t >= 8000 ? POUR_SUCCEEDED : POUR_WORKING);
        for (int i = 0; i < p.gone; i++)
            before += pour_melt(&p, i, &(int){0}, &(float){0}, &(float){0}) == 2;
    }
    pour_work(&p, START + t + 3600000u, 1, POUR_SUCCEEDED);
    for (int i = 0; i < p.total; i++)
        after += pour_melt(&p, i, &(int){0}, &(float){0}, &(float){0}) != 0;
    for (unsigned d = 16; d <= POUR_CATCH_MS + 1000; d += 16)      /* then the rest caught up as the line goes */
        pour_work(&p, START + t + 3600000u + d, 1, POUR_SUCCEEDED);
    char what[200];
    snprintf(what, sizeof what, "an hour's jump with %d melting: %d still melting after it, then full %s", before, after,
             pour_full(&p) && pour_glow(&p) > 0 ? "and glowing" : "NOT and glowing");
    check(before && !after && pour_full(&p) && pour_glow(&p) > 0, what, "melt");
}

/* SR-TD4b: the install with one more piece due every 2 s, so that each lands on a surface at rest. */
static float apart(unsigned t)
{
    return piece_level(&POUR_INSTALL, (int)(t / 2000));
}

/* How far the line h lies over the line b on the mean. */
static float mean_rise(const float h[POUR_COLS], const float b[POUR_COLS])
{
    float m = 0;
    for (int c = 0; c < POUR_COLS; c++)
        m += (h[c] - b[c]) / POUR_COLS;
    return m;
}

/* The piece landed at most ms ago (-1: none). */
static int landed_within(const pour *p, unsigned ms)
{
    for (int i = p->gone - 1; i >= 0; i--)
        if (p->now >= pour_lands(p, i) && p->now < pour_lands(p, i) + ms)
            return i;
    return -1;
}

/* A crest of the line d at column c: the highest within 8 columns (none as high on its left), with the line within
 * them lower on both sides by at least 0.05 px (so a flat stretch is none). */
static int crest(const float d[POUR_COLS], int c)
{
    float left = 1e9f, right = 1e9f;
    for (int k = c - 8; k <= c + 8; k++) {
        if (k < 0 || k >= POUR_COLS || k == c)
            continue;
        if (k < c ? d[k] >= d[c] : d[k] > d[c])
            return 0;
        if (k < c)
            left = fminf(left, d[k]);
        else
            right = fminf(right, d[k]);
    }
    return d[c] - left >= 0.05f && d[c] - right >= 0.05f;
}

/* SR-TD4b, the blow on liquid (the canvas's z2): every 1 ms of the job above, over the first 0.1 s after each touch on
 * liquid, against the surface of the frame before the touch: the six middle columns lower, beside them (8 to 24 px
 * from the middle) higher on each side, and on each side one crest only (no ring of waves); then within 1 s the swell
 * has climbed both walls, and from POUR_REST_MS after the touch (the time "Antwort" comes to rest, plus 20 ms) the
 * surface is the calm curve under the level again, the wave at rest. */
static void blow_liquid(void)
{
    pour p;
    pour_start(&p, &POUR_INSTALL, START);
    float prev[POUR_COLS], before[POUR_COLS], h[POUR_COLS], calm[POUR_COLS], wall[POUR_BALLS][2] = {{0}};
    float dip = 1e9f, rim = 1e9f;
    int frames = 0, higher = 0, lower = 0, ring = 0, unsettled = 0, settled = 0, cur = -1;
    pour_wave rest;
    memset(&rest, 0, sizeof rest);
    pour_heights(&p, prev);
    for (unsigned t = 1; t <= 52000; t++) {
        pour_work(&p, START + t, apart(t), t >= 50000 ? POUR_SUCCEEDED : POUR_WORKING);
        pour_heights(&p, h);
        int i = landed_within(&p, 2000);
        if (i != cur)           /* the frame of a touch: the surface of the frame before it */
            cur = i, memcpy(before, prev, sizeof before);
        memcpy(prev, h, sizeof prev);
        if (i > 0 && p.now >= pour_lands(&p, i) + POUR_REST_MS + 20) {
            pour_wave_heights(&rest, pour_level(&p), 0, calm);
            unsettled += p.wave.moving || memcmp(h, calm, sizeof h) != 0;
            settled++;
        }
        if (i > 0 && p.now < pour_lands(&p, i) + 1000)
            for (int side = 0; side < 2; side++)
                wall[i][side] = fmaxf(wall[i][side], h[side ? POUR_COLS - 1 : 0] - before[side ? POUR_COLS - 1 : 0] -
                                                         mean_rise(h, before));
        if (i <= 0 || p.now >= pour_lands(&p, i) + 100)
            continue;
        frames++;
        float under = 0, d[POUR_COLS];
        for (int c = 0; c < POUR_COLS; c++)
            d[c] = h[c] - before[c];
        for (int c = POUR_COLS / 2 - 3; c < POUR_COLS / 2 + 3; c++)
            under += d[c] / 6;
        lower += under < 0;
        dip = fminf(dip, -under);
        for (int side = 0; side < 2; side++) {
            float most = -1e9f;
            int crests = 0;
            for (int x = 8; x <= 24; x++)
                most = fmaxf(most, d[side ? POUR_COLS / 2 + x - 1 : POUR_COLS / 2 - x]);
            higher += most > 0;
            rim = fminf(rim, most);
            for (int x = 4; x < POUR_COLS / 2; x++)
                crests += crest(d, side ? POUR_COLS / 2 + x - 1 : POUR_COLS / 2 - x);
            ring += crests != 1;
        }
    }
    float walls = 1e9f;
    for (int i = 1; i < p.total; i++)
        walls = fminf(walls, fminf(wall[i][0], wall[i][1]));
    char what[400];
    snprintf(what, sizeof what, "%d frames within 0.1 s of a touch on liquid: %d with the middle lower (least by %.2f "
             "px), %d sides higher beside it (least by %.2f px) of %d, %d sides with other than one crest; the swell up "
             "both walls by at least %.2f px; %d frames from POUR_REST_MS + 20 ms after a touch, %d of them not at rest "
             "on the calm curve", frames, lower, dip, higher, rim, 2 * frames, ring, walls, settled, unsettled);
    check(frames == 100 * (p.total - 1) && lower == frames && higher == 2 * frames && !ring && walls > 0.2f &&
          settled > 2000 &&
          !unsettled, what, "blow");
}

/* SR-TD4b, the first pool (the canvas's lens for z2): from the first piece's touch on the dry floor, every 1 ms until
 * it lies over the whole floor, its half width (the farthest wet column from the middle) never shrinks, and its edge
 * runs faster in the first 0.1 s than in the next 0.1 s; it is wet at the touch already. */
static void blow_pool(void)
{
    pour p;
    pour_start(&p, &POUR_INSTALL, START);
    float h[POUR_COLS], half = 0, at[3] = {-1, -1, -1};
    int shrinks = 0, frames = 0;
    for (unsigned t = 1; t <= 4000; t++) {
        pour_work(&p, START + t, apart(t), POUR_WORKING);
        if (!p.gone || p.now < pour_lands(&p, 0) || half >= POUR_COLS / 2)
            continue;
        pour_heights(&p, h);
        float w = 0;
        for (int c = 0; c < POUR_COLS; c++)
            if (h[c] > 0)
                w = fmaxf(w, fabsf(c + 0.5f - POUR_COLS / 2.0f) + 0.5f);
        shrinks += w < half;
        half = w;
        frames++;
        for (int k = 0; k < 3; k++)
            if (p.now == pour_lands(&p, 0) + 100 * (unsigned)k)
                at[k] = w;
    }
    char what[300];
    snprintf(what, sizeof what, "the first pool over %d frames: %d with its half width shrinking; half width at the "
             "touch, 0.1 s and 0.2 s after it %.1f, %.1f, %.1f px (the edge faster first)", frames, shrinks, at[0],
             at[1], at[2]);
    check(frames > 100 && !shrinks && at[0] > 0 && at[1] - at[0] > at[2] - at[1] && at[2] > at[1], what, "blow");
}

/* SR-TD4b, the level: on the job above and on the install done evenly (every 1 ms), it never falls; on the job above a
 * piece's liquid comes with time, not at the touch (at the touch the level is up by at most 10 % of its part, 0.1 s
 * after it by at most a third), and POUR_RISE_MS after the touch it is what it was before SR-TD4b: every piece landed
 * so far its part, to the bit; the mean of the column heights never falls by more than 0.01 px while a piece melts. */
static void blow_level(void)
{
    int falls = 0, jump = 0, early = 0, off = 0, sinks = 0, checked = 0;
    float sink = 0;
    for (int n = 0; n < 2; n++) {
        pour p;
        pour_start(&p, &POUR_INSTALL, START);
        float last = 0, last_mean = 0, h[POUR_COLS];
        for (unsigned t = 1; t <= (n ? 14000u : 52000u); t++) {
            float want = n ? even(t) : apart(t);
            pour_work(&p, START + t, want, t >= (n ? 8000u : 50000u) ? POUR_SUCCEEDED : POUR_WORKING);
            float l = pour_level(&p), mean = 0;
            falls += l < last;
            pour_heights(&p, h);
            for (int c = 0; c < POUR_COLS; c++)
                mean += h[c] / POUR_COLS;
            int i = landed_within(&p, POUR_MELT_MS);
            if (i >= 0 && mean < last_mean - 0.01f)
                sinks++, sink = fmaxf(sink, last_mean - mean);
            if (n == 0 && i >= 0) {
                uint64_t at = pour_lands(&p, i);
                float base = 0, part = piece_part(p.t, i);
                for (int j = 0; j < i; j++)
                    base += piece_part(p.t, j);
                jump += p.now == at && l > base + 0.1f * part + EPS;
                early += p.now == at + 100 && l > base + part / 3 + EPS;
            }
            for (int k = 0; k < p.gone && n == 0; k++)
                if (p.now == pour_lands(&p, k) + POUR_RISE_MS) {
                    float sum = 0;
                    for (int j = 0; j < p.gone && pour_lands(&p, j) + POUR_RISE_MS <= p.now; j++)
                        sum += piece_part(p.t, j);
                    off += fabsf(l - fminf(sum, 1)) > 1e-6f, checked++;
                }
            last = l, last_mean = mean;
        }
    }
    char what[400];
    snprintf(what, sizeof what, "the level: %d frames falling, %d touches with it up at once, %d up by more than a "
             "third 0.1 s after; %d of %d pieces not at their parts POUR_RISE_MS after the touch; the mean height "
             "falling during a melt %d times (most %.3f px)", falls, jump, early, off, checked, sinks, sink);
    check(!falls && !jump && !early && !off && checked == pieces_of(&POUR_INSTALL) && !sinks, what, "blow");
}

/* The mean over the six middle columns of a - b. */
static float mid_drop(const float a[POUR_COLS], const float b[POUR_COLS])
{
    float m = 0;
    for (int c = POUR_COLS / 2 - 3; c < POUR_COLS / 2 + 3; c++)
        m += (a[c] - b[c]) / 6;
    return m;
}

/* SR-TD5, a full tile takes no push: on every work above on frames of 16 and 5 ms, from the frame the tile shows full
 * on no piece lands and nothing reaches its surface (the pieces landed, the last landing's step, the pool and the
 * press as they were, the press never started anew), and a landing and a pool tried on its surface in any of those
 * frames (pour_land, pour_pool) leave the surface's state as it was, to the byte. Just below full a push still shows:
 * on the job above the last piece's blow (into 23 of 24 parts) lowers the middle by at least 0.5 px within 0.1 s of
 * its touch, and the weakest blow (0.62, rims 0.78) into a surface at rest at that level by at least 0.5 px. */
static void blow_full(void)
{
    static const unsigned DT[2] = {16, 5};
    int fulls = 0, reached = 0, tried = 0, works = 0;
    for (int f = 0; f < 2; f++)
        for (size_t k = 0; k < sizeof WORKS / sizeof WORKS[0]; k++) {
            const work *w = &WORKS[k];
            pour p;
            pour_start(&p, w->t, START);
            pour_wave was = p.wave;
            int landed = p.landed, seen = 0;
            for (unsigned t = DT[f]; t <= w->over + POUR_CATCH_MS + 2000; t += DT[f]) {
                pour_work(&p, START + t, w->level(t), t >= w->over ? POUR_SUCCEEDED : POUR_WORKING);
                int now_full = pour_full(&p);
                if (now_full)       /* against the frame before, the one before full included */
                    reached += p.landed != landed || p.wave.last != was.last || p.wave.pool != was.pool ||
                               p.wave.pooled != was.pooled || p.wave.pressing > was.pressing;
                was = p.wave, landed = p.landed;
                if (!now_full)
                    continue;
                seen = 1;
                pour q = p;
                pour_land(&q.wave, POUR_COLS / 2.0f, 1, 1, 1);
                pour_pool(&q.wave, (float)(q.now - q.began) / 1000.0f);
                tried += memcmp(&q.wave, &p.wave, sizeof q.wave) != 0;
                fulls++;
            }
            works += seen;
        }
    pour p;
    pour_start(&p, &POUR_INSTALL, START);
    float prev[POUR_COLS], before[POUR_COLS], h[POUR_COLS], last = 0;
    int cur = -1;
    pour_heights(&p, prev);
    for (unsigned t = 1; t <= 52000; t++) {
        pour_work(&p, START + t, apart(t), t >= 50000 ? POUR_SUCCEEDED : POUR_WORKING);
        pour_heights(&p, h);
        int i = landed_within(&p, 100);
        if (i != cur)
            cur = i, memcpy(before, prev, sizeof before);
        memcpy(prev, h, sizeof prev);
        if (i == p.total - 1)
            last = fmaxf(last, -mid_drop(h, before));
    }
    pour_wave rest, s;
    memset(&rest, 0, sizeof rest);
    s = rest;
    pour_land(&s, POUR_COLS / 2.0f, 0.62f, 0.78f, 0.78f);
    float calm[POUR_COLS];
    pour_wave_heights(&s, 23.0f / 24, 0, h);            /* a nearly full tile */
    pour_wave_heights(&rest, 23.0f / 24, 0, calm);
    float weak = -mid_drop(h, calm);
    char what[400];
    snprintf(what, sizeof what, "a full tile takes no push: %d works shown full over %d frames, %d frames with a "
             "landing or a push reaching the surface, %d with a tried landing or pool changing it; just below full the "
             "last piece's blow lowers the middle by %.2f px, the weakest blow by %.2f px", works, fulls, reached,
             tried, last, weak);
    check(works == 2 * N_WORKS && fulls > 10000 && !reached && !tried && last >= 0.5f && weak >= 0.5f, what, "blow");
}

/* SR-TD5, the share of a blow in the frame of its touch: on the job above (every 1 ms), for every piece landing on
 * liquid, the middle of the surface's deflection (its wave, not cut at the floor) against the frame before the touch
 * is lowered in the frame of the touch by
 * PUSH_NOW (45 %) of the blow, which is 0.45 / 0.745 of what is pressed in when the press window is over (the
 * canvas's press cut after 9 of its steps: 0.45 + 0.55 * 9 / 16.8 = 0.745); measured against the deepest the middle
 * goes within 60 ms of the touch, between 0.62 and 0.655 of it (0.637 measured: the wave carries a little away while
 * it presses), which holds PUSH_NOW to within about 3 points of 45 %. */
static void blow_share(void)
{
    pour p;
    pour_start(&p, &POUR_INSTALL, START);
    float prev[POUR_COLS], before[POUR_COLS], now_[POUR_BALLS] = {0}, most[POUR_BALLS] = {0};
    int cur = -1;
    memcpy(prev, p.wave.w, sizeof prev);
    for (unsigned t = 1; t <= 52000; t++) {
        pour_work(&p, START + t, apart(t), t >= 50000 ? POUR_SUCCEEDED : POUR_WORKING);
        int i = landed_within(&p, 60);
        if (i != cur)
            cur = i, memcpy(before, prev, sizeof before);
        memcpy(prev, p.wave.w, sizeof prev);
        if (i <= 0)
            continue;
        float d = -mid_drop(p.wave.w, before);
        if (p.now == pour_lands(&p, i))
            now_[i] = d;
        most[i] = fmaxf(most[i], d);
    }
    float lo = 1e9f, hi = -1e9f;
    int n = 0;
    for (int i = 1; i < p.total; i++) {
        float r = most[i] > 0 ? now_[i] / most[i] : 0;
        lo = fminf(lo, r), hi = fmaxf(hi, r), n++;
    }
    char what[300];
    snprintf(what, sizeof what, "the blow in the frame of its touch: %d landings on liquid, its share of the pressed-in "
             "dip %.3f to %.3f (PUSH_NOW / 0.745 = %.3f)", n, lo, hi, 0.45f / 0.745f);
    check(n == pieces_of(&POUR_INSTALL) - 1 && lo >= 0.62f && hi <= 0.655f, what, "blow");
}

/* The canvas's first pool for z2 (S/sr/tile6/gen.py, pool and the shape in timeline): per column its share of the
 * level tau s after the touch (LATE included), the lens going over into the level surface from 0.42 s over 0.40 s. */
static void canvas_pool(float tau, float g[POUR_COLS])
{
    float r = 8 + 12 * (1 - expf(-tau / 0.05f)) + 22 * (1 - expf(-tau / 0.3f)) + 80 * fmaxf(0, tau - 0.4f);
    float rim = 0.7f * expf(-tau / 0.3f), a = fminf(1, fmaxf(0, (tau - 0.42f) / 0.40f)), u = a * a * (3 - 2 * a);
    float sum = 0;
    for (int i = 0; i < POUR_COLS; i++) {
        float x = fabsf(i + 0.5f - POUR_COLS / 2.0f), q = 1 - (x / r) * (x / r), e = (x - (r - 2.4f)) / 1.7f;
        g[i] = q > 0 ? sqrtf(q) * (1 + rim * expf(-e * e)) : 0;
        sum += g[i] / POUR_COLS;
    }
    for (int i = 0; i < POUR_COLS; i++)
        g[i] = (1 - u) * g[i] / sum + u;
}

/* SR-TD5, the first pool goes over into the level surface (the canvas's flat for z2, from 0.42 s over 0.40 s): on the
 * job above, every 1 ms from the first piece's touch on the dry floor for 1.2 s, the pool's share of the level per
 * column is the canvas's (read off the surface at the full level, where the wave and the drift do not show: within
 * 0.01 px of 82); from 0.82 s after the touch (LATE included) the surface is the level surface, to the bit. */
static void blow_flat(void)
{
    pour p;
    pour_start(&p, &POUR_INSTALL, START);
    pour_wave rest;
    memset(&rest, 0, sizeof rest);
    float h[POUR_COLS], level[POUR_COLS], g[POUR_COLS], off = 0;
    int frames = 0, after = 0, not_level = 0, pooled = 0;
    pour_wave_heights(&rest, 1, 0, level);
    for (unsigned t = 1; t <= 4000; t++) {
        pour_work(&p, START + t, apart(t), POUR_WORKING);
        if (!p.gone || p.now < pour_lands(&p, 0) || p.now > pour_lands(&p, 0) + 1200)
            continue;
        /* the time since the touch as pour.c counts it: two floats on the work's clock */
        float clock = (float)(p.now - p.began) / 1000.0f;
        float tau = clock - (float)(pour_lands(&p, 0) - p.began) / 1000.0f + 0.025f;
        pour_wave_heights(&p.wave, 1, clock, h);
        canvas_pool(tau, g);
        for (int c = 0; c < POUR_COLS; c++)
            off = fmaxf(off, fabsf(h[c] - fmaxf(0, POUR_TRAVEL * g[c] - (POUR_TRAVEL - level[c]))));
        frames++;
        pooled += p.wave.pooled;
        if (tau >= 0.821f) {
            pour_wave flat = p.wave;
            float lv[POUR_COLS];
            flat.pooled = 0;
            pour_heights(&p, h);
            pour_wave_heights(&flat, pour_level(&p), clock, lv);
            not_level += memcmp(h, lv, sizeof h) != 0;
            after++;
        }
    }
    char what[300];
    snprintf(what, sizeof what, "the first pool over %d frames from its touch: off the canvas's share by up to %.4f px "
             "at the full level; %d of %d frames from 0.82 s on off the level surface", frames, off, not_level, after);
    check(frames == 1201 && pooled == frames && off <= 0.01f && after > 300 && !not_level, what, "blow");
}

/* SR-TD4b: by the clock alone, through the first pool, every blow and the level's coming: the install (reported every
 * 400 ms) on frames of 16, 5 and 33 ms and uneven ones, at the moments compared (every 20 ms) the surface and the level
 * the same to the bit; then an hour's jump during a blow: at once the surface at rest on the calm curve and the level
 * every piece's part. */
static void blow_clock(void)
{
    static const unsigned STEPS[4] = {16, 5, 33, 0};
    pour c[4];
    unsigned f = 0;
    for (int n = 0; n < 4; n++)
        pour_start(&c[n], &POUR_INSTALL, START);
    int differ = 0, blows = 0;
    for (unsigned t = 20; t <= 12000; t += 20) {
        for (int n = 0; n < 4; n++)
            for (unsigned tc = c[n].now - START; tc < t;) {
                unsigned d = STEPS[n] ? STEPS[n] : FRAMES[f++ % (sizeof FRAMES / sizeof FRAMES[0])];
                tc = tc + d < t ? tc + d : t;
                pour_work(&c[n], START + tc, reported(tc), tc >= 8000 ? POUR_SUCCEEDED : POUR_WORKING);
            }
        float h0[POUR_COLS], h[POUR_COLS];
        pour_heights(&c[0], h0);
        blows += landed_within(&c[0], 100) >= 0;
        for (int n = 1; n < 4; n++) {
            pour_heights(&c[n], h);
            differ += memcmp(h, h0, sizeof h) != 0 || pour_level(&c[n]) != pour_level(&c[0]);
        }
    }
    pour p;
    pour_start(&p, &POUR_INSTALL, START);
    unsigned t = 0;
    while (p.gone < 3 || landed_within(&p, 30) != 2) {
        t++;
        pour_work(&p, START + t, reported(t), POUR_WORKING);
    }
    pour_work(&p, START + t + 3600000u, reported(t), POUR_WORKING);
    float h[POUR_COLS], calm[POUR_COLS], level = pour_level(&p);
    pour_wave rest;
    memset(&rest, 0, sizeof rest);
    pour_heights(&p, h);
    pour_wave_heights(&rest, level, 0, calm);
    float parts = 0;
    for (int i = 0; i < p.gone; i++)
        parts += piece_part(p.t, i);
    int jumped = !p.wave.moving && !memcmp(h, calm, sizeof h) && fabsf(level - parts) < 1e-6f;
    char what[300];
    snprintf(what, sizeof what, "the surface and the level by the clock: every 20 ms (%d within 0.1 s of a touch), on "
             "5 ms, 33 ms and uneven frames %d not as on 16 ms frames to the bit; an hour's jump during a blow: %s",
             blows, differ, jumped ? "at rest, the level whole" : "NOT at rest with the level whole");
    check(!differ && blows > 4 * c[0].gone && c[0].gone == c[0].total && jumped, what, "blow");
}

int main(void)
{
    tables();
    for (size_t k = 0; k < sizeof WORKS / sizeof WORKS[0]; k++)
        run(&WORKS[k]);
    frozen();
    surface_answer();
    surface_level();
    surface_clock();
    piece_fall();
    piece_turn();
    holder_poses();
    swing_bounds();
    swing_clock();
    line_clock();
    line_groups();
    swing_rest();
    swing_light();
    melt_landing();
    melt_clock();
    melt_jump();
    blow_liquid();
    blow_pool();
    blow_level();
    blow_clock();
    blow_full();
    blow_share();
    blow_flat();
    printf("%d failed\n", failed);
    return failed != 0;
}
