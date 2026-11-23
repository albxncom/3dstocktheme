/* The installing screen's picture (R9, D3), in plain C so the host tests run it: small glass pieces (octahedrons, the
 * canvas's piece b "Gedreht", SR-TD3a; the model still calls them balls) hang from holders on a line, ride in from the
 * right, are let go over a glass box's middle, fall into it turning and fill it with a liquid;
 * each empty holder rides on to the left and leaves the screen; when the work is over and the box shows full, the
 * box glows. Nothing here draws (the design draws from what it says, app_ui.c); it only follows the work.
 *
 * The rule: the picture follows the real steps and is never ahead of them. The work's level (0 to 1, each step its
 * share of the work) is what the worker reports; the box gives each step an equal part of its height, as the list
 * beside it counts steps, so it is full only as the last step ends; each ball brings an equal part of its step's, and
 * a ball is let go only once the work has passed the point in its step that the ball brings the box to. The box rises
 * only as pieces land, so what it shows is never more than the work has done. When the work runs ahead, the line
 * follows at its own pace (the groups below), so the picture trails a step done at once by POUR_TRAIL_MS at most (a
 * whole work done before a frame showed it by POUR_CATCH_MS); the work never waits for it. A failed step
 * freezes everything where it is; there is no glow then. Everything goes by the clock (ms), never by frames. */
#ifndef POUR_H
#define POUR_H

#include <stdint.h>

#define POUR_STEPS 6            /* the most steps a table has (the badge write) */
#define POUR_BALLS 16           /* the most balls a table has */
#define POUR_STEP_MOST 4        /* the most balls a step has */

/* the line, in top screen px: holders come in at ENTER_X (past the right edge), a ball is let go over the vessel's
 * middle at MOUTH_X (pour_layout.h), an empty holder is gone at LEAVE_X (past the left edge) */
#define POUR_ENTER_X 408
#define POUR_MOUTH_X 128
#define POUR_LEAVE_X (-12)
/* The pieces come in groups, piece by piece (AN, C99): the holders hang in groups of POUR_GROUP_A and POUR_GROUP_B in
 * turn, POUR_GAP px apart inside a group (each gap its own, up to POUR_GAP_OFF more or less, always the same) and
 * POUR_GROUP_GAP px from one group to the next. The line brings a group at POUR_SPEED at most and comes to stand with
 * its first piece over the mouth; a piece is let go once the line has stood for its beat (POUR_BEAT_MS and up to
 * POUR_BEAT_MORE_MS more, each piece's own, always the same); the rest of the group follows at POUR_GIVE_SPEED at
 * most, let go in passing while the work is ahead (POUR_PASS 1) or each after a stop and its beat (0). The line
 * starts and stops by POUR_ACCEL. When the screen opens the first holder is POUR_READY px before the mouth.
 * The three takes of the canvas (AN) differ by these alone, by POUR_STEP_MOST and by the tables' pieces (pour.c);
 * the one built is V3. SWING_MOST is the widest swing the host tests let the take reach (degrees; test_pour): the
 * canvas's 8 to 11 and a margin of 1.5, for V2's stronger swing (13.8) its own 15.5:
 *                 GROUP_A,_B  GAP,_OFF  GROUP_GAP  SPEED  GIVE_SPEED  PASS  ACCEL  BEAT_MS,_MORE  SWING_FELT  STEP_MOST
 *   V1 "Ruhig"       4, 4      26, 2        92       88       54       1     240     140, 140        0.5         3
 *   V2 "Schwung"     5, 5      26, 3        92       96       62       1     300     110, 150        0.6         4
 *   V3 "Wechsel"     4, 5      27, 2       112       88       80       0     480      50, 130        0.3         4
 *                 SWING_MOST: V1 12.5, V2 15.5, V3 12.5 */
#define POUR_GROUP_A 4
#define POUR_GROUP_B 5
#define POUR_GAP 27
#define POUR_GAP_OFF 2
#define POUR_GROUP_GAP 112
#define POUR_READY 40
#define POUR_SPEED 88           /* px/s */
#define POUR_GIVE_SPEED 80
#define POUR_PASS 0
#define POUR_ACCEL 480          /* px/s^2 */
#define POUR_BEAT_MS 50
#define POUR_BEAT_MORE_MS 130
#define POUR_SWING_MOST 12.5f
/* a piece falls as the canvas lets it fall (SR-T4): from rest by POUR_GRAVITY, stepped once a canvas frame
 * (POUR_FALL_FPS: each frame the speed, then the place), and lands on the first frame its foot is 2 px into the
 * surface over the vessel's middle, in the empty vessel POUR_FALL px under where it hung (pour_layout.h), there in
 * POUR_FALL_MS; it is never drawn further down than that nor more than 1 px into the floor. It turns about its upright
 * axis through its POUR_TURNS frames (a quarter turn), counted back from the touch as the canvas counts it (SR-T6,
 * LAND_TURN): from the hanging piece's frame 0 at the let-go evenly on to POUR_LAND_TURN, the frame its landing is
 * bent from (stock_art.py MELT_TURN), reached POUR_TURN_HOLD_MS before the touch, so that the last falling frame drawn
 * at 60 frames a second shows it; a fall too short for that at one frame a 60th of a second starts on frame 1. Its
 * liquid joins the level with time, not at once (the canvas's share for z2): on liquid from 0.05 s after the touch
 * over 0.6 s, on the dry floor 7 % with the blow and the rest from 0.12 s over 0.55 s; all of it by POUR_RISE_MS */
#define POUR_FALL 96
#define POUR_GRAVITY 600.0f
#define POUR_FALL_FPS 30
#define POUR_FALL_MS 534
#define POUR_TURNS 12
#define POUR_LAND_TURN 8
#define POUR_TURN_HOLD_MS 17
/* From the frame of its touch a piece gives and runs out (C24, C31; the canvas's impact z2 "Schwer", SR-T6): it plays
 * POUR_MELT_FRAMES frames, one every POUR_MELT_MS / POUR_MELT_FRAMES ms (pour_layout.h POUR_MELT_*; frame 0 is
 * already pressed in): it goes flat in one go into a thick lens that spreads and fades. Its ground is where it
 * touched: the floor in a dry vessel, otherwise the surface over the vessel's middle as its fall met it, from there
 * sinking into the liquid as the lens spreads, and never over the surface as that moves. The box shows full only once
 * the last one has run out. */
#define POUR_MELT_FRAMES 15
#define POUR_MELT_MS 500
/* two balls are let go at least this far apart, so that at most two pieces are in the air at once (a column of them
 * catching up reads as a fault): half the longest fall */
#define POUR_SPACE_MS (POUR_FALL_MS / 2)
#define POUR_RISE_MS 650
#define POUR_MARK_MS 150        /* a step in the list turns running or done in this time */
#define POUR_GLOW_MS 600        /* the full box's glow comes in in this time, then breathes once (POUR_BREATH_MS) */
#define POUR_BREATH_MS 1000
#define POUR_END_MS 1200        /* the screen stays this long after the glow began, before "Installiert" */
/* The longest from one let-go to the next with the work ahead: inside a group (its way at POUR_GIVE_SPEED, the
 * start and the stop, the beat) and on to the next group's first (the same at POUR_SPEED); 16 ms for the line's
 * ticks. From them the longest the picture trails a step done at once with the line caught up before it (a step
 * begins at most two groups), and a whole work done at once before a frame showed it (the host tests check both). */
#define POUR_GIVE_MS ((POUR_GAP + POUR_GAP_OFF) * 1000 / POUR_GIVE_SPEED + POUR_GIVE_SPEED * 1000 / POUR_ACCEL + \
                      POUR_BEAT_MS + POUR_BEAT_MORE_MS + 16)
#define POUR_BRING_MS (POUR_GROUP_GAP * 1000 / POUR_SPEED + POUR_SPEED * 1000 / POUR_ACCEL + POUR_BEAT_MS + \
                       POUR_BEAT_MORE_MS + 16)
#define POUR_TRAIL_MS (2 * POUR_BRING_MS + (POUR_STEP_MOST - 2) * POUR_GIVE_MS + POUR_FALL_MS + POUR_RISE_MS)
#define POUR_GROUP_MIN (POUR_GROUP_A < POUR_GROUP_B ? POUR_GROUP_A : POUR_GROUP_B)
#define POUR_GROUPS ((POUR_BALLS + POUR_GROUP_MIN - 1) / POUR_GROUP_MIN)
#define POUR_CATCH_MS (POUR_GROUPS * POUR_BRING_MS + (POUR_BALLS - POUR_GROUPS) * POUR_GIVE_MS + POUR_FALL_MS + \
                       POUR_RISE_MS)

/* The liquid's surface (the canvas's "Antwort", SR-TD2): calm, with a faint drift while drops come; a piece that lands
 * on liquid presses a dip under it and throws up one soft raised rim beside it (the canvas's blow z2 "Schwer", SR-T6),
 * the swell runs to the walls, climbs them a little and is gone within about a second; the first piece on the dry
 * floor makes a lens whose edge runs outward, fast at once and slowing, a thicker rim leading, and which goes over
 * into the level surface; the level lifts it whole, without a wave. A damped wave over the inner shape's columns (the deflection and
 * its speed per column), stepped POUR_WAVE_HZ times a second by the model's clock, so it is the same at any frame rate
 * and for both eyes; from POUR_QUIET_MS after the last landing what is left fades out, and from POUR_REST_MS on it is
 * at rest: nothing moves, nothing is stepped, every frame the same. */
#define POUR_COLS 80            /* the inner shape's columns (pour_layout.h POUR_INNER_W) */
#define POUR_TRAVEL 82          /* the level's way in px: the inner shape's height and its curve's (POUR_CURVE) */
#define POUR_WAVE_HZ 240
#define POUR_QUIET_MS 1000
#define POUR_REST_MS 1500

/* The holders (C24, the canvas's B "Zange", SR-T5): tongs on a joint under the clamp, their points on the piece's upper
 * facets. On a release they go through their poses in order: they open as the piece is let go, stand open while it
 * falls, spring back, and stay a little open once empty (so one sees which have given theirs), by the clock from the
 * let-go: POUR_OPEN_MS, POUR_SPRING_MS, POUR_EMPTY_MS. */
enum { POUR_HOLDS, POUR_OPENS, POUR_LETS_GO, POUR_SPRINGS, POUR_EMPTY, POUR_POSES };
#define POUR_OPEN_MS 40
#define POUR_SPRING_MS 460
#define POUR_EMPTY_MS 680
/* The swing (C19; the canvas's SR-T5): holder and piece hang as one body from the joint, a damped pendulum per holder
 * with a period, a damping and a reach of its own, pushed by the line's speeding up and slowing down: when the line
 * starts they lag, when it stops they swing on and settle. An emptied holder springs back once and swings lighter and
 * faster. Stepped POUR_SWING_HZ times a second by the model's clock (the canvas's 4 steps per frame at 30 frames), so
 * it is the same at any frame rate and for both eyes. A holder stands still once its swing is small with the line
 * steady, all of them at the latest POUR_SWING_STILL_MS after the line's last push; at rest nothing is stepped. The
 * line starts and stops by POUR_ACCEL within about 0.4 s, as the canvas's (0.35 s): the swing feels its acceleration
 * POUR_SWING_FELT as strongly (1: as it is; the canvas's 8 to 11 degrees). */
#define POUR_SWING_HZ 120
#define POUR_SWING_STILL_MS 1500
#define POUR_SWING_FELT 0.3f

typedef struct pour_swing {
    float th, om;               /* the angle (radians; positive: the foot to the right, turned anticlockwise) and its
                                 * speed (radians/s) */
    float f, z, g, kick;        /* its own: period, damping and reach (factors); its push when emptied (signed) */
    int light;                  /* emptied: lighter, faster */
} pour_swing;

typedef struct pour_wave {
    float w[POUR_COLS], v[POUR_COLS];   /* the deflection (px, up) and its speed (px/s) per column */
    float press[POUR_COLS];             /* what landings still press in, over the next `pressing` steps */
    int pressing;
    uint32_t step, last;                /* steps done since the work began; the last landing's */
    float pool;                         /* the first pool's touch (s, on the drift's clock) if pooled */
    int pooled;
    float live;                         /* the drift's share: in after a landing, out towards rest */
    int moving;                         /* 0: at rest, all of it 0 */
    int full;                           /* the box shows full: nothing lands in it any more */
} pour_wave;

/* What fills the box: the steps in order, each its share of it (together 1) and the balls that bring it; and the list
 * beside it: its rows, each holding the next held[r] steps in order. */
typedef struct pour_table {
    int steps;
    float share[POUR_STEPS];
    int balls[POUR_STEPS];
    int rows;
    int held[POUR_STEPS];
} pour_table;

/* The install's four steps, a quarter each; the badge write's six, by their real shares (badges_ui.h badges_step). */
extern const pour_table POUR_INSTALL, POUR_BADGES;

enum { POUR_WORKING, POUR_SUCCEEDED, POUR_FAILED };

typedef struct pour {
    const pour_table *t;
    int total;                  /* balls in all */
    uint64_t began, now;        /* the model's clock: when the work began, the last update */
    uint64_t line;              /* the line's: it moves in whole ticks from began (pour.c TICK_MS), up to now */
    float real;                 /* the work's level, as far as it went */
    float way, speed;           /* how far the line has moved to the left (px), how fast (px/s) */
    float at[POUR_BALLS];       /* each holder's place on the line: how far behind the first (px) */
    uint64_t stood;             /* the line's clock when it came to stand with a piece over the mouth (0: it moves) */
    int gone;                   /* balls let go so far */
    uint64_t let_go[POUR_BALLS];
    float let_angle[POUR_BALLS];  /* each holder's angle as it let its ball go (the drawing's: the piece leaves in it) */
    uint64_t full_at;           /* when the box showed full with the work done (0: not yet): the glow from then */
    int landed;                 /* pieces pressed into the surface so far */
    pour_wave wave;
    pour_swing swing[POUR_BALLS];
    uint64_t swung;             /* swing steps passed since the work began */
    uint32_t swing_work;        /* of them the ones computed (not at rest) */
    uint32_t quiet;             /* steps since the line last pushed */
    float swing_v;              /* the line's speed at the last step */
    int emptied;                /* holders that have sprung back */
    int swinging;               /* 0: every holder at rest */
    int how;                    /* POUR_WORKING, _SUCCEEDED or _FAILED */
} pour;

/* A new piece of work at now: the box empty, the first group on the line. */
void pour_start(pour *p, const pour_table *t, uint64_t now);
/* The work at now: its level (0 to 1; a level below one reported before changes nothing) and how it is (POUR_*).
 * Once per frame. POUR_FAILED stops everything where it is, for good. */
void pour_work(pour *p, uint64_t now, float level, int how);

/* What the picture shows at the model's clock. */
float pour_level(const pour *p);                /* the liquid: 0 empty to 1 full */
int pour_full(const pour *p);                   /* every drop has landed and risen, with the work succeeded */
float pour_glow(const pour *p);                 /* 0 to 1 */
float pour_ending(const pour *p);               /* the end's texts coming in: 0 to 1, from the glow's start */
int pour_over(const pour *p);                   /* the end has been shown long enough (POUR_END_MS) */
/* Holder i (0 to total - 1): its x, and whether it still carries its ball. 0 when it is not on the screen. */
int pour_holder(const pour *p, int i, float *x, int *ball);
/* Holder i's pose (POUR_HOLDS to POUR_EMPTY) and its angle (radians, as pour_swing's th; 0 at rest). */
int pour_holder_pose(const pour *p, int i);
float pour_holder_angle(const pour *p, int i);
/* Piece i: falling (1) with how far its middle is under where it hung (px) and its turn frame (0 to POUR_LAND_TURN),
 * by the clock; 0 when it is not falling: from the frame it meets the surface or the floor it melts (pour_melt), and
 * the surface answers it (pour_land on liquid, pour_pool on the dry floor, once per piece). When it lands (or landed): 0 when it has not been let
 * go. */
int pour_piece(const pour *p, int i, float *down, int *turn);
uint64_t pour_lands(const pour *p, int i);
/* Piece i landed and melting: 1 on the dry floor, 2 on liquid (what of it is under the surface shows faintly through
 * the liquid), with its frame (0 to POUR_MELT_FRAMES - 1), its ground's height over the inner shape's floor (px) and
 * its alpha; 0 when it is not (falling, not let go, or melted). */
int pour_melt(const pour *p, int i, int *frame, float *ground, float *alpha);
/* The surface: per column its height over the inner shape's floor (px) at the model's clock. */
void pour_heights(const pour *p, float h[POUR_COLS]);
/* Step k in the list: 0 to come, 1 under way, 2 done in the picture, between two on its way (POUR_MARK_MS). */
float pour_mark(const pour *p, int k);
/* Row r of the list, as pour_mark: under way from its first step's start, done with its last step's end. */
float pour_row_mark(const pour *p, int r);


/* The surface's parts, for whatever lands in it (today the pieces): something landed on liquid at x (px across the
 * inner shape from its left) with a weight (1: the canvas's blow, a dip 7.5 px deep under x and a rim 4.6 px high
 * 16 px to each side; about 0.6 to 1.1 for a piece by its speed) and the rim's share on the left and on the right
 * (0.78 to 1); the first liquid landed on the dry floor at a time (s, the drift's); neither once the box shows full
 * (its `full`, set by pour_work); one fixed step of the wave; the column heights at a level (0 to 1) and a time (s,
 * the drift's). */
void pour_land(pour_wave *s, float x, float weight, float left, float right);
void pour_pool(pour_wave *s, float t);
void pour_wave_step(pour_wave *s);
void pour_wave_heights(const pour_wave *s, float level, float t, float h[POUR_COLS]);
/* A holder's swing: its own factors (f, z, g about 1; kick about -1 or 1); its piece let go (it springs back and swings
 * light from then on); one fixed step with the line's acceleration as felt (px/s^2, positive: towards the right):
 * returns 0 once it stands still. */
void pour_swing_init(pour_swing *s, float f, float z, float g, float kick);
void pour_swing_empty(pour_swing *s);
int pour_swing_step(pour_swing *s, float a);

#endif
