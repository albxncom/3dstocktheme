/* The application's own widgets (app_ui.h): the parts of its screens only it has, drawn with the drawing layer
 * (uikit/source/ui.c) through kit.h and widget.h, as ui.c drew them. */
#include "app_ui.h"
#include "kit.h"
#include "look.h"
#include "widget.h"

#include "ease.h"
#include "progress.h"
#include "swarm.h"
#include "turn.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static u64 start_since;         /* the start screen's clock: when it opened, the house face-on */
static u32 start_seen;          /* the frame it was drawn in last */
_Static_assert(ART_MUSIC_BADGE_PLATIN == ART_MUSIC_BADGE + GLASS_PLATIN &&
               ART_DONE_BADGE_PLATIN == ART_DONE_BADGE + GLASS_PLATIN, "a badge's styles stand in a row");

/* ---------------------------------------------------------------- text */

static float widest(const ui_block *b)
{
    float w = 0;
    for (int i = 0; i < b->count; i++) {
        float lw = ui_text_width(b->line[i], b->px);
        if (lw > w)
            w = lw;
    }
    return w;
}

/* ---------------------------------------------------------------- top screen */

/* The way to the theme in the HOME Menu: house, "HOME-Menü", chevron, "Design ändern", one line from x. */
static float way_width(const char *home, const char *change)
{
    return ui_ew(ART_HOME) + WAY_GAP + roundf(ui_text_width(home, WAY_PX)) + WAY_GAP + ui_ew(ART_CHEVRON) + WAY_GAP +
           ui_text_width(change, WAY_PX);
}

static void way(float x, float top, const char *home, const char *change, float most, uint32_t color)
{
    float b = ui_baseline(top, WAY_LH, WAY_PX), at = roundf(x);
    ui_inline_symbol(ART_HOME, at, b);
    at += ui_ew(ART_HOME) + WAY_GAP;
    ui_text(home, at, b, WAY_PX, color, UI_LEFT);
    at = roundf(at + ui_text_width(home, WAY_PX) + WAY_GAP);
    ui_inline_symbol(ART_CHEVRON, at, b);
    at += ui_ew(ART_CHEVRON) + WAY_GAP;
    ui_text_fit(change, at, b, x + most - at, WAY_PX, color, UI_LEFT, 0);
}

void ui_hero(int symbol, const char *title, const char *home, const char *change)
{
    ui_dots(HERO_DOTS_Y);
    rect c = {TOP_PAD, HERO_CARD_Y, TOP_W - 2 * TOP_PAD, HERO_CARD_H};
    ui_layer(DEPTH_CARD);
    ui_lk->draw.card(c, -1);
    float h = ui_eh(symbol) + HERO_GAP + HERO_LH + (home ? WAY_GAP + WAY_LH : 0);
    float top = roundf(c.y + (c.h - h) / 2);
    ui_layer(DEPTH_ICON);
    ui_sprite(symbol, roundf((TOP_W - ui_ew(symbol)) / 2.0f), top);
    top += ui_eh(symbol) + HERO_GAP;
    ui_layer(DEPTH_TITLE);
    ui_text_fit(title, TOP_W / 2.0f, ui_baseline(top, HERO_LH, HERO_PX), c.w - 2 * CARD_PAD, HERO_PX, INK, UI_CENTER,
                0);
    if (home) {
        ui_layer(DEPTH_HINT);
        float most = c.w - 2 * CARD_PAD, w = way_width(home, change);
        way(roundf((TOP_W - (w < most ? w : most)) / 2.0f), top + HERO_LH + WAY_GAP, home, change, most, MUTED);
    }
}

void ui_detail(const uint8_t *smdh_icon, int symbol, const char *name, const char *note, uint32_t note_color)
{
    static ui_block note_lines;
    rect c = ui_card_rect();
    ui_layer(DEPTH_CARD);
    ui_lk->draw.card(c, -1);
    float x = c.x + CARD_PAD, iw = DETAIL_ICON, ih = DETAIL_ICON;
    if (!smdh_icon) {
        iw = ui_ew(symbol);
        ih = ui_eh(symbol);
    }
    float tx = x + iw + DETAIL_GAP, tw = c.x + c.w - CARD_PAD - tx;
    char shown[2 * UI_LINE_BYTES];      /* ui_wrap ends a line at "\n" (the copy's paragraphs); in a name it is a space */
    size_t n = strlen(name);
    ui_wrap(&ui_scratch, n < sizeof shown ? ui_plain(shown, name, n) : name, tw, NAME_PX, NAME_MIN_PX, 2, 0, NAME_LH);
    int notes = note && *note;
    if (notes)
        ui_wrap(&note_lines, note, tw, NOTE_PX, NOTE_PX, NOTE_LINES, 0, NOTE_LH);
    float h = ui_block_height(&ui_scratch) + (notes ? DETAIL_SPACE + ui_block_height(&note_lines) : 0);
    float top = roundf(c.y + (c.h - h) / 2), iy = roundf(c.y + (c.h - ih) / 2);
    ui_layer(DEPTH_ICON);
    if (smdh_icon)
        ui_icon(smdh_icon, x, iy, DETAIL_ICON);
    else
        ui_sprite(symbol, x, iy);
    if (!smdh_icon)
        ui_top_signs |= ui_motif(symbol);
    ui_layer(DEPTH_NAME);
    ui_block_draw(&ui_scratch, tx, top, INK, UI_LEFT);
    top += ui_block_height(&ui_scratch);
    ui_layer(DEPTH_CARD_TEXT);
    if (notes)
        ui_block_draw(&note_lines, tx, top + DETAIL_SPACE, note_color, UI_LEFT);
}

void ui_busy_card(const uint8_t *smdh_icon, int symbol, const char *name, float fraction, const char *line)
{
    rect c = ui_card_rect();
    ui_layer(DEPTH_CARD);
    ui_card_glass(c);
    float x = c.x + CARD_PAD, w = c.w - 2 * CARD_PAD, iw = 0, ih = 0;
    if (smdh_icon)
        iw = ih = SMALL_ICON;
    else if (symbol >= 0)
        iw = ui_ew(symbol), ih = ui_eh(symbol);
    float row = ih > NAME_LH ? ih : NAME_LH;
    float h = row + CARD_SPACE + PROGRESS_H + (line ? CARD_SPACE + STEP_LH : 0);
    float top = roundf(c.y + (c.h - h) / 2);
    ui_layer(DEPTH_ICON);
    if (smdh_icon)
        ui_icon(smdh_icon, x, top + (row - ih) / 2, SMALL_ICON);
    else if (symbol >= 0)
        ui_sprite(symbol, x, roundf(top + (row - ih) / 2));
    ui_layer(DEPTH_NAME);
    float tx = iw ? x + iw + SMALL_GAP : x;
    ui_text_fit(name, tx, ui_baseline(top, row, NAME_PX), x + w - tx, NAME_PX, INK, UI_LEFT, 0);
    top += row + CARD_SPACE;
    ui_layer(DEPTH_PROGRESS);
    ui_progress((rect){x, top, w, PROGRESS_H}, fraction);
    ui_layer(DEPTH_CARD_TEXT);
    top += PROGRESS_H + CARD_SPACE;
    if (line) {
        ui_text_fit(line, x + w / 2, ui_baseline(top, STEP_LH, STEP_PX), w, STEP_PX, MUTED, UI_CENTER, 0);
    }
}

void ui_done_card(const uint8_t *smdh_icon, const char *name, const char *home, const char *change)
{
    rect c = {TOP_PAD, DONE_CARD_Y, TOP_W - 2 * TOP_PAD, DONE_CARD_H};
    float most = c.w - 2 * CARD_PAD;
    ui_layer(DEPTH_CARD);
    ui_lk->draw.card(c, -1);
    ui_layer(DEPTH_ICON);
    ui_icon(smdh_icon, DONE_PICTURE_X, DONE_PICTURE_Y, DONE_PICTURE);
    if (smdh_icon) {
        /* the middle pane's glass and rim, as on the check screen before, scaled to the picture */
        float k = (float)DONE_PICTURE / (PANE_BIG - 2 * ui_lk->m.pane_rim_big), rim = ui_lk->m.pane_rim_big * k;
        ui_sprite_tinted(ART_PANE_BIG_RIM, DONE_PICTURE_X - rim, DONE_PICTURE_Y - rim, k, k, NULL);
    }
    ui_layer(DEPTH_BADGE);
    ui_sprite(ART_DONE_BADGE + ui_glass_want, DONE_BADGE_X, DONE_BADGE_Y);
    ui_layer(DEPTH_NAME);
    ui_text_fit(name, TOP_W / 2.0f, ui_baseline(DONE_TEXT_Y, NAME_LH, NAME_PX), most, NAME_PX, INK, UI_CENTER, 0);
    if (!home)
        return;
    ui_layer(DEPTH_CARD_TEXT);
    float w = way_width(home, change);
    way(roundf((TOP_W - (w < most ? w : most)) / 2.0f), DONE_TEXT_Y + NAME_LH + DONE_TEXT_GAP, home, change, most,
        INK);
}

void ui_way_pill(const char *home, const char *change)
{
    float w = way_width(home, change), most = BOTTOM_W - 2 * (TILE_X1 + PILL_PAD);
    rect r = {0, PILL_Y, ceilf(w < most ? w : most) + 2 * PILL_PAD, PILL_H};
    r.x = roundf((BOTTOM_W - r.w) / 2);
    ui_lk->draw.card(r, -1);
    way(r.x + PILL_PAD, r.y + (PILL_H - WAY_LH) / 2, home, change, r.w - 2 * PILL_PAD, INK);
}

/* The house's picture for this moment and this eye (turn.h), from its sheet (the part's pictures). */
static void start_logo(void)
{
    C3D_Tex *sheet = ui_part_pictures();
    if (ui_f.discard || !sheet)
        return;
    int at = turn_picture(MOVING_MS(ui_f.ms - start_since), ui_f.slider > 0 ? (ui_f.eye ? -1 : 1) : 0, ui_f.slider);
    int per_row = LOGO_SHEET / HERO_ICON;
    float x = (float)(at % per_row * HERO_ICON), y = (float)(at / per_row * HERO_ICON), n = LOGO_SHEET;
    Tex3DS_SubTexture sub = {HERO_ICON, HERO_ICON, x / n, 1.0f - y / n, (x + HERO_ICON) / n,
                             1.0f - (y + HERO_ICON) / n};
    C2D_DrawParams params = {{START_HOUSE_X, START_HOUSE_Y, HERO_ICON, HERO_ICON}, {0, 0}, 0, 0};
    ui_layer(DEPTH_ICON);
    ui_picture(sheet, &sub, &params, NULL, (order_rect){params.pos.x, params.pos.y, params.pos.w, params.pos.h});
}

void ui_start(void)
{
    if (turn_opens(ui_f.no, start_seen))
        start_since = ui_f.ms;
    start_seen = ui_f.no;
    ui_start_textures();        /* the pattern behind it: scene drew it already */
    /* the word's two lines, each breathing on its own (turn.h), drawn where they are to a fraction of a px */
    ui_layer(DEPTH_WORD);
    u64 since = MOVING_MS(ui_f.ms - start_since);
    ui_sprite_scaled(ART_WORD_TOP, START_WORD_X + WORD_W / 2.0f,
                  START_WORD_Y + WORD_TOP_ROW + WORD_TOP_H / 2.0f + turn_breath(since, 0), 1.0f, 1.0f);
    ui_sprite_scaled(ART_WORD_LOW, START_WORD_X + WORD_W / 2.0f,
                  START_WORD_Y + WORD_LOW_ROW + WORD_LOW_H / 2.0f + turn_breath(since, WORD_LAG_MS), 1.0f, 1.0f);
    /* the house last; the pattern, the word and the house are pictures in the sense of order.h, so whatever text
     * follows them gets the atlas's transparent piece first (ui_text) */
    start_logo();
}

/* The name's baseline in G1, centred to the eye between the picture's lower edge and the card's (as much room from
 * the picture down to the capitals as from the baseline down to the card's edge, the badge and accents not
 * counted), then GAL_NAME_UP px up, for more room under it; the placeholder stands on the same baseline. */
static float gallery_baseline(void)
{
    float top = GAL_PICTURE_Y + GAL_PICTURE, low = GAL_CARD_Y + GAL_CARD_H;
    return (top + low + FONT_CAP * ui_scale_of(GAL_NAME_PX)) / 2 - GAL_NAME_UP;
}

/* G1 as a swarm (0.9.3, round 14 F3; swarm.h, ui_layout.h SWARM_* and layout.h PANE_*) */
static swarm gallery_swarm;

/* The veil (round 14): a pane is drawn as solid as it is and mixed towards what lies behind it by 1 - its opacity,
 * the same on all its images: at its upper corners towards the ground's colour at its upper edge, at its lower ones
 * towards the card's (the panes reach below the card's upper edge); the outer side of the outermost pair fainter
 * still. The GPU blends between the corners. Its alpha: how see-through (only on the way to and from the gone slot),
 * times a popup's fade. */
static void veil_tint(C2D_ImageTint *tint, swarm_pane p)
{
    uint32_t a = SC(COL_TOP_BG_A), b = SC(COL_TOP_BG_B);
    float alpha = p.alpha * ui_f.solid, low_y = p.y + p.h, inner = 1 - p.opacity, outer = 1 - p.opacity * (1 - p.edge);
    u32 top = ui_mix(a, b, p.y / SCREEN_H, alpha);
    u32 low = low_y > GAL_CARD_Y ? ui_mix(SC(COL_CARD), SC(COL_CARD), 0, alpha) : ui_mix(a, b, low_y / SCREEN_H, alpha);
    C2D_SetImageTint(tint, C2D_TopLeft, top, p.side < 0 ? outer : inner);
    C2D_SetImageTint(tint, C2D_TopRight, top, p.side > 0 ? outer : inner);
    C2D_SetImageTint(tint, C2D_BotLeft, low, p.side < 0 ? outer : inner);
    C2D_SetImageTint(tint, C2D_BotRight, low, p.side > 0 ? outer : inner);
}

/* The idle light (ui_layout.h, SWARM_LIGHT_*) on a picture at x, y, w x h, t of its way across (0 to 1): a window as
 * large as the picture moved along the light's strip, so the band is cut off at the picture's box without a scissor;
 * the rim drawn after it covers the box's corners. */
static void pane_light(float x, float y, float w, float h, float t, const C2D_ImageTint *tint)
{
    const art_sprite *s = ui_sprite_info(ART_PANE_LIGHT);
    C3D_Tex *tex = ui_sprite_tex(ART_PANE_LIGHT, 1);
    if (ui_f.discard || !s || !tex)
        return;
    float side = s->h, o = (1 - t) * (s->w - side), tw = tex->width, th = tex->height;
    Tex3DS_SubTexture sub = {(u16)side, (u16)side, (s->x + o) / tw, 1.0f - s->y / th, (s->x + o + side) / tw,
                             1.0f - (s->y + side) / th};
    C2D_DrawParams params = {{x, y, w, h}, {0, 0}, 0, 0};
    ui_picture(tex, &sub, &params, tint, (order_rect){x, y, w, h});
}

/* One pane: the picture (item index's icon in its slot; else the empty pane's white and the symbol, if any, scaled
 * with the pane), the idle light when light is 0 to 1, then the glass over it and its rim, of the class nearest in
 * height; all in the pane's box, scaled together, with one veil. */
static void pane(ui_pic pic, size_t index, swarm_pane p, float light)
{
    int big = p.h > PANE_CLASS_AT;
    float n = big ? PANE_BIG : PANE_SMALL, rim = big ? ui_lk->m.pane_rim_big : ui_lk->m.pane_rim_small;
    float kx = p.w / n, ky = p.h / n;
    float hx = p.x + rim * kx, hy = p.y + rim * ky;
    C2D_ImageTint tint;
    veil_tint(&tint, p);
    ui_layer(p.depth);
    if (pic.icon) {
        ui_icon_put(pic.icon, icon_slot_keyed(index), (order_rect){hx, hy, (n - 2 * rim) * kx, (n - 2 * rim) * ky},
                 &tint);
    } else {
        ui_sprite_tinted(big ? ART_PANE_BIG_FILL : ART_PANE_SMALL_FILL, hx, hy, kx, ky, &tint);
        if (pic.symbol >= 0) {
            float k = fminf(p.w, p.h) / GAL_PICTURE;
            ui_sprite_tinted(pic.symbol, p.x + (p.w - ui_ew(pic.symbol) * k) / 2, p.y + (p.h - ui_eh(pic.symbol) * k) / 2,
                             k, k, &tint);
        }
    }
    if (light >= 0)
        pane_light(hx, hy, (n - 2 * rim) * kx, (n - 2 * rim) * ky, light, &tint);
    ui_sprite_tinted(big ? ART_PANE_BIG_RIM : ART_PANE_SMALL_RIM, p.x, p.y, kx, ky, &tint);
}

/* The panes of the items round pos, far to near (the middle one last, so each nearer pane covers the farther). */
static void panes(ui_pic (*at)(size_t i), size_t count, float pos, const swarm *lit)
{
    long first, last, near[SWARM_KEYED];
    int n = 0;
    swarm_span(pos, count, &first, &last);
    for (long i = first; i <= last && n < SWARM_KEYED; i++) {
        int j = n++;
        for (; j > 0 && fabsf((float)near[j - 1] - pos) < fabsf((float)i - pos); j--)
            near[j] = near[j - 1];
        near[j] = i;
    }
    for (int j = 0; j < n; j++) {
        ui_pic pic = at((size_t)near[j]);
        if (pic.icon || pic.symbol >= 0)
            pane(pic, (size_t)near[j], swarm_at((float)near[j] - pos, ui_lk->m.pane_apart),
                 lit ? swarm_light(lit, ui_f.ms, (int)(near[j] - (long)pos)) : -1);
    }
}

static void gallery_card(void)
{
    ui_layer(DEPTH_CARD);
    ui_lk->draw.card((rect){TOP_PAD, GAL_CARD_Y, TOP_W - 2 * TOP_PAD, GAL_CARD_H}, -1);
}

/* The music badge on the middle pane's lower right corner, and the name. */
static void gallery_name(const char *name, int music)
{
    if (music) {
        ui_layer(DEPTH_BADGE);
        float cx = GAL_PICTURE_X + GAL_PICTURE - GAL_BADGE_IN, cy = GAL_PICTURE_Y + GAL_PICTURE - GAL_BADGE_IN;
        ui_sprite(ART_MUSIC_BADGE + ui_glass_want, roundf(cx - GAL_BADGE / 2.0f), roundf(cy - GAL_BADGE / 2.0f));
    }
    ui_layer(DEPTH_NAME);
    ui_text_fit(name, TOP_W / 2.0f, gallery_baseline(), TOP_W - 2 * TOP_PAD - 2 * CARD_PAD, GAL_NAME_PX, INK, UI_CENTER,
                0);
}

static ui_pic gallery_three[3];

static ui_pic three_at(size_t i)
{
    return gallery_three[i];
}

void ui_gallery(ui_pic now, ui_pic prev, ui_pic next, const char *name, int music)
{
    gallery_three[0] = prev, gallery_three[1] = now, gallery_three[2] = next;
    gallery_card();
    panes(three_at, 3, 1, NULL);
    gallery_name(name, music);
}

void ui_swarm(ui_pic (*pic)(size_t i), size_t count, size_t cursor, const void *list, const char *name, int music)
{
    float pos = swarm_follow(&gallery_swarm, list, cursor, ui_f.ms, ui_f.no);
    gallery_card();
    panes(pic, count, pos, &gallery_swarm);
    /* the note appears on a move's last frame and leaves on its first; the name changes on the first */
    gallery_name(name, music && !eased_moving(&gallery_swarm.pos, ui_f.ms));
}

void ui_gallery_empty(void)
{
    gallery_card();
    pane((ui_pic){NULL, -1}, 0, swarm_at(0, ui_lk->m.pane_apart), -1);
    /* the name's placeholder where ui_gallery draws the name: its capitals on its baseline, reaching EMPTY_NAME_BELOW
     * px under it and the rest of EMPTY_NAME_MORE above them */
    ui_layer(DEPTH_NAME);
    ui_sprite(ART_EMPTY_NAME, roundf((TOP_W - EMPTY_NAME_W) / 2.0f),
              roundf(gallery_baseline()) + EMPTY_NAME_BELOW - ui_eh(ART_EMPTY_NAME));
}

/* The step marks and the line by glass style (0.9.3, round 12): the images of a step to come, under way and done,
 * each STEP_DROP_LATER, _NOW, _DONE px across where it shows at its own size; the liquid that wells up in a mark
 * becoming done (none: -1); the line's image and its fill; where a step's stretch of the line ends (before, after:
 * layout.h; 0 and 0 centre to centre) and how far the fill is at least. Clear (C1) and Platin (P2): chambers or
 * settings round a glass tube, their images reaching along it, so the mark under way comes in at its own size by its
 * opacity alone (grow 0), not growing from the small mark (the piece of tube in it would be scaled). Shard (S1): its
 * marks of 0.9.2 on a rod of Shard glass. The images carry their shadow as a pad all round (art.py "installing"). */
static const struct {
    short later, now, done, core, line, fill, grow, before, after, least, front;
} STEP_ART[GLASS_COUNT] = {
    [GLASS_CLEAR] = {ART_STEP_CHAMBER_LATER, ART_STEP_CHAMBER_NOW, ART_STEP_CHAMBER_DONE, ART_STEP_CHAMBER_CORE,
                     ART_STEP_TUBE, ART_STEP_TUBE_FILL, 0, STEP_TUBE_BEFORE, STEP_TUBE_AFTER, STEP_FILL_LEAST, 0},
    [GLASS_SHARD] = {ART_STEP_DROP_LATER, ART_STEP_NOW_GLASS_SHARD, ART_STEP_DONE_GLASS_SHARD, -1, ART_STEP_ROD,
                     ART_STEP_ROD_FILL, 1, 0, 0, STEP_ROD_LEAST, STEP_ROD_FRONT},
    [GLASS_PLATIN] = {ART_STEP_SETTING_LATER, ART_STEP_SETTING_NOW, ART_STEP_SETTING_DONE, ART_STEP_SETTING_CORE,
                      ART_STEP_TUBE, ART_STEP_TUBE_FILL, 0, STEP_TUBE_BEFORE, STEP_TUBE_AFTER, STEP_FILL_LEAST, 0},
};

/* One image of a step mark about cx, cy: id shows px across at its own size and is wanted size across, as solid as
 * alpha says. At its own size it is drawn on whole pixels as every sprite; at another, scaled and smoothed. */
static void step_image(int id, float px, float size, float cx, float cy, float alpha)
{
    if (alpha <= 0)
        return;
    if (size != px) {
        ui_sprite_scaled(id, cx, cy, size / px, alpha);
        return;
    }
    rect at = {roundf(cx - ui_ew(id) / 2), roundf(cy - ui_eh(id) / 2), ui_ew(id), ui_eh(id)};
    C2D_ImageTint tint;
    C2D_AlphaImageTint(&tint, alpha);
    ui_sprite_draw(id, at, -1e9f, 1e9f, alpha < 1 ? &tint : NULL);
}

/* A step's mark at level mark (0 to come, 1 under way, 2 done; between two: on its way): what progress_mark_look
 * says, the bead breathing by the clock; where the style has one, the liquid welling up from the middle while the
 * step becomes done. */
static void step_mark(float cx, float cy, float mark)
{
    float t = (float)(ui_f.ms % STEP_PULSE_MS) / STEP_PULSE_MS, pulse = 0.5f - 0.5f * cosf(TAU * t);
    progress_look l = progress_mark_look(mark, pulse);
    float way = mark > 1 ? mark - 1 : 0, now_px = STEP_ART[ui_glass_want].grow ? l.now_px :
                                                  progress_mark_look(1, pulse).now_px;
    step_image(STEP_ART[ui_glass_want].later, STEP_DROP_LATER, STEP_DROP_LATER, cx, cy, l.later);
    step_image(STEP_ART[ui_glass_want].now, STEP_DROP_NOW, now_px, cx, cy, l.now);
    if (STEP_ART[ui_glass_want].core >= 0 && way > 0 && way < 1) {
        float opaque = way * 100 / STEP_CORE_SOLID_PCT, from = STEP_CORE_FROM_PCT / 100.0f;
        ui_sprite_scaled(STEP_ART[ui_glass_want].core, cx, cy,
                      from + (1 - from) * ease_both(way * 100 / STEP_CORE_GROWN_PCT), opaque < 1 ? opaque : 1);
    }
    step_image(STEP_ART[ui_glass_want].done, STEP_DROP_DONE, l.done_px, cx, cy, l.done);
}

void ui_steps(const char *title, const char *name, const char *const steps[4], const float mark[4], float line_end)
{
    static const float cx[4] = {STEPS_X1, STEPS_X2, STEPS_X3, STEPS_X4};
    ui_layer(DEPTH_DROPS_BEHIND);
    ui_sprite(ART_DROP_SUN, DROP_SUN_X, DROP_SUN_Y);
    ui_layer(DEPTH_TITLE);
    float tw = STEPS_TITLE_ICON + TAG_ICON_GAP * 2 + ui_text_width(title, STEPS_TITLE_PX);
    float tx = roundf((TOP_W - tw) / 2), tb = ui_baseline(STEPS_TITLE_Y, LINE_LH + 4, STEPS_TITLE_PX);
    ui_sprite(ART_STEPS_TITLE, tx, roundf(tb) - STEPS_TITLE_ICON + 2);
    ui_text(title, tx + STEPS_TITLE_ICON + TAG_ICON_GAP * 2, tb, STEPS_TITLE_PX, UI_COLOR(SC(COL_TEAL)), UI_LEFT);
    ui_dots(STEPS_DOTS_Y);
    ui_layer(DEPTH_CARD);
    ui_card_glass((rect){TOP_PAD, STEPS_CARD_Y, TOP_W - 2 * TOP_PAD, STEPS_CARD_H});
    ui_layer(DEPTH_NAME);
    ui_text_fit(name, TOP_W / 2.0f, ui_baseline(STEPS_NAME_Y, NAME_LH, STEPS_NAME_PX), TOP_W - 2 * TOP_PAD - 2 * CARD_PAD,
                STEPS_NAME_PX, INK, UI_CENTER, 0);
    ui_layer(DEPTH_CARD_TEXT);
    /* the line: the tube (to just past the last mark's image) or the rod (to its centre), the liquid or teal rod in
     * it up to the line's end, held off the marks where the style's marks reach along the tube */
    float line_top = STEPS_CY - STEP_TUBE_H / 2, end = progress_line_held(line_end, STEPS_LINE_X, cx, mark,
                                                                     STEP_ART[ui_glass_want].before,
                                                                     STEP_ART[ui_glass_want].after);
    float reach = STEP_ART[ui_glass_want].after ? STEPS_X4 + STEP_TUBE_AFTER + STEP_TUBE_TAIL : STEPS_X4;
    float filled = roundf(end - STEPS_LINE_X) + STEP_ART[ui_glass_want].front;
    ui_sprite_box(STEP_ART[ui_glass_want].line, (rect){STEPS_LINE_X, line_top, reach - STEPS_LINE_X, STEP_TUBE_H});
    ui_sprite_box(STEP_ART[ui_glass_want].fill, (rect){STEPS_LINE_X, line_top, filled > STEP_ART[ui_glass_want].least ? filled :
                                                    STEP_ART[ui_glass_want].least, STEP_TUBE_H});
    for (int k = 0; k < 4; k++) {
        /* the depth and the label's ink go along with the mark: on the card to come, in front of it under way, on
         * the card again when done. In front, the mark is drawn smoothed, so its place in stereo need not be a whole
         * pixel and it comes forward without a step */
        float m = mark[k] < 0 ? 0 : mark[k] > 2 ? 2 : mark[k], begun = m < 1 ? m : 1;
        float raised = progress_mark_look(m, 0).raised;
        if (ui_f.top == 1 && raised > 0)
            ui_view(ui_zoom, TOP_W / 2.0f, SCREEN_H / 2.0f,
                    (ui_f.eye ? -1 : 1) * (DEPTH_CARD_TEXT + (DEPTH_STEP_NOW - DEPTH_CARD_TEXT) * raised) *
                        ui_f.slider / 2);
        step_mark(cx[k], STEPS_CY, m);
        ui_layer(DEPTH_CARD_TEXT);
        ui_text_fit(steps[k], cx[k], ui_baseline(STEPS_LABEL_Y, STEP_LH, STEPS_LABEL_PX), 76, STEPS_LABEL_PX,
                    ui_mix(SC(COL_MUTED), SC(COL_INK), begun, 1.0f), UI_CENTER, 0);
    }
}

#include "pour_ui.h"

/* url with its middle (between the host and the file name) cut to keep bytes, the rest as "...". */
static void middle_cut(char *out, size_t size, const char *url, size_t keep)
{
    const char *scheme = strstr(url, "://");
    const char *host_end = strchr(scheme ? scheme + 3 : url, '/');
    const char *file = strrchr(url, '/');
    size_t n = strlen(url);
    size_t head = host_end ? (size_t)(host_end - url) + 1 : n, tail = file && file >= host_end ? (size_t)(url + n - file - 1) : 0;
    if (head + tail >= n || head + tail + keep >= n) {
        snprintf(out, size, "%s", url);
        return;
    }
    size_t a = wrap_prev(url, head + keep / 2 + 1), b = n - tail - (keep - keep / 2);
    if (a < head)
        a = head;
    size_t at = 0;
    while (at < b)                /* the first character starting at or after b */
        at = wrap_next(url, at);
    snprintf(out, size, "%.*s...%s", (int)a, url, url + at);
}

/* The address in its lines, wrapped once for each address (not every frame): shrunk down to URL_MIN_PX, and if it
 * still does not fit, the middle between host and file name cut as little as fits. */
static const ui_block *url_lines(const char *url, float most)
{
    static char key[2 * 4096];
    static ui_block lines;
    if (!strcmp(key, url))
        return &lines;
    snprintf(key, sizeof key, "%s", url);
    if (!ui_wrap(&lines, url, most, HOST_PX, URL_MIN_PX, URL_LINES, 0, HOST_LH)) {
        static char shown[sizeof key];
        size_t lo = 0, hi = strlen(url);
        while (lo < hi) {
            size_t mid = (lo + hi + 1) / 2;
            middle_cut(shown, sizeof shown, url, mid);
            if (ui_wrap(&lines, shown, most, HOST_PX, URL_MIN_PX, URL_LINES, 0, HOST_LH))
                lo = mid;
            else
                hi = mid - 1;
        }
        middle_cut(shown, sizeof shown, url, lo);
        ui_wrap(&lines, shown, most, HOST_PX, URL_MIN_PX, URL_LINES, 0, HOST_LH);
    }
    return &lines;
}

void ui_url_card(const char *url)
{
    rect c = ui_card_rect();
    ui_layer(DEPTH_CARD);
    ui_lk->draw.card(c, -1);
    float most = c.w - 2 * CARD_PAD - GLOBE - DETAIL_GAP;
    const ui_block *lines = url_lines(url, most);
    float tw = widest(lines), gx = roundf(c.x + (c.w - (GLOBE + DETAIL_GAP + tw)) / 2);
    if (gx < c.x + CARD_PAD)
        gx = c.x + CARD_PAD;
    ui_layer(DEPTH_ICON);
    ui_sprite(ART_GLOBE, gx, roundf(c.y + (c.h - GLOBE) / 2));
    ui_layer(DEPTH_CARD_TEXT);
    ui_block_draw(lines, gx + GLOBE + DETAIL_GAP, roundf(c.y + (c.h - ui_block_height(lines)) / 2), INK, UI_LEFT);
}

void ui_brackets(void)
{
    float x0 = (TOP_W - BRACKET) / 2, y0 = (SCREEN_H - BRACKET) / 2;
    float far = BRACKET - (BRACKET_INSET + BRACKET_ARM + BRACKET_STROKE);
    ui_layer(DEPTH_FRAME);
    ui_sprite(ART_BRACKET_TL, x0, y0);
    ui_sprite(ART_BRACKET_TR, x0 + far, y0);
    ui_sprite(ART_BRACKET_BR, x0 + far, y0 + far);
    ui_sprite(ART_BRACKET_BL, x0, y0 + far);
}

/* ---------------------------------------------------------------- bottom screen */

void ui_row_theme(rect r, int state, const uint8_t *smdh_icon, const char *name, const char *sub, int music,
                  uint32_t sub_color, int installed)
{
    r = ui_lk->draw.face(FACE_ROW, r, installed ? state | UI_OFF : state, -1);
    float down = state & UI_DOWN ? 1 : 0;
    uint32_t ink = down ? PRESS_INK : TILE_INK;
    if (down)
        sub_color = PRESS_INK;
    float ix = r.x + ROW_ICON_X, iy = r.y + (r.h - ROW_ICON) / 2 + down;
    if (smdh_icon)
        ui_icon(smdh_icon, ix, iy, ROW_ICON);
    else
        ui_sprite(ART_WARN_ROW, ix + (ROW_ICON - ROW_WARN) / 2, iy + (ROW_ICON - ROW_WARN) / 2);
    float tx = r.x + ROW_TEXT_X, right = r.x + r.w - ROW_PAD - (installed ? ui_ew(ART_CHECK_ROW) + ROW_PAD : 0);
    int has_sub = sub && *sub;
    float h = ROW_NAME_LH + (has_sub ? ROW_SUB_GAP + ROW_SUB_LH : 0), top = r.y + (r.h - h) / 2 + down;
    float name_b = ui_baseline(top, ROW_NAME_LH, ROW_NAME_PX);
    ui_text_fit(name, tx, name_b, right - tx, ROW_NAME_PX, ink, UI_LEFT, 0);
    if (has_sub) {
        float sy = top + ROW_NAME_LH + ROW_SUB_GAP, sx = tx;
        float sub_b = ui_baseline(sy, ROW_SUB_LH, ROW_SUB_PX);
        if (music) {
            ui_inline_symbol(ART_MUSIC_ROW, sx, sub_b);
            sx += ui_ew(ART_MUSIC_ROW) + TAG_ICON_GAP;
        }
        ui_text_fit(sub, sx, sub_b, right - sx, ROW_SUB_PX, sub_color, UI_LEFT, 0);
    }
    if (installed)    /* on the name's line, standing on its baseline */
        ui_inline_symbol(ART_CHECK_ROW, r.x + r.w - ROW_PAD - ui_ew(ART_CHECK_ROW), name_b);
}

// ---------------------------------------------------------------- the settings (the audio lead's, 0.9.2)

void ui_choices_word(void)
{
    rect c = ui_card_rect();
    float x = c.x + c.w - CARD_PAD - WORD_W, y = roundf(c.y + (c.h - WORD_H) / 2);
    ui_layer(DEPTH_CARD_TEXT);
    ui_sprite(ART_WORD_TOP, x, y + WORD_TOP_ROW);
    ui_sprite(ART_WORD_LOW, x, y + WORD_LOW_ROW);
}

int ui_choices_credits(float choices_end, const char *const lines[], int n)
{
    rect c = ui_card_rect();
    float w = c.w - 2 * CARD_PAD, h = 0;
    int whole = 1;
    for (int k = 0; k < n; k++) {
        whole &= ui_wrap(&ui_scratch, lines[k], w, CREDIT_PX, CREDIT_PX, 0, 0, CREDIT_LH);
        h += ui_block_height(&ui_scratch);
    }
    float top = c.y + c.h - CARD_PAD - h;
    ui_layer(DEPTH_CARD_TEXT);
    for (int k = 0; k < n; k++) {
        ui_wrap(&ui_scratch, lines[k], w, CREDIT_PX, CREDIT_PX, 0, 0, CREDIT_LH);
        ui_block_draw(&ui_scratch, c.x + CARD_PAD, top, MUTED, UI_LEFT);
        top += ui_block_height(&ui_scratch);
    }
    return whole && c.y + c.h - CARD_PAD - h - choices_end >= CREDIT_ROOM_MIN;
}
