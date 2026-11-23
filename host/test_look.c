/* The boundary between the drawing layer and the looks (uikit/include/look.h, look.c; G1): every look this build holds
 * fills its table and its art inflates for every scheme; a number not held draws Stock; a look told is drawn from
 * the next frame's start on, the old one put away first and every image of the new one's art unpacked anew; the
 * popup's tone reaches the look's buttons once; every look's loader draws, keeps the drawing rule under its ground
 * and is the one drawn (G1 0f). host/test_ui.c's recorder, ui.c and app_ui.c are included (its main
 * renamed), and the test-only look host/look_fake.c stands in under Prisma's number (ui.c asks this file which
 * look a number is: test_look_get, test_look_built), so the swap is seen with any build's looks. Every check of
 * test_ui then runs with the test-only look, which differs from Stock where a port may: they read its own
 * expectations (host/expect_fake.h, G1 0g). The kit's pieces a port asked for (G1 0g): ui_pattern_over, ui_pattern unshifted on the
 * bottom screen, ui_select_brackets from a look's own corners.
 *
 *   test_look   prints one line per check and "N failed"; exit 1 if any failed */
#define look_get test_look_get
#define look_built test_look_built
#define ui_sprite_turned real_sprite_turned
#define main test_ui_main
#include "test_ui.c"
#include "look_prism.h"
#include "art_of.h"
#undef main
#undef look_get
#undef look_built
#undef ui_sprite_turned

/* ui_sprite_turned as ui.c draws it, its calls counted with the last one's sprite and angle (Nachtglas's loader, C32):
 * ui.c's own is renamed above, the looks call this one. */
void real_sprite_turned(int id, float cx, float cy, float angle, const C2D_ImageTint *tint);
static int turns, turned_id = -1;
static float turned_angle;

void ui_sprite_turned(int id, float cx, float cy, float angle, const C2D_ImageTint *tint)
{
    turns++, turned_id = id, turned_angle = angle;
    real_sprite_turned(id, cx, cy, angle, tint);
}

const look *look_get(int id);
int look_built(int id);
extern const look look_fake;
extern char fake_calls[64];
extern int fake_scheme, fake_pattern, fake_removes, fake_loaders;
extern float fake_loader_cx, fake_loader_cy, fake_loader_alpha;
extern u64 fake_loader_ms;

#define FAKE_ID LOOK_PRISM
static int fake_on;             /* the test-only look stands under FAKE_ID */

int test_look_built(int id)
{
    return (fake_on && id == FAKE_ID) || look_built(id);
}

const look *test_look_get(int id)
{
    return fake_on && id == FAKE_ID ? &look_fake : look_get(id);
}

/* a blob inflated, when it is exactly size bytes: the bytes (until the next call), else NULL */
static const uint8_t *inflated(const uint8_t *z, uint32_t z_size, uLongf size)
{
    static uint8_t out[1 << 23];
    uLongf got = sizeof out;
    return z && size <= sizeof out && uncompress(out, &got, z, z_size) == Z_OK && got == size ? out : NULL;
}

static int inflates(const uint8_t *z, uint32_t z_size, uLongf size)
{
    return inflated(z, z_size, size) != NULL;
}

/* a texture holds exactly what the blob inflates to */
static int holds(const C3D_Tex *t, const uint8_t *z, uint32_t z_size)
{
    uLongf size = (uLongf)t->width * t->height * 4;
    const uint8_t *want = inflated(z, z_size, size);
    return want && !memcmp(t->data, want, size);
}

static void table(const look *l)
{
    char what[200];
    const look_draw *d = &l->draw;
    const short *m = &l->m.dim_pct;
    int entries = !!d->load + !!d->unload + !!d->refresh + !!d->ground + !!d->rule + !!d->card + !!d->face +
                !!d->select + !!d->bar + !!d->bar_label + !!d->popup_panel + !!d->popup_buttons + !!d->loader,
        numbers = 0, sun = expect_of(l->name)->ground_sun;
    for (size_t k = 0; k + 2 < sizeof l->m / sizeof *m; k++)  /* may be 0: pane_apart, ground_sun */
        numbers += m[k] != 0;
    snprintf(what, sizeof what, "%s: a name, its art, %d of 13 drawing functions, %d of 15 numbers not 0, the "
             "installing tile's drop the ground's %d (%d)", l->name, entries, numbers, l->m.ground_sun, sun);
    check(l->name && *l->name && l->art && entries == 13 && sizeof l->m / sizeof *m == 17 && numbers == 15 &&
          l->m.ground_sun == sun, what);
    const look_art *a = l->art;
    const uLongf tile = (uLongf)PATTERN_TILE * PATTERN_TILE * 4;
    int atlases = 0, sheets = 0, houses = 0, patterns = 0;
    for (int s = 0; s < ART_SCHEMES; s++) {
        const art_tex pa = art_tex_of(a, ART_ALL, s), ps = art_tex_of(a, ART_ALL + ART_APP_COUNT, s);
        atlases += inflates(a->atlas_z[s], a->atlas_z_size[s], (uLongf)ART_ATLAS_W * ART_ATLAS_H * 4) &&
                   inflates(pa.z, pa.z_size, (uLongf)ART_APP_PART.atlas_w * ART_APP_PART.atlas_h * 4);
        sheets += inflates(a->sheet_z[s], a->sheet_z_size[s], (uLongf)ART_GLASS_SHEET_W * ART_GLASS_SHEET_H * 4) &&
                  inflates(ps.z, ps.z_size, (uLongf)ART_APP_PART.sheet_w * ART_APP_PART.sheet_h * 4);
        const art_tex ph = art_pictures_of(a, s);
        houses += ph.z && inflates(ph.z, ph.z_size, (uLongf)ph.w * ph.h * 4);
        for (int p = 0; p < ART_PATTERNS; p++)
            patterns += inflates(a->pattern_z[s][p], a->pattern_z_size[s][p], tile);
    }
    snprintf(what, sizeof what, "%s: for the %d schemes %d atlases and %d sheets (the kit's and the part's), %d "
             "houses and %d of %d patterns' tiles inflate to their textures' sizes", l->name, ART_SCHEMES, atlases, sheets, houses, patterns,
             ART_SCHEMES * ART_PATTERNS);
    check(atlases == ART_SCHEMES && sheets == ART_SCHEMES && houses == ART_SCHEMES &&
          patterns == ART_SCHEMES * ART_PATTERNS, what);
}

/* a frame of the start screen's top over an empty bottom screen */
static void start_frame(void)
{
    recs = 0;
    ui_begin(1.0f);
    ui_top();
    ui_start();
    ui_bottom();
    ui_end();
}

/* The frame recorded: the texts after an image of another texture than the atlas, and the transparent pieces
 * (kit.h, the drawing rule; as test_ui's judge counts them). */
static void order_faults(int *after, int *pieces)
{
    const C3D_Tex *last = &atlas;
    for (int i = 0; i < recs; i++) {
        if (rec[i].kind == 'I') {
            int clear = rec[i].tex == &atlas && rec[i].x == 0 && rec[i].y == 0 && rec[i].w == 8 && rec[i].h == 8;
            *pieces += clear;
            if (!clear)
                last = rec[i].tex;
        } else if (rec[i].kind == 'R') {
            last = &atlas;
        } else {
            *after += last != &atlas;
        }
    }
}

/* ui_pattern_over (Frost's R1): a glass's tile over x, y, w, h shows the tile where the ground's pattern is on the
 * screen, for each eye with the 3D slider up, in a layer that moves it otherwise than the pattern; drawn at once
 * as a ground, an atlas image after it, then the glass's text: no text after a picture's texture. And on the bottom
 * screen (Frost's R2) the pattern and the glass's tile are not moved by the slider. */
static void pattern_over(void)
{
    ui_look(LOOK_STOCK, ART_SCHEME_WEISS, 1, PATTERN_HOUSES);
    for (int pass = 0; pass < 2; pass++)
        start_frame();
    for (int eye = 0; eye < 3; eye++) {         /* the top screen's left eye, its right eye, the bottom screen */
        recs = 0;
        watched = eye;
        ui_begin(1.0f);
        ui_f.slider = 1;        /* the recorder's slider is down; up, the eyes see the layers apart */
        for (int e = 0; e <= eye && e < 2; e++) {
            if (e)
                ui_next_eye();
            ui_top();
            if (eye == 2)
                break;
            ui_layer(DEPTH_ICON);
            ui_pattern_over(&pattern_tex, 100, 60, 80, 40, 1.0f);
            ui_sprite(ART_DOT, 100, 60);
            ui_text("Glas", 140, 90, FONT_PX, INK, UI_CENTER);
            ui_bottom();
        }
        if (eye == 2) {
            ui_bottom();
            ui_pattern(0, SCREEN_H, 1.0f);
            ui_pattern_over(&pattern_tex, 100, 60, 80, 40, 1.0f);
            ui_sprite(ART_DOT, 100, 60);
            ui_text("Glas", 140, 90, FONT_PX, INK, UI_CENTER);
        }
        ui_end();
        watched = 2;
        int ground = -1, glass = -1, after = 0, pieces = 0;
        for (int i = 0; i < recs; i++)
            if (rec[i].kind == 'I' && rec[i].tex == &pattern_tex)
                *(rec[i].x == -BLEED ? &ground : &glass) = i;
        order_faults(&after, &pieces);
        float n = PATTERN_TILE, du = 0, dv = 0;
        if (ground >= 0 && glass >= 0) {
            /* the tile's place one screen pixel left of the glass's edge, by the ground's quad and by the glass's */
            float at = rec[glass].x + rec[glass].tx, from = rec[ground].x + rec[ground].tx;
            du = rec[glass].u0 - (rec[ground].u0 + (at - from) / n);
            dv = rec[glass].v0 - (rec[ground].v0 - (rec[glass].y - rec[ground].y) / n);
        }
        char what[240];
        snprintf(what, sizeof what, "ui_pattern_over, %s: the glass's tile %s the ground's pattern (%g, %g "
                 "tiles off), the view moving the ground by %g px and the glass by %g; %d texts after a picture's "
                 "texture, %d transparent pieces", eye == 2 ? "the bottom screen" : eye ? "the right eye" :
                 "the left eye", fabsf(du) < 1e-4f && fabsf(dv) < 1e-4f ?
                 "where" : "NOT where", du, dv, ground >= 0 ? rec[ground].tx : 0, glass >= 0 ? rec[glass].tx : 0,
                 after, pieces);
        int moved = eye == 2 ? ground >= 0 && glass >= 0 && rec[ground].tx == 0 && rec[glass].tx == 0
                             : ground >= 0 && glass >= 0 && rec[ground].tx != rec[glass].tx && rec[glass].tx != 0;
        check(ground >= 0 && glass >= 0 && fabsf(du) < 1e-4f && fabsf(dv) < 1e-4f && moved && !after && !pieces,
              what);
    }
}

/* The alpha of the last image of the pattern's texture drawn, -1 for none; 1 when it is not tinted. */
static float pattern_alpha(void)
{
    float a = -1;
    for (int i = 0; i < recs; i++)
        if (rec[i].kind == 'I' && rec[i].tex == &pattern_tex) {
            float c[4];
            unpack_color(rec[i].tint.corners[0].color, c);
            a = rec[i].tinted ? c[3] : 1;
        }
    return a;
}

/* Frost's ground carries the pattern on both screens: under a popup half open (the bar's band going) and under the
 * loader's cover half way, the pattern is as solid as the ground, not solid from the first frame of the fade (the
 * join's review, finding 4). Nachtglas's carries it on the top screen: under the loader's cover there half way, as
 * solid as the ground. Stock's pattern (top screen only) stays solid, as its lists were. */
static void pattern_fades(void)
{
    if (!look_built(LOOK_FROST)) {
        check(1, "Frost not held: no ground with the pattern on the bottom screen to fade");
        return;
    }
    ui_look(LOOK_FROST, ART_SCHEME_WEISS, 1, PATTERN_HOUSES);
    for (int pass = 0; pass < 2; pass++)
        start_frame();
    ui_popup pop;
    memset(&pop, 0, sizeof pop);
    float under[2];
    for (int k = 0; k < 2; k++) {
        recs = 0;
        ui_begin(1.0f);
        ui_top();
        ui_bottom();
        if (k)
            ui_loader(NULL, NULL, 0.5f, 0);
        else
            ui_popup_draw(&pop, "Abbrechen", "OK", -1, -1, 0.5f);
        ui_end();
        under[k] = pattern_alpha();
    }
    float open = ease_both(0.5f);
    char what[200];
    snprintf(what, sizeof what, "Frost: the pattern under a popup half open is as solid as the ground (%g, the "
             "ground's %g), under the loader's cover half way too (%g, 0.5)", under[0], open, under[1]);
    check(ui_lk == &look_frost && fabsf(under[0] - open) < 0.01f && fabsf(under[1] - 0.5f) < 0.01f, what);
    float top[2] = {-1, -1};
    for (int k = 0; k < 2; k++) {
        if (!k && !look_built(LOOK_NIGHT))
            continue;
        ui_look(k ? LOOK_STOCK : LOOK_NIGHT, k ? 5 : ART_SCHEME_WEISS, 1, k ? PATTERN_PLUS : PATTERN_HOUSES);
        for (int pass = 0; pass < 2; pass++)
            start_frame();
        recs = 0;
        watched = 0;
        ui_begin(1.0f);
        ui_top();
        ui_loader(NULL, NULL, 0.5f, 0);
        ui_bottom();
        ui_end();
        watched = 2;
        top[k] = pattern_alpha();
    }
    snprintf(what, sizeof what, "Nachtglas: its pattern under the loader's cover half way is as solid as the ground "
             "(%g, 0.5)", top[0]);
    check(!look_built(LOOK_NIGHT) || fabsf(top[0] - 0.5f) < 0.01f, look_built(LOOK_NIGHT) ? what :
          "Nachtglas not held: nothing to fade");
    snprintf(what, sizeof what, "Stock: its pattern under the loader's cover half way stays solid (%g)", top[1]);
    check(top[1] == 1, what);
}

/* 1 when the icon texture's slot holds a red icon whose corners are mask's */
static int slot_masked(int slot, const uint8_t *mask)
{
    const uint32_t *t = (const uint32_t *)icon_tex.data + slot * 64 * 64;
    for (unsigned py = 0; py < ART_MASK_SIZE; py++)
        for (unsigned px = 0; px < ART_MASK_SIZE; px++) {
            uint32_t v = t[((py >> 3) * 8 + (px >> 3)) * 64 + ui_morton(px & 7, py & 7)];
            if (v >> 8 != 0xFF0000u || (v & 255) != mask[py * ART_MASK_SIZE + px])
                return 0;
        }
    return 1;
}

/* A theme's icon stays in its slot from frame to frame, converted once; a look swap converts it again, so the next
 * frame's icon has the new look's corners (the join's review, finding 2). Stock and a look held whose mask differs. */
static void icon_mask_swap(void)
{
    int was = fake_on, other = -1;
    fake_on = 0;
    size_t n = (size_t)ART_MASK_SIZE * ART_MASK_SIZE;
    for (int id = 0; id < LOOK_COUNT && other < 0; id++)
        if (look_built(id) && memcmp(look_get(id)->art->mask, look_stock.art->mask, n))
            other = id;
    uint8_t icon[48 * 48 * 2];
    for (size_t i = 0; i < sizeof icon; i += 2)
        icon[i] = 0x00, icon[i + 1] = 0xF8;     /* RGB565 red, little end first */
    int masked[2] = {0, 0};
    for (int k = 0; k < 2 && other >= 0; k++) {
        const look *want = look_get(k ? other : LOOK_STOCK);
        ui_look(k ? other : LOOK_STOCK, ART_SCHEME_WEISS, 1, PATTERN_HOUSES);
        ui_begin(1.0f);
        ui_top();
        ui_bottom();
        ui_icon(icon, 20, 20, 48);
        ui_end();
        masked[k] = ui_lk == want && slot_masked(SWARM_KEYED, want->art->mask);
    }
    char what[200];
    snprintf(what, sizeof what, "a theme's icon under Stock has Stock's corners (%s), the same icon the frame after a "
             "swap to %s has that look's (%s)", masked[0] ? "yes" : "no", other >= 0 ? look_get(other)->name : "none",
             masked[1] ? "yes" : "no");
    check(other < 0 || (masked[0] && masked[1]), other < 0 ? "no look held whose corners differ from Stock's: "
          "nothing to swap to" : what);
    ui_look(LOOK_STOCK, 5, 1, PATTERN_PLUS);
    start_frame();
    fake_on = was;
}

/* ui_select_brackets with a look's own first corner (Frost's R3): the eight corners (four at rest, four out) from
 * the atlas, each the size of one of the eight from first on (radius 0: first + 0 to 3 at rest, + 4 to 7 out), none
 * from the corner texture; with -1 the eight from the corner texture. */
static void brackets_first(void)
{
    const int square[4] = {0, 0, 0, 0};
    rect r = {100, 80, 120, 60};
    int atlas_n[2] = {0, 0}, corner_n[2] = {0, 0}, sized = 0;
    for (int k = 0; k < 2; k++) {
        frame_begin();
        int before = recs;
        ui_select_brackets(r, square, k ? ART_TILE : -1);
        ui_flush();
        for (int i = before; i < recs; i++) {
            if (rec[i].kind != 'I')
                continue;
            atlas_n[k] += rec[i].tex == &atlas || rec[i].tex == &atlas_smooth;
            corner_n[k] += rec[i].tex == &corner_tex;
            int fits = 0;
            for (int m = 0; k && m < 8; m++)
                fits |= rec[i].w == ui_sprite_info(ART_TILE + m)->w && rec[i].h == ui_sprite_info(ART_TILE + m)->h;
            sized += fits;
        }
        ui_end();
    }
    char what[200];
    snprintf(what, sizeof what, "ui_select_brackets: -1, %d corners from the corner texture (%d from the atlas); a "
             "first corner, %d from the atlas (%d of them its sprites' size, %d from the corner texture)", corner_n[0],
             atlas_n[0], atlas_n[1], sized, corner_n[1]);
    check(corner_n[0] == 8 && atlas_n[0] == 0 && atlas_n[1] == 8 && sized == 8 && corner_n[1] == 0, what);
}

/* The look drawn now (lk): its loader alone draws something, in Weiss and Nacht; and the loader as the application
 * shows it (ui_loader: over the look's ground, with its line) at three moments, fading in and whole, has no text
 * after a picture's texture and needs no transparent piece (kit.h, the drawing rule), on the bottom screen and on
 * the top one as the badge removal shows it (its title, the loader with the step's line). */
static void loader_of(int id)
{
    const int schemes[2] = {ART_SCHEME_WEISS, ART_SCHEME_NACHT};
    int drawn = 1, after = 0, pieces = 0;
    for (int k = 0; k < 2; k++) {
        ui_look(id, schemes[k], 1, 0);
        for (int pass = 0; pass < 2; pass++)
            start_frame();
        frame_begin();
        int before = recs;
        ui_lk->draw.loader(BOTTOM_W / 2.0f, SCREEN_H / 2.0f, ui_f.ms, 1.0f);
        ui_flush();
        drawn &= recs > before;
        ui_end();
        for (int f = 0; f < 6; f++) {
            frame_begin();
            ui_loader("Wird gelesen", NULL, 1.0f, f < 3 ? 0.5f : 1.0f);
            ui_end();
            order_faults(&after, &pieces);
            recs = 0;
            watched = 0;
            ui_begin(1.0f);
            ui_top();
            ui_head("Badges entfernen", NULL);
            ui_loader("Sicherung wird geschrieben", NULL, 0, f < 3 ? 0.5f : 1.0f);
            ui_bottom();
            ui_end();
            watched = 2;
            order_faults(&after, &pieces);
        }
    }
    char what[200];
    snprintf(what, sizeof what, "%s: its loader draws in Weiss and Nacht (%s); under its ground with the line on both "
             "screens, %d texts after a picture's texture, %d transparent pieces", ui_lk->name, drawn ? "yes" : "no", after,
             pieces);
    check(ui_lk == test_look_get(id) && drawn && after == 0 && pieces == 0, what);
}

/* Q3 (Prisma): a popup's paragraphs are laid out in the room above the buttons' band, DLG_H - DLG_BTN_H, which does
 * not move with the panel's height: under the test-only look's taller panel the same question has its texts at
 * Stock's sizes and places. */
static int popup_texts(int id, float *at, int most)
{
    static const char *const paras[2] = {"\"Morgenlicht.zip\" löschen?",
                                         "Die Datei wird von der SD-Karte gelöscht und kann nicht zurückgeholt werden."};
    ui_look(id, 0, 1, 0);
    for (int pass = 0; pass < 2; pass++)
        start_frame();
    ui_popup p;
    ui_popup_text(&p, paras, 2);
    frame_begin();
    ui_popup_draw(&p, "Abbrechen", "Löschen", -1, -1, 1);
    ui_end();
    int n = 0;
    for (int i = 0; i < recs && n + 3 <= most; i++)
        if (rec[i].kind == 'T')
            at[n++] = rec[i].x, at[n++] = rec[i].y, at[n++] = (float)p.para[0].px;
    return n;
}

static void popup_text_place(void)
{
    float stock[96], fake[96];
    int n = popup_texts(LOOK_STOCK, stock, 96), m = popup_texts(FAKE_ID, fake, 96), same = n == m && n > 0;
    for (int i = 0; same && i < n; i++)
        same = stock[i] == fake[i];
    char what[200];
    snprintf(what, sizeof what, "Q3: under a panel %d px taller the popup's %d texts stand where Stock's do, at its "
             "size (%s)", look_fake.m.dlg_h - look_stock.m.dlg_h, n / 3, same ? "yes" : "no");
    check(look_fake.m.dlg_h > look_stock.m.dlg_h && same, what);
}

/* The rows and columns of the screen where the image recorded as r is at least lim solid, near the help button:
 * b = left, top, right, bottom (exclusive). */
static void help_box(int i, float lim, float b[4])
{
    b[0] = b[1] = 1e9f, b[2] = b[3] = -1e9f;
    for (int y = 0; y < 70; y++)
        for (int x = 240; x < BOTTOM_W; x++)
            if (rec_alpha(&rec[i], x + 0.5f, y + 0.5f) >= lim)
                b[0] = fminf(b[0], x), b[1] = fminf(b[1], y), b[2] = fmaxf(b[2], x + 1), b[3] = fmaxf(b[3], y + 1);
}

/* The help button drawn (ui_help) in the look id: the record of its button and of its mark, -1 if not drawn. */
static void help_drawn(int id, int scheme, int down, int *button, int *mark)
{
    ui_look(id, scheme, 1, 0);
    start_frame();
    start_frame();
    frame_begin();
    ui_help(down);
    ui_end();
    *button = *mark = -1;
    for (int i = 0; i < recs; i++) {
        if (atlas_sprite(&rec[i]) == (down ? ART_HELP_DOWN : ART_HELP))
            *button = i;
        if (rec[i].kind == 'I' && rec[i].tex == ui_sprite_tex(ART_HELP_MARK, 0))
            *mark = i;
    }
}

/* G1-R1 Q1 (C12, Frost): on "Datei loeschen?" the help button's mark (where it is a quarter solid or more) stands
 * centred on the button's face, within half a pixel, with 2 px or more of the face round it on every side; resting
 * and held, in Weiss and Nacht. The face: the element HELP_FACE across less FROST_HELP_LIP below, a pill
 * (look_frost.py help_button); held, a pixel in at the sides and 2 down (a circle). */
static void help_mark_frost(void)
{
    if (!look_built(LOOK_FROST))
        return;
    for (int k = 0; k < 2; k++)
        for (int down = 0; down < 2; down++) {
            int button, mark;
            help_drawn(LOOK_FROST, k ? ART_SCHEME_NACHT : ART_SCHEME_WEISS, down, &button, &mark);
            char what[240];
            if (button < 0 || mark < 0) {
                check(0, "Q1 Frost: the help button and its mark drawn");
                continue;
            }
            const art_sprite *s = ui_sprite_info(down ? ART_HELP_DOWN : ART_HELP);
            float n = HELP_FACE, x0 = rec[button].x + s->pad[0], y0 = rec[button].y + s->pad[1];
            float fx0 = x0 + down, fy0 = y0 + 2 * down, fx1 = x0 + n - down, fy1 = y0 + n - (down ? 0 : FROST_HELP_LIP);
            float r = fminf(fx1 - fx0, fy1 - fy0) / 2, cx = (fx0 + fx1) / 2, cy = (fy0 + fy1) / 2;
            float half = (fx1 - fx0) / 2 - r;           /* the pill's straight part, each side of the middle */
            float m[4];
            help_box(mark, 0.25f, m);
            float room = 1e9f;
            for (int c = 0; c < 4; c++) {
                float dx = fmaxf(0, fabsf(m[c & 1 ? 2 : 0] - cx) - half), dy = fabsf(m[c & 2 ? 3 : 1] - cy);
                room = fminf(room, r - hypotf(dx, dy));
            }
            float off_x = (m[0] + m[2]) / 2 - cx, off_y = (m[1] + m[3]) / 2 - cy;
            snprintf(what, sizeof what, "Q1 Frost %s%s: the help mark (%g,%g to %g,%g) %g px inside the face (%g,%g to "
                     "%g,%g) at its nearest corner, its middle %g, %g px from the face's", k ? "Nacht" : "Weiss",
                     down ? ", held" : "", m[0], m[1], m[2], m[3], room, fx0, fy0, fx1, fy1, off_x, off_y);
            check(room >= 2 && fabsf(off_x) <= 0.5f && fabsf(off_y) <= 0.5f, what);
            if (down)
                continue;
            float b[4];
            help_box(button, 0.5f, b);
            snprintf(what, sizeof what, "Q1 Frost %s: the help button's lowest half-solid row ends at %g (ui_help_low "
                     "%g), the file's box %g px under it (6 in every look at bc61fe5)", k ? "Nacht" : "Weiss", b[3],
                     ui_help_low(), ui_help_low() + DONE_FACTS_AIR - b[3]);
            check(b[3] == ui_help_low() && DONE_FACTS_AIR >= 6, what);
        }
}

/* G1-R1 Q2 (C12, every look): on "Datei loeschen?" the file's box 4 px lower than at bc61fe5 (48 there in every
 * look; one more, as the help button is HELP_FACE in every look, Frost's size since Q1: G1-R2), at least 8 px
 * above Keep and Delete (CHOICE_Y, unmoved). The title is main.c's, one place for every look (test_design, Q2). */
static void done_box_lower(void)
{
    int on = fake_on;
    fake_on = 0;
    for (int id = 0; id < LOOK_COUNT; id++) {
        if (!look_built(id))
            continue;
        ui_look(id, ART_SCHEME_WEISS, 1, 0);
        start_frame();
        float y = ui_help_low() + DONE_FACTS_AIR, want = 48 + 4 + 1;
        float low = y + 2 * FACT_PAD_Y + FACT_ROW_H;
        char what[160];
        snprintf(what, sizeof what, "Q2 %s: the file's box at y %g (%g), %g px above Keep and Delete", ui_lk->name, y, want,
                 CHOICE_Y - low);
        check(y == want && CHOICE_Y - low >= 8, what);
    }
    fake_on = on;
}

/* G1-R1 Q5 (C16, every look): on "Datei loeschen?" the distance from the help button's lowest drawn row (alpha 0.5
 * or more: its body; the soft shadow or glow under it, alpha 0.03 at y 45 to 50 by look and scheme at bc61fe5, is
 * not counted) to the file's box is one number in every look and scheme, DONE_FACTS_AIR. And (G1-R3, the review of
 * the join line) the glow itself stays HELP_CLEAR rows or more above the box: from its first drawn pixel (alpha
 * 1 / 255) in every look and scheme (at a6b4e06 Nachtglas's reached the box's first row). */
#define HELP_CLEAR 3

static void help_box_gap(void)
{
    int on = fake_on, same = 1, first = -1, clear = 1;
    fake_on = 0;
    char what[400] = "Q5: the help button's lower edge to the file's box (the rows clear under its glow):";
    for (int id = 0; id < LOOK_COUNT; id++)
        for (int k = 0; k < 2 && look_built(id); k++) {
            int button, mark;
            help_drawn(id, k ? ART_SCHEME_NACHT : ART_SCHEME_WEISS, 0, &button, &mark);
            float b[4] = {0, 0, 0, 0}, g[4] = {0, 0, 0, 0};
            if (button >= 0)
                help_box(button, 0.5f, b), help_box(button, 1 / 255.0f, g);
            int gap = button < 0 ? -1 : (int)(ui_help_low() + DONE_FACTS_AIR - b[3]);
            int rows = button < 0 ? -1 : (int)(ui_help_low() + DONE_FACTS_AIR - g[3]);
            first = first < 0 ? gap : first;
            same &= gap == first && gap == DONE_FACTS_AIR;
            clear &= rows >= HELP_CLEAR;
            snprintf(what + strlen(what), sizeof what - strlen(what), " %s %s %d (%d clear)", ui_lk->name,
                     k ? "Nacht" : "Weiss", gap, rows);
        }
    check(same && clear, what);
    fake_on = on;
}

/* G1-R2 part 3 (C16, Q6): the help button one size in every look, HELP_FACE (its element centred on HELP_X and
 * HELP_Y's HELP_SIZE), so the file's box on "Datei loeschen?" stands at one place in every look; the fields already
 * one (the wrench's x on the settings tab, the panes' rims) as Stock's. And in the looks whose face is the element
 * (Stock's and Nachtglas's disc, Prisma's eight-sided stone, its corners cut HELP_CUT; Frost's pill above its lip:
 * help_mark_frost) the mark (where it is a quarter solid or more) with 2 px or more of the face round it, resting
 * and held (the face a pixel in on every side), in Weiss and Nacht; resting centred on the face within half a pixel,
 * held help_mark_down lower. The face by its shape, not its alpha: Nachtglas's glass is clear in the middle. */
#define HELP_CUT 9      /* look_prism.py help_button's cut */
static void help_one_size(void)
{
    int on = fake_on;
    fake_on = 0;
    char what[300];
    float box = -1, cx = HELP_X + HELP_SIZE / 2.0f, cy = HELP_Y + HELP_SIZE / 2.0f;
    for (int id = 0; id < LOOK_COUNT; id++) {
        if (!look_built(id))
            continue;
        const look_metrics *m = &look_get(id)->m, *s = &look_stock.m;
        ui_look(id, ART_SCHEME_WEISS, 1, 0);
        start_frame();
        float y = ui_help_low() + DONE_FACTS_AIR;
        box = box < 0 ? y : box;
        snprintf(what, sizeof what, "Q6 %s: the help button %d px (HELP_FACE %d), the file's box at y %g (Stock's %g); "
                 "the wrench's x %d, the panes' rims %d, %d (Stock's %d, %d, %d)", ui_lk->name, m->help_size, HELP_FACE, y,
                 box, m->tab_icon_x, m->pane_rim_big, m->pane_rim_small, s->tab_icon_x, s->pane_rim_big,
                 s->pane_rim_small);
        check(m->help_size == HELP_FACE && y == box && m->tab_icon_x == s->tab_icon_x &&
              m->pane_rim_big == s->pane_rim_big && m->pane_rim_small == s->pane_rim_small, what);
        if (id == LOOK_FROST)
            continue;
        for (int k = 0; k < 2; k++)
            for (int down = 0; down < 2; down++) {
                int button, mark;
                help_drawn(id, k ? ART_SCHEME_NACHT : ART_SCHEME_WEISS, down, &button, &mark);
                if (button < 0 || mark < 0) {
                    check(0, "Q6: the help button and its mark drawn");
                    continue;
                }
                float h = HELP_FACE / 2.0f - down, q[4], room = 1e9f;
                help_box(mark, 0.25f, q);
                for (int py = (int)q[1]; py < (int)q[3]; py++)
                    for (int px = (int)q[0]; px < (int)q[2]; px++) {
                        if (rec_alpha(&rec[mark], px + 0.5f, py + 0.5f) < 0.25f)
                            continue;
                        float dx = fmaxf(fabsf(px - cx), fabsf(px + 1 - cx));
                        float dy = fmaxf(fabsf(py - cy), fabsf(py + 1 - cy));
                        float in = h - hypotf(dx, dy);
                        if (id == LOOK_PRISM)
                            in = fminf(fminf(h - dx, h - dy), (2 * h - HELP_CUT - dx - dy) / sqrtf(2));
                        room = fminf(room, in);
                    }
                float off_x = (q[0] + q[2]) / 2 - cx, off_y = (q[1] + q[3]) / 2 - cy - (down ? m->help_mark_down : 0);
                snprintf(what, sizeof what, "Q6 %s %s%s: the help mark (%g,%g to %g,%g) with %g px of the face (%g "
                         "across, middle %g, %g) round it, its middle %g, %g px from the face's%s", ui_lk->name,
                         k ? "Nacht" : "Weiss", down ? ", held" : "", q[0], q[1], q[2], q[3], room, 2 * h, cx, cy,
                         off_x, off_y, down ? " less help_mark_down" : "");
                check(room >= 2 && fabsf(off_x) <= 0.5f && fabsf(off_y) <= 0.5f, what);
            }
    }
    fake_on = on;
}

static uint32_t fnv(uint32_t h, const void *p, size_t n)
{
    for (size_t i = 0; i < n; i++)
        h = (h ^ ((const uint8_t *)p)[i]) * 16777619u;
    return h;
}

/* G1-R1 Q3 (C14, Prisma): its loader without the spark. At 100 moments of a round, solid and half faded, in Weiss and
 * Nacht, every image it draws is the crystal's (its stone or a lit facet; the spark was an image of its own), and
 * those images (sprite, place, size, tint) are the ones bc61fe5 drew besides the spark: their hash then was
 * PRISM_LOADER_HASH (each sprite counted from the crystal's first, so that another look's sprites before it do not
 * move it; the same at 74e7ba2, whose absolute ids still gave bc61fe5's 1458712485). */
#define PRISM_LOADER_HASH 4027720805u
static void prism_loader_no_spark(void)
{
    if (!look_built(LOOK_PRISM))
        return;
    int on = fake_on, others = 0, images = 0;
    fake_on = 0;
    uint32_t h = 2166136261u;
    for (int k = 0; k < 2; k++) {
        ui_look(LOOK_PRISM, k ? ART_SCHEME_NACHT : ART_SCHEME_WEISS, 1, 0);
        start_frame();
        start_frame();
        for (int half = 0; half < 2; half++)
            for (int i = 0; i < 100; i++) {
                frame_begin();
                int before = recs;
                ui_lk->draw.loader(BOTTOM_W / 2.0f, SCREEN_H / 2.0f, (u64)LOADER_MS * (7 + i / 100.0), half ? 0.5f : 1.0f);
                ui_flush();
                for (int r = before; r < recs; r++) {
                    if (rec[r].kind != 'I')
                        continue;
                    int id = atlas_sprite(&rec[r]);
                    if (id < ART_PRISM_LOADER_STONE || id > ART_PRISM_LOADER_LIT_7) {
                        others++;
                        continue;
                    }
                    float at[4] = {rec[r].x, rec[r].y, rec[r].w, rec[r].h};
                    images++;
                    int stone = id - ART_PRISM_LOADER_STONE;      /* the crystal's images, whatever comes before */
                    h = fnv(h, &stone, sizeof stone);
                    h = fnv(h, at, sizeof at);
                    h = fnv(h, &rec[r].tinted, sizeof rec[r].tinted);
                    if (rec[r].tinted)
                        h = fnv(h, &rec[r].tint, sizeof rec[r].tint);
                }
                ui_end();
            }
    }
    char what[200];
    snprintf(what, sizeof what, "Q3 Prisma: its loader at 400 moments draws %d images of the crystal (hash %u, bc61fe5's "
             "%u) and %d others (the spark)", images, h, PRISM_LOADER_HASH, others);
    check(others == 0 && images > 0 && h == PRISM_LOADER_HASH, what);
    fake_on = on;
}

/* The alpha (0 to 255) of the pixel x, y of the texture the sprite id of a look lies in (art_tex_of), in a scheme,
 * inflated (the texture's tiled order). */
static int atlas_alpha(const look_art *a, int scheme, int id, int x, int y)
{
    const art_tex t = art_tex_of(a, id, scheme);
    const uint8_t *px = t.z ? inflated(t.z, t.z_size, (uLongf)t.w * t.h * 4) : NULL;
    if (!px)
        return -1;
    uint32_t texel;
    memcpy(&texel, px + 4 * (((y >> 3) * (t.w >> 3) + (x >> 3)) * 64 + ui_morton(x & 7, y & 7)), 4);
    return texel & 255;
}

/* G1-R1 Q4 (C15, Prisma): the popup's panel has four equal corners, each the top left's mirrored: in its image (DLG),
 * in Weiss and Nacht, the square of PRISM_PLATE_CUT + 2 px at each corner of the element. Left and right are
 * compared pixel by pixel; the lower corners against the upper ones where the plate covers (alpha 255 above), as its
 * shadow lies below it. And the top left square is the cut form of bc61fe5 (before the four were made one): its
 * alpha row by row, hashed FNV-1a, is PRISM_CORNER_HASH (taken from bc61fe5's art.py, Weiss and Nacht), so four
 * corners equally wrong do not pass. */
static const uint32_t PRISM_CORNER_HASH[2] = {2831414394u, 150395790u};

static void prism_popup_corners(void)
{
    if (!look_built(LOOK_PRISM))
        return;
    const look_art *a = look_get(LOOK_PRISM)->art;
    const art_sprite *d = art_of(a, ART_DLG);
    int c = PRISM_PLATE_CUT + 2, x0 = d->x + d->pad[0], y0 = d->y + d->pad[1];
    int x1 = d->x + d->w - d->pad[2] - 1, y1 = d->y + d->h - d->pad[3] - 1;
    const int schemes[2] = {ART_SCHEME_WEISS, ART_SCHEME_NACHT};
    for (int k = 0; k < 2; k++) {
        int sides = 0, low = 0, top_cover = 0, low_cover = 0;
        uint32_t h = 2166136261u;
        for (int j = 0; j < c; j++)
            for (int i = 0; i < c; i++) {
                int tl = atlas_alpha(a, schemes[k], ART_DLG, x0 + i, y0 + j),
                    tr = atlas_alpha(a, schemes[k], ART_DLG, x1 - i, y0 + j);
                h = (h ^ (uint32_t)tl) * 16777619u;
                int bl = atlas_alpha(a, schemes[k], ART_DLG, x0 + i, y1 - j),
                    br = atlas_alpha(a, schemes[k], ART_DLG, x1 - i, y1 - j);
                sides += (tl != tr) + (bl != br);
                top_cover += tl == 255;
                low_cover += bl == 255;
                low += (tl == 255) != (bl == 255);
            }
        char what[260];
        snprintf(what, sizeof what, "Q4 Prisma %s: the popup's corners (%d px squares): %d pixels differ left to right, "
                 "covered %d above and %d below, %d covered pixels differ; the top left's hash %u (bc61fe5's %u)",
                 k ? "Nacht" : "Weiss", c, sides, top_cover, low_cover, low, h, PRISM_CORNER_HASH[k]);
        check(sides == 0 && low == 0 && h == PRISM_CORNER_HASH[k], what);
    }
}

/* The first row (from the inner shape's top; the same count from its floor) in which column c of the installing
 * screen's inner shape lies wholly inside it: the shape's corners cut off at 45 degrees with legs of leg px (cut), or
 * rounded by POUR_INNER_R. By the pixel's corner nearest the shape's corner, not by pour_ui.h's rows. */
static int inner_first(int c, int cut, float leg)
{
    int u = c < POUR_INNER_W - 1 - c ? c : POUR_INNER_W - 1 - c, y = 0;
    const float r = POUR_INNER_R;
    while (cut ? u + y < leg : u < r && y < r && (r - u) * (r - u) + (r - y) * (r - y) > r * r)
        y++;
    return y;
}

/* The liquid's surface as a look draws it at eleven levels from empty to full: every piece of POUR_SURFACE recorded,
 * column by column. *wrong counts the columns whose strip does not start in the surface's own row or, where that row
 * lies outside the shape, in the column's first row inside it, or ends past the column's last row inside it;
 * returns the strips' pixels. */
static int surface_pixels(int id, int cut, float leg, int *wrong)
{
    int pixels = 0;
    ui_look(id, ART_SCHEME_WEISS, 1, 0);
    for (int k = 0; k <= 10; k++) {
        pour p;
        float h[POUR_COLS];
        pour_settle(&p, 0, k / 10.0f, POUR_WORKING, POUR_SETTLE_MS);
        pour_heights(&p, h);
        pour_draw(&p, 0);
        for (int i = 0; i < recs; i++) {
            float x0, y0, x1, y1;
            if (atlas_sprite(&rec[i]) != ART_POUR_SURFACE)
                continue;
            rec_box(&rec[i], &x0, &y0, &x1, &y1);
            for (int x = (int)roundf(x0); x < (int)roundf(x1); x++) {
                int c = x - POUR_INNER_X, first = inner_first(c, cut, leg);
                int row = (int)floorf(POUR_INNER_Y + POUR_INNER_H - fmaxf(h[c], 0)), top = POUR_INNER_Y + first;
                *wrong += (int)roundf(y0) != (row > top ? row : top) ||
                          (int)roundf(y1) > POUR_INNER_Y + POUR_INNER_H - first;
                pixels += (int)roundf(y1) - (int)roundf(y0);
            }
        }
    }
    return pixels;
}

/* G2-M3 (C48, the canvas's Prisma a "geschnittene Ecken"): Prisma's tile in its own material, cut crystal, drawn by
 * the shared code. (1) Every image of the installing screen has Stock's size, reach past its element and fixed edges
 * (pour_ui.h places them alike in every look). (2) The corners are cut POUR_PRISM_CUT px at 45 degrees inside the
 * same box: in every scheme the front glass covers the pixel on the box's edge 12 px from each corner, both ways
 * (inside the cut; Stock's rounded tile is clear there), and is clear 4 px in from each corner on the diagonal
 * (outside the cut), the end's rim of light covers the same pixels on the edge (a glow, not held clear outside);
 * the liquid's image and its end light fill the inner shape, POUR_RIM inside that outline, alike
 * (10 px from its corners on its edge, where Stock's liquid is clear; clear 3 px in on the diagonal). (3) The
 * liquid's surface follows the cut (pour_ui.h, inner_span): at eleven levels every strip of POUR_SURFACE drawn in
 * Prisma starts in its surface's row or in its column's first row inside the cut shape and ends inside it. (4) Behind
 * the look's boundary: Stock's strips, drawn in the same run before and after Prisma's, keep to the rounded shape
 * both times, as many pixels each time and fewer than Prisma's (the cut shape holds the rounded one). (5) The cut is
 * the application's by the look's number (pour_ui.h, ui_f.look), not the look's: the test-only look, standing under
 * Prisma's number with Stock's images, draws Prisma's strips in the cut shape. */
static void prism_tile_material(void)
{
    if (!look_built(LOOK_PRISM))
        return;
    const look_art *pr = look_get(LOOK_PRISM)->art, *st = look_get(LOOK_STOCK)->art;
    int same = 0;
    for (int id = ART_POUR_HALO; id <= ART_POUR_TITLE_DONE; id++) {
        const art_sprite *a = art_of(pr, id), *b = art_of(st, id);
        same += a->w && a->w == b->w && a->h == b->h && !memcmp(a->pad, b->pad, 4) && !memcmp(a->cap, b->cap, 4);
    }
    char what[300];
    snprintf(what, sizeof what, "G2-M3 Prisma's installing-screen images: %d of %d with Stock's size, reach and fixed "
             "edges", same, ART_POUR_TITLE_DONE - ART_POUR_HALO + 1);
    check(same == ART_POUR_TITLE_DONE - ART_POUR_HALO + 1, what);

    /* an image's pixels by its element (box w x h from ex, ey in the image): far px from each corner along both
     * edges (inside the cut), in px from each corner on the diagonal (outside it) */
    const struct { int id, w, h, far, in, least; } image[4] = {
        {ART_POUR_FRONT, POUR_TILE_W, POUR_TILE_H, 12, 4, 64},
        {ART_POUR_LIQUID, POUR_INNER_W, POUR_INNER_H, 10, 3, 200},
        {ART_POUR_LIT, POUR_INNER_W, POUR_INNER_H, 10, 3, 32},
        {ART_POUR_LIT_RIM, POUR_TILE_W, POUR_TILE_H, 12, 4, 64}};
    int least[4] = {255, 255, 255, 255}, most_out = 0, stock_most = 0;
    for (int s = 0; s < ART_SCHEMES; s++)
        for (int k = 0; k < 4; k++) {
            const art_sprite *a = art_of(pr, image[k].id), *b = art_of(st, image[k].id);
            const int w = image[k].w, h = image[k].h, far = image[k].far, in = image[k].in;
            for (int q = 0; q < 4; q++) {
                const int ex = q & 1 ? w - 1 : 0, ey = q & 2 ? h - 1 : 0, dx = q & 1 ? -1 : 1, dy = q & 2 ? -1 : 1;
                const int at[3][2] = {{ex + dx * far, ey}, {ex, ey + dy * far}, {ex + dx * in, ey + dy * in}};
                for (int j = 0; j < 3; j++) {
                    int own = atlas_alpha(pr, s, image[k].id, a->x + a->pad[0] + at[j][0], a->y + a->pad[1] + at[j][1]);
                    int stock = atlas_alpha(st, s, image[k].id, b->x + b->pad[0] + at[j][0],
                                            b->y + b->pad[1] + at[j][1]);
                    if (j == 2)         /* the rim of light glows past the outline in both looks: its least only */
                        most_out = k < 3 && own > most_out ? own : most_out;
                    else {
                        least[k] = own < least[k] ? own : least[k];
                        stock_most = k < 3 && stock > stock_most ? stock : stock_most;
                    }
                }
            }
        }
    snprintf(what, sizeof what, "G2-M3 Prisma's cut corners in eight schemes: beside the cuts' ends the front glass "
             "has an alpha of %d or more, the liquid %d, its end light %d (Stock's rounded images there at most %d), "
             "the end's rim of light %d; outside the cuts the three at most %d", least[0], least[1], least[2],
             stock_most, least[3], most_out);
    check(least[0] >= image[0].least && least[1] >= image[1].least && least[2] >= image[2].least &&
          least[3] >= image[3].least && stock_most < 8 && most_out == 0, what);

    const float leg = POUR_PRISM_CUT - 0.5857864f * POUR_RIM;   /* the outline's legs less (2 - sqrt 2) * POUR_RIM */
    int on = fake_on;
    fake_on = 0;
    int wrong = 0, wrong_stock = 0, before = surface_pixels(LOOK_STOCK, 0, 0, &wrong_stock);
    int cut = surface_pixels(LOOK_PRISM, 1, leg, &wrong), round = surface_pixels(LOOK_STOCK, 0, 0, &wrong_stock);
    snprintf(what, sizeof what, "G2-M3 the liquid's surface at eleven levels in Prisma: %d pixels of its strips, %d "
             "columns not starting and ending by the cut inner shape", cut, wrong);
    check(cut > 0 && wrong == 0, what);
    snprintf(what, sizeof what, "G2-M3 the liquid's surface at eleven levels in Stock, drawn before and after "
             "Prisma: %d and %d pixels of its strips (fewer than Prisma's %d), %d columns not starting and ending by "
             "the rounded inner shape", before, round, cut, wrong_stock);
    check(round > 0 && before == round && round < cut && wrong_stock == 0, what);
    int wrong_fake = 0;
    fake_on = 1;
    int fake = surface_pixels(FAKE_ID, 1, leg, &wrong_fake);
    snprintf(what, sizeof what, "G2-M3b the liquid's surface at eleven levels in the test-only look under Prisma's "
             "number (%s drawn): %d pixels of its strips (Prisma's %d), %d columns not starting and ending by the "
             "cut inner shape", ui_lk->name, fake, cut, wrong_fake);
    check(ui_lk == &look_fake && fake == cut && wrong_fake == 0, what);
    fake_on = on;
}

/* a sprite's pixel x, y (from its own top left) in the texture it lies in (art_tex_of): straight r, g, b, a in out;
 * 0 when it cannot be read */
static int sprite_px(const look_art *a, int scheme, int id, int x, int y, int out[4])
{
    const art_sprite *s = art_of(a, id);
    const art_tex tex = art_tex_of(a, id, scheme);
    const uint8_t *px = tex.z ? inflated(tex.z, tex.z_size, (uLongf)tex.w * tex.h * 4) : NULL;
    if (!px || !s->w || x < 0 || y < 0 || x >= s->w || y >= s->h)
        return 0;
    int ax = s->x + x, ay = s->y + y;
    const uint8_t *t = px + 4 * (((ay >> 3) * (tex.w >> 3) + (ax >> 3)) * 64 + ui_morton(ax & 7, ay & 7));
    out[0] = t[3], out[1] = t[2], out[2] = t[1], out[3] = t[0];
    return 1;
}

static double rel_lum(const double c[3])
{
    double y = 0;
    const double k[3] = {0.2126, 0.7152, 0.0722};
    for (int i = 0; i < 3; i++) {
        double v = c[i] / 255.0;
        y += k[i] * (v <= 0.04045 ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4));
    }
    return y;
}

/* what lies at a sprite's pixel seen through POUR_FRONT's pixel over it, against that front over white: the contrast
 * (WCAG's ratio) */
static double through_front(const int front[4], const int under[4])
{
    double a = front[3] / 255.0, seen[3], ground[3];
    for (int i = 0; i < 3; i++) {
        seen[i] = front[i] * a + under[i] * (1 - a);
        ground[i] = front[i] * a + 255 * (1 - a);
    }
    return (rel_lum(ground) + 0.05) / (rel_lum(seen) + 0.05);
}

/* G2-M1 (C48, the canvas's Frost a "ganz mattiert"): Frost's tile in its own material, drawn by the shared code.
 * (1) Every image of the installing screen has Stock's size, reach past its element and fixed edges (pour_ui.h places
 * them alike in every look). (2) Frosted all over: in every scheme POUR_FRONT covers the face inside the polished edge
 * and off the picture, at its top, its foot and both sides (9 px under the top and over the foot, 8 px in from the
 * sides, on the middle lines), with an alpha of at least 24; Stock's clear glass has under 16 at the foot. (3) The
 * canvas's weak point, in the light schemes: the falling piece's middle (POUR_PIECE_TURN's first frame) seen through
 * the frost where it passes, 12 px under the tile's top, stands against the frost over white with a contrast of 2.5
 * or more (the canvas's material gave 2.0), the liquid's foot (6 px over the inner shape's floor) with 1.5 or more
 * (1.3). (4) Nothing of the liquid's images outside the inner shape: their corner pixels clear. */
static void frost_tile_material(void)
{
    if (!look_built(LOOK_FROST))
        return;
    const look_art *f = look_get(LOOK_FROST)->art, *st = look_get(LOOK_STOCK)->art;
    int same = 0;
    for (int id = ART_POUR_HALO; id <= ART_POUR_TITLE_DONE; id++) {
        const art_sprite *a = art_of(f, id), *b = art_of(st, id);
        same += a->w && a->w == b->w && a->h == b->h && !memcmp(a->pad, b->pad, 4) && !memcmp(a->cap, b->cap, 4);
    }
    char what[300];
    snprintf(what, sizeof what, "G2-M1 Frost's installing-screen images: %d of %d with Stock's size, reach and fixed "
             "edges", same, ART_POUR_TITLE_DONE - ART_POUR_HALO + 1);
    check(same == ART_POUR_TITLE_DONE - ART_POUR_HALO + 1, what);

    const int m = art_of(f, ART_POUR_FRONT)->pad[0], tw = POUR_TILE_W, th = POUR_TILE_H;
    const int at[4][2] = {{tw / 2, 9}, {tw / 2, th - 9}, {8, th / 2}, {tw - 9, th / 2}};
    int least = 255, stock_foot = 255, read = 1, clear = 1;
    double piece = 99, pool = 99;
    for (int s = 0; s < ART_SCHEMES; s++) {
        int c[4], fr[4], under[4];
        for (int k = 0; k < 4; k++) {
            read &= sprite_px(f, s, ART_POUR_FRONT, m + at[k][0], m + at[k][1], c);
            least = c[3] < least ? c[3] : least;
        }
        read &= sprite_px(st, s, ART_POUR_FRONT, m + at[1][0], m + at[1][1], c);
        stock_foot = s == ART_SCHEME_WEISS ? c[3] : stock_foot;
        const int liquid[2] = {ART_POUR_LIQUID, ART_POUR_LIT};
        for (int k = 0; k < 2; k++) {
            const art_sprite *l = art_of(f, liquid[k]);
            for (int q = 0; q < 4; q++) {
                read &= sprite_px(f, s, liquid[k], q & 1 ? l->w - 1 : 0, q & 2 ? l->h - 1 : 0, c);
                clear &= c[3] == 0;
            }
        }
        if (s == ART_SCHEME_NACHT)
            continue;
        read &= sprite_px(f, s, ART_POUR_FRONT, m + tw / 2, m + 12, fr) &&
                sprite_px(f, s, ART_POUR_PIECE_TURN, POUR_PIECE_W / 2, POUR_PIECE_H / 2 - 1, under);
        double r = through_front(fr, under);
        piece = r < piece ? r : piece;
        read &= sprite_px(f, s, ART_POUR_FRONT, m + POUR_RIM + tw / 2 - POUR_RIM, m + th - POUR_RIM - 6, fr) &&
                sprite_px(f, s, ART_POUR_LIQUID, tw / 2 - POUR_RIM, th - 2 * POUR_RIM - 6, under);
        r = through_front(fr, under);
        pool = r < pool ? r : pool;
    }
    snprintf(what, sizeof what, "G2-M1 Frost's frost over the whole face: the least alpha at its top, foot and sides "
             "in eight schemes %d (Stock's at the foot in Weiss %d)", least, stock_foot);
    check(read && least >= 24 && stock_foot < 16, what);
    snprintf(what, sizeof what, "G2-M1 through Frost's frost in the light schemes: the falling piece's contrast %.2f "
             "(2.5 or more), the liquid's foot %.2f (1.5 or more)", piece, pool);
    check(read && piece >= 2.5 && pool >= 1.5, what);
    check(read && clear, "G2-M1 Frost's liquid and its end light: their corner pixels clear (inside the inner shape)");
}

/* The middle of what of the image recorded as r is at least nine tenths opaque, on the screen, counting only its pixels
 * whose middle lies within the eighth of a turn round cx, cy that holds direction a (any, when a < -10); 0 when none. */
static int opaque_middle(const typeof(rec[0]) *r, float cx, float cy, float a, float *mx, float *my)
{
    float sx = 0, sy = 0;
    int n = 0;
    for (int py = (int)floorf(r->y); py < r->y + r->h; py++)
        for (int px = (int)floorf(r->x); px < r->x + r->w; px++) {
            float x = px + 0.5f, y = py + 0.5f;
            if (a > -10) {
                float d = remainderf(atan2f(y - cy, x - cx) - a, TAU);
                if (fabsf(d) > TAU / 16)
                    continue;
            }
            if (rec_alpha(r, x, y) >= 0.9f)
                sx += x, sy += y, n++;
        }
    if (n)
        *mx = sx / n, *my = sy / n;
    return n;
}

/* C20 (Frost, "Mattglas-Plättchen"): its loader is eight small panes set like a dial, one lit at a time going round
 * as Stock's beads do. At eight moments of each of two rounds (k / 8 of LOADER_MS on), Weiss and Nacht: the dial at
 * rest drawn once, in LOADER_BOX round the loader's middle, holding a pane at each of the dial's places (the middle of
 * its opaque pixels in each eighth of the turn round the middle within DIAL_NEAR px of the place: FROST_LOADER_DIAL_R2
 * / 2 px from the middle, from the top, clockwise; its lip below it moves that middle down a little); every lit image
 * drawn at its pane's place (its opaque pixels' middle within DIAL_NEAR / 2 px of it); exactly one fully lit (its
 * tint's alpha 0.99 or more), pane k: the light goes round once a round. */
#define DIAL_NEAR 0.75f

static void frost_loader_dial(void)
{
    if (!look_built(LOOK_FROST))
        return;
    int on = fake_on, rests = 1, panes = 1, lit = 1, round = 1, moments = 0;
    fake_on = 0;
    float cx = BOTTOM_W / 2.0f, cy = SCREEN_H / 2.0f, r = FROST_LOADER_DIAL_R2 / 2.0f;
    for (int s = 0; s < 2; s++) {
        ui_look(LOOK_FROST, s ? ART_SCHEME_NACHT : ART_SCHEME_WEISS, 1, 0);
        start_frame();
        start_frame();
        for (int n = 0; n < 2; n++)
            for (int k = 0; k < LOADER_DOTS; k++) {
                frame_begin();
                int before = recs, dials = 0, full = 0, which = -1;
                ui_lk->draw.loader(cx, cy, (u64)LOADER_MS * (7 + n) + (u64)(k * LOADER_MS) / LOADER_DOTS, 1.0f);
                ui_flush();
                for (int q = before; q < recs; q++) {
                    if (rec[q].kind != 'I')
                        continue;
                    int id = atlas_sprite(&rec[q]);
                    float mx, my;
                    if (id == ART_FROST_LOADER_DIAL) {
                        dials++;
                        rests &= rec[q].x == roundf(cx - LOADER_BOX / 2.0f) - ui_sprite_info(id)->pad[0] &&
                                 rec[q].y == roundf(cy - LOADER_BOX / 2.0f) - ui_sprite_info(id)->pad[1];
                        for (int i = 0; n == 0 && k == 0 && i < LOADER_DOTS; i++) {
                            float a = TAU * i / LOADER_DOTS - TAU / 4;
                            panes &= opaque_middle(&rec[q], cx, cy, a, &mx, &my) > 0 &&
                                     hypotf(mx - cx - r * cosf(a), my - cy - r * sinf(a)) <= DIAL_NEAR;
                        }
                    } else if (id >= ART_FROST_LOADER_LIT_0 && id <= ART_FROST_LOADER_LIT_7) {
                        int i = id - ART_FROST_LOADER_LIT_0;
                        float a = TAU * i / LOADER_DOTS - TAU / 4, c[4];
                        lit &= opaque_middle(&rec[q], 0, 0, -100, &mx, &my) > 0 &&
                               hypotf(mx - cx - r * cosf(a), my - cy - r * sinf(a)) <= DIAL_NEAR / 2;
                        unpack_color(rec[q].tint.corners[0].color, c);
                        if (rec[q].tinted && c[3] >= 0.99f)
                            full++, which = i;
                    } else {
                        rests = 0;
                    }
                }
                rests &= dials == 1;
                round &= full == 1 && which == k;
                moments++;
                ui_end();
            }
    }
    char what[220];
    snprintf(what, sizeof what, "C20 Frost's loader at %d moments: the dial at rest once in its box (%s), a pane at each "
             "of its eight places (%s), each lit image at its pane (%s), exactly one fully lit, going round once a "
             "round (%s)", moments, rests ? "yes" : "no", panes ? "yes" : "no", lit ? "yes" : "no", round ? "yes" : "no");
    check(rests && panes && lit && round, what);
    fake_on = on;
}

/* C32 (Nachtglas, "Klares Glas"): its loader is a tube of clear glass with the light inside it. Drawn alone at a
 * clock ms and alpha in LOADER_BOX round cx, cy: the images it draws (their records' places in at, at most 8), each
 * one's sprite in ids, and through the spy its turned images and the last angle. */
static int night_loader_draws(float cx, float cy, u64 ms, float alpha, int ids[8], int at[8])
{
    frame_begin();
    int before = recs, n = 0;
    turns = 0, turned_id = -1;
    ui_lk->draw.loader(cx, cy, ms, alpha);
    ui_flush();
    for (int q = before; q < recs; q++)
        if (rec[q].kind == 'I') {
            if (n < 8)
                ids[n] = atlas_sprite(&rec[q]), at[n] = q;
            n++;
        }
    return n;
}

/* N1-N3: at eight moments of each of two rounds (k / 8 of LOADER_MS on), Weiss and Nacht, solid and half faded:
 * N1 exactly three images, in the order body, light, front: the body and the front from the atlas at the box's corner
 * (less their pad), the light the one image turned (read smoothed, its middle at the box's), each as solid as the
 * loader; N2 the light's angle is k / 8 of a turn (within a millisecond's), the same for the same clock, and from
 * clocks that jump far ahead or wrap (0, a round less one, 10^12 ms on, the largest clock and half a round before it)
 * between 0 and a whole turn, as that clock's part of its round; N3 the light lies in the glass: the front is drawn
 * after it and covers its square, and no texel of it lies outside the tube (LOADER_BEAD / 2 px round the ring). */
#define NG_TURN_NEAR (TAU / LOADER_MS)

static void night_loader_glass(void)
{
    if (!look_built(LOOK_NIGHT))
        return;
    int on = fake_on, three = 1, order = 1, placed = 1, solid = 1, angle = 1, same = 1, wraps = 1, inside = 1,
        moments = 0;
    fake_on = 0;
    float cx = BOTTOM_W / 2.0f, cy = SCREEN_H / 2.0f + 0.4f;
    float bx = roundf(cx - LOADER_BOX / 2.0f), by = roundf(cy - LOADER_BOX / 2.0f);
    int ids[8], at[8];
    for (int s = 0; s < 4; s++) {
        float alpha = s & 1 ? 0.5f : 1.0f;
        ui_look(LOOK_NIGHT, s < 2 ? ART_SCHEME_WEISS : ART_SCHEME_NACHT, 1, 0);
        start_frame();
        start_frame();
        for (int n = 0; n < 2; n++)
            for (int k = 0; k < LOADER_DOTS; k++) {
                u64 ms = (u64)LOADER_MS * (7 + n) + (u64)(k * LOADER_MS) / LOADER_DOTS;
                int got = night_loader_draws(cx, cy, ms, alpha, ids, at);
                float first = turned_angle;
                moments++;
                three &= got == 3 && turns == 1 && turned_id == ART_NG_LOADER_LIGHT;
                if (got != 3) {
                    order = placed = solid = inside = 0;
                    ui_end();
                    continue;
                }
                order &= ids[0] == ART_NG_LOADER_BODY && ids[1] == ART_NG_LOADER_LIGHT && ids[2] == ART_NG_LOADER_FRONT;
                const typeof(rec[0]) *body = &rec[at[0]], *light = &rec[at[1]], *front = &rec[at[2]];
                for (int i = 0; i < 3; i += 2) {
                    const art_sprite *a = ui_sprite_info(ids[i]);
                    placed &= rec[at[i]].tex == &atlas && a && rec[at[i]].x == bx - a->pad[0] &&
                              rec[at[i]].y == by - a->pad[1];
                }
                placed &= light->tex == &atlas_smooth && light->x == bx + LOADER_BOX / 2.0f &&
                          light->y == by + LOADER_BOX / 2.0f;
                for (int i = 0; i < 3; i++) {
                    float c[4];
                    unpack_color(rec[at[i]].tint.corners[0].color, c);
                    solid &= alpha < 1 ? rec[at[i]].tinted && fabsf(c[3] - alpha) < 0.01f : !rec[at[i]].tinted;
                }
                float lx = light->x - light->w / 2.0f, ly = light->y - light->h / 2.0f;
                inside &= at[2] > at[1] && front->x <= lx && front->y <= ly && front->x + front->w >= lx + light->w &&
                          front->y + front->h >= ly + light->h && body->x <= lx;
                typeof(rec[0]) flat = *light;       /* unturned: the tube is round, so turning keeps it inside */
                flat.x = lx, flat.y = ly, flat.cx = flat.cy = flat.angle = 0;
                for (int py = 0; s == 0 && n == 0 && py < (int)light->h; py++)
                    for (int px = 0; px < (int)light->w; px++) {
                        float d = hypotf(px + 0.5f - light->w / 2.0f, py + 0.5f - light->h / 2.0f);
                        if (rec_alpha(&flat, lx + px + 0.5f, ly + py + 0.5f) > 1 / 255.0f)
                            inside &= fabsf(d - LOADER_BEAD_R) <= LOADER_BEAD / 2.0f;
                    }
                ui_end();
                angle &= fabsf(first - TAU * k / LOADER_DOTS) <= NG_TURN_NEAR;
                night_loader_draws(cx, cy, ms, alpha, ids, at);
                same &= turns == 1 && turned_angle == first;
                ui_end();
            }
    }
    const u64 clocks[5] = {0, LOADER_MS - 1, 1000000000000ull + LOADER_MS / 2, UINT64_MAX, UINT64_MAX - LOADER_MS / 2};
    for (int i = 0; i < 5; i++) {
        night_loader_draws(cx, cy, clocks[i], 1.0f, ids, at);
        float want = TAU * (float)(clocks[i] % LOADER_MS) / LOADER_MS;
        wraps &= turns == 1 && isfinite(turned_angle) && turned_angle >= 0 && turned_angle < TAU &&
                 fabsf(turned_angle - want) <= NG_TURN_NEAR;
        ui_end();
    }
    char what[240];
    snprintf(what, sizeof what, "N1 Nachtglas's loader at %d moments: three images (%s) in the order body, light, "
             "front (%s), in its box, the light turned at its middle (%s), as solid as the loader (%s)", moments,
             three ? "yes" : "no", order ? "yes" : "no", placed ? "yes" : "no", solid ? "yes" : "no");
    check(three && order && placed && solid, what);
    snprintf(what, sizeof what, "N2 Nachtglas's loader: the light turned k / 8 of a turn at k / 8 of a round (%s), the "
             "same for the same clock (%s), a valid angle from clocks that jump or wrap (%s)", angle ? "yes" : "no",
             same ? "yes" : "no", wraps ? "yes" : "no");
    check(three && angle && same && wraps, what);
    snprintf(what, sizeof what, "N3 Nachtglas's loader: the light lies in the glass, under the front and inside the "
             "tube (%s)", inside ? "yes" : "no");
    check(three && inside, what);
    fake_on = on;
}

/* N4: Nachtglas's loader as the application shows it (ui_loader, with its line, fading in and whole) on the bottom
 * screen and on the top one's left eye as the badge removal shows it: the image right before the line is the front,
 * an image of the atlas, and no text follows a picture's texture, no transparent piece is needed. */
static void night_loader_line(void)
{
    if (!look_built(LOOK_NIGHT))
        return;
    int on = fake_on, fronts = 0, frames = 0, after = 0, pieces = 0;
    fake_on = 0;
    for (int s = 0; s < 2; s++) {
        ui_look(LOOK_NIGHT, s ? ART_SCHEME_NACHT : ART_SCHEME_WEISS, 1, 0);
        start_frame();
        start_frame();
        for (int f = 0; f < 4; f++)
            for (int eye = 0; eye < 3; eye += 2) {
                const char *line = eye ? "Wird gelesen" : "Sicherung wird geschrieben";
                recs = 0;
                watched = eye;
                ui_begin(1.0f);
                ui_top();
                if (!eye)
                    ui_loader(line, NULL, 0, f & 1 ? 1.0f : 0.5f);
                ui_bottom();
                if (eye)
                    ui_loader(line, NULL, 1.0f, f & 1 ? 1.0f : 0.5f);
                ui_end();
                order_faults(&after, &pieces);
                int last = -1;
                for (int q = 0; q < recs; q++) {
                    if (rec[q].kind == 'T' && !strcmp(rec[q].text, line)) {
                        fronts += last >= 0 && rec[last].tex == &atlas &&
                                  atlas_sprite(&rec[last]) == ART_NG_LOADER_FRONT;
                        break;
                    }
                    if (rec[q].kind == 'I')
                        last = q;
                }
                frames++;
            }
    }
    watched = 2;
    char what[220];
    snprintf(what, sizeof what, "N4 Nachtglas's loader with its line on both screens: the front right before the line "
             "in %d of %d frames, %d texts after a picture's texture, %d transparent pieces", fronts, frames, after,
             pieces);
    check(fronts == frames && after == 0 && pieces == 0, what);
    fake_on = on;
}

/* G4-A: Nachtglas's finished step mark (POUR_TICK) reads at true size: in each of the eight schemes its check stands
 * at least 4.5 to 1 against the disc's face (it was white on the pale teal, 1.4 to 1). In the image as packed: the
 * face its most frequent opaque colour, the check its opaque pixel farthest from the face by luminance. */
static double texel_lum(uint32_t t)
{
    double c[3] = {(t >> 24) / 255.0, (t >> 16 & 255) / 255.0, (t >> 8 & 255) / 255.0}, w[3] = {0.2126, 0.7152, 0.0722};
    double l = 0;
    for (int i = 0; i < 3; i++)
        l += w[i] * (c[i] <= 0.04045 ? c[i] / 12.92 : pow((c[i] + 0.055) / 1.055, 2.4));
    return l;
}

static void night_done_mark(void)
{
    if (!look_built(LOOK_NIGHT))
        return;
    const look_art *a = look_get(LOOK_NIGHT)->art;
    const art_sprite *s = art_of(a, ART_POUR_TICK);
    double low = 1e9;
    for (int k = 0; k < ART_SCHEMES; k++) {
        const art_tex tex = art_tex_of(a, ART_POUR_TICK, k);
        const uint8_t *px = tex.z ? inflated(tex.z, tex.z_size, (uLongf)tex.w * tex.h * 4) : NULL;
        if (!px) {
            low = 0;
            break;
        }
        uint32_t t[32 * 32];
        int n = 0;
        for (int y = s->y; y < s->y + s->h && n < 32 * 32; y++)
            for (int x = s->x; x < s->x + s->w && n < 32 * 32; x++) {
                uint32_t v;
                memcpy(&v, px + 4 * (((y >> 3) * (tex.w >> 3) + (x >> 3)) * 64 + ui_morton(x & 7, y & 7)), 4);
                if ((v & 255) == 255)
                    t[n++] = v;
            }
        int best = 0, face = 0;
        for (int i = 0; i < n; i++) {
            int same = 0;
            for (int j = 0; j < n; j++)
                same += t[j] == t[i];
            if (same > best)
                best = same, face = i;
        }
        double lf = n ? texel_lum(t[face]) : 0, far = lf;
        for (int i = 0; i < n; i++)
            if (fabs(texel_lum(t[i]) - lf) > fabs(far - lf))
                far = texel_lum(t[i]);
        double r = ((lf > far ? lf : far) + 0.05) / ((lf > far ? far : lf) + 0.05);
        low = r < low ? r : low;
    }
    char what[200];
    snprintf(what, sizeof what, "G4-A Nachtglas's finished step mark: its check against the disc's face at least "
             "%.2f to 1 in %d schemes", low, ART_SCHEMES);
    check(low >= 4.5, what);
}

/* G1-R4 (C36, C37, Stock): a settings row's face runs smoothly from its light top edge to its dark foot. In every
 * scheme, at every height the rows are drawn at (SET_ROW_H of one to SET_SHOWN rows), resting, held and selected (the
 * cursor's corners over it), down the face's middle column as painted: between SET_RUN_TOP rows below its top and
 * SET_RUN_FOOT rows above its lower edge (the foot darkens faster there, as measured) no channel changes by more than
 * SET_RUN_STEP from one pixel row to the next, the tone (the channels' sum) changes direction at most once, and no
 * more than SET_RUN_FLAT rows in a row are equal (at 92c02e3 the image's one middle row stretched over 10 or 11 rows:
 * a flat band between two graded halves). */
/* G2-M2 (C48, the canvas's Nachtglas as shown): Nachtglas's tile in its own material, drawn by the shared code.
 * (1) Every image of the installing screen has Stock's size, reach past its element and fixed edges (pour_ui.h places
 * them alike in every look), and nothing of the liquid's images lies outside the inner shape (their corner pixels
 * clear). (2) Smoked glass without a sheen: in every scheme the body (POUR_BACK) 9 px under the tile's top on its
 * middle line is see-through (alpha 60 to 140) and the front glass there and 8 px in from the left side is dark (its
 * colour's luminance 0.05 or less; Stock's sheen at the top in Weiss is over 0.5). (3) The canvas's weak point (the
 * liquid nearly colourless on the look's violet ground, the end's glow nearly white): in every scheme the liquid 6 px
 * over the inner shape's floor keeps a saturation ((most - least) / most of its channels) of 0.25 or more, with the
 * end's light (POUR_LIT) over it 0.18 or more, and the halo over the tile's top 0.2 or more. (4) The level and the
 * landing read and the piece is lit from within, in every scheme: the surface's light line (POUR_SURFACE's first row)
 * stands against the liquid's foot with a contrast (WCAG's ratio) of 1.8 or more, the landing's skin (the topmost
 * covered pixel on the middle column of its frame 8) with 1.8 or more; the falling piece's middle has a luminance of
 * 0.6 or more, its body 4 px left of it 0.25 or less. */
static double px_lum(const int c[4])
{
    const double v[3] = {c[0], c[1], c[2]};
    return rel_lum(v);
}

static double px_sat(const double c[3])
{
    double most = fmax(c[0], fmax(c[1], c[2])), least = fmin(c[0], fmin(c[1], c[2]));
    return most > 0 ? (most - least) / most : 0;
}

static void night_tile_material(void)
{
    if (!look_built(LOOK_NIGHT))
        return;
    const look_art *n = look_get(LOOK_NIGHT)->art, *st = look_get(LOOK_STOCK)->art;
    const int m = art_of(n, ART_POUR_FRONT)->pad[0], tw = POUR_TILE_W, th = POUR_TILE_H;
    const int lx = tw / 2 - POUR_RIM, ly = th - 2 * POUR_RIM - 6;
    int same = 0, read = 1, clear = 1, body_least = 255, body_most = 0;
    double face = 0, stock_face = 0, sat = 9, end = 9, halo = 9, line = 99, skin = 99, core = 9, off = 0;
    for (int id = ART_POUR_HALO; id <= ART_POUR_TITLE_DONE; id++) {
        const art_sprite *a = art_of(n, id), *b = art_of(st, id);
        same += a->w && a->w == b->w && a->h == b->h && !memcmp(a->pad, b->pad, 4) && !memcmp(a->cap, b->cap, 4);
    }
    for (int s = 0; s < ART_SCHEMES; s++) {
        int c[4], q[4];
        const int liquid[2] = {ART_POUR_LIQUID, ART_POUR_LIT};
        for (int k = 0; k < 2; k++) {
            const art_sprite *l = art_of(n, liquid[k]);
            for (int j = 0; j < 4; j++) {
                read &= sprite_px(n, s, liquid[k], j & 1 ? l->w - 1 : 0, j & 2 ? l->h - 1 : 0, c);
                clear &= c[3] == 0;
            }
        }
        read &= sprite_px(n, s, ART_POUR_BACK, m + tw / 2, m + 9, c);
        body_least = c[3] < body_least ? c[3] : body_least;
        body_most = c[3] > body_most ? c[3] : body_most;
        read &= sprite_px(n, s, ART_POUR_FRONT, m + tw / 2, m + 9, c) && sprite_px(n, s, ART_POUR_FRONT, m + 8, m + th / 2, q);
        face = fmax(face, fmax(px_lum(c), px_lum(q)));
        if (s == ART_SCHEME_WEISS) {
            read &= sprite_px(st, s, ART_POUR_FRONT, m + tw / 2, m + 9, c);
            stock_face = px_lum(c);
        }
        read &= sprite_px(n, s, ART_POUR_LIQUID, lx, ly, c) && sprite_px(n, s, ART_POUR_LIT, lx, ly, q);
        const double foot[3] = {c[0], c[1], c[2]}, a = q[3] / 255.0;
        const double lit[3] = {q[0] * a + c[0] * (1 - a), q[1] * a + c[1] * (1 - a), q[2] * a + c[2] * (1 - a)};
        sat = fmin(sat, px_sat(foot));
        end = fmin(end, px_sat(lit));
        read &= sprite_px(n, s, ART_POUR_HALO, m + tw / 2, m - 4, c);
        const double glow[3] = {c[0], c[1], c[2]};
        halo = fmin(halo, px_sat(glow));
        read &= sprite_px(n, s, ART_POUR_SURFACE, 0, 0, c);
        line = fmin(line, (px_lum(c) + 0.05) / (rel_lum(foot) + 0.05));
        int top = 0;
        const int fx = 8 % POUR_MELT_COLS * POUR_MELT_W + POUR_MELT_W / 2, fy = 8 / POUR_MELT_COLS * POUR_MELT_H;
        for (int y = 0; y < POUR_MELT_H && !top; y++)
            top = sprite_px(n, s, ART_POUR_MELT, fx, fy + y, c) && c[3] > 200;
        read &= top;
        skin = fmin(skin, (px_lum(c) + 0.05) / (rel_lum(foot) + 0.05));
        read &= sprite_px(n, s, ART_POUR_PIECE_TURN, POUR_PIECE_W / 2, POUR_PIECE_H / 2 - 1, c) &&
                sprite_px(n, s, ART_POUR_PIECE_TURN, POUR_PIECE_W / 2 - 4, POUR_PIECE_H / 2 - 1, q);
        core = fmin(core, px_lum(c));
        off = fmax(off, px_lum(q));
    }
    char what[320];
    snprintf(what, sizeof what, "G2-M2 Nachtglas's installing-screen images: %d of %d with Stock's size, reach and fixed "
             "edges; the liquid's and its end light's corner pixels %s", same, ART_POUR_TITLE_DONE - ART_POUR_HALO + 1,
             clear ? "clear" : "not clear");
    check(read && clear && same == ART_POUR_TITLE_DONE - ART_POUR_HALO + 1, what);
    snprintf(what, sizeof what, "G2-M2 Nachtglas's smoked glass in eight schemes: the body's alpha %d to %d (60 to 140), "
             "the face's luminance at most %.3f (0.05 or less; Stock's sheen in Weiss %.2f)", body_least, body_most, face,
             stock_face);
    check(read && body_least >= 60 && body_most <= 140 && face <= 0.05 && stock_face > 0.5, what);
    snprintf(what, sizeof what, "G2-M2 Nachtglas's light keeps its colour in eight schemes: the liquid's saturation at "
             "least %.2f (0.25 or more), under the end's light %.2f (0.18 or more), the halo's %.2f (0.2 or more)", sat,
             end, halo);
    check(read && sat >= 0.25 && end >= 0.18 && halo >= 0.2, what);
    snprintf(what, sizeof what, "G2-M2 Nachtglas's level and landing in eight schemes: the surface's line against the "
             "liquid %.2f, the landing's skin %.2f (1.8 or more each); the piece's middle %.2f (0.6 or more), its body "
             "%.2f (0.25 or less)", line, skin, core, off);
    check(read && line >= 1.8 && skin >= 1.8 && core >= 0.6 && off <= 0.25, what);
}

/* LQ (C99, "die farben, die das glas füllen, sollen ein bisschen kräftiger sein ... aktuell wirkt es fast grau"): the
 * liquid is its colour and strong in every look and scheme (pour_art.py, liquid_tone and LIQUID_STEPS). POUR_LIQUID
 * beside the picture (LQ_SIDE px in from the inner shape's left side) at the inner shape's middle row and 6 px over
 * its floor: OKLCH chroma LQ_CHROMA or more, OKLCH lightness from LQ_LIGHT_MIN to LQ_LIGHT_MAX (not pale, not
 * black); and the end's light (POUR_LIT laid over it there) keeps LQ_END_CHROMA or more. With the rule off (gain 1,
 * no floor) the least is chroma 0.046 (Stock in Weiss) and every look fails. */
#define LQ_SIDE 8
#define LQ_CHROMA 0.085
#define LQ_LIGHT_MIN 0.40
#define LQ_LIGHT_MAX 0.85
#define LQ_END_CHROMA 0.07

/* LQ-2 (C103, "und sand näher an braun") and C110-Q ("gelb: mach für gelb ein orange, ähnlich wie das original ...
 * ein bisschen kräftiger"): Gelb's and Sand's liquids (pour_art.py, LIQUID_TAKES) at the middle row beside the
 * picture, in every look. Gelb's an orange: OKLCH hue LQ2_GELB_HUE_MIN to LQ2_GELB_HUE_MAX (his example's fill turns
 * from 45 under the surface to 82 at the foot; the yellow of LQ-2 stood at 90), chroma LQ2_GELB_CHROMA or more (the
 * example shows 0.13 under its white cast; built 0.180, in Nachtglas, whose inner glow whitens it, 0.147) and light
 * (lightness LQ2_GELB_LIGHT or more). Sand's a brown (hue LQ2_SAND_HUE or less, chroma LQ2_SAND_CHROMA or less;
 * Nachtglas's own steps turn a hue about 3 degrees warmer, so the hue bound leaves that room over the takes' 62 and
 * 65). An orange and a brown can share a hue (Gelb's middle in Stock stands 6 degrees from Sand's): the eye parts
 * them by lightness and colour, so the two stand at least LQ2_APART_LIGHT in lightness and LQ2_APART_CHROMA in chroma
 * apart (built: 0.157 to 0.225 and 0.032 to 0.070). With Gelb's take off (its liquid back to the amber rule) the check
 * fails in all four looks. */
#define LQ2_GELB_HUE_MIN 45.0
#define LQ2_GELB_HUE_MAX 88.0
#define LQ2_GELB_CHROMA 0.14
#define LQ2_GELB_LIGHT 0.70
#define LQ2_SAND_HUE 70.0
#define LQ2_SAND_CHROMA 0.125
#define LQ2_APART_LIGHT 0.15
#define LQ2_APART_CHROMA 0.025

static void oklab_lab(const double c[3], double *lightness, double ab[2])
{
    double v[3];
    for (int i = 0; i < 3; i++) {
        double x = c[i] / 255.0;
        v[i] = x <= 0.04045 ? x / 12.92 : pow((x + 0.055) / 1.055, 2.4);
    }
    double l = cbrt(0.4122214708 * v[0] + 0.5363325363 * v[1] + 0.0514459929 * v[2]);
    double m = cbrt(0.2119034982 * v[0] + 0.6806995451 * v[1] + 0.1073969566 * v[2]);
    double s = cbrt(0.0883024619 * v[0] + 0.2817188376 * v[1] + 0.6299787005 * v[2]);
    ab[0] = 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s;
    ab[1] = 0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s;
    *lightness = 0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s;
}

static void oklab_lc(const double c[3], double *lightness, double *chroma)
{
    double ab[2];
    oklab_lab(c, lightness, ab);
    *chroma = hypot(ab[0], ab[1]);
}

static void liquid_strength(void)
{
    char what[400];
    for (int id = 0; id < LOOK_COUNT; id++) {
        if (!look_built(id))
            continue;
        const look_art *a = look_get(id)->art;
        double least_c = 9, least_l = 9, most_l = 0, least_end = 9;
        int read = 1, weakest = -1;
        for (int s = 0; s < ART_SCHEMES; s++) {
            const int rows[2] = {POUR_INNER_H / 2, POUR_INNER_H - 6};
            for (int k = 0; k < 2; k++) {
                int c[4], q[4];
                double light, chroma, end_light, end_chroma;
                read &= sprite_px(a, s, ART_POUR_LIQUID, LQ_SIDE, rows[k], c) &&
                        sprite_px(a, s, ART_POUR_LIT, LQ_SIDE, rows[k], q);
                const double body[3] = {c[0], c[1], c[2]}, al = q[3] / 255.0;
                const double lit[3] = {q[0] * al + c[0] * (1 - al), q[1] * al + c[1] * (1 - al),
                                       q[2] * al + c[2] * (1 - al)};
                oklab_lc(body, &light, &chroma);
                oklab_lc(lit, &end_light, &end_chroma);
                if (chroma < least_c)
                    least_c = chroma, weakest = s;
                least_l = fmin(least_l, light);
                most_l = fmax(most_l, light);
                least_end = fmin(least_end, end_chroma);
            }
        }
        snprintf(what, sizeof what, "LQ %s's liquid in eight schemes, beside the picture at the middle and the foot: "
                 "OKLCH chroma at least %.3f (scheme %d; %.3f or more), lightness %.3f to %.3f (%.2f to %.2f), under "
                 "the end's light chroma at least %.3f (%.2f or more)", look_get(id)->name, least_c, weakest,
                 LQ_CHROMA, least_l, most_l, LQ_LIGHT_MIN, LQ_LIGHT_MAX, least_end, LQ_END_CHROMA);
        check(read && least_c >= LQ_CHROMA && least_l >= LQ_LIGHT_MIN && most_l <= LQ_LIGHT_MAX &&
              least_end >= LQ_END_CHROMA, what);
    }
}

static void gelb_sand_apart(void)
{
    char what[400];
    for (int id = 0; id < LOOK_COUNT; id++) {
        if (!look_built(id))
            continue;
        const look_art *a = look_get(id)->art;
        double l[2], c[2], h[2];
        int read = 1;
        for (int k = 0; k < 2; k++) {
            int p[4];
            double ab[2];
            read &= sprite_px(a, k ? ART_SCHEME_SAND : ART_SCHEME_GELB, ART_POUR_LIQUID, LQ_SIDE, POUR_INNER_H / 2, p);
            const double body[3] = {p[0], p[1], p[2]};
            oklab_lab(body, &l[k], ab);
            c[k] = hypot(ab[0], ab[1]);
            h[k] = fmod(atan2(ab[1], ab[0]) * 180.0 / acos(-1.0) + 360.0, 360.0);
        }
        snprintf(what, sizeof what, "LQ-2 %s's liquid at the middle: Gelb's hue %.0f (%.0f to %.0f), chroma %.3f "
                 "(%.2f or more), lightness %.3f (%.2f or more); Sand's hue %.0f (%.0f or less), chroma %.3f (%.3f or "
                 "less); apart %.3f in lightness (%.2f or more), %.3f in chroma (%.3f or more)", look_get(id)->name,
                 h[0], LQ2_GELB_HUE_MIN, LQ2_GELB_HUE_MAX, c[0], LQ2_GELB_CHROMA, l[0], LQ2_GELB_LIGHT, h[1],
                 LQ2_SAND_HUE, c[1], LQ2_SAND_CHROMA, l[0] - l[1], LQ2_APART_LIGHT, c[0] - c[1], LQ2_APART_CHROMA);
        check(read && h[0] >= LQ2_GELB_HUE_MIN && h[0] <= LQ2_GELB_HUE_MAX && c[0] >= LQ2_GELB_CHROMA &&
              l[0] >= LQ2_GELB_LIGHT && h[1] <= LQ2_SAND_HUE && c[1] <= LQ2_SAND_CHROMA &&
              l[0] - l[1] >= LQ2_APART_LIGHT && c[0] - c[1] >= LQ2_APART_CHROMA, what);
    }
}

/* BAR-C (C144, "für alle stile: mach den progressbar loader passend zur ausgewählten farbe"; C145, "ausser bei gelb,
 * da bleibt er türkis"): the download bar's fill (ART_PROGRESS_FILL_SHARD, the slot every look draws it from; Stock
 * draws its three styles alike) in the scheme's colour, the family of the installing tile's liquid (art.py bar_tone,
 * take a). In every look and scheme but Gelb the fill's OKLCH hue (its middle row's middle third, by alpha) lies
 * within BAR_HUE_SPAN degrees of the liquid's (ART_POUR_LIQUID beside the picture at the middle, as LQ reads it) and
 * its chroma is BAR_CHROMA or more (not a grey); built: at most 1.5 (Stock), 9.4 (Frost), 17.8 (Nachtglas, whose
 * violet into blue keeps its spread of 30 degrees about the tone) and 8.5 (Prisma) apart, the least chroma 0.057.
 * Gelb's fill is the teal's byte for byte: the hash of its pixels is BAR_GELB_HASH, per look (Stock's and Nachtglas's
 * 1c023b7's; Frost's and Prisma's, whose own bars came in BAR-S, C146, bar-v's 54dea0f and 3f1b47d, drawn in the
 * teal; with those bars Frost's hue is at most 8.9 and Prisma's 2.3 degrees apart, the least chroma 0.075). With every
 * scheme's bar left teal (bar_turned giving its colour back) the hue check fails in all four looks; with Gelb's bar
 * turned to its liquid the hash fails in all four. */
#define BAR_HUE_SPAN 25.0
#define BAR_CHROMA 0.04
static const uint32_t BAR_GELB_HASH[LOOK_COUNT] = {371395077u, 663654538u, 524548157u, 1422664968u};

/* a sprite's pixels (straight r, g, b, a, row by row) in the texture it lies in (art_tex_of) in a scheme; 0 when it
 * cannot be read */
static int sprite_pixels(const look_art *a, int scheme, int id, int *out, int most)
{
    const art_sprite *s = art_of(a, id);
    const art_tex tex = art_tex_of(a, id, scheme);
    const uint8_t *px = tex.z ? inflated(tex.z, tex.z_size, (uLongf)tex.w * tex.h * 4) : NULL;
    if (!px || !s->w || s->w * s->h > most)
        return 0;
    for (int y = 0; y < s->h; y++)
        for (int x = 0; x < s->w; x++) {
            int ax = s->x + x, ay = s->y + y, *o = out + 4 * (y * s->w + x);
            const uint8_t *t = px + 4 * (((ay >> 3) * (tex.w >> 3) + (ax >> 3)) * 64 + ui_morton(ax & 7, ay & 7));
            o[0] = t[3], o[1] = t[2], o[2] = t[1], o[3] = t[0];
        }
    return 1;
}

static double hue_of(const double ab[2])
{
    return fmod(atan2(ab[1], ab[0]) * 180.0 / acos(-1.0) + 360.0, 360.0);
}

static void bar_colour(void)
{
    static int px[4 * 4096];
    char what[400];
    for (int id = 0; id < LOOK_COUNT; id++) {
        if (!look_built(id))
            continue;
        const look_art *a = look_get(id)->art;
        const art_sprite *s = art_of(a, ART_PROGRESS_FILL_SHARD);
        double worst = 0, least_c = 9;
        int read = 1, at = -1;
        uint32_t gelb = 0;
        for (int k = 0; k < ART_SCHEMES; k++) {
            read &= sprite_pixels(a, k, ART_PROGRESS_FILL_SHARD, px, 4096);
            if (!read)
                break;
            if (k == ART_SCHEME_GELB) {
                gelb = fnv(2166136261u, px, sizeof(int) * 4 * s->w * s->h);
                continue;
            }
            double sum[3] = {0, 0, 0}, wsum = 0, l, ab[2], lab[2];
            for (int x = s->w / 3; x < 2 * s->w / 3; x++) {
                const int *p = px + 4 * ((s->h / 2) * s->w + x);
                for (int i = 0; i < 3; i++)
                    sum[i] += p[i] * p[3];
                wsum += p[3];
            }
            int q[4];
            read &= wsum > 0 && sprite_px(a, k, ART_POUR_LIQUID, LQ_SIDE, POUR_INNER_H / 2, q);
            if (!read)
                break;
            const double fill[3] = {sum[0] / wsum, sum[1] / wsum, sum[2] / wsum}, liquid[3] = {q[0], q[1], q[2]};
            oklab_lab(fill, &l, ab);
            oklab_lab(liquid, &l, lab);
            double apart = fabs(fmod(hue_of(ab) - hue_of(lab) + 540.0, 360.0) - 180.0);
            if (apart > worst)
                worst = apart, at = k;
            least_c = fmin(least_c, hypot(ab[0], ab[1]));
        }
        snprintf(what, sizeof what, "BAR-C %s's bar fill in the scheme's colour: its hue at most %.1f degrees from the "
                 "tile's liquid's (scheme %d; %.0f or less), chroma at least %.3f (%.3f or more); Gelb's teal "
                 "(hash %u, pinned %u)", look_get(id)->name, worst, at, BAR_HUE_SPAN, least_c, BAR_CHROMA, gelb,
                 BAR_GELB_HASH[id]);
        check(read && worst <= BAR_HUE_SPAN && least_c >= BAR_CHROMA && gelb == BAR_GELB_HASH[id], what);
    }
}

#define SET_RUN_TOP 3
#define SET_RUN_FOOT 10
#define SET_RUN_STEP 4
#define SET_RUN_FLAT 4

static void settings_row_run(void)
{
    static const char *const NAMES[8] = {"Weiss", "Gelb", "Sand", "Minze", "Himmel", "Flieder", "Rose", "Nacht"};
    static const char *const STATES[3] = {"resting", "held", "selected"};
    int on = fake_on, x = SET_X + SET_W / 2;
    fake_on = 0;
    for (int scheme = 0; scheme < ART_SCHEMES; scheme++) {
        ui_look(LOOK_STOCK, scheme, 1, 0);
        start_frame();
        int worst_step = 0, worst_bends = 0, worst_flat = 0, ok = 1;
        for (int n = 1; n <= SET_SHOWN; n++) {
            int h = SET_ROW_H(n);
            for (int state = 0; state < 3; state++) {
                rect r = {SET_X, SET_Y, SET_W, h};
                frame_begin();
                ui_lk->draw.face(FACE_SET, r, state == 1 ? UI_DOWN : 0, -1);
                if (state == 2)
                    ui_select(r, ROW_R);
                ui_end();
                paint_top(1);
                int step = 0, bends = 0, flat = 1, run = 1, last = 0;
                for (int y = SET_Y + SET_RUN_TOP + 1; y < SET_Y + h - SET_RUN_FOOT; y++) {
                    int d = 0, same = 1;
                    for (int j = 0; j < 3; j++) {
                        int c = (int)roundf(paint[y][x][j]) - (int)roundf(paint[y - 1][x][j]);
                        d += c;
                        step = abs(c) > step ? abs(c) : step;
                        same &= c == 0;
                    }
                    run = same ? run + 1 : 1;
                    flat = run > flat ? run : flat;
                    int dir = d > 0 ? 1 : d < 0 ? -1 : 0;
                    if (dir && last && dir != last)
                        bends++;
                    last = dir ? dir : last;
                }
                int fine = step <= SET_RUN_STEP && bends <= 1 && flat <= SET_RUN_FLAT;
                ok &= fine;
                worst_step = step > worst_step ? step : worst_step;
                worst_bends = bends > worst_bends ? bends : worst_bends;
                worst_flat = flat > worst_flat ? flat : worst_flat;
                if (!fine)
                    printf("  %s %s, %d px: largest step %d, %d changes of direction, %d equal rows\n", NAMES[scheme],
                           STATES[state], h, step, bends, flat);
            }
        }
        char what[260];
        snprintf(what, sizeof what, "R4 Stock %s: a settings row's face down its middle (rows of %d to %d px, resting, "
                 "held, selected): largest step %d (at most %d), %d changes of direction (at most 1), %d equal rows "
                 "in a row (at most %d)", NAMES[scheme], SET_ROW_H(SET_SHOWN), SET_ROW_H(1), worst_step, SET_RUN_STEP,
                 worst_bends, worst_flat, SET_RUN_FLAT);
        check(ok, what);
    }
    fake_on = on;
}

int main(void)
{
    char what[200];
    ui_part(&ART_APP_PART);
    if (ui_init()) {
        printf("FAIL ui_init\n1 failed\n");
        return 1;
    }
    int held = 0;
    for (int id = 0; id < LOOK_COUNT; id++)
        if (look_built(id)) {
            held++;
            table(look_get(id));
        }
    check(held >= 1 && look_built(LOOK_STOCK) && look_get(LOOK_STOCK) == &look_stock, "Stock is held");
    for (int id = 0; id < LOOK_COUNT; id++)
        if (look_built(id))
            loader_of(id);

    int others = 1;
    const int numbers[] = {-1, LOOK_COUNT, 99};
    for (size_t k = 0; k < sizeof numbers / sizeof *numbers; k++)
        others &= !look_built(numbers[k]) && look_get(numbers[k]) == &look_stock;
    for (int id = 0; id < LOOK_COUNT; id++)
        others &= look_built(id) || look_get(id) == &look_stock;
    ui_look(99, 3, 1, 0);
    start_frame();
    check(others && ui_lk == &look_stock && ui_f.look == LOOK_STOCK && ui_f.scheme == 3,
          "a number this build does not hold (-1, LOOK_COUNT, 99, and every look not built): Stock is drawn");

    /* the test-only look told between two frames: nothing changes until the next one begins */
    fake_on = 1;
    ui_look(FAKE_ID, 5, 1, PATTERN_PLUS);
    check(ui_lk == &look_stock && !fake_calls[0] && ui_f.look == LOOK_STOCK && ui_f.scheme == 3,
          "another look told: the frame drawn so far keeps the old one, nothing made yet");
    memset(atlas.data, 0, (size_t)ART_ATLAS_W * ART_ATLAS_H * 4);
    memset(glass_tex.data, 0, (size_t)ART_GLASS_SHEET_W * ART_GLASS_SHEET_H * 4);
    memset(part_atlas.data, 0, (size_t)ART_APP_PART.atlas_w * ART_APP_PART.atlas_h * 4);
    memset(part_sheet.data, 0, (size_t)ART_APP_PART.sheet_w * ART_APP_PART.sheet_h * 4);
    recs = 0;
    ui_begin(1.0f);
    snprintf(what, sizeof what, "the next frame's start: the new look loaded, then told its atlas (calls \"%s\", "
             "scheme %d, pattern %d), and drawn from then on", fake_calls, fake_scheme, fake_pattern);
    check(!strcmp(fake_calls, "lr") && fake_scheme == 5 && fake_pattern == -1 && ui_lk == &look_fake &&
          ui_f.look == FAKE_ID && ui_f.scheme == 5 && glass_sheet_in == 5 && pictures_in == -1 && ui_f.pattern == -1,
          what);
    check(atlas_ok && glass_ok && holds(&atlas, look_fake.art->atlas_z[5], look_fake.art->atlas_z_size[5]) &&
          holds(&glass_tex, look_fake.art->sheet_z[5], look_fake.art->sheet_z_size[5]),
          "its atlas and sheet unpacked anew into their textures");
    const art_tex pa = art_tex_of(look_fake.art, ART_ALL, 5), ps = art_tex_of(look_fake.art, ART_ALL + ART_APP_COUNT, 5);
    check(part_atlas_made && part_sheet_made && part_atlas_ok && part_sheet_ok && holds(&part_atlas, pa.z, pa.z_size) &&
              holds(&part_sheet, ps.z, ps.z_size) && ui_sprite_tex(ART_ALL, 0) == &part_atlas &&
              ui_sprite_tex(ART_ALL, 1) == &part_atlas_smooth &&
              ui_sprite_tex(ART_ALL + ART_APP_COUNT, 0) == &part_sheet && ui_sprite_tex(0, 0) == &atlas,
          "and the application's part's atlas and sheet into theirs, its sprites read from them");
    ui_top();
    ui_start();
    ui_bottom();
    ui_end();
    snprintf(what, sizeof what, "the start screen then unpacks its house and pattern and tells the look (calls \"%s\", "
             "pattern %d)", fake_calls, fake_pattern);
    check(!strcmp(fake_calls, "lrr") && fake_pattern == PATTERN_PLUS && pictures_in == 5 && ui_f.pattern == PATTERN_PLUS,
          what);
    ui_look(FAKE_ID, 5, 1, PATTERN_PLUS);
    start_frame();
    check(!strcmp(fake_calls, "lrr"), "the same look told again: nothing made or unpacked anew");

    /* the popup's tone: the next popup's buttons are told it, the one after that 0 again */
    ui_popup pop;
    memset(&pop, 0, sizeof pop);
    int tones[3];
    for (int k = 0; k < 3; k++) {
        if (k == 0)
            ui_popup_tone(1);
        fake_removes = -1;
        ui_begin(1.0f);
        ui_top();
        ui_bottom();
        ui_popup_draw(&pop, "Abbrechen", "Löschen", -1, -1, 1.0f);
        ui_end();
        tones[k] = fake_removes;
    }
    snprintf(what, sizeof what, "ui_popup_tone(1): the next popup's buttons are told it deletes (%d), the two after "
             "it not (%d, %d)", tones[0], tones[1], tones[2]);
    check(tones[0] == 1 && tones[1] == 0 && tones[2] == 0, what);

    /* the loader is the look's: the test-only look's own is drawn, handed its place, the clock and how solid */
    table(&look_fake);
    loader_of(FAKE_ID);
    fake_loaders = 0;
    recs = 0;
    ui_begin(1.0f);
    ui_top();
    ui_bottom();
    ui_loader("Wird gelesen", NULL, 1.0f, 0.5f);
    ui_end();
    float box_cy = roundf((SCREEN_H - (LOADER_BOX + LOADER_GAP + LOADER_LH)) / 2.0f + LOADER_BOX / 2.0f);
    int turned = 0;             /* ui_sprite_turned's bead: an image of the atlas's smoothed view at the middle */
    for (int i = 0; i < recs; i++)
        turned += rec[i].kind == 'I' && rec[i].tex == &atlas_smooth;
    snprintf(what, sizeof what, "ui_loader draws the look's loader: %d calls, its middle at (%g, %g), the frame's "
             "clock %s, alpha %g; %d turned image", fake_loaders, fake_loader_cx, fake_loader_cy,
             fake_loader_ms == ui_f.ms ? "yes" : "no", fake_loader_alpha, turned);
    check(fake_loaders == 1 && fake_loader_cx == BOTTOM_W / 2.0f && fake_loader_cy == box_cy &&
          fake_loader_ms == ui_f.ms && fake_loader_alpha == 0.5f && turned == 1, what);
    fake_loaders = 0;           /* on the top screen (the badge removal's): its middle there, solid */
    recs = 0;
    watched = 0;
    ui_begin(1.0f);
    ui_top();
    ui_loader("Sicherung wird geschrieben", NULL, 0, 1);
    ui_bottom();
    ui_end();
    watched = 2;
    snprintf(what, sizeof what, "ui_loader on the top screen: %d calls, its middle at (%g, %g), alpha %g", fake_loaders,
             fake_loader_cx, fake_loader_cy, fake_loader_alpha);
    check(fake_loaders == 1 && fake_loader_cx == TOP_W / 2.0f && fake_loader_cy == box_cy && fake_loader_alpha == 1.0f,
          what);

    /* back to Stock: the test-only look put away, Stock's images unpacked anew */
    fake_calls[0] = 0;
    memset(atlas.data, 0, (size_t)ART_ATLAS_W * ART_ATLAS_H * 4);
    memset(part_atlas.data, 0, (size_t)ART_APP_PART.atlas_w * ART_APP_PART.atlas_h * 4);
    ui_look(LOOK_STOCK, 5, 1, PATTERN_PLUS);
    start_frame();
    snprintf(what, sizeof what, "Stock told again: the other look put away (calls \"%s\"), Stock drawn, its atlas "
             "and the part's unpacked anew", fake_calls);
    check(!strcmp(fake_calls, "u") && ui_lk == &look_stock && ui_f.look == LOOK_STOCK && ui_f.scheme == 5 &&
          holds(&atlas, ART_STOCK.atlas_z[5], ART_STOCK.atlas_z_size[5]) &&
              holds(&part_atlas, ART_APP_PART.looks[0].atlas_z[5], ART_APP_PART.looks[0].atlas_z_size[5]), what);

    help_mark_frost();
    done_box_lower();
    help_box_gap();
    help_one_size();
    prism_loader_no_spark();
    prism_popup_corners();
    prism_tile_material();
    frost_loader_dial();
    frost_tile_material();
    night_loader_glass();
    night_loader_line();
    night_done_mark();
    night_tile_material();
    liquid_strength();
    gelb_sand_apart();
    bar_colour();
    settings_row_run();
    pattern_over();
    brackets_first();
    icon_mask_swap();
    pattern_fades();

    popup_text_place();

    /* test_ui's checks with the test-only look: its own expectations (host/expect.h), not Stock's */
    /* G2-L: no card on the installing screen, so the tile's glow stands on the look's ground, and the test-only
     * look's is two greys in every scheme (look_fake.c), not a scheme's ground: there the glow has to show, not to
     * reach a shipped look's measure (in Nacht 9.0, in Weiss 14.2 of the 15 the looks' own grounds are held to) */
    pour_glow_seen = 8;
    checks_of_look(FAKE_ID);
    pour_glow_seen = POUR_GLOW_SEEN;
    fake_on = 0;
    printf("%d failed\n", failed);
    return failed != 0;
}
