/* The installing screen (R9, the tile d "Nachbau") as every look draws it (C44), from what the model says (pour.h):
 * included by app_ui.c for its helpers, drawn from the look's atlas (the look's images under the same names and
 * sizes: pour_art.py with the look's tones). The layout is C "Zwei Teile" (G2-L; pour_layout.h): nothing stands on a
 * card. Layers: on the screen's plane the circle, the small head with its rule, the name under the tile, the steps
 * and the end's line; in front of it the tile: the end's halo, the liquid's light on the ground
 * under it and its back (its shadow and its glass body), the pieces (falling through the closed top edge down the
 * tile's middle, turning, and melting where they land) and over them the liquid with its surface (hiding what has
 * sunk in but for a faint trace of a piece melting in it), the picture in the middle of the face over them
 * (hiding what lies behind it), the front glass over the picture (its lights, its edge, the recess the picture sits in:
 * pour_art.py), the end's rim of light; at the tile's depth the line with its holders and hanging pieces
 * (pour_holders: what hangs swings about the holder's joint). The picture is one of its own (the theme's or the
 * set's icon) in its frame's line (R19); the name, the steps and the end's line are text, ordered round it by order.h
 * like every picture (the front glass, an atlas image, lies between the picture and the name). The liquid stays
 * inside the tile's inner shape at every level and however its surface moves (pour.h: the canvas's "Antwort"): it is
 * drawn column by column from each column's own surface down, its image that shape; the surface's strip on it is
 * cut to the shape's whole pixels; a piece melts where it meets the surface or the floor, inside the inner shape,
 * the surface's dip answers it, and the box shows full only once it has run out (O9: a drop flattening on the liquid
 * of a full tile showed as a pale oval). No scissor: the top screen is drawn once per eye, from the same
 * model, so both eyes see the same surface. */
#include "pour.h"

_Static_assert(2 * POUR_PICTURE_X + POUR_PICTURE == 2 * POUR_TILE_X + POUR_TILE_W &&
               2 * POUR_PICTURE_Y + POUR_PICTURE == 2 * POUR_TILE_Y + POUR_TILE_H, "the picture in the middle");
_Static_assert(2 * POUR_MOUTH_X == 2 * POUR_TILE_X + POUR_TILE_W, "the drops fall through the middle, behind it");
/* the model's other ties to the vessel: POUR_FALL is a piece's way from where it hangs until its foot is 2 px into
 * the empty vessel's floor, as the canvas lands it; the surface's columns are the inner shape's, its level's way the
 * inner shape's height and the curve's; a new vessel changes them */
_Static_assert(POUR_INNER_Y + POUR_INNER_H + 2 - POUR_PIECE_FOOT - POUR_PIECE_Y == POUR_FALL,
               "pour.h POUR_FALL matches the vessel");
_Static_assert(POUR_COLS == POUR_INNER_W && POUR_TRAVEL == POUR_INNER_H + POUR_CURVE, "pour.h's surface fits the vessel");
_Static_assert(POUR_MELT_N == POUR_MELT_FRAMES && POUR_MELT_N * 1000 == POUR_MELT_FPS * POUR_MELT_MS,
               "pour.h's landing plays the sprite's frames at their rate");

#define POUR_DEPTH_BEHIND 0     /* the circle and the head's rule: on the screen's plane (C140) */
#define POUR_DEPTH_LINE POUR_DEPTH_TILE /* the line, its holders and what hangs from them: at the tile's depth (C140) */
#define POUR_DEPTH_TILE DEPTH_ICON      /* the tile and what falls into it: furthest in front */
#define POUR_FOOT_IN 0.4f       /* the liquid's light on the ground comes in over the level's first 40 % */
#define POUR_GHOST 0.4f         /* what of a piece melting on liquid lies under the surface shows this much through it */

/* A tint that only fades: alpha a at the top, b at the bottom (and as solid as the screen is drawn). */
static C2D_ImageTint *pour_fade(C2D_ImageTint *t, float a, float b)
{
    u32 top = C2D_Color32(0, 0, 0, (u8)(255 * ui_f.solid * a + 0.5f));
    u32 low = C2D_Color32(0, 0, 0, (u8)(255 * ui_f.solid * b + 0.5f));
    C2D_SetImageTint(t, C2D_TopLeft, top, 0);
    C2D_SetImageTint(t, C2D_TopRight, top, 0);
    C2D_SetImageTint(t, C2D_BotLeft, low, 0);
    C2D_SetImageTint(t, C2D_BotRight, low, 0);
    return t;
}

/* Of sprite id's image (one without pad) the part sx, sy, sw x sh on whole pixels at x, y, stretched to w columns
 * (one column of it, sw 1, across as many), tinted (NULL: as solid as the screen). */
static void sprite_part(int id, int sx, int sy, int sw, int sh, float x, float y, float w, const C2D_ImageTint *tint)
{
    const art_sprite *s = ui_sprite_info(id);
    C3D_Tex *tex = ui_sprite_tex(id, 0);
    if (s && tex)
        ui_piece(tex, s->x + sx, s->y + sy, sw, sh, x, y, w, sh, tint);
}

/* Of the landing's frame k (POUR_MELT: POUR_MELT_COLS frames a row) the part sx, sy, sw x sh at x, y, tinted. */
static void melt_part(int k, int sx, int sy, int sw, int sh, float x, float y, const C2D_ImageTint *tint)
{
    sprite_part(ART_POUR_MELT, k % POUR_MELT_COLS * POUR_MELT_W + sx, k / POUR_MELT_COLS * POUR_MELT_H + sy, sw, sh, x,
                y, sw, tint);
}

/* The tile's corners in the look drawn (ui_f.look): in Prisma cut off at 45 degrees POUR_PRISM_CUT px along each
 * edge of its outline, the inner shape's with them; elsewhere 0, the inner shape rounded by POUR_INNER_R.
 * The liquid's images are the shape either way (pour_art.py, the look's material). */
static int inner_cut(void)
{
    return ui_f.look == LOOK_PRISM ? POUR_PRISM_CUT : 0;
}

/* The columns of the inner shape wholly inside it in the screen's row y (y to y + 1), as [*x0, *x1): 0 when none. */
static int inner_span(float y, float *x0, float *x1)
{
    const float top = POUR_INNER_Y, low = POUR_INNER_Y + POUR_INNER_H, r = POUR_INNER_R;
    if (y < top || y + 1 > low)
        return 0;
    float d = y < top + r ? top + r - y : y + 1 > low - r ? y + 1 - (low - r) : 0;
    float in = r - sqrtf(r * r - d * d);
    if (inner_cut()) {          /* the outline's legs inner_cut(), the inner shape POUR_RIM inside it: its legs
                                   shorter by (2 - sqrt 2) * POUR_RIM; the row's edge nearer the top or the floor */
        const float leg = inner_cut() - 0.5857864f * POUR_RIM;
        d = fminf(y - top, low - (y + 1));
        in = d < leg ? leg - d : 0;
    }
    *x0 = ceilf(POUR_INNER_X + in), *x1 = floorf(POUR_INNER_X + POUR_INNER_W - in);
    return *x1 > *x0;
}

/* Column i of the inner shape: the rows wholly inside it, [*top, *low). */
static void pour_column(int i, int *top, int *low)
{
    static int tops[POUR_COLS], lows[POUR_COLS], ready = -1;    /* ready: made for inner_cut() of this value */
    if (ready != inner_cut()) {
        for (int c = 0; c < POUR_COLS; c++) {
            float x = POUR_INNER_X + c, x0, x1;
            int y = POUR_INNER_Y, end = POUR_INNER_Y + POUR_INNER_H;
            while (y < end && !(inner_span(y, &x0, &x1) && x0 <= x && x + 1 <= x1))
                y++;
            while (end > y && !(inner_span(end - 1, &x0, &x1) && x0 <= x && x + 1 <= x1))
                end--;
            tops[c] = y, lows[c] = end;
        }
        ready = inner_cut();
    }
    *top = tops[i], *low = lows[i];
}

/* The first row wholly under a surface h px over the inner shape's floor, within the inner shape's rows. */
static int pour_under(float h)
{
    const int floor_y = POUR_INNER_Y + POUR_INNER_H, row = (int)floorf(floor_y - fmaxf(h, 0)) + 1;
    return row < POUR_INNER_Y ? POUR_INNER_Y : row > floor_y ? floor_y : row;
}

/* The liquid under the surface h (pour_heights): each column from the row under its surface down (POUR_LIQUID, its
 * shape's own edge), on it the surface's strip from the surface's row (POUR_SURFACE: its column for where the surface
 * lies in that row, which it covers as far as the liquid reaches) cut to the column's whole pixels; neighbours alike
 * as one piece. Lighter by glow at the end. */
static void pour_liquid(const float h[POUR_COLS], float glow)
{
    const art_sprite *strip = ui_sprite_info(ART_POUR_SURFACE);
    const int floor_y = POUR_INNER_Y + POUR_INNER_H;
    int row[POUR_COLS], phase[POUR_COLS], from[POUR_COLS], a[POUR_COLS], b[POUR_COLS];
    for (int i = 0; i < POUR_COLS; i++) {
        float s = floor_y - fmaxf(h[i], 0);
        int top, low;
        row[i] = (int)floorf(s);
        phase[i] = (int)((s - row[i]) * strip->w);
        phase[i] = phase[i] < strip->w ? phase[i] : strip->w - 1;
        from[i] = pour_under(h[i]);
        pour_column(i, &top, &low);
        a[i] = row[i] > top ? row[i] : top;
        b[i] = row[i] + strip->h < low ? row[i] + strip->h : low;
    }
    for (int i = 0, j; i < POUR_COLS; i = j) {
        for (j = i + 1; j < POUR_COLS && from[j] == from[i]; j++)
            ;
        sprite_part(ART_POUR_LIQUID, i, from[i] - POUR_INNER_Y, j - i, floor_y - from[i], POUR_INNER_X + i, from[i],
                    j - i, NULL);
    }
    for (int i = 0, j; i < POUR_COLS; i = j) {
        for (j = i + 1; j < POUR_COLS && row[j] == row[i] && phase[j] == phase[i] && a[j] == a[i] && b[j] == b[i]; j++)
            ;
        sprite_part(ART_POUR_SURFACE, phase[i], a[i] - row[i], 1, b[i] - a[i], POUR_INNER_X + i, a[i], j - i, NULL);
    }
    C2D_ImageTint t;
    if (glow > 0)
        ui_sprite_tinted(ART_POUR_LIT, POUR_INNER_X, POUR_INNER_Y, 1, 1, pour_fade(&t, glow, glow));
}

/* A melting piece's frame on the screen with its ground ground px over the inner shape's floor (pour_melt): its
 * rows [*y0, *y0 + the count returned) cut to the inner shape over the frame's columns, of them the sprite's first
 * *sy. */
static int pour_melt_rows(float ground, int *y0, int *sy)
{
    int top = POUR_INNER_Y, low = POUR_INNER_Y + POUR_INNER_H;
    for (int c = 0; c < POUR_MELT_W; c++) {
        int t, l;
        pour_column(POUR_MOUTH_X - POUR_MELT_W / 2 - POUR_INNER_X + c, &t, &l);
        top = t > top ? t : top, low = l < low ? l : low;
    }
    int foot = (int)roundf(POUR_INNER_Y + POUR_INNER_H - ground), y = foot - POUR_MELT_H;
    *sy = y < top ? top - y : 0;
    *y0 = y + *sy;
    return (foot < low ? foot : low) - *y0;
}

/* The pieces falling from the line through the closed top edge (the owner: "gar kein schlitz") down the tile's
 * middle, each as what it is, turning through its frames (POUR_PIECE_TURN) on whole pixels, behind the liquid and the
 * picture, until it meets the surface or the floor (pour.h); from that frame each melts there (POUR_MELT, pour_melt):
 * its frame on whole pixels, its ground under the frame's last row, faded by its alpha, inside the inner shape.
 * A piece leaves its holder in the pose the holder holds it in (AN-3: it may be let go while the holder still swings):
 * turned about the joint by the holder's angle at the let-go (let_angle), which goes to nothing over POUR_LEAVE_MS
 * (ease_both), the joint going down with the piece; its way down, its turn frames, its landing and its melt are the
 * model's as before. */
static void pour_hung(int id, int sx, int w, int h, float x, float y, float jx, float jy, float angle);

static void pour_pieces(const pour *p)
{
    for (int i = 0; i < p->gone; i++) {
        float down;
        int turn;
        if (pour_piece(p, i, &down, &turn)) {
            float y = roundf(POUR_PIECE_Y + down) - (POUR_PIECE_H / 2 - 1);
            float held = 1 - ease_both(ease_span(p->now, p->let_go[i], POUR_LEAVE_MS)), angle = -p->let_angle[i] * held;
            pour_hung(ART_POUR_PIECE_TURN, turn * POUR_PIECE_W, POUR_PIECE_W, POUR_PIECE_H,
                      POUR_MOUTH_X - POUR_PIECE_W / 2, y, POUR_MOUTH_X,
                      POUR_JOINT_Y + y - (POUR_PIECE_Y - POUR_PIECE_H / 2 + 1), angle);
            if (angle != 0)
                ui_flush();     /* turned it is a picture (order.h): drawn here, behind the liquid, not later */
        }
    }
    for (int i = 0; i < p->gone; i++) {
        float ground, alpha;
        int k, y0, sy, n;
        C2D_ImageTint t;
        if (pour_melt(p, i, &k, &ground, &alpha) && (n = pour_melt_rows(ground, &y0, &sy)) > 0)
            melt_part(k, 0, sy, POUR_MELT_W, n, POUR_MOUTH_X - POUR_MELT_W / 2, y0, pour_fade(&t, alpha, alpha));
    }
}

/* What of a piece melting on liquid lies under the surface h shows faintly through the liquid (the canvas's ghost):
 * its frame's columns from the first row wholly under their surface down, faded by POUR_GHOST, in runs of columns
 * whose surface lies in the same row. */
static void pour_ghosts(const pour *p, const float h[POUR_COLS])
{
    const int x0 = POUR_MOUTH_X - POUR_MELT_W / 2, c0 = x0 - POUR_INNER_X;
    for (int i = 0; i < p->gone; i++) {
        float ground, alpha;
        int k, y0, sy, n;
        C2D_ImageTint t;
        if (pour_melt(p, i, &k, &ground, &alpha) != 2 || (n = pour_melt_rows(ground, &y0, &sy)) <= 0)
            continue;
        pour_fade(&t, POUR_GHOST * alpha, POUR_GHOST * alpha);
        for (int c = 0, d; c < POUR_MELT_W; c = d) {
            int from = pour_under(h[c0 + c]), a = from > y0 ? from : y0;
            for (d = c + 1; d < POUR_MELT_W && pour_under(h[c0 + d]) == from; d++)
                ;
            if (a < y0 + n)
                melt_part(k, c, sy + a - y0, d - c, y0 + n - a, x0 + c, a, &t);
        }
    }
}

/* The head, one small line from the left edge: while the work runs title after the download symbol, at the end
 * done_title alone (ending, 0 to 1, the change): the end's one check stands in its own line (pour_end). */
static void pour_head(const char *title, const char *done_title, float ending)
{
    float was = ui_f.solid, b = ui_baseline(POUR_HEAD_Y, POUR_HEAD_LH, POUR_HEAD_PX);
    if (ending < 0.5f) {        /* one after the other (G2-L1c): out in the first half, in during the second */
        ui_f.solid = was * (1 - 2 * ending);
        ui_sprite(ART_POUR_HEAD, TOP_PAD, POUR_HEAD_Y + (POUR_HEAD_LH - POUR_HEAD_ICON) / 2);
        ui_text(title, TOP_PAD + POUR_HEAD_ICON + POUR_HEAD_GAP, b, POUR_HEAD_PX, UI_COLOR(SC(COL_TEAL)), UI_LEFT);
    }
    if (ending > 0.5f) {
        ui_f.solid = was * (2 * ending - 1);
        ui_text(done_title, TOP_PAD, b, POUR_HEAD_PX, UI_COLOR(SC(COL_TEAL)), UI_LEFT);
    }
    ui_f.solid = was;
}

/* The name under the tile, centred under it: one line, cut at its end with "..." when too wide (C146). */
static void pour_name(const char *name)
{
    ui_text_fit(name, POUR_TILE_X + POUR_TILE_W / 2.0f, ui_baseline(POUR_NAME_Y, POUR_NAME_LH, POUR_NAME_PX),
                POUR_NAME_W, POUR_NAME_PX, INK, UI_CENTER, 0);
}

/* The steps beside the stage, a column without a box: each step is its mark and its name and nothing else (C50, C53:
 * no band behind the step under way, no rail joining the marks). To come: a ring, the name dimmed. Under way: the
 * ring with a dot in the accent tone, the name in full tone. Done: the check, which pops in. */
static void pour_list(const pour *p, const char *const steps[])
{
    int n = p->t->rows;
    float tx = POUR_LIST_X + POUR_MARK + POUR_MARK_GAP, cx = POUR_LIST_X + POUR_MARK / 2.0f;
    for (int k = 0; k < n; k++) {
        float m = pour_row_mark(p, k), begun = m < 1 ? m : 1, done = m > 1 ? m - 1 : 0, y = POUR_LIST_Y + k * POUR_ROW;
        float my = y + (POUR_ROW - POUR_MARK) / 2;
        C2D_ImageTint now;
        if (begun < 1)
            ui_sprite(ART_POUR_RING, POUR_LIST_X, my);
        if (begun > 0 && done < 1)
            ui_sprite_tinted(ART_POUR_NOW, POUR_LIST_X, my, 1, 1, pour_fade(&now, begun, begun));
        if (done > 0)           /* the check pops: past its size and back */
            ui_sprite_scaled(ART_POUR_TICK, cx, my + POUR_MARK / 2.0f,
                          done < 0.55f ? 0.4f + 0.85f * done / 0.55f : 1.25f - 0.25f * (done - 0.55f) / 0.45f, done);
        ui_text_fit(steps[k], tx, ui_baseline(y, POUR_ROW, POUR_ROW_PX), TOP_W - TOP_PAD - tx, POUR_ROW_PX,
                    ui_mix(SC(COL_MUTED), SC(COL_INK), begun, 0.8f + 0.2f * begun), UI_LEFT, 0);
    }
}

/* The end's line (C53): a check and the sentence, centred, in the head's tone; it comes in with ending. */
static void pour_end(const char *line, float ending)
{
    float was = ui_f.solid, tick = ui_ew(ART_POUR_TITLE_DONE) + POUR_END_GAP, most = TOP_W - 2 * TOP_PAD - tick;
    float w = ui_text_width(line, POUR_END_PX), x = roundf((TOP_W - tick - (w < most ? w : most)) / 2.0f);
    ui_f.solid = was * ending;
    ui_sprite(ART_POUR_TITLE_DONE, x, POUR_END_Y + (POUR_END_LH - POUR_END_TICK) / 2);
    ui_text_fit(line, x + tick, ui_baseline(POUR_END_Y, POUR_END_LH, POUR_END_PX), most, POUR_END_PX,
                UI_COLOR(SC(COL_TEAL)), UI_LEFT, 0);
    ui_f.solid = was;
}

/* The picture on the tile's face: the theme's or the set's (or the glass sheet's symbol on the card's tone) under its
 * frame's line, drawn as the Done screen draws its picture (R19). */
static void pour_picture(const uint8_t *picture, int symbol)
{
    float k = (float)POUR_PICTURE / (PANE_BIG - 2 * PANE_BIG_RIM), rim = PANE_BIG_RIM * k;
    if (picture)
        ui_icon(picture, POUR_PICTURE_X, POUR_PICTURE_Y, POUR_PICTURE);
    else if (symbol >= 0) {
        ui_sprite_tinted(ART_PANE_BIG_FILL, POUR_PICTURE_X, POUR_PICTURE_Y, k, k, NULL);
        float ks = ui_ew(symbol) > POUR_PICTURE - 8 ? (POUR_PICTURE - 8.0f) / ui_ew(symbol) : 1.0f;
        ui_sprite_scaled(symbol, POUR_PICTURE_X + POUR_PICTURE / 2.0f, POUR_PICTURE_Y + POUR_PICTURE / 2.0f, ks, 1);
    } else
        return;
    ui_sprite_tinted(ART_PANE_BIG_RIM, POUR_PICTURE_X - rim, POUR_PICTURE_Y - rim, k, k, NULL);
}

/* Of sprite id's image the part from column sx, w x h, as it hangs from the joint at jx, jy when upright with its
 * top left at x, y, turned about the joint by angle (radians, citro2d's: clockwise on the screen). Upright on whole
 * pixels from the atlas as it is; turned by the graphics unit, read smoothed, a picture for order.h (ui.c,
 * sprite_turned): citro2d places the params' centre at pos and turns the image about it. */
static void pour_hung(int id, int sx, int w, int h, float x, float y, float jx, float jy, float angle)
{
    const art_sprite *s = ui_sprite_info(id);
    C3D_Tex *tex = ui_sprite_tex(id, angle != 0);
    if (ui_f.discard || !s || !tex)
        return;
    if (angle == 0) {
        sprite_part(id, sx, 0, w, h, x, y, w, NULL);
        return;
    }
    float tw = tex->width, th = tex->height, c = cosf(angle), n = sinf(angle);
    Tex3DS_SubTexture sub = {(u16)w, (u16)h, (s->x + sx) / tw, 1.0f - s->y / th, (s->x + sx + w) / tw,
                             1.0f - (s->y + h) / th};
    C2D_DrawParams params = {{jx, jy, w, h}, {jx - x, jy - y}, 0, angle};
    C2D_ImageTint faded, *tint = NULL;
    if (ui_f.solid < 1.0f)
        C2D_AlphaImageTint(&faded, ui_f.solid), tint = &faded;
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (int k = 0; k < 4; k++) {       /* where its corners come to lie */
        float u = (k & 1 ? x + w : x) - jx, v = (k & 2 ? y + h : y) - jy;
        float cx = jx + u * c - v * n, cy = jy + u * n + v * c;
        x0 = fminf(x0, cx), x1 = fmaxf(x1, cx), y0 = fminf(y0, cy), y1 = fmaxf(y1, cy);
    }
    ui_picture(tex, &sub, &params, tint, (order_rect){x0, y0, x1 - x0, y1 - y0});
}

/* The holders on the line (C24, the canvas's B "Zange"; C19, its swing): what hangs from each holder's joint, the
 * piece it still carries and the tongs in front of it in their pose, as one body turned about the joint by the
 * holder's angle, then the clamp on the line, which does not swing. At rest every holder is upright and sharp. */
static void pour_holders(const pour *p)
{
    for (int i = 0; i < p->total; i++) {
        float x;
        int ball;
        if (!pour_holder(p, i, &x, &ball))
            continue;
        float jx = roundf(x - POUR_TONGS_W / 2.0f) + POUR_TONGS_W / 2, angle = -pour_holder_angle(p, i);
        if (ball)
            pour_hung(ART_POUR_PIECE, 0, POUR_PIECE_W, POUR_PIECE_H, jx - POUR_PIECE_W / 2,
                      POUR_PIECE_Y - POUR_PIECE_H / 2 + 1, jx, POUR_JOINT_Y, angle);
        pour_hung(ART_POUR_TONGS, pour_holder_pose(p, i) * POUR_TONGS_W, POUR_TONGS_W, POUR_TONGS_H,
                  jx - POUR_TONGS_W / 2, POUR_TONGS_Y, jx, POUR_JOINT_Y, angle);
        ui_sprite(ART_POUR_HOLDER, jx - POUR_HOLDER_W / 2, POUR_HOLDER_Y);
    }
}

/* done_line: the end's sentence. The ninth argument is not drawn: it was the second part of the path line that
 * stood where done_line stands and stays in the signature until app_ui.h and the callers follow. */
void ui_pour(const pour *p, const char *title, const char *done_title, const char *name, const uint8_t *picture,
             int symbol, const char *const steps[], const char *done_line, const char *unused)
{
    const float tx = POUR_TILE_X, ty = POUR_TILE_Y;
    float level = pour_level(p), glow = pour_glow(p), ending = pour_ending(p), h[POUR_COLS];
    C2D_ImageTint lit;
    (void)unused;
    ui_ground_flat();           /* the ground's light behind the tile on the screen's plane (C147) */
    pour_heights(p, h);
    ui_layer(POUR_DEPTH_BEHIND);
    if (!ui_lk->m.ground_sun)   /* the drop behind the tile, unless the look's ground draws it (C147) */
        ui_sprite(ART_DROP_SUN, DROP_SUN_X, DROP_SUN_Y);
    ui_dots(POUR_HEAD_DOTS_Y);  /* the rule before the head: a look's rule may lie under it */
    ui_layer(DEPTH_HEADER);
    pour_head(title, done_title, ending);
    ui_layer(POUR_DEPTH_TILE);
    if (glow > 0)               /* the glow behind the tile */
        ui_sprite_tinted(ART_POUR_HALO, tx, ty, 1, 1, pour_fade(&lit, glow, glow));
    if (level > 0) {            /* the liquid's light on the ground under the tile */
        float foot = fminf(level / POUR_FOOT_IN, 1);
        ui_sprite_tinted(ART_POUR_FOOT, tx, ty, 1, 1, pour_fade(&lit, foot, foot));
    }
    ui_sprite(ART_POUR_BACK, tx, ty);
    pour_pieces(p);
    if (level > 0) {
        pour_liquid(h, glow);
        pour_ghosts(p, h);
    }
    pour_picture(picture, symbol);
    ui_sprite(ART_POUR_FRONT, tx, ty);
    if (glow > 0)
        ui_sprite_tinted(ART_POUR_LIT_RIM, tx, ty, 1, 1, pour_fade(&lit, glow, glow));
    ui_layer(DEPTH_NAME);
    pour_name(name);
    ui_layer(DEPTH_CARD_TEXT);
    pour_list(p, steps);
    ui_layer(POUR_DEPTH_LINE);
    float over = fabsf(ui_f.shift);     /* the layer's shift: the line, all one tone, reaches both edges in either eye */
    ui_sprite_box(ART_POUR_LINE, (rect){-over, POUR_LINE_Y, TOP_W + 2 * over, ui_eh(ART_POUR_LINE)});
    ui_sprite(ART_POUR_KNOT, POUR_MOUTH_X - POUR_KNOT / 2, POUR_LINE_Y + 1 - POUR_KNOT / 2);
    pour_holders(p);
    if (ending > 0) {           /* the end: the check and the sentence */
        ui_layer(DEPTH_HINT);
        pour_end(done_line, ending);
    }
}
