#include "pour.h"

#include <math.h>
#include <stddef.h>

#include "ease.h"

/* The pieces (AN): fewer than before, in whole groups (pour.h): three groups for either work. Per step, the install
 * and the badge write: V1 {3, 3, 3, 3} and {2, 2, 2, 2, 2, 2} (12); V2 {4, 4, 4, 3} and {3, 2, 3, 2, 3, 2} (15);
 * V3 {3, 3, 3, 4} and {2, 2, 2, 2, 2, 3} (13). */
const pour_table POUR_INSTALL = {4, {0.25f, 0.25f, 0.25f, 0.25f}, {3, 3, 3, 4}, 4, {1, 1, 1, 1}};
/* badges_ui.h badges_step: FROM and TO 0, 0.08, 0.45, 0.78, 0.82, 0.9, 1; each step is a sixth of the box */
/* The list shows the six as four rows (BS, C69): the read; the backup written and checked; prepared and written; the
 * read back. The box and its pieces go by the six as before. */
const pour_table POUR_BADGES = {6, {0.08f, 0.37f, 0.33f, 0.04f, 0.08f, 0.10f}, {2, 2, 2, 2, 2, 3}, 4, {1, 2, 2, 1}};

_Static_assert(POUR_GIVE_SPEED <= POUR_SPEED && POUR_READY <= POUR_GROUP_GAP && POUR_GAP_OFF < POUR_GAP &&
               POUR_STEP_MOST <= POUR_GROUP_MIN + 1, "the groups as pour.h's bounds take them");

#define TICK_MS 4               /* the line is moved in steps of this from the work's start, however far apart the
                                 * frames are: so where it is and how fast is the clock's alone (the swing feels it) */
#define SLACK 1e-4f             /* a level reported as a float meets a ball's level within this */
#define RUN (POUR_ENTER_X - POUR_MOUTH_X)       /* a holder's way from coming in to the mouth */

/* Ball i: the work's level once it has landed (what it may be let go at), by the steps' shares of the work. */
static float ball_level(const pour_table *t, int i)
{
    float below = 0;
    for (int s = 0; s < t->steps; s++) {
        if (i < t->balls[s])
            return below + t->share[s] * (float)(i + 1) / (float)t->balls[s];
        i -= t->balls[s];
        below += t->share[s];
    }
    return 1;
}

/* Ball i's part of the box: each step an equal part of it, as the list beside it counts steps (O10: by the shares the
 * badge write's tile showed full at its fourth step of six), each of the step's balls an equal part of that. */
static float ball_part(const pour_table *t, int i)
{
    for (int s = 0; s < t->steps; s++) {
        if (i < t->balls[s])
            return 1.0f / ((float)t->steps * (float)t->balls[s]);
        i -= t->balls[s];
    }
    return 0;
}

static float ball_hash(int i, int k);

/* Holder i's place in its group (0: its first): the groups hold POUR_GROUP_A and POUR_GROUP_B in turn. */
static int place(int i)
{
    for (int g = 0;; g++) {
        int n = g % 2 ? POUR_GROUP_B : POUR_GROUP_A;
        if (i < n)
            return i;
        i -= n;
    }
}

/* Ball n may be let go: the work's level has reached its own, and the one before it fell POUR_SPACE_MS ago. */
static int ready(const pour *p, int n)
{
    return ball_level(p->t, n) <= p->real + SLACK && (!n || p->line >= p->let_go[n - 1] + POUR_SPACE_MS);
}

/* The beat the line stands before ball i is let go: its own, always the same. */
static unsigned beat(int i)
{
    return POUR_BEAT_MS + (unsigned)(POUR_BEAT_MORE_MS * ball_hash(i, 9));
}

void pour_start(pour *p, const pour_table *t, uint64_t now)
{
    *p = (pour){.t = t, .now = now, .way = RUN - POUR_READY, .how = POUR_WORKING};
    for (int k = 0; k < t->steps; k++)
        p->total += t->balls[k];
    if (p->total > POUR_BALLS)
        p->total = POUR_BALLS;
    p->began = p->line = now;
    for (int i = 1; i < p->total; i++)      /* whole px: the holders move as one */
        p->at[i] = p->at[i - 1] + (place(i) ? POUR_GAP - POUR_GAP_OFF + (int)(ball_hash(i, 8) * (2 * POUR_GAP_OFF + 1))
                                            : POUR_GROUP_GAP);
    for (int i = 0; i < POUR_BALLS; i++) {  /* each holder a little different, always the same (the canvas's ranges) */
        float kick = (ball_hash(i, 6) < 0.5f ? -1 : 1) * (0.7f + 0.3f * ball_hash(i, 7));
        pour_swing_init(&p->swing[i], 0.86f + 0.28f * ball_hash(i, 3), 0.8f + 0.45f * ball_hash(i, 4),
                        0.8f + 0.4f * ball_hash(i, 5), kick);
    }
}

/* The line over ms: it runs towards the place where the next ball is over the mouth (beyond the last holder's
 * leaving once all are let go), starting and stopping by POUR_ACCEL, never faster than POUR_SPEED, inside a group
 * than POUR_GIVE_SPEED. It comes to stand there, and the ball is let go once it may be (ready) and the line has stood
 * for its beat; with POUR_PASS a ball after its group's first that may be let go as it comes is let go in passing.
 * The last ones ride out without slowing. */
static void move(pour *p, unsigned ms)
{
    int n = p->gone, more = n < p->total, giving = more && place(n) > 0;
    float s = (float)ms / 1000.0f, end = p->at[p->total - 1] + POUR_ENTER_X - POUR_LEAVE_X;
    float target = more ? p->at[n] + RUN : end, top = giving ? POUR_GIVE_SPEED : POUR_SPEED;
    float pass = !more ? POUR_SPEED : giving && POUR_PASS && ready(p, n) ? POUR_GIVE_SPEED : 0;
    float v = fminf(top, p->speed + POUR_ACCEL * s);
    int coming = p->way < target;
    v = fminf(v, sqrtf(pass * pass + 2.0f * POUR_ACCEL * fmaxf(target - p->way, 0)));
    p->way = fminf(target, p->way + v * s);
    p->line += ms;
    if (p->way < target) {
        p->speed = v;
        p->stood = 0;
        return;
    }
    p->speed = more && coming && pass > 0 ? v : 0;
    if (!more)
        return;
    if (p->speed == 0 && !p->stood)
        p->stood = p->line;
    if (p->speed > 0 || (ready(p, n) && p->line >= p->stood + beat(n))) {
        p->let_angle[p->gone] = p->swing[p->gone].th;
        p->let_go[p->gone++] = p->line;
        p->stood = 0;
    }
}

static float smooth(float t);
static float bowl(int i);

/* Ball i's way (px) from where it hung until its foot is 2 px into the surface over the vessel's middle (the canvas's
 * six middle columns, at rest) in the box as full as the balls before it make it; at least 1 px off the floor. */
static float fall_way(const pour_table *t, int i)
{
    float below = 0, mid = 0;
    for (int j = 0; j < i; j++)
        below += ball_part(t, j);
    float lpx = below * POUR_TRAVEL, flat = smooth(lpx / 8);
    for (int c = POUR_COLS / 2 - 3; c < POUR_COLS / 2 + 3; c++)
        mid += (lpx - flat * bowl(c)) / 6;
    return POUR_FALL - fmaxf(mid, 0);
}

/* How far a piece has fallen s seconds after it let go: on the canvas's frames, j frames after (0 the first) by
 * POUR_GRAVITY / FPS^2 * (j + 1) * (j + 2) / 2 (each frame the speed, then the place), between them on the same curve. */
static float fallen(float s)
{
    const float d = 1.0f / POUR_FALL_FPS;
    return POUR_GRAVITY / 2 * (s + d) * (s + 2 * d);
}

/* Ball i's fall: the first canvas frame it is as far as its way on (0 the first), in ms */
static int fall_frames(const pour_table *t, int i)
{
    float way = fall_way(t, i);
    int j = 0;
    while (j < 2 * POUR_FALL_FPS && fallen((float)j / POUR_FALL_FPS) < way)
        j++;
    return j;
}

static unsigned fall_ms(const pour_table *t, int i)
{
    return (unsigned)((fall_frames(t, i) * 1000 + POUR_FALL_FPS - 1) / POUR_FALL_FPS);
}

/* Ball i lands on the dry floor: the box as full as the balls before it make it has no liquid over its middle. */
static int dry(const pour_table *t, int i)
{
    return POUR_FALL - fall_way(t, i) < 0.5f;
}

static uint64_t landed_at(const pour *p, int i)
{
    return p->let_go[i] + fall_ms(p->t, i);
}

static uint64_t risen_at(const pour *p, int i)
{
    return landed_at(p, i) + POUR_RISE_MS;
}

static int full_now(const pour *p)
{
    return p->how == POUR_SUCCEEDED && p->gone == p->total && p->now >= risen_at(p, p->total - 1) &&
           p->now >= landed_at(p, p->total - 1) + POUR_MELT_MS;
}

/* The surface (the canvas's "Antwort"): a damped wave over the columns, stepped as the canvas's sheet steps it: the speed by the wave's pull (WAVE_C), its damping and its viscosity, then the deflection by the speed;
 * the walls mirror. A landing's blow (the canvas's z2, SR-T6; its px are these columns' and rows': the same 80 columns
 * and the same wave) is a dip under it and a rim on each side, less their mean; PUSH_NOW of it is in the surface from
 * the step of the touch on, as the canvas showed it in the touch's frame, the rest pressed in over the next
 * PUSH_STEPS: the canvas pressed the remaining 55 % at a rate of all of it per 0.07 s from its next frame on, until
 * 0.07 s after the touch, which is 9 of its steps (37.5 ms), so 0.55 * 9 / 16.8 of it. */
#define WAVE_C 75.0f            /* px/s */
#define WAVE_DAMP 1.5f          /* 1/s */
#define WAVE_VISC 40.0f         /* px^2/s */
#define PUSH_DIP 7.5f           /* px, its spread PUSH_DIP_W */
#define PUSH_DIP_W 8.5f
#define PUSH_RIM 4.6f           /* px, PUSH_RIM_AT beside the middle, its spread PUSH_RIM_W */
#define PUSH_RIM_AT 16.0f
#define PUSH_RIM_W 3.8f
#define PUSH_NOW 0.45f
#define PUSH_STEPS 9
#define PUSH_REST (0.55f * PUSH_STEPS / (0.07f * POUR_WAVE_HZ))
#define LATE (0.75f / POUR_FALL_FPS)    /* s: a canvas frame shows a landing this long after its time (its LATE) */
#define WAVE_QUIET (POUR_QUIET_MS * POUR_WAVE_HZ / 1000)
#define WAVE_FADE ((POUR_REST_MS - POUR_QUIET_MS) * POUR_WAVE_HZ / 1000)
#define WAVE_IN (300 * POUR_WAVE_HZ / 1000)     /* the drift comes in over 0.3 s after a landing */
#define BOWL 4.0f               /* the calm curve: the walls this far over its lowest point (pour_layout.h POUR_CURVE), */
#define BOWL_LOW 0.57f          /* which lies this far across from the left */

void pour_land(pour_wave *s, float x, float weight, float left, float right)
{
    if (s->full)
        return;
    float hat[POUR_COLS], mean = 0;
    for (int i = 0; i < POUR_COLS; i++) {
        float d = i + 0.5f - x, a = d / PUSH_DIP_W, b = (fabsf(d) - PUSH_RIM_AT) / PUSH_RIM_W;
        hat[i] = weight * (PUSH_RIM * (d > 0 ? right : left) * expf(-b * b / 2) - PUSH_DIP * expf(-a * a / 2));
        mean += hat[i];
    }
    mean /= POUR_COLS;
    for (int i = 0; i < POUR_COLS; i++) {
        s->w[i] += PUSH_NOW * (hat[i] - mean);
        s->press[i] += PUSH_REST * (hat[i] - mean);
    }
    s->pressing = PUSH_STEPS;
    s->last = s->step;
    s->moving = 1;
}

void pour_wave_step(pour_wave *s)
{
    if (!s->moving) {
        s->step++;
        return;
    }
    const float d = 1.0f / POUR_WAVE_HZ;
    float lap[POUR_COLS], lapv[POUR_COLS];
    if (s->pressing) {
        for (int i = 0; i < POUR_COLS; i++) {
            float k = s->press[i] / s->pressing;
            s->w[i] += k;
            s->press[i] -= k;
        }
        s->pressing--;
    }
    for (int i = 0; i < POUR_COLS; i++) {
        int l = i ? i - 1 : 0, r = i + 1 < POUR_COLS ? i + 1 : i;
        lap[i] = s->w[l] - 2 * s->w[i] + s->w[r];
        lapv[i] = s->v[l] - 2 * s->v[i] + s->v[r];
    }
    for (int i = 0; i < POUR_COLS; i++) {
        s->v[i] += d * (WAVE_C * WAVE_C * lap[i] - 2 * WAVE_DAMP * s->v[i] + WAVE_VISC * lapv[i]);
        s->w[i] += d * s->v[i];
    }
    s->step++;
    int since = (int)(s->step - s->last);
    if (since < WAVE_QUIET) {
        s->live = fminf(1, s->live + 1.0f / WAVE_IN);
        return;
    }
    int left = WAVE_QUIET + WAVE_FADE - since;     /* fading out: what is left goes linearly to nothing */
    float k = left > 0 ? (float)left / (left + 1) : 0;
    for (int i = 0; i < POUR_COLS; i++)
        s->w[i] *= k, s->v[i] *= k;
    s->live *= k;
    if (left <= 0)
        *s = (pour_wave){.step = s->step, .last = s->last, .pool = s->pool, .pooled = s->pooled, .full = s->full};
}

void pour_pool(pour_wave *s, float t)
{
    if (s->full)
        return;
    s->pool = t;
    s->pooled = 1;
}

/* The swing as the canvas steps it (SR-T5 gen.py, its holder h2): the speed by the pull back, the damping and the
 * line's push, then a little dry friction, then the angle by the speed; still once small with the line steady. */
#define SW_T 0.56f              /* s: a loaded holder's period */
#define SW_T_LIGHT 0.36f        /* an emptied one's */
#define SW_Z 0.2f               /* the damping, a share of the critical one */
#define SW_L 17.0f              /* px: the line's acceleration a turns it by a / L, a loaded holder */
#define SW_L_LIGHT 30.0f        /* an emptied one */
#define SW_KICK 1.6f            /* radians/s: an emptied holder springs back */
#define SW_FR 0.5f              /* radians/s^2: the dry friction */
#define SWING_STILL (POUR_SWING_STILL_MS * POUR_SWING_HZ / 1000)

void pour_swing_init(pour_swing *s, float f, float z, float g, float kick)
{
    *s = (pour_swing){.f = f, .z = z, .g = g, .kick = kick};
}

void pour_swing_empty(pour_swing *s)
{
    if (s->light)
        return;
    s->light = 1;
    s->om += SW_KICK * s->kick;
}

int pour_swing_step(pour_swing *s, float a)
{
    const float e = 1.0f / POUR_SWING_HZ;
    float om0 = 2 * 3.14159265f / ((s->light ? SW_T_LIGHT : SW_T) * s->f), zeta = SW_Z * s->z;
    float reach = (s->light ? SW_L_LIGHT : SW_L) / s->g;
    s->om += e * (-om0 * om0 * s->th - 2 * zeta * om0 * s->om - a / reach);
    s->om -= copysignf(fminf(fabsf(s->om), SW_FR * e), s->om);
    s->th += e * s->om;
    if (fabsf(a) < 1 && fabsf(s->th) < 0.006f && fabsf(s->om) < 0.06f) {
        s->th = s->om = 0;
        return 0;
    }
    return 1;
}

/* The swing up to the model's clock in its fixed steps, once per tick of the line (a step at most per tick), each
 * pushed by the line's change of speed since the step before. At rest with the line steady the steps pass uncomputed;
 * what still swings POUR_SWING_STILL_MS after the line's last push is stopped, so a jump of the clock computes at most
 * the line's start, its stop and that much after them. */
static void swing_follow(pour *p)
{
    uint64_t due = (p->line - p->began) * POUR_SWING_HZ / 1000;
    while (p->swung < due) {
        float a = (p->swing_v - p->speed) * POUR_SWING_HZ * POUR_SWING_FELT;   /* the line runs to the left */
        int push = fabsf(a) >= 1;
        p->swing_v = p->speed;
        p->swung++;
        while (p->emptied < p->gone) {
            pour_swing_empty(&p->swing[p->emptied++]);
            push = 1;
        }
        if (push)
            p->quiet = 0, p->swinging = 1;
        if (!p->swinging) {
            p->swung = due;
            break;
        }
        int moving = 0;
        for (int i = 0; i < p->total; i++)
            moving |= pour_swing_step(&p->swing[i], a);
        p->swing_work++;
        if (!moving || ++p->quiet >= SWING_STILL) {
            for (int i = 0; i < p->total; i++)
                p->swing[i].th = p->swing[i].om = 0;
            p->swinging = 0;
        }
    }
}

/* A slow uneven drift of about -1 to 1: three sines whose periods share no small multiple. */
static float drift(float t)
{
    static const float PERIOD[3] = {6.1f, 9.7f, 14.3f}, PHASE[3] = {0.9f, 3.3f, 5.2f};
    float f = 0;
    for (int k = 0; k < 3; k++)
        f += sinf(2 * 3.14159265f * t / PERIOD[k] + PHASE[k]);
    return f / 2.2f;
}

static float smooth(float t)
{
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    return t * t * (3 - 2 * t);
}

/* The calm curve at column i: how far over its lowest point. */
static float bowl(int i)
{
    float u = (i + 0.5f) / POUR_COLS, x = u < BOWL_LOW ? (BOWL_LOW - u) / BOWL_LOW : (u - BOWL_LOW) / (1 - BOWL_LOW);
    return BOWL * (1 - powf(x < 1 ? x : 1, 1.7f));
}

/* 0 to 1 at once and slowing: what a blow does */
static float gone(float tau, float t)
{
    return 1 - expf(-tau / t);
}

/* The first pool's share of the level per column (their mean 1; 1 everywhere once it has gone over into the level
 * surface): the canvas's lens for z2 (its pool), half width 8 px thrown outward in one go, fast at once and slowing,
 * a thicker rim leading that fades within about 0.3 s, from 0.42 s over 0.40 s the level surface. */
static void pool_shape(const pour_wave *s, float t, float g[POUR_COLS])
{
    float tau = fmaxf(0, t - s->pool + LATE), u = smooth((tau - 0.42f) / 0.40f), sum = 0;
    for (int i = 0; i < POUR_COLS; i++)
        g[i] = 1;
    if (!s->pooled || u >= 1)
        return;
    float r = 8 + 12 * gone(tau, 0.05f) + 22 * gone(tau, 0.3f) + 80 * fmaxf(0, tau - 0.4f);
    float rim = 0.7f * expf(-tau / 0.3f);
    for (int i = 0; i < POUR_COLS; i++) {
        float x = fabsf(i + 0.5f - POUR_COLS / 2.0f), q = 1 - (x / r) * (x / r), e = (x - (r - 2.4f)) / 1.7f;
        g[i] = q > 0 ? sqrtf(q) * (1 + rim * expf(-e * e)) : 0;
        sum += g[i];
    }
    for (int i = 0; i < POUR_COLS; i++)
        g[i] = (1 - u) * g[i] * POUR_COLS / sum + u;
}

void pour_wave_heights(const pour_wave *s, float level, float t, float h[POUR_COLS])
{
    float lpx = level * POUR_TRAVEL, flat = smooth(lpx / 8), calm = (POUR_TRAVEL - lpx) / 10, g[POUR_COLS];
    float tilt = 0.45f * drift(t) * smooth(s->live);
    calm = calm < 0 ? 0 : calm > 1 ? 1 : calm;
    pool_shape(s, t, g);
    for (int i = 0; i < POUR_COLS; i++) {
        float u = (i + 0.5f) / POUR_COLS;
        h[i] = fmaxf(0, lpx * g[i] - flat * bowl(i) + calm * flat * (s->w[i] + tilt * (2 * u - 1)));
    }
}

/* Ball i's landing: with a strength and a rim on each side of its own, the same every time. */
static float ball_hash(int i, int k)
{
    uint32_t x = (uint32_t)i * 0x9E3779B9u + (uint32_t)k * 0x85EBCA6Bu + 0x165667B1u;
    x ^= x >> 16, x *= 0x7FEB352Du, x ^= x >> 15, x *= 0x846CA68Bu, x ^= x >> 16;
    return (float)(x >> 8) / 16777216.0f;
}

/* Ball i lands: on the dry floor the first pool, on liquid the blow, by the speed it falls with (the canvas's: a fall
 * to the empty vessel's floor at full strength, 62 % at none) and a strength and rims of its own. */
static void land(pour *p, int i)
{
    if (dry(p->t, i)) {
        pour_pool(&p->wave, (float)(landed_at(p, i) - p->began) / 1000.0f);
        return;
    }
    const float full = sqrtf(2 * POUR_GRAVITY * (POUR_FALL - 2));
    float v = POUR_GRAVITY * (float)(fall_frames(p->t, i) + 1) / POUR_FALL_FPS;
    pour_land(&p->wave, POUR_COLS / 2.0f, (0.62f + 0.38f * fminf(1, v / full)) * (0.92f + 0.16f * ball_hash(i, 1)),
              0.78f + 0.22f * ball_hash(i, 0), 0.78f + 0.22f * ball_hash(i, 2));
}

/* The wave up to the model's clock in its fixed steps, each landing pressed in in the step it falls in, from the
 * clock's landing on, so that the frame of the touch shows it already. */
static void wave_follow(pour *p)
{
    pour_wave *s = &p->wave;
    uint64_t due = (p->now - p->began) * POUR_WAVE_HZ / 1000;
    for (;;) {
        while (p->landed < p->gone && landed_at(p, p->landed) <= p->now &&
               (landed_at(p, p->landed) - p->began) * POUR_WAVE_HZ / 1000 <= s->step)
            land(p, p->landed++);
        if (s->step >= due)
            break;
        if (s->moving)
            pour_wave_step(s);
        else if (p->landed < p->gone) {     /* at rest: on to the next landing's step at once */
            uint64_t at = (landed_at(p, p->landed) - p->began) * POUR_WAVE_HZ / 1000;
            s->step = (uint32_t)(at < due ? at : due);
        } else
            s->step = (uint32_t)due;
    }
}

void pour_heights(const pour *p, float h[POUR_COLS])
{
    pour_wave_heights(&p->wave, pour_level(p), (float)(p->now - p->began) / 1000.0f, h);
}

void pour_work(pour *p, uint64_t now, float level, int how)
{
    if (p->how == POUR_FAILED)
        return;
    if (how == POUR_FAILED) {
        p->how = POUR_FAILED;
        return;
    }
    while (p->line + TICK_MS <= now) {     /* up to now as the work stood; what it says now counts from now on */
        move(p, TICK_MS);
        swing_follow(p);
    }
    if (now > p->now)
        p->now = now;
    if (how == POUR_SUCCEEDED) {
        p->how = POUR_SUCCEEDED;
        level = 1;
    }
    if (level > p->real)
        p->real = level < 1 ? level : 1;
    wave_follow(p);
    if (!p->full_at && full_now(p)) {
        p->full_at = p->now;
        p->wave.full = 1;
    }
}

/* The share of a piece's liquid in the level tau s after its touch as a frame shows it (the canvas's share for z2). */
static float share(float tau, int on_floor)
{
    if (!on_floor)
        return smooth((tau - 0.05f) / 0.6f);
    return 0.07f * gone(tau, 0.035f) + 0.93f * smooth((tau - 0.12f) / 0.55f);
}

float pour_level(const pour *p)
{
    float f = 0;
    for (int i = 0; i < p->gone; i++) {
        uint64_t at = landed_at(p, i);
        if (p->now >= at)
            f += ball_part(p->t, i) * (p->now >= at + POUR_RISE_MS ? 1 : share((float)(p->now - at) / 1000.0f + LATE,
                                                                                 dry(p->t, i)));
    }
    return f < 1 ? f : 1;
}

int pour_full(const pour *p)
{
    return p->full_at != 0;
}

float pour_glow(const pour *p)
{
    if (!p->full_at)
        return 0;
    uint64_t in = p->full_at + POUR_GLOW_MS;
    if (p->now < in)
        return ease_out(ease_span(p->now, p->full_at, POUR_GLOW_MS));
    return 1.0f - 0.3f * sinf(3.14159265f * ease_span(p->now, in, POUR_BREATH_MS));
}

float pour_ending(const pour *p)
{
    return p->full_at ? ease_span(p->now, p->full_at, POUR_MARK_MS + 100) : 0;
}

int pour_over(const pour *p)
{
    return p->full_at && p->now >= p->full_at + POUR_END_MS;
}

int pour_holder(const pour *p, int i, float *x, int *ball)
{
    if (i < 0 || i >= p->total)
        return 0;
    *x = (float)POUR_ENTER_X - (p->way - p->at[i]);
    *ball = i >= p->gone;
    return *x > POUR_LEAVE_X && *x < POUR_ENTER_X;
}

int pour_holder_pose(const pour *p, int i)
{
    if (i < 0 || i >= p->gone)
        return POUR_HOLDS;
    uint64_t t = p->now - p->let_go[i];
    return t < POUR_OPEN_MS     ? POUR_OPENS
           : t < POUR_SPRING_MS ? POUR_LETS_GO
           : t < POUR_EMPTY_MS  ? POUR_SPRINGS
                                : POUR_EMPTY;
}

float pour_holder_angle(const pour *p, int i)
{
    return i >= 0 && i < p->total ? p->swing[i].th : 0;
}

int pour_piece(const pour *p, int i, float *down, int *turn)
{
    if (i < 0 || i >= p->gone)
        return 0;
    uint64_t t = p->now - p->let_go[i];
    if (t >= fall_ms(p->t, i))
        return 0;
    *down = fminf(fallen((float)t / 1000.0f), fminf(fall_way(p->t, i), POUR_FALL - 1));
    uint64_t span = fall_ms(p->t, i) - POUR_TURN_HOLD_MS;
    int from = POUR_LAND_TURN * 16 <= span ? 0 : 1;     /* at most one frame a 60th of a second */
    uint64_t k = (uint64_t)from + (POUR_LAND_TURN - from) * t / span;
    *turn = k < POUR_LAND_TURN ? (int)k : POUR_LAND_TURN;
    return 1;
}

uint64_t pour_lands(const pour *p, int i)
{
    return i >= 0 && i < p->gone ? landed_at(p, i) : 0;
}

/* The canvas's pose of the landing z2 in frame k (MELT_LATE of a frame after its time): how far its ground has sunk
 * into the liquid (px), its alpha. */
#define MELT_LATE 0.75f

int pour_melt(const pour *p, int i, int *frame, float *ground, float *alpha)
{
    if (i < 0 || i >= p->gone || p->now < landed_at(p, i) || p->now >= landed_at(p, i) + POUR_MELT_MS)
        return 0;
    int k = (int)((p->now - landed_at(p, i)) * POUR_MELT_FRAMES / POUR_MELT_MS);
    float tau = (k + MELT_LATE) * POUR_MELT_MS / (1000.0f * POUR_MELT_FRAMES);
    float dive = 5.5f * (1 - expf(-tau / 0.05f)) + 2.0f * smooth((tau - 0.22f) / 0.28f);
    float touch = POUR_FALL - fall_way(p->t, i), h[POUR_COLS], now = 0;
    *frame = k;
    *alpha = 1 - smooth((tau - 0.32f) / 0.18f);
    if (dry(p->t, i)) {         /* on the floor */
        *ground = 0;
        return 1;
    }
    pour_heights(p, h);
    for (int c = POUR_COLS / 2 - 3; c < POUR_COLS / 2 + 3; c++)
        now += h[c] / 6;
    *ground = fmaxf(0, fminf(touch - dive, now - 1));
    return 2;
}

/* When step k showed done in the picture (its last drop risen; 0: not yet). */
static uint64_t step_done_at(const pour *p, int k)
{
    int last = -1;
    for (int s = 0; s <= k && s < p->t->steps; s++)
        last += p->t->balls[s];
    return last < p->gone && p->now >= risen_at(p, last) ? risen_at(p, last) : 0;
}

float pour_mark(const pour *p, int k)
{
    if (k < 0 || k >= p->t->steps)
        return 0;
    uint64_t runs = k ? step_done_at(p, k - 1) : p->began, done = step_done_at(p, k);
    return (runs ? ease_span(p->now, runs, POUR_MARK_MS) : 0) + (done ? ease_span(p->now, done, POUR_MARK_MS) : 0);
}

float pour_row_mark(const pour *p, int r)
{
    if (r < 0 || r >= p->t->rows)
        return 0;
    int first = 0;
    for (int k = 0; k < r; k++)
        first += p->t->held[k];
    float a = pour_mark(p, first), b = pour_mark(p, first + p->t->held[r] - 1);
    return (a < 1 ? a : 1) + (b > 1 ? b - 1 : 0);
}
