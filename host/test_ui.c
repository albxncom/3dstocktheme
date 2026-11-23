/* The drawing layer, uikit/source/ui.c, and the application's widgets, app/ui/app_ui.c, themselves (included, so
 * their textures are reachable), on the Mac: citro2d and citro3d as a recorder (uikit/host/gpu/), the images of
 * build/host/art (Stock's).
 * R7, the white flicker (order.h; ui.c, ui_picture): on the bottom screen no text comes directly after
 * an image of a texture other than the atlas, and the transparent piece ui_text puts between is never needed, for
 * the tiles, the wide buttons, the green and the cream buttons, held or not, with the selection over them; and the
 * symbol of each is drawn all the same, from the glass sheet, after its label. R9: the installing screen D3
 * on the top screen keeps the same rule at its moments, the install's and the badge write's, the pattern behind it.
 * In Stock, its vessel, the glass tile D5a, painted from the recorded frame: its numbers, its closed top edge, no
 * liquid outside its inner shape at eleven levels, the layers liquid and drops, front glass, picture, and the end's
 * glow in every scheme.
 * A1, the moving background: the pattern is drawn on the top screen of every kind of screen, once, over the
 * whole screen as on the start screen, before any text, and an image of the atlas comes right after its texture.
 * And on each of these top screens, as R7 on the bottom: no text after a picture's texture, no
 * transparent piece needed. R2 follow-up: the settings tab's wrench, resting and held, one pixel higher and one
 * further left than the folder button's symbol (Stock's place). Where a look differs from Stock on purpose (the
 * wrench's place, a top screen without the pattern), its host/expect_<name>.h says so (host/expect.h). R20 (Stock):
 * the carousel's middle pane and its neighbours apart, their frames measured at rest and at five moments of a move.
 *
 *   test_ui[_stock]   prints one line per check and "N failed"; exit 1 if any failed */
#include "ui.c"
#include "app_ui.c"
#include "expect.h"
#include <stdarg.h>
#include <stdlib.h>

/* ---- the recorder: what reaches the bottom screen (or the top's left eye, watched 0), in order */
#define REC_MAX 4096
static struct {
    char kind;                  /* I an image, T a text, R a rectangle */
    const C3D_Tex *tex;
    float x, y, w, h;
    char text[UI_LINE_BYTES + 4];      /* a text whole, as ui_text_fit hands it on */
    float u0, v0, u1, v1, k, tx, ty;   /* an image's texture coordinates and the view it was drawn in (paint_top) */
    float cx, cy, angle;                /* the point of it citro2d places at x, y and turns it about, by angle */
    int tinted;
    C2D_ImageTint tint;
    u32 top, low;                       /* a rectangle's colours at its top and its bottom */
    int eye;                            /* its target: 0 the top's left eye, 1 its right, 2 the bottom */
} rec[REC_MAX];
static int recs, target_now, watched = 2;     /* the target recorded: the bottom's, the top's (0), both eyes (3) */
static float slider_now;                      /* the 3D slider's state */
static C3D_RenderTarget targets[3] = {{0}, {1}, {2}};
struct gpu_textbuf { char mem[1 << 16]; size_t used, glyphs; };

static int put(char kind, const C3D_Tex *tex, float x, float y, float w, float h, const char *text)
{
    if ((target_now != watched && !(watched == 3 && target_now < 2)) || recs == REC_MAX)
        return 0;
    rec[recs].eye = target_now;
    rec[recs].kind = kind, rec[recs].tex = tex, rec[recs].x = x, rec[recs].y = y, rec[recs].w = w, rec[recs].h = h;
    snprintf(rec[recs].text, sizeof rec[recs].text, "%s", text ? text : "");
    recs++;
    return 1;
}

void gfxInitDefault(void) {}
void gfxExit(void) {}
void gfxSet3D(bool on) { (void)on; }
float osGet3DSliderState(void) { return slider_now; }
Result GSPGPU_FlushDataCache(const void *adr, u32 size) { (void)adr; (void)size; return 0; }
static int clock_held;      /* the clock stands: a look's moving ground (A1) stands with it */
u64 osGetTime(void) { static u64 t = 1000; return clock_held ? t : (t += 16); }
bool C3D_Init(size_t size) { (void)size; return true; }
void C3D_Fini(void) {}
bool C2D_Init(size_t n) { (void)n; return true; }
void C2D_Fini(void) {}
void C2D_Prepare(void) {}
C3D_RenderTarget *C2D_CreateScreenTarget(gfxScreen_t screen, gfx3dSide_t side)
{
    return &targets[screen == GFX_BOTTOM ? 2 : side == GFX_RIGHT ? 1 : 0];
}
bool C3D_TexInit(C3D_Tex *tex, u16 w, u16 h, int fmt)
{
    tex->data = calloc((size_t)w * h, 4);
    tex->width = w, tex->height = h, tex->fmt = fmt;
    return tex->data != NULL;
}
void C3D_TexDelete(C3D_Tex *tex) { free(tex->data); tex->data = NULL; }
void C3D_TexSetFilter(C3D_Tex *tex, int mag, int min) { (void)tex; (void)mag; (void)min; }
void C3D_TexSetWrap(C3D_Tex *tex, int s, int t) { (void)tex; (void)s; (void)t; }
void C3D_TexFlush(C3D_Tex *tex) { (void)tex; }
bool C3D_FrameBegin(int flags) { (void)flags; return true; }
void C3D_FrameEnd(int flags) { (void)flags; }
float C3D_GetCmdBufUsage(void) { return 0; }
void C2D_TargetClear(C3D_RenderTarget *t, u32 color) { (void)t; (void)color; }
void C2D_SceneBegin(C3D_RenderTarget *t) { target_now = t->id; }
static C3D_Mtx view_now = {1, 0, 0};
void C2D_ViewReset(void) { view_now = (C3D_Mtx){1, 0, 0}; }
void C2D_ViewTranslate(float x, float y) { view_now.tx += view_now.k * x, view_now.ty += view_now.k * y; }
void C2D_ViewScale(float x, float y) { (void)y; view_now.k *= x; }
void C2D_ViewSave(C3D_Mtx *m) { *m = view_now; }
void C2D_ViewRestore(const C3D_Mtx *m) { view_now = *m; }
void C2D_Fade(u32 color) { (void)color; }
bool C2D_DrawRectangle(float x, float y, float z, float w, float h, u32 c0, u32 c1, u32 c2, u32 c3)
{
    (void)z; (void)c1; (void)c3;
    if (put('R', NULL, x, y, w, h, NULL))
        rec[recs - 1].top = c0, rec[recs - 1].low = c2, rec[recs - 1].k = view_now.k, rec[recs - 1].tx = view_now.tx,
        rec[recs - 1].ty = view_now.ty;
    return true;
}
bool C2D_DrawRectSolid(float x, float y, float z, float w, float h, u32 c) { return C2D_DrawRectangle(x, y, z, w, h, c, c, c, c); }
bool C2D_DrawImage(C2D_Image img, const C2D_DrawParams *p, const C2D_ImageTint *tint)
{
    if (put('I', img.tex, p->pos.x, p->pos.y, p->pos.w, p->pos.h, NULL)) {
        typeof(rec[0]) *r = &rec[recs - 1];
        r->u0 = img.subtex->left, r->v0 = img.subtex->top, r->u1 = img.subtex->right, r->v1 = img.subtex->bottom;
        r->k = view_now.k, r->tx = view_now.tx, r->ty = view_now.ty;
        r->cx = p->center.x, r->cy = p->center.y, r->angle = p->angle;
        r->tinted = tint != NULL;
        if (tint)
            r->tint = *tint;
    }
    return true;
}
bool C2D_DrawImageAt(C2D_Image img, float x, float y, float z, const C2D_ImageTint *tint, float sx, float sy)
{
    C2D_DrawParams p = {{x, y, img.subtex->width * sx, img.subtex->height * sy}, {0, 0}, z, 0};
    return C2D_DrawImage(img, &p, tint);
}
void C2D_SetImageTint(C2D_ImageTint *tint, C2D_Corner corner, u32 color, float blend)
{
    tint->corners[corner].color = color, tint->corners[corner].blend = blend;
}
void C2D_PlainImageTint(C2D_ImageTint *tint, u32 color, float blend)
{
    for (int k = 0; k < 4; k++)
        C2D_SetImageTint(tint, (C2D_Corner)k, color, blend);
}
void C2D_AlphaImageTint(C2D_ImageTint *tint, float alpha)
{
    C2D_PlainImageTint(tint, C2D_Color32(0, 0, 0, (u8)(alpha * 255.0f + 0.5f)), 0);
}
C2D_TextBuf C2D_TextBufNew(size_t glyphs) { (void)glyphs; return calloc(1, sizeof(struct gpu_textbuf)); }
void C2D_TextBufDelete(C2D_TextBuf b) { free(b); }
void C2D_TextBufClear(C2D_TextBuf b) { b->used = b->glyphs = 0; }
size_t C2D_TextBufGetNumGlyphs(C2D_TextBuf b) { return b->glyphs; }
const char *C2D_TextParse(C2D_Text *t, C2D_TextBuf b, const char *s)
{
    size_t n = strlen(s) + 1;
    if (b->used + n > sizeof b->mem)
        b->used = 0;
    memcpy(b->mem + b->used, s, n);
    t->s = b->mem + b->used;
    b->used += n, b->glyphs += n - 1;
    return s + n - 1;
}
void C2D_TextOptimize(const C2D_Text *t) { (void)t; }
void C2D_TextGetDimensions(const C2D_Text *t, float sx, float sy, float *w, float *h)
{
    if (w)
        *w = 13.0f * (float)strlen(t->s) * sx;     /* about the console font's average advance at FONT_PX */
    if (h)
        *h = 30 * sy;
}
void C2D_DrawText(const C2D_Text *t, u32 flags, float x, float y, float z, float sx, float sy, ...)
{
    (void)z; (void)sx; (void)sy;
    va_list ap;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wvarargs"     /* citro2d's own signature ends in a float before its "..." */
    va_start(ap, sy);
#pragma GCC diagnostic pop
    u32 color = flags & C2D_WithColor ? va_arg(ap, u32) : 0xFF000000;
    va_end(ap);
    if (put('T', NULL, x, y, 0, 0, t->s))      /* a text's colour (ABGR) where a rectangle keeps its top's */
        rec[recs - 1].k = view_now.k, rec[recs - 1].tx = view_now.tx, rec[recs - 1].ty = view_now.ty,
        rec[recs - 1].top = color;
}

/* ---- the checks */
static int failed;

static void check(int ok, const char *what)
{
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    failed += !ok;
}

static int sheet(const C3D_Tex *t)
{
    return t == &glass_tex || t == &glass_smooth || t == &part_sheet || t == &part_sheet_smooth;
}

/* an atlas, the kit's or the application's part's: on whole pixels, and smoothed */
static int atlas_tex(const C3D_Tex *t)
{
    return t == &atlas || t == &part_atlas;
}

static int atlas_smooth_tex(const C3D_Tex *t)
{
    return t == &atlas_smooth || t == &part_atlas_smooth;
}

/* The letters' rule: texts drawn while the last texture is not the kit's atlas (the part's atlas counts as a
 * picture's texture here, though order.h draws it at once), and transparent pieces of the kit's atlas that were needed
 * after a picture's texture other than the part's atlas (after the part's atlas ui_text puts one by design; a piece
 * makes the kit's atlas the last texture). */
static int clear_piece(int i)
{
    return rec[i].kind == 'I' && rec[i].tex == &atlas && rec[i].x == 0 && rec[i].y == 0 && rec[i].w == 8 &&
           rec[i].h == 8;
}

static void letters(const C3D_Tex **last, int i, int *after, int *fallback)
{
    if (clear_piece(i)) {
        *fallback += !atlas_tex(*last);
        *last = &atlas;
    } else if (rec[i].kind == 'I') {
        *last = rec[i].tex;
    } else if (rec[i].kind == 'R') {
        *last = &atlas;
    } else {
        *after += *last != &atlas;
    }
}

/* The bottom screen's frame recorded: the letters' rule's two counts and the sheet's images; alone (one element in
 * the frame): whether its label came before its symbol. */
static void judge(const char *name, int alone)
{
    int after = 0, fallback = 0, sheets = 0, first_sheet = -1, last_text = -1;
    const C3D_Tex *last = &atlas;
    for (int i = 0; i < recs; i++) {
        letters(&last, i, &after, &fallback);
        if (rec[i].kind == 'I' && sheet(rec[i].tex) && !sheets++)
            first_sheet = i;
        else if (rec[i].kind != 'I' && rec[i].kind != 'R')
            last_text = i;
    }
    char what[200];
    snprintf(what, sizeof what, "R7 %s: %d texts after a picture's texture, %d transparent pieces, %d images of the "
             "sheet%s", name, after, fallback, sheets, alone ? ", the symbol after the label" : "");
    if (getenv("TEST_UI_DUMP"))     /* the frame as recorded, to see why a check failed */
        for (int i = 0; i < recs; i++)
            printf("  %c %s %g,%g %gx%g %s\n", rec[i].kind,
                   atlas_tex(rec[i].tex) ? "atlas" : sheet(rec[i].tex) ? "sheet" : rec[i].tex ? "other" : "-", rec[i].x,
                   rec[i].y, rec[i].w, rec[i].h, rec[i].text);
    check(after == 0 && fallback == 0 && sheets > 0 && last_text >= 0 && (!alone || first_sheet > last_text),
          what);
}

static void judge_top(const char *name);

/* The installing screen D3 (R9) on the top screen, as main.c and badges_ui.h draw it: after the ground an atlas image
 * comes before the first letters, no letters directly after a picture's texture (the sticker's picture, the glass
 * sheet's symbol, the scaled drops), the transparent piece never needed, and the sticker's picture drawn. */
static void judge_pour(const char *name)
{
    int after = 0, fallback = 0, pictures = 0, first_text = -1, atlas_first = 0;
    const C3D_Tex *last = &atlas;
    for (int i = 0; i < recs; i++) {
        letters(&last, i, &after, &fallback);
        if (rec[i].kind == 'I' && !clear_piece(i)) {
            pictures += !atlas_tex(rec[i].tex);
            atlas_first |= first_text < 0 && atlas_tex(rec[i].tex);
        } else if (rec[i].kind != 'I' && rec[i].kind != 'R' && first_text < 0) {
            first_text = i;
        }
    }
    char what[200];
    snprintf(what, sizeof what, "R9 %s: an atlas image before the first letters, %d texts after a picture's "
             "texture, %d transparent pieces, %d images of other textures", name, after, fallback, pictures);
    if (getenv("TEST_UI_DUMP"))
        for (int i = 0; i < recs; i++)
            printf("  %c %s %g,%g %gx%g %s\n", rec[i].kind,
                   atlas_tex(rec[i].tex) ? "atlas" : sheet(rec[i].tex) ? "sheet" : rec[i].tex ? "other" : "-", rec[i].x,
                   rec[i].y, rec[i].w, rec[i].h, rec[i].text);
    check(atlas_first && first_text >= 0 && after == 0 && fallback == 0 && pictures > 0, what);
}

static int pour_look;       /* the look the tile's checks are run for (checks_of_look) */
static uint8_t stand_in[48 * 48 * 2];       /* the design's picture: one plain tone */
static const char *const four[4] = {"Messen", "Katalog", "Design", "Abschluss"};
static const char *const badge_steps[4] = {"Badges lesen", "Sicherung", "Schreiben", "Prüfen"};

/* The installing screen's top, as main.c (n 0, the theme's picture) and badges_ui.h (n 1, the glass sheet's symbol)
 * draw it, recorded. */
static void pour_draw(const pour *p, int n)
{
    recs = 0;
    watched = 0;
    ui_begin(1.0f);
    ui_top();
    if (n)
        ui_pour(p, "Badges installieren", "Installiert", "Tiere", NULL, ART_BADGES, badge_steps, "Erfolgreich installiert", NULL);
    else
        ui_pour(p, "Wird installiert", "Installiert", "Sommerwiese", stand_in, -1, four,
                "Erfolgreich installiert", NULL);
    ui_bottom();
    ui_end();
    watched = 2;
}

/* The work at level for ms with how, from its start: long enough that every drop has landed and its ripple is gone. */
/* long enough for every piece a level lets go to have landed and the surface to be at rest (AN) */
#define POUR_SETTLE_MS (POUR_CATCH_MS + POUR_REST_MS)
static void pour_settle(pour *p, int n, float level, int how, unsigned ms)
{
    pour_start(p, n ? &POUR_BADGES : &POUR_INSTALL, 1000);
    for (unsigned t = 16; t <= ms; t += 16)
        pour_work(p, 1000 + t, level, how);
}

static void pour_frames(void)
{
    memset(stand_in, 0x5a, sizeof stand_in);
    for (int n = 0; n < 2; n++) {
        static const char *const MOMENT[3] = {"at the start", "half way", "glowing at the end"};
        /* the end: the whole work done at once shows full by POUR_CATCH_MS, its glow in POUR_GLOW_MS later (AN-3: at
         * 9000 ms the slower line had not filled the tile, so the end's order went unchecked) */
        static const unsigned AT[3] = {16, 3000, POUR_CATCH_MS + POUR_GLOW_MS};
        for (int m = 0; m < 3; m++) {
            pour p;
            pour_start(&p, n ? &POUR_BADGES : &POUR_INSTALL, 1000);
            for (unsigned t = 16; t <= AT[m]; t += 16)
                pour_work(&p, 1000 + t, m == 1 ? 0.5f : m ? 1 : 0,
                          m == 2 && t > 2000 ? POUR_SUCCEEDED : POUR_WORKING);
            pour_draw(&p, n);
            char name[96];
            snprintf(name, sizeof name, "%s %s", n ? "the badge write" : "the install", MOMENT[m]);
            if (m == 2) {
                char what[160];
                snprintf(what, sizeof what, "%s at %u ms: the tile full (%d), its glow %.2f (0.9 or more)", name, AT[m],
                         pour_full(&p), pour_glow(&p));
                check(pour_full(&p) && pour_glow(&p) >= 0.9f, what);
            }
            judge_pour(name);
            judge_top(name);        /* A1: the pattern behind it, as behind every top screen */
        }
    }
}

/* SR-R: removing a badge set, Stock's top screen as badges_ui.h draws it: the title, the loader with the step under
 * way, the footer's dots. The pattern behind it and the order as on every top screen (A1, R7); the ring's dots and
 * its line centred on the top screen, not where the bottom screen's middle is. */
static void loader_top(void)
{
    recs = 0;
    watched = 0;
    ui_begin(1.0f);
    ui_top();
    ui_head("Badges entfernen", NULL);
    int from = recs;
    ui_loader("Sicherung wird geschrieben", NULL, 0, 1);
    int to = recs;
    ui_dots(FOOT_DOTS_Y);
    ui_bottom();
    ui_end();
    watched = 2;
    float x0 = 1e9f, x1 = -1e9f, line_x = -1;
    int dots = 0;
    for (int i = from; i < to; i++)
        if (rec[i].kind == 'I' && atlas_tex(rec[i].tex)) {
            dots++;
            x0 = rec[i].x < x0 ? rec[i].x : x0;
            x1 = rec[i].x + rec[i].w > x1 ? rec[i].x + rec[i].w : x1;
        } else if (rec[i].kind == 'T' && !strcmp(rec[i].text, "Sicherung wird geschrieben")) {
            line_x = rec[i].x;      /* centred: citro2d is given the middle */
        }
    char what[200];
    snprintf(what, sizeof what, "SR-R the badge removal's top: %d images of the loader's dots from x %g to %g, its "
             "line's middle at %g (the screen's at %d)", dots, x0, x1, line_x, TOP_W / 2);
    check(dots >= LOADER_DOTS && (x0 + x1) / 2 == TOP_W / 2.0f && fabsf(line_x - TOP_W / 2.0f) < 1, what);
    judge_top("the badge removal's top");
}

/* G4-A: the loader on the bottom screen with its line and the notice under it: the notice one LOADER_LH under the
 * line, centred, in the line's tone; ring and lines as one block, so the ring stands LOADER_LH / 2 higher than with
 * the line alone; without a line no notice. The ring's top and the texts' places by the recorded calls. */
static void loader_place(const char *line, const char *notice, float *ring_y, float *line_y, float *notice_y,
                         float *line_x, float *notice_x, u32 *line_c, u32 *notice_c)
{
    recs = 0;
    ui_begin(1.0f);
    ui_top();
    ui_bottom();
    int from = recs;
    ui_loader(line, notice, 0, 1);
    ui_flush();
    int to = recs;
    ui_end();
    *ring_y = 1e9f, *line_y = *notice_y = *line_x = *notice_x = -1, *line_c = *notice_c = 0;
    for (int i = from; i < to; i++)
        if (rec[i].kind == 'I' && atlas_tex(rec[i].tex))
            *ring_y = rec[i].y < *ring_y ? rec[i].y : *ring_y;
        else if (rec[i].kind == 'T' && line && !strcmp(rec[i].text, line))
            *line_y = rec[i].y, *line_x = rec[i].x, *line_c = rec[i].top;
        else if (rec[i].kind == 'T' && notice && !strcmp(rec[i].text, notice))
            *notice_y = rec[i].y, *notice_x = rec[i].x, *notice_c = rec[i].top;
}

static void loader_notice(void)
{
    float r1, l1, n1, lx1, nx1, r2, l2, n2, lx2, nx2, r3, l3, n3, lx3, nx3;
    u32 lc1, nc1, lc2, nc2, lc3, nc3;
    loader_place("Design wird gelöscht ...", NULL, &r1, &l1, &n1, &lx1, &nx1, &lc1, &nc1);
    loader_place("Design wird gelöscht ...", "Nicht ausschalten", &r2, &l2, &n2, &lx2, &nx2, &lc2, &nc2);
    loader_place(NULL, "Nicht ausschalten", &r3, &l3, &n3, &lx3, &nx3, &lc3, &nc3);
    char what[300];
    snprintf(what, sizeof what, "G4-A the loader's notice: line at y %g, notice at y %g (%g under it), both centred "
             "(x %g, %g), same tone (%d); the ring %g higher than with the line alone; no notice without a line (%g)",
             l2, n2, n2 - l2, lx2, nx2, lc2 == nc2, r1 - r2, n3);
    check(l2 >= 0 && n2 >= 0 && fabsf(n2 - l2 - LOADER_LH) < 0.01f && fabsf(lx2 - BOTTOM_W / 2.0f) < 1 &&
          fabsf(nx2 - BOTTOM_W / 2.0f) < 1 && lc2 == nc2 && lc2 == lc1 && fabsf(r1 - r2 - LOADER_LH / 2.0f) <= 0.5f &&
          fabsf((l1 - r1) - (l2 - r2)) < 0.01f && n1 < 0 && n3 < 0 && l3 < 0, what);
}

/* ST-B: the start's black (ui_black) is the last thing drawn on every screen and eye, over a popup too: one rectangle
 * over the whole screen at its plane (the view reset, no texture), black at the strength told; a frame not told
 * draws none. */
static void black_cover(void)
{
    static const char *const paras[1] = {"Den Ordner /Themes auf der SD-Karte erstellen?"};
    ui_popup p;
    ui_popup_text(&p, paras, 1);
    int last[3] = {-1, -1, -1}, covers[3] = {0}, later = 0;
    for (int pass = 0; pass < 4; pass++) {
        watched = pass & 1 ? 2 : 3;     /* the top's two eyes, then the bottom */
        slider_now = 1;
        recs = 0;
        ui_begin(0.0f);
        if (pass < 2)
            ui_black(1);
        do {
            ui_top();
            ui_start();
            ui_foot(HERO_FOOT_DOTS_Y, ART_HOME, "2 Designs installiert", MUTED, 0, "v0.9.3");
            ui_bottom();
            ui_bar(UI_KEY_B, "Zurück", 0);
            ui_popup_draw(&p, "Abbrechen", "Erstellen", -1, -1, 1);
        } while (ui_next_eye());
        ui_end();
        slider_now = 0;
        for (int i = 0; i < recs; i++) {
            int e = rec[i].eye, w = e == 2 ? BOTTOM_W : TOP_W;
            int cover = rec[i].kind == 'R' && rec[i].x == 0 && rec[i].y == 0 && rec[i].w == w && rec[i].h == SCREEN_H &&
                        rec[i].top == C2D_Color32(0, 0, 0, 255) && rec[i].low == rec[i].top && rec[i].k == 1 &&
                        rec[i].tx == 0 && rec[i].ty == 0;
            if (pass < 2) {
                covers[e] += cover;
                if (cover)
                    last[e] = i == recs - 1 || rec[i + 1].eye != e;
            } else {
                later += cover;
            }
        }
    }
    watched = 2;
    char what[240];
    snprintf(what, sizeof what, "ST-B the start's black: one full black rectangle at the screen's plane on the top's "
             "left eye (%d, last %d), its right eye (%d, last %d) and the bottom over the popup (%d, last %d); none in a "
             "frame not told (%d)", covers[0], last[0], covers[1], last[1], covers[2], last[2], later);
    check(covers[0] == 1 && covers[1] == 1 && covers[2] == 1 && last[0] == 1 && last[1] == 1 && last[2] == 1 &&
          later == 0, what);
}

/* ---- R9, the vessel D5a "Glaskachel": the recorded top screen painted as the GPU draws it (nearest texels; citro2d's
 * tint per corner, its alpha times the image's and its colour blended in by its blend; straight alpha over what is
 * there), the images and rectangles only: not the texts, nor the drifting pattern, which moves from frame to frame. */
static float paint[SCREEN_H][TOP_W][3];

static void unpack_color(u32 c, float out[4])
{
    out[0] = c & 255, out[1] = c >> 8 & 255, out[2] = c >> 16 & 255, out[3] = (c >> 24) / 255.0f;
}

/* The atlas sprite an image of it was drawn from (the first whose place holds its top left texel: in the kit's atlas
 * the kit's, in the application's part's the part's), -1 for none. */
static int atlas_sprite(const typeof(rec[0]) *r)
{
    if (r->kind != 'I' || (!atlas_tex(r->tex) && !atlas_smooth_tex(r->tex)))
        return -1;
    const int part = r->tex == &part_atlas || r->tex == &part_atlas_smooth;
    float sx = r->u0 * r->tex->width + 0.01f, sy = (1 - r->v0) * r->tex->height + 0.01f;
    for (int k = part ? ART_COUNT : 0; k < (part ? ART_COUNT + ART_APP_COUNT : ART_COUNT); k++) {
        int id = k < ART_COUNT ? k : ART_ALL + k - ART_COUNT;
        const art_sprite *a = ui_sprite_info(id);
        if (sx >= a->x && sx < a->x + a->w && sy >= a->y && sy < a->y + a->h)
            return id;
    }
    return -1;
}

/* Where on the image recorded as r the screen's point px, py lies, as shares of its width and height (outside it: below
 * 0 or from 1 on): citro2d places its centre at its x, y and turns it about it by its angle (clockwise on the screen,
 * ui.c sprite_turned). */
static void rec_uv(const typeof(rec[0]) *r, float px, float py, float *u, float *v)
{
    float lx = (px - r->tx) / r->k - r->x, ly = (py - r->ty) / r->k - r->y, c = cosf(r->angle), n = sinf(r->angle);
    *u = (lx * c + ly * n + r->cx) / r->w, *v = (ly * c - lx * n + r->cy) / r->h;
}

/* The screen's box round the image recorded as r, turned as it is. */
static void rec_box(const typeof(rec[0]) *r, float *x0, float *y0, float *x1, float *y1)
{
    float c = cosf(r->angle), n = sinf(r->angle);
    *x0 = *y0 = 1e9f, *x1 = *y1 = -1e9f;
    for (int q = 0; q < 4; q++) {
        float u = (q & 1 ? r->w : 0) - r->cx, v = (q & 2 ? r->h : 0) - r->cy;
        float x = r->k * (r->x + u * c - v * n) + r->tx, y = r->k * (r->y + u * n + v * c) + r->ty;
        *x0 = fminf(*x0, x), *x1 = fmaxf(*x1, x), *y0 = fminf(*y0, y), *y1 = fmaxf(*y1, y);
    }
}

/* line 0: without the line's holders and pieces (they hang over the tile's top edge wherever the work stands) and the
 * liquid's light on the card under the tile (it follows the level) */
static void paint_top(int line)
{
    memset(paint, 0, sizeof paint);
    for (int i = 0; i < recs; i++) {
        const typeof(rec[0]) *r = &rec[i];
        if (r->kind == 'T' || (r->kind == 'I' && (r->tex == &pattern_tex || !r->tex || !r->tex->data)))
            continue;
        int id = atlas_sprite(r);
        if (!line && (id == ART_POUR_PIECE || id == ART_POUR_HOLDER || id == ART_POUR_TONGS || id == ART_POUR_FOOT))
            continue;
        float bx0, by0, bx1, by1;
        rec_box(r, &bx0, &by0, &bx1, &by1);
        int x0 = (int)ceilf(bx0 - 0.5f), x1 = (int)ceilf(bx1 - 0.5f), y0 = (int)ceilf(by0 - 0.5f);
        int y1 = (int)ceilf(by1 - 0.5f);
        for (int py = y0 < 0 ? 0 : y0; py < y1 && py < SCREEN_H; py++) {
            for (int px = x0 < 0 ? 0 : x0; px < x1 && px < TOP_W; px++) {
                float u, v, c[4];
                rec_uv(r, px + 0.5f, py + 0.5f, &u, &v);
                if (u < 0 || u >= 1 || v < 0 || v >= 1)
                    continue;
                if (r->kind == 'R') {
                    float a[4], b[4];
                    unpack_color(r->top, a), unpack_color(r->low, b);
                    for (int j = 0; j < 4; j++)
                        c[j] = a[j] + (b[j] - a[j]) * v;
                } else {
                    const C3D_Tex *t = r->tex;
                    int tw = t->width, th = t->height;
                    int sx = (int)floorf((r->u0 + (r->u1 - r->u0) * u) * tw);
                    int sy = (int)floorf((1.0f - (r->v0 + (r->v1 - r->v0) * v)) * th);
                    sx = (sx % tw + tw) % tw, sy = (sy % th + th) % th;
                    uint32_t texel = ((const uint32_t *)t->data)[((sy >> 3) * (tw >> 3) + (sx >> 3)) * 64 +
                                                                 ui_morton(sx & 7, sy & 7)];
                    c[0] = texel >> 24, c[1] = texel >> 16 & 255, c[2] = texel >> 8 & 255;
                    c[3] = (texel & 255) / 255.0f;
                    if (r->tinted) {
                        float k[4] = {(1 - u) * (1 - v), u * (1 - v), (1 - u) * v, u * v}, tint[4] = {0}, blend = 0;
                        for (int q = 0; q < 4; q++) {
                            float one[4];
                            unpack_color(r->tint.corners[q].color, one);
                            for (int j = 0; j < 4; j++)
                                tint[j] += k[q] * one[j];
                            blend += k[q] * r->tint.corners[q].blend;
                        }
                        for (int j = 0; j < 3; j++)
                            c[j] += (tint[j] - c[j]) * blend;
                        c[3] *= tint[3];
                    }
                }
                for (int j = 0; j < 3; j++)
                    paint[py][px][j] = c[j] * c[3] + paint[py][px][j] * (1 - c[3]);
            }
        }
    }
}

/* The signed distance of a point to a rounded box (negative inside). */
static float box_sd(float px, float py, float x, float y, float w, float h, float r)
{
    float qx = fabsf(px - x - w / 2) - w / 2 + r, qy = fabsf(py - y - h / 2) - h / 2 + r;
    return (qx > 0 && qy > 0 ? sqrtf(qx * qx + qy * qy) : fmaxf(qx, qy)) - r;
}

/* The signed distance to the inner shape under the number the tile's checks are run for: rounded, or with its corners
 * cut (expect.h, EXPECT_PRISM_POUR_CUT: the outline's legs; the inner shape's are (2 - sqrt 2) * POUR_RIM shorter). */
static float inner_sd(float px, float py)
{
    int cut = pour_look == LOOK_PRISM ? EXPECT_PRISM_POUR_CUT : 0;
    if (!cut)
        return box_sd(px, py, POUR_INNER_X, POUR_INNER_Y, POUR_INNER_W, POUR_INNER_H, POUR_INNER_R);
    float qx = fabsf(px - POUR_INNER_X - POUR_INNER_W / 2.0f) - POUR_INNER_W / 2.0f;
    float qy = fabsf(py - POUR_INNER_Y - POUR_INNER_H / 2.0f) - POUR_INNER_H / 2.0f;
    return fmaxf(fmaxf(qx, qy), (qx + qy + cut - 0.5857864f * POUR_RIM) * 0.70710678f);
}

static float paint_diff(float a[SCREEN_H][TOP_W][3], int x, int y)
{
    float d = 0;
    for (int j = 0; j < 3; j++)
        d = fmaxf(d, fabsf(a[y][x][j] - paint[y][x][j]));
    return d;
}

/* The vessel's numbers: a tile seen from the front (no depth drawn as a side), nearly square, rounded, at the sketch's
 * place (G2-L, the layout C: x 86 to 170, y 86 to 168 of the top screen since FB-L, 4 px higher), the picture in
 * the middle of it on both axes and the drops'
 * way through its middle, behind the picture (the owner, 2026-11-15); its form d "Nachbau" of the canvas (C8): the
 * corners' radius 19, the glass 2 px thick round the liquid, the surface's curve 4 px high over its lowest point and
 * the deeper tone under it fading over 11 rows. */
static void pour_numbers(void)
{
    int square = abs(POUR_TILE_W - POUR_TILE_H) * 10 <= POUR_TILE_W, frontal = 1;
#ifdef POUR_DEEP_X
    frontal = 0;
#endif
    int place = abs(POUR_TILE_X - 86) <= 2 && abs(POUR_TILE_Y - 86) <= 2 && abs(POUR_TILE_X + POUR_TILE_W - 170) <= 2 &&
                abs(POUR_TILE_Y + POUR_TILE_H - 168) <= 2 && POUR_TILE_R >= 8 && POUR_INNER_X > POUR_TILE_X &&
                POUR_INNER_X + POUR_INNER_W < POUR_TILE_X + POUR_TILE_W;
    int middle = 2 * POUR_PICTURE_X + POUR_PICTURE == 2 * POUR_TILE_X + POUR_TILE_W &&
                 2 * POUR_PICTURE_Y + POUR_PICTURE == 2 * POUR_TILE_Y + POUR_TILE_H;
    int way = 2 * POUR_MOUTH_X == 2 * POUR_TILE_X + POUR_TILE_W;
    int d = POUR_TILE_R == 19 && POUR_RIM == 2 && POUR_CURVE == 4 && POUR_UNDER == 11;
    char what[240];
    snprintf(what, sizeof what, "R9 the vessel: a tile %dx%d at %d,%d, radius %d, %s, nearly square, the picture in "
             "its middle, the drops' way through its middle; tile d: radius 19 (%d), glass 2 (%d), curve 4 (%d), under "
             "it 11 (%d)", POUR_TILE_W, POUR_TILE_H, POUR_TILE_X, POUR_TILE_Y, POUR_TILE_R,
             frontal ? "seen from the front" : "with a side", POUR_TILE_R, POUR_RIM, POUR_CURVE, POUR_UNDER);
    check(square && frontal && place && middle && way && d, what);
}

/* The tile's top edge closed (the owner: "gar kein schlitz"): in every scheme the front glass's rows from 2 px over
 * the tile down through its rim run smoothly along the straight part of the edge, the drops' way included: no texel
 * stands out from its two neighbours (the light along the top is brightest in the middle and fades to the corners);
 * and none is thin (the review's T3: a soft gap would pass the first): along the straight part every texel of the
 * rim's rows has at least POUR_RIM_LEAST of the alpha its row has at most there, and at least POUR_RIM_ALPHA of 255. */
#define POUR_RIM_LEAST 0.85f
#define POUR_RIM_ALPHA 120
static void pour_closed_top(void)
{
    const art_sprite *s = ui_sprite_info(ART_POUR_FRONT);
    int open = 0, thin = 0, low = 255;
    float least = 1;
    for (int k = 0; k < ART_SCHEMES; k++) {
        pour p;
        ui_look(pour_look, k, 1, PATTERN_HOUSES);
        pour_settle(&p, 0, 0, POUR_WORKING, 16);
        pour_draw(&p, 0);       /* the scheme's images unpacked */
        const int x0 = s->x + s->pad[0] + POUR_TILE_R + 1, x1 = s->x + s->pad[0] + POUR_TILE_W - POUR_TILE_R - 1;
        for (int y = s->y + s->pad[1] - 2; y < s->y + s->pad[1] + POUR_RIM; y++) {
            const uint32_t *t = part_atlas.data;
            for (int x = x0; x < x1; x++) {     /* each texel with both neighbours on the straight part */
                uint32_t a = t[((y >> 3) * (part_atlas.width >> 3) + ((x - 1) >> 3)) * 64 + ui_morton((x - 1) & 7, y & 7)];
                uint32_t b = t[((y >> 3) * (part_atlas.width >> 3) + (x >> 3)) * 64 + ui_morton(x & 7, y & 7)];
                uint32_t c = t[((y >> 3) * (part_atlas.width >> 3) + ((x + 1) >> 3)) * 64 + ui_morton((x + 1) & 7, y & 7)];
                for (int j = 0; j < 32; j += 8)
                    open += abs((int)(a >> j & 255) - 2 * (int)(b >> j & 255) + (int)(c >> j & 255)) > 3;
            }
        }
        for (int y = s->y + s->pad[1]; y < s->y + s->pad[1] + POUR_RIM; y++) {
            const uint32_t *t = part_atlas.data;
            int most = 0, row[POUR_TILE_W];
            for (int x = x0; x < x1; x++) {
                row[x - x0] = (int)(t[((y >> 3) * (part_atlas.width >> 3) + (x >> 3)) * 64 + ui_morton(x & 7, y & 7)] & 255);
                most = row[x - x0] > most ? row[x - x0] : most;
            }
            for (int x = x0; x < x1; x++) {
                float share = most ? (float)row[x - x0] / most : 0;
                least = share < least ? share : least;
                low = row[x - x0] < low ? row[x - x0] : low;
                thin += share < POUR_RIM_LEAST || row[x - x0] < POUR_RIM_ALPHA;
            }
        }
    }
    ui_look(pour_look, ART_SCHEME_WEISS, 1, PATTERN_HOUSES);
    char what[240];
    snprintf(what, sizeof what, "R9 the vessel's top edge closed in all %d schemes: %d texels of its rim standing out "
             "from their neighbours, %d thinner than %.2f of their row's most or %d of 255 (the least %.2f, %d)",
             ART_SCHEMES, open, thin, POUR_RIM_LEAST, POUR_RIM_ALPHA, least, low);
    check(open == 0 && thin == 0, what);
}

/* The vessel as it is, painted without the pieces in the air (they are not the liquid), against the empty vessel's
 * paint: pixels wholly outside the inner shape changed, pixels over their column's surface (the row its surface lies
 * in, pour_heights) changed, the most either changed by; whether the liquid shows under the surface. A pixel is wholly
 * outside when its middle lies more than 0.5 px outside, but a slanted edge (Prisma's cut) may still cover a corner of
 * it up to half its diagonal: a straight edge with the middle sd outside covers at most the corner triangle an edge at
 * 45 degrees cuts off, (0.7071 - sd)^2 of the pixel, so such a pixel may change by that share of the most a channel
 * can change (255) besides the 2 every pixel may (LQ: the strong liquid made those corners count; honest: 0.586 out,
 * 3.7 allowed, 2.1 seen). */
static float pour_corner(float sd)
{
    float c = sd < 0.70710678f ? 0.70710678f - sd : 0;
    return 255 * c * c;
}

static void pour_bounds(pour *p, int n, const float empty[SCREEN_H][TOP_W][3], int *outside, int *over, float *worst,
                        int *shown)
{
    float h[POUR_COLS];
    pour_heights(p, h);
    pour_draw(p, n);
    for (int i = 0; i < recs; i++)     /* the liquid's bounds: what falls and what melts on it left out */
        if (atlas_sprite(&rec[i]) == ART_POUR_PIECE_TURN || atlas_sprite(&rec[i]) == ART_POUR_MELT)
            rec[i].kind = 'T';
    paint_top(0);
    for (int y = POUR_TILE_Y - 4; y < POUR_TILE_Y + POUR_TILE_H + 4; y++) {
        for (int x = POUR_TILE_X - 4; x < POUR_TILE_X + POUR_TILE_W + 4; x++) {
            int col = x - POUR_INNER_X, in = col >= 0 && col < POUR_COLS;
            float top = in ? floorf(POUR_INNER_Y + POUR_INNER_H - fmaxf(h[col], 0)) : 0;
            float d = paint_diff((float (*)[TOP_W][3])empty, x, y), sd = inner_sd(x + 0.5f, y + 0.5f);
            int out = sd > 0.5f && d > 2 + pour_corner(sd), above = sd <= 0.5f && in && y < top && d > 2;
            if (out || above) {
                *outside += out, *over += above;
                *worst = fmaxf(*worst, d);
            }
            *shown += in && sd < -0.5f && y > top + 3 && d > 24;
        }
    }
}

/* No pixel of the liquid outside the tile's inner shape at eleven levels from empty to full, for the install and the
 * badge write: round the tile, every pixel wholly outside the shape is as in the empty vessel (the line's balls left
 * out: which wait over the tile depends on the level; and the liquid's light on the card), so is every pixel over its
 * column's surface, and under the surface the liquid shows. */
static void pour_inside_check(void)
{
    static float empty[SCREEN_H][TOP_W][3];
    ui_look(pour_look, ART_SCHEME_WEISS, 1, PATTERN_HOUSES);
    for (int n = 0; n < 2; n++) {
        pour p;
        pour_settle(&p, n, 0, POUR_WORKING, 4000);
        pour_draw(&p, n);
        paint_top(0);
        memcpy(empty, paint, sizeof empty);
        int outside = 0, over = 0, unseen = 0;
        float worst = 0;
        for (int k = 0; k <= 10; k++) {
            int shown = 0;
            pour_settle(&p, n, k / 10.0f, POUR_WORKING, POUR_SETTLE_MS);
            pour_bounds(&p, n, empty, &outside, &over, &worst, &shown);
            unseen += pour_level(&p) >= 0.08f && !shown;
        }
        char what[200];
        snprintf(what, sizeof what, "R9 the vessel, %s at 11 levels: %d pixels outside the inner shape and %d over "
                 "the surface changed (most %.0f), %d levels without the liquid showing",
                 n ? "the badge write" : "the install", outside, over, worst, unseen);
        check(outside == 0 && over == 0 && unseen == 0, what);
    }
}

/* The layers from the back (the owner: "it drops behind the glas so you see it still through the glass but once it's
 * behind the images it no longer visible"): the falling piece, the liquid over it (the shape, its surface, its light),
 * then the picture over them, then the front glass over the picture (tile d: the recess the picture sits in); and no
 * text directly after the picture's texture (judge_pour); for the install with a piece falling behind the picture and
 * full. */
static void pour_picture_order(void)
{
    ui_look(pour_look, ART_SCHEME_WEISS, 1, PATTERN_HOUSES);
    for (int m = 0; m < 2; m++) {
        pour p;
        pour_settle(&p, 0, m ? 1.0f : 0.5f, POUR_WORKING, POUR_SETTLE_MS);
        int falling = 0;
        for (uint64_t t = p.now; !m && !falling && t < p.now + 5000;) {    /* the next step: a piece half way down */
            pour_work(&p, t += 16, 0.75f, POUR_WORKING);
            for (int i = 0; i < p.gone; i++) {
                float down;
                int turn;
                falling |= pour_piece(&p, i, &down, &turn) == 1 && down > POUR_PICTURE_Y - POUR_PIECE_Y;
            }
        }
        pour_draw(&p, 0);
        int last = -1, pieces = 0, glass = -1, picture = -1, behind = 1, liquid = -1;
        for (int i = 0; i < recs; i++) {
            int id = atlas_sprite(&rec[i]);
            if (rec[i].kind == 'I' && rec[i].tex == &icon_tex && picture < 0)
                picture = i;
            if (id == ART_POUR_LIQUID || id == ART_POUR_SURFACE || id == ART_POUR_LIT || id == ART_POUR_PIECE_TURN ||
                id == ART_POUR_MELT)
                last = i, pieces += id == ART_POUR_PIECE_TURN;
            if (id == ART_POUR_LIQUID && liquid < 0)
                liquid = i;
            behind &= id != ART_POUR_PIECE_TURN || liquid < 0;
            if (id == ART_POUR_FRONT)
                glass = i;
        }
        char what[200];
        snprintf(what, sizeof what, "R9 the vessel %s: the piece falling, the liquid over it (%s), the last of them %d, "
                 "the picture over them (%d), the front glass over it (%d); %d pieces",
                 m ? "full" : "with a piece falling behind the picture", behind ? "yes" : "no", last, picture, glass,
                 pieces);
        check(last >= 0 && picture > last && glass > picture && behind && (m || (pieces > 0 && liquid >= 0)), what);
        judge_pour(m ? "the install full" : "the install with a piece falling");
    }
}

/* The surface stands still (C7: "es muss keine wellen schlagen"): at a level the work has reached, two frames a second
 * apart are the same in the tile's box, for the install and the badge write. */
static void pour_still(void)
{
    static float first[SCREEN_H][TOP_W][3];
    ui_look(pour_look, ART_SCHEME_WEISS, 1, PATTERN_HOUSES);
    for (int n = 0; n < 2; n++) {
        pour p;
        pour_settle(&p, n, 0.5f, POUR_WORKING, POUR_SETTLE_MS);
        pour_draw(&p, n);
        paint_top(0);
        memcpy(first, paint, sizeof first);
        for (uint64_t t = p.now, end = p.now + 1000; t < end;)
            pour_work(&p, t += 16, 0.5f, POUR_WORKING);
        pour_draw(&p, n);
        paint_top(0);
        int moved = 0;
        for (int y = POUR_TILE_Y; y < POUR_TILE_Y + POUR_TILE_H; y++)
            for (int x = POUR_TILE_X; x < POUR_TILE_X + POUR_TILE_W; x++)
                moved += paint_diff(first, x, y) > 0;
        char what[160];
        snprintf(what, sizeof what, "R9 the vessel's surface still, %s at half: %d pixels of the tile changed in a "
                 "second", n ? "the badge write" : "the install", moved);
        check(moved == 0, what);
    }
}

/* The liquid's draws in the last recorded frame: its column pieces and its surface's strips. */
static int pour_quads(void)
{
    int n = 0;
    for (int i = 0; i < recs; i++) {
        int id = atlas_sprite(&rec[i]);
        n += id == ART_POUR_LIQUID || id == ART_POUR_SURFACE;
    }
    return n;
}

/* The installing screen's top drawn as main.c draws a frame with the 3D slider up (both eyes, one pass each), both
 * eyes recorded; the surface's draws (the liquid's columns, the surface's strips, the lit liquid, what falls) of the
 * two passes compared, or with holders the line's (what hangs, the tongs, the clamps; SR-TD3b): how many differ in
 * anything but the eye's parallax shift (one shift for all of them), -1 when an eye drew none. */
static int pour_eyes_differ(const pour *p, int n, int holders)
{
    static int left[REC_MAX], right[REC_MAX];
    int nl = 0, nr = 0, differ = 0;
    slider_now = 1;
    recs = 0;
    watched = 3;
    ui_begin(1.0f);
    do {
        ui_top();
        if (n)
            ui_pour(p, "Badges installieren", "Installiert", "Tiere", NULL, ART_BADGES, badge_steps, "Erfolgreich installiert", NULL);
        else
            ui_pour(p, "Wird installiert", "Installiert", "Sommerwiese", stand_in, -1, four,
                    "Erfolgreich installiert", NULL);
        ui_bottom();
    } while (ui_next_eye());
    ui_end();
    watched = 2;
    slider_now = 0;
    for (int i = 0; i < recs; i++) {
        int id = atlas_sprite(&rec[i]);
        int line = id == ART_POUR_PIECE || id == ART_POUR_TONGS || id == ART_POUR_HOLDER;
        int surface = id == ART_POUR_LIQUID || id == ART_POUR_SURFACE || id == ART_POUR_LIT || id == ART_POUR_PIECE_TURN ||
                      id == ART_POUR_MELT;
        if (holders ? line : surface) {
            if (rec[i].eye)
                right[nr++] = i;
            else
                left[nl++] = i;
        }
    }
    if (!nl || nl != nr)
        return nl ? abs(nl - nr) : -1;
    float shift = rec[left[0]].tx - rec[right[0]].tx;
    for (int i = 0; i < nl; i++) {
        const typeof(rec[0]) *a = &rec[left[i]], *b = &rec[right[i]];
        differ += a->tex != b->tex || a->x != b->x || a->y != b->y || a->w != b->w || a->h != b->h || a->u0 != b->u0 ||
                  a->v0 != b->v0 || a->u1 != b->u1 || a->v1 != b->v1 || a->k != b->k || a->ty != b->ty ||
                  a->tinted != b->tinted || (a->tinted && memcmp(&a->tint, &b->tint, sizeof a->tint)) ||
                  a->cx != b->cx || a->cy != b->cy || a->angle != b->angle || a->tx - b->tx != shift;
    }
    return differ;
}

static int pour_same(const float a[SCREEN_H][TOP_W][3])
{
    int moved = 0;
    for (int y = POUR_TILE_Y; y < POUR_TILE_Y + POUR_TILE_H; y++)
        for (int x = POUR_TILE_X; x < POUR_TILE_X + POUR_TILE_W; x++)
            moved += paint_diff((float (*)[TOP_W][3])a, x, y) > 0;
    return moved == 0;
}

/* SR-TD2, the surface's answer (the canvas's "Antwort", C17): for the install and the badge write, drops landing in a
 * vessel at rest at eleven levels from empty to full (the work raised by an eleventh each time). From the first landing
 * the surface moves: 50 ms after it the tile differs from the same vessel with its surface at rest, and drawn with the
 * 3D slider up both eyes' passes draw the same surface but for the eye's shift (pour_eyes_differ, the review's T2); its
 * deflection (the wave's, the drift left out) stays within the reference's for a few drops in a row: 5 px at most, at
 * the walls 3 (one landing dips 4.5 at most and its swell climbs the walls by about 1.5 to 2.2, test_pour; the two or
 * three drops of an eleventh, 180 ms apart, add up as on the canvas); in frames every 32 ms through the swell no pixel
 * of liquid leaves the inner shape or shows over its column's surface; from POUR_REST_MS after the last landing the
 * vessel is at rest: the tile as in a vessel that came to the same level long ago, and a second later the same again.
 * The liquid's draws a frame stay at most two per column. */
static void pour_answer(void)
{
    static float empty[SCREEN_H][TOP_W][3], calm[SCREEN_H][TOP_W][3];
    ui_look(pour_look, ART_SCHEME_WEISS, 1, PATTERN_HOUSES);
    for (int n = 0; n < 2; n++) {
        pour p, q;
        pour_settle(&p, n, 0, POUR_WORKING, 4000);
        pour_draw(&p, n);
        paint_top(0);
        memcpy(empty, paint, sizeof empty);
        int outside = 0, over = 0, shown = 0, flat = 0, eyes = 0, restless = 0, quads = 0;
        float worst = 0, most = 0, walls = 0;
        for (int k = 0; k <= 10; k++) {
            pour_settle(&p, n, k / 11.0f, POUR_WORKING, POUR_SETTLE_MS);
            int landed = p.landed;
            /* a level whose only landing is the first piece on the dry floor (AN: the install's first eleventh) presses
             * no blow: the wave never moves while its pool spreads and the liquid rises, so its bound counts from
             * that touch */
            uint32_t blown = p.wave.last;
            while (p.landed == landed && p.now < 1000 + POUR_SETTLE_MS + POUR_TRAIL_MS)
                pour_work(&p, p.now + 4, (k + 1) / 11.0f, POUR_WORKING);
            if (p.landed == landed)     /* no ball in this eleventh */
                continue;
            uint64_t first = p.now, painted = 0;
            /* the pieces this level lets go (AN: the next group's first may still be on its way when the wave of
             * the piece before it is at rest) */
            pour_settle(&q, n, (k + 1) / 11.0f, POUR_WORKING, POUR_SETTLE_MS);
            int due = q.gone;
            for (; p.now < 1000 + POUR_SETTLE_MS + POUR_TRAIL_MS + 20000 &&
                   (p.wave.moving || p.landed < due ||
                    (p.wave.last == blown && p.now < pour_lands(&p, p.landed - 1) + POUR_REST_MS));) {
                float h[POUR_COLS], r[POUR_COLS];
                pour_wave still = p.wave;
                memset(still.w, 0, sizeof still.w);
                pour_heights(&p, h);
                pour_wave_heights(&still, pour_level(&p), (float)(p.now - 1000) / 1000.0f, r);
                for (int i = 0; i < POUR_COLS; i++)
                    most = fmaxf(most, fabsf(h[i] - r[i]));
                walls = fmaxf(walls, fmaxf(fabsf(h[0] - r[0]), fabsf(h[POUR_COLS - 1] - r[POUR_COLS - 1])));
                if (p.now >= painted + 32) {
                    painted = p.now;
                    pour_bounds(&p, n, empty, &outside, &over, &worst, &shown);
                    if (quads < pour_quads())
                        quads = pour_quads();
                }
                if (p.now >= first + 50 && p.now < first + 54) {
                    pour_draw(&p, n);
                    paint_top(0);
                    memcpy(calm, paint, sizeof calm);
                    eyes += pour_eyes_differ(&p, n, 0) != 0;
                    q = p;
                    q.wave = (pour_wave){.step = p.wave.step};
                    pour_draw(&q, n);
                    paint_top(0);
                    flat += pour_same(calm);
                }
                pour_work(&p, p.now + 4, (k + 1) / 11.0f, POUR_WORKING);
            }
            pour_work(&p, p.now + 16, (k + 1) / 11.0f, POUR_WORKING);
            pour_draw(&p, n);
            paint_top(0);
            memcpy(calm, paint, sizeof calm);
            pour_settle(&q, n, (k + 1) / 11.0f, POUR_WORKING, POUR_SETTLE_MS);
            pour_draw(&q, n);
            paint_top(0);
            uint64_t touched = p.wave.last != blown ? 1000 + (uint64_t)p.wave.last * 1000 / POUR_WAVE_HZ
                                                    : pour_lands(&p, p.landed - 1);
            restless += !pour_same(calm) || p.now > touched + POUR_REST_MS + 24;
            for (uint64_t t = p.now, end = p.now + 1000; t < end;)
                pour_work(&p, t += 16, (k + 1) / 11.0f, POUR_WORKING);
            pour_draw(&p, n);
            paint_top(0);
            restless += !pour_same(calm);
        }
        char what[320];
        snprintf(what, sizeof what, "SR-TD2 the surface's answer, %s at 11 levels: %d still after a landing, %d drawn "
                 "differently for the second eye, largest deflection %.2f px (walls %.2f; 5, 3), %d pixels outside the inner "
                 "shape and %d over the surface changed (most %.0f), %d not at rest after %d ms, at most %d draws of "
                 "the liquid", n ? "the badge write" : "the install", flat, eyes, most, walls, outside, over, worst,
                 restless, POUR_REST_MS, quads);
        check(!flat && !eyes && most > 0.5f && most <= 5.0f && walls <= 3.0f && !outside && !over && !restless &&
              quads <= 2 * POUR_COLS, what);
    }
}

/* The front glass's alpha (0 to 255) over the top screen's pixel x, y. */
static int front_alpha(int x, int y)
{
    const art_sprite *s = ui_sprite_info(ART_POUR_FRONT);
    int tx = s->x + s->pad[0] + x - POUR_TILE_X, ty = s->y + s->pad[1] + y - POUR_TILE_Y;
    return (int)(((const uint32_t *)part_atlas.data)[((ty >> 3) * (part_atlas.width >> 3) + (tx >> 3)) * 64 +
                                                ui_morton(tx & 7, ty & 7)] & 255);
}

/* SR-TD2, the recess round the picture follows the picture's own corners (the icons' mask: DETAIL_ICON_R per 48 px,
 * 5.5 px at 44), so no wedge of its shade lies on the picture's corners: in every scheme the front glass at each
 * corner, on the diagonal 1 px inside the picture's rounded corner, is within 24 of 255 of the same glass 1 px inside
 * the middle of the edge above or below it. In a look whose picture has cut corners (expect.h,
 * EXPECT_<NAME>_PICTURE_CUT) the same 1 px inside the middle of each cut. */
static void pour_recess(void)
{
    const float r = DETAIL_ICON_R * POUR_PICTURE / 48.0f, in = 1.0f;
    const float leg = expect_of(look_get(pour_look)->name)->picture_cut * POUR_PICTURE / 48.0f;
    int worst = 0;
    for (int k = 0; k < ART_SCHEMES; k++) {
        pour p;
        ui_look(pour_look, k, 1, PATTERN_HOUSES);
        pour_settle(&p, 0, 0, POUR_WORKING, 16);
        pour_draw(&p, 0);       /* the scheme's images unpacked */
        for (int c = 0; c < 4; c++) {
            int right = c & 1, low = c >> 1;
            float cx = right ? POUR_PICTURE_X + POUR_PICTURE - r : POUR_PICTURE_X + r;
            float cy = low ? POUR_PICTURE_Y + POUR_PICTURE - r : POUR_PICTURE_Y + r;
            float d = (r - in) * 0.70710678f;
            if (leg > 0)        /* from the corner's own corner: half the leg and the way in, both along the axes */
                cx = right ? POUR_PICTURE_X + POUR_PICTURE : POUR_PICTURE_X,
                cy = low ? POUR_PICTURE_Y + POUR_PICTURE : POUR_PICTURE_Y, d = -(leg / 2 + in * 0.70710678f);
            int x = (int)floorf(cx + (right ? d : -d)), y = (int)floorf(cy + (low ? d : -d));
            int ey = (int)floorf(low ? POUR_PICTURE_Y + POUR_PICTURE - in : POUR_PICTURE_Y + in);
            int diff = abs(front_alpha(x, y) - front_alpha(POUR_PICTURE_X + POUR_PICTURE / 2, ey));
            worst = diff > worst ? diff : worst;
        }
    }
    ui_look(pour_look, ART_SCHEME_WEISS, 1, PATTERN_HOUSES);
    char what[200];
    snprintf(what, sizeof what, "SR-TD2 the recess round the picture follows its corners (%s %.1f) in all %d "
             "schemes: the glass on the corners' diagonal differs from the edges' by %d of 255 at most (24)",
             leg > 0 ? "cut" : "radius", leg > 0 ? leg : r, ART_SCHEMES, worst);
    check(worst <= 24, what);
}

/* O9 (the emulator at c68dcd2: a pale oval on the top of a full tile, where a drop lands), with the landing
 * (SR-TD4a): the install's last step's pieces landing in a tile from three quarters to full, the work done, frames of
 * 8 ms. While a piece melts, the pixels its images change lie inside the inner shape; from POUR_MELT_MS after the last
 * landing, with no piece falling, nothing of a piece is drawn; and while the tile shows full (it glows) nothing falls or
 * melts in it: no piece lands or melts in a full tile. */
static void pour_full_landing(void)
{
    static float with[SCREEN_H][TOP_W][3];
    ui_look(pour_look, ART_SCHEME_WEISS, 1, PATTERN_HOUSES);
    pour p;
    pour_settle(&p, 0, 0.75f, POUR_WORKING, POUR_SETTLE_MS);
    int falling[POUR_BALLS] = {0}, melting = 0, changed = 0, outside = 0, after = 0, in_full = 0, full = 0;
    int landings = 0;
    uint64_t landed = 0;
    for (uint64_t end = p.now + 6000; p.now < end;) {
        pour_work(&p, p.now + 8, 1, POUR_SUCCEEDED);
        int air = 0;
        for (int i = 0; i < p.gone; i++) {
            float down;
            int turn, now_falling = pour_piece(&p, i, &down, &turn) == 1;
            if (falling[i] && !now_falling)
                landed = p.now, landings++;
            falling[i] = now_falling;
            air += now_falling;
        }
        pour_draw(&p, 0);
        int pieces = 0, melts = 0;
        for (int i = 0; i < recs; i++) {
            int id = atlas_sprite(&rec[i]);
            pieces += id == ART_POUR_PIECE_TURN || id == ART_POUR_MELT;
            melts += id == ART_POUR_MELT;
        }
        int shown_full = pour_full(&p) || pour_glow(&p) > 0;
        full += shown_full;
        in_full += shown_full && pieces;
        after += !air && landed && p.now >= landed + POUR_MELT_MS && pieces;
        if (!melts || p.now % 16)
            continue;
        melting++;
        paint_top(0);
        memcpy(with, paint, sizeof with);
        for (int i = 0; i < recs; i++)
            if (atlas_sprite(&rec[i]) == ART_POUR_MELT)
                rec[i].kind = 'T';
        paint_top(0);
        for (int y = POUR_TILE_Y; y < POUR_TILE_Y + POUR_TILE_H; y++)
            for (int x = POUR_TILE_X; x < POUR_TILE_X + POUR_TILE_W; x++)
                if (paint_diff(with, x, y) > 2)
                    changed++, outside += inner_sd(x + 0.5f, y + 0.5f) >= 0;
    }
    char what[300];
    snprintf(what, sizeof what, "O9 pieces landing in a tile from three quarters to full: %d landings, %d frames "
             "melting changing %d pixels of the tile, %d of them outside the inner shape; %d frames drawing a piece "
             "POUR_MELT_MS after the last landing, %d of %d frames showing full drawing one", landings, melting,
             changed, outside, after, in_full, full);
    check(landings == POUR_INSTALL.balls[3] && melting > 0 && changed > 0 && !outside && !after && full > 0 && !in_full, what);
}

/* SR-TD3a, the pieces (the canvas's glass octahedron b "Gedreht", C18) in place of the balls and the drops: the
 * install and the badge write in every scheme, frames of 16 ms from the start to the end. The line hangs pieces: one
 * POUR_PIECE at its size under each holder that still carries one, at its place (its middle on the holder's x and
 * POUR_PIECE_Y), and no other image there; a falling piece is drawn as itself: one frame of POUR_PIECE_TURN at its
 * size (not stretched), the model's turn frame at the model's place on whole pixels on the tiles' middle, and these
 * frames in order over its fall (in the first scheme, drawn every frame, from frame 0 or 1 one at a time; in the
 * others, drawn every 64 ms, never back), drawn right after the tile's back (behind the liquid, the picture and the glass).
 * In its first POUR_LEAVE_MS (AN-3) it is turned about the joint over it by its holder's angle at the let-go as it goes
 * to nothing (ease_both), read smoothed, its place upright as before. */
static void pour_pieces_drawn(void)
{
    const art_sprite *turns = ui_sprite_info(ART_POUR_PIECE_TURN);
    int hanging = 0, misplaced = 0, falls = 0, wrong = 0, other = 0, order = 0, frames = 0, seen = 0;
    for (int n = 0; n < 2; n++) {
        for (int k = 0; k < ART_SCHEMES; k++) {
            ui_look(pour_look, k, 1, PATTERN_HOUSES);
            pour p;
            int last[POUR_BALLS];
            for (int i = 0; i < POUR_BALLS; i++)
                last[i] = -1;
            pour_start(&p, n ? &POUR_BADGES : &POUR_INSTALL, 1000);
            for (unsigned t = 16; t <= 14000; t += 16) {
                pour_work(&p, 1000 + t, t < 9000 ? t / 9000.0f : 1, t < 9500 ? POUR_WORKING : POUR_SUCCEEDED);
                if (k && t % 64)        /* every scheme, the first every frame */
                    continue;
                pour_draw(&p, n);
                frames++;
                int carried = 0, drawn = 0, falling = 0, turn_draws = 0, back = -1;
                float want_x[POUR_BALLS];
                for (int i = 0; i < p.total; i++) {
                    float x;
                    int ball;
                    if (pour_holder(&p, i, &x, &ball) && ball)
                        want_x[carried++] = roundf(x - POUR_PIECE_W / 2.0f);
                }
                for (int i = 0; i < recs; i++) {
                    int id = atlas_sprite(&rec[i]);
                    if (id == ART_POUR_BACK)
                        back = i;
                    if (id == ART_POUR_PIECE) {
                        int at = 0;
                        for (int j = 0; j < carried; j++)       /* where it hangs upright (SR-TD3b: it swings) */
                            at |= rec[i].x - rec[i].cx == want_x[j];
                        misplaced += !at || rec[i].y - rec[i].cy != POUR_PIECE_Y - POUR_PIECE_H / 2 + 1 ||
                                     rec[i].w != POUR_PIECE_W || rec[i].h != POUR_PIECE_H;
                        drawn++;
                    }
                    if (id == ART_POUR_PIECE_TURN)     /* right after the back or another piece */
                        other += back < 0 || (i != back + 1 && atlas_sprite(&rec[i - 1]) != ART_POUR_PIECE_TURN);
                }
                hanging += drawn;
                misplaced += drawn != carried;
                for (int i = 0, r = 0; i < p.gone; i++) {
                    float down;
                    int turn;
                    if (!pour_piece(&p, i, &down, &turn))
                        continue;
                    falling++;
                    if (k)              /* every 64 ms: never back */
                        order += last[i] >= 0 && turn < last[i];
                    else
                        order += last[i] < 0 ? turn > 1 : turn != last[i] && turn != last[i] + 1;
                    last[i] = turn;
                    while (r < recs && atlas_sprite(&rec[r]) != ART_POUR_PIECE_TURN)
                        r++;
                    if (r == recs) {
                        wrong++;
                        continue;
                    }
                    int frame = (int)floorf(rec[r].u0 * rec[r].tex->width + 0.01f) - turns->x;
                    float y = roundf(POUR_PIECE_Y + down) - (POUR_PIECE_H / 2 - 1);
                    float angle = -p.let_angle[i] * (1 - ease_both(ease_span(p.now, p.let_go[i], POUR_LEAVE_MS)));
                    wrong += frame != turn * POUR_PIECE_W || rec[r].w != POUR_PIECE_W || rec[r].h != POUR_PIECE_H ||
                             rec[r].angle != angle ||
                             (angle != 0 ? !atlas_smooth_tex(rec[r].tex) || rec[r].x != POUR_MOUTH_X ||
                                           rec[r].x - rec[r].cx != POUR_MOUTH_X - POUR_PIECE_W / 2 ||
                                           rec[r].y - rec[r].cy != y
                                         : !atlas_tex(rec[r].tex) || rec[r].x != POUR_MOUTH_X - POUR_PIECE_W / 2 ||
                                           rec[r].y != y);
                    seen = turn > seen ? turn : seen;
                    r++;
                }
                for (int i = 0; i < recs; i++)
                    turn_draws += atlas_sprite(&rec[i]) == ART_POUR_PIECE_TURN;
                falls += falling;
                wrong += turn_draws != falling;
            }
        }
    }
    ui_look(pour_look, ART_SCHEME_WEISS, 1, PATTERN_HOUSES);
    char what[320];
    snprintf(what, sizeof what, "SR-TD3a the pieces over %d frames in 8 schemes: %d hanging, %d of them or their count "
             "off; %d falling, %d drawn otherwise than as the model's turn frame at its place, turn frames out of order "
             "%d (up to frame %d); %d not right after the tile's back", frames, hanging,
             misplaced, falls, wrong, order, seen, other);
    check(hanging > 0 && !misplaced && falls > 0 && !wrong && !order && seen >= 6 && !other, what);
}

/* Whether the image recorded as r is faded to a (a tint of black with blend 0 and alpha a at every corner, to a
 * 255th). */
static int faded_to(const typeof(rec[0]) *r, float a)
{
    int ok = r->tinted;
    for (int q = 0; q < 4 && ok; q++)
        ok = (r->tint.corners[q].color & 0xFFFFFF) == 0 && r->tint.corners[q].blend == 0 &&
             fabsf((float)(r->tint.corners[q].color >> 24) - 255 * a) <= 1;
    return ok;
}

/* SR-TD4a, the landing (the canvas's impact z2 "Schwer", C24, C31): the install and the badge write in every scheme,
 * frames of 16 ms from the start to the end (the first scheme every frame, the others every 64 ms). From the frame a
 * piece stops falling it is drawn melting, until POUR_MELT_MS after its landing and never after: one POUR_MELT draw
 * a melting piece, at its size, the clock's frame ((now - landing) * POUR_MELT_FRAMES / POUR_MELT_MS: from 0, one by
 * one, none left out) on whole pixels with the place of the touch on the tile's middle, its foot on its ground (the
 * floor for the first piece), cut to the inner shape's rows, faded by its alpha (pour_melt); these right after the
 * falling pieces (or the tile's back) and right before the liquid. Each melting on liquid shows under the surface
 * through it: right after the liquid (and its light) and before the picture, POUR_MELT draws faded by 0.4 of its alpha
 * (the canvas's GHOST) covering exactly its frame's rows under each column's surface row (the same rows of the same
 * frame); no other POUR_MELT draw. Every pixel of every POUR_MELT draw lies inside the inner shape. A frame with a melt
 * keeps the drawing rule, and drawn with the 3D slider up both eyes draw the same. The most draws on the top's left eye
 * in a frame, the liquid's (pour_quads) and the landings' are stated. */
static void pour_melt_drawn(void)
{
    const art_sprite *cells = ui_sprite_info(ART_POUR_MELT);
    const int x0 = POUR_MOUTH_X - POUR_MELT_W / 2, floor_y = POUR_INNER_Y + POUR_INNER_H;
    int frames = 0, melts = 0, ghosts = 0, wrong = 0, late = 0, order = 0, unseen = 0, outside = 0, cut = 0, dry = 0;
    int place = 0, eyes = 0, eyed = 0, most = 0, quads = 0, landing = 0, judged = 0;
    for (int n = 0; n < 2; n++) {
        for (int s = 0; s < ART_SCHEMES; s++) {
            ui_look(pour_look, s, 1, PATTERN_HOUSES);
            pour p;
            int last[POUR_BALLS], seen[POUR_BALLS] = {0};
            for (int i = 0; i < POUR_BALLS; i++)
                last[i] = -1;
            pour_start(&p, n ? &POUR_BADGES : &POUR_INSTALL, 1000);
            for (unsigned t = 16; t <= 14000; t += 16) {
                pour_work(&p, 1000 + t, t < 9000 ? t / 9000.0f : 1, t < 9500 ? POUR_WORKING : POUR_SUCCEEDED);
                if (s && t % 64)
                    continue;
                pour_draw(&p, n);
                frames++;
                most = recs > most ? recs : most;
                quads = pour_quads() > quads ? pour_quads() : quads;
                float h[POUR_COLS];
                pour_heights(&p, h);
                int m[POUR_BALLS], k[POUR_BALLS], piece[POUR_BALLS], nm = 0;
                float g[POUR_BALLS], a[POUR_BALLS];
                for (int i = 0; i < p.gone; i++) {
                    float down;
                    int turn, j = nm;
                    uint64_t land = pour_lands(&p, i);
                    int on = pour_piece(&p, i, &down, &turn) != 1 && p.now >= land && p.now < land + POUR_MELT_MS;
                    if (!(m[j] = pour_melt(&p, i, &k[j], &g[j], &a[j]))) {
                        late += on;
                        if (!s && last[i] >= 0 && last[i] < POUR_MELT_FRAMES)
                            order += last[i] != POUR_MELT_FRAMES - 1, last[i] = POUR_MELT_FRAMES;
                        continue;
                    }
                    late += !on || k[j] != (int)((p.now - land) * POUR_MELT_FRAMES / POUR_MELT_MS);
                    dry += (i == 0) != (m[j] == 1);
                    if (!s) {
                        order += last[i] == POUR_MELT_FRAMES || (last[i] < 0 ? k[j] != 0 :
                                                                 k[j] != last[i] && k[j] != last[i] + 1);
                        last[i] = k[j], seen[i] |= 1 << k[j];
                    }
                    piece[nm++] = i;
                }
                int back = -1, turns = -1, liquid = -1, lit = -1, front = -1, all = 0, drawn = 0;
                for (int r = 0; r < recs; r++) {
                    int id = atlas_sprite(&rec[r]);
                    back = id == ART_POUR_BACK ? r : back;
                    turns = id == ART_POUR_PIECE_TURN ? r : turns;
                    liquid = (id == ART_POUR_LIQUID || id == ART_POUR_SURFACE) && liquid < 0 ? r : liquid;
                    lit = id == ART_POUR_LIQUID || id == ART_POUR_SURFACE || id == ART_POUR_LIT ? r : lit;
                    front = id == ART_POUR_FRONT ? r : front;
                    all += id == ART_POUR_MELT;
                    if (id == ART_POUR_MELT) {
                        float bx0, by0, bx1, by1;
                        rec_box(&rec[r], &bx0, &by0, &bx1, &by1);
                        for (float y = by0 + 0.5f; y < by1; y++)
                            for (float x = bx0 + 0.5f; x < bx1; x++)
                                outside += inner_sd(x, y) >= 0;
                    }
                }
                /* the melting pieces' frames, in order, right after the falling pieces */
                int r = (turns >= 0 ? turns : back) + 1, top[POUR_BALLS], foot[POUR_BALLS];
                for (int j = 0; j < nm; j++, r++) {
                    foot[j] = (int)roundf(floor_y - g[j]);
                    int y = foot[j] - POUR_MELT_H;
                    top[j] = y < POUR_INNER_Y ? POUR_INNER_Y : y;
                    cut += top[j] > y;
                    if (r >= recs || atlas_sprite(&rec[r]) != ART_POUR_MELT) {
                        wrong++;
                        continue;
                    }
                    const typeof(rec[0]) *d = &rec[r];
                    int sx = (int)floorf(d->u0 * d->tex->width + 0.01f) - cells->x;
                    int sy = (int)floorf((1 - d->v0) * d->tex->height + 0.01f) - cells->y;
                    wrong += d->x != x0 || d->y != top[j] || d->w != POUR_MELT_W || d->h != foot[j] - top[j] ||
                             d->k != 1 || d->angle != 0 || sx != k[j] % POUR_MELT_COLS * POUR_MELT_W ||
                             sy != k[j] / POUR_MELT_COLS * POUR_MELT_H + top[j] - y ||
                             !faded_to(d, a[j]);
                    place += piece[j] == 0 && foot[j] != floor_y;
                    drawn++;
                }
                order += r < recs && atlas_sprite(&rec[r]) == ART_POUR_MELT;
                order += liquid >= 0 && r != liquid;
                /* what of them lies under the surface, right after the liquid */
                static int covered[POUR_BALLS][POUR_MELT_W];
                memset(covered, 0, sizeof covered);
                int r1 = lit + 1;
                for (r = r1; lit >= 0 && r < recs && atlas_sprite(&rec[r]) == ART_POUR_MELT; r++) {
                    const typeof(rec[0]) *d = &rec[r];
                    int sx = (int)floorf(d->u0 * d->tex->width + 0.01f) - cells->x;
                    int sy = (int)floorf((1 - d->v0) * d->tex->height + 0.01f) - cells->y, j = 0, c = sx % POUR_MELT_W;
                    while (j < nm && !(m[j] == 2 && k[j] == sy / POUR_MELT_H * POUR_MELT_COLS + sx / POUR_MELT_W))
                        j++;
                    ghosts++;
                    if (j == nm || d->x != x0 + c || d->w < 1 || c + d->w > POUR_MELT_W || d->k != 1 ||
                        sy % POUR_MELT_H != d->y - (foot[j] - POUR_MELT_H) || !faded_to(d, 0.4f * a[j])) {
                        wrong++;
                        continue;
                    }
                    for (int q = c; q < c + d->w; q++) {
                        int under = (int)floorf(floor_y - fmaxf(h[x0 + q - POUR_INNER_X], 0)) + 1;
                        under = under < top[j] ? top[j] : under;
                        wrong += covered[j][q]++ || d->y != under || d->y + d->h != foot[j];
                    }
                }
                for (int j = 0; j < nm; j++)
                    for (int q = 0; q < POUR_MELT_W && m[j] == 2; q++) {
                        int under = (int)floorf(floor_y - fmaxf(h[x0 + q - POUR_INNER_X], 0)) + 1;
                        wrong += !covered[j][q] && (under < top[j] ? top[j] : under) < foot[j];
                    }
                order += r > r1 && r >= front;
                wrong += all != drawn + (r - r1);
                melts += drawn;
                landing = all > landing ? all : landing;
                if (!s && nm && r > r1 && judged == n) {
                    judge_pour(n ? "the badge write with a piece melting on liquid" :
                               "the install with a piece melting on liquid");
                    judged++;
                }
                if (!s && nm) {
                    eyed++;
                    eyes += pour_eyes_differ(&p, n, 0) != 0;
                }
            }
            for (int i = 0; !s && i < p.total; i++)
                unseen += seen[i] != (1 << POUR_MELT_FRAMES) - 1 || last[i] != POUR_MELT_FRAMES;
        }
    }
    ui_look(pour_look, ART_SCHEME_WEISS, 1, PATTERN_HOUSES);
    char what[640];
    snprintf(what, sizeof what, "SR-TD4a the landing over %d frames in 8 schemes: %d melting draws and %d under the "
             "surface, %d of them drawn otherwise, %d at a time the clock does not say, %d out of order or place in the "
             "layers, %d pieces with a frame left out, %d pixels outside the inner shape, %d cut at its top, %d of the "
             "wrong ground kind, %d first pieces off the floor; with the 3D slider up %d of %d frames differing between "
             "the eyes; at most %d draws on the top's left eye a frame, the liquid's %d, the landings' %d",
             frames, melts, ghosts, wrong, late, order, unseen, outside, cut, dry, place, eyes, eyed, most, quads, landing);
    check(melts > 0 && ghosts > 0 && cut > 0 && !wrong && !late && !order && !unseen && !outside && !dry && !place &&
          eyed > 0 && !eyes && judged == 2, what);
}

/* The holder of the line's draw r (the piece it carries, its tongs or its clamp): the index of the holder on the
 * screen whose joint is where r hangs from, upright, -1 for none; *jx the joint's x. */
static int holder_of(const pour *p, const typeof(rec[0]) *r, int id, float *jx)
{
    float ux = r->x - r->cx;
    *jx = id == ART_POUR_HOLDER ? ux + POUR_HOLDER_W / 2 : ux + POUR_TONGS_W / 2;
    for (int i = 0; i < p->total; i++) {
        float x;
        int ball;
        if (pour_holder(p, i, &x, &ball) && roundf(x - POUR_TONGS_W / 2.0f) + POUR_TONGS_W / 2 == *jx)
            return i;
    }
    return -1;
}

/* SR-TD3c (SR-TD3b's holder B "Zange" and its swing, C19, C24): on the install and the badge write in Weiss and Nacht,
 * every frame of the work: each holder on the screen drawn once as its piece (while it carries it), its tongs in the
 * model's pose and its clamp, in that order; the piece and the tongs hang from the joint (POUR_JOINT_Y, under the
 * clamp's middle) turned as one body by the model's angle (citro2d's, clockwise: the model's negated), the piece where
 * it hangs upright and the tongs' cell its pose's; turned they are read smoothed with the joint as their centre, at
 * angle 0 sharp from the atlas on whole pixels; the clamp never turns. Swinging frames and every pose seen. */
static void pour_holders_drawn(void)
{
    const art_sprite *tongs = ui_sprite_info(ART_POUR_TONGS);
    int frames = 0, swinging = 0, turned = 0, upright = 0, wrong = 0, order = 0, count = 0, poses = 0;
    static const int LOOKS[2] = {ART_SCHEME_WEISS, ART_SCHEME_NACHT};
    for (int n = 0; n < 2; n++) {
        for (int k = 0; k < 2; k++) {
            ui_look(pour_look, LOOKS[k], 1, PATTERN_HOUSES);
            pour p;
            pour_start(&p, n ? &POUR_BADGES : &POUR_INSTALL, 1000);
            for (unsigned t = 16; t <= 14000; t += 16) {
                pour_work(&p, 1000 + t, t < 9000 ? t / 9000.0f : 1, t < 9500 ? POUR_WORKING : POUR_SUCCEEDED);
                pour_draw(&p, n);
                frames++;
                int at[POUR_BALLS][3], swung = 0;
                memset(at, -1, sizeof at);
                for (int i = 0; i < recs; i++) {
                    int id = atlas_sprite(&rec[i]), part = id == ART_POUR_PIECE ? 0 : id == ART_POUR_TONGS ? 1
                                                         : id == ART_POUR_HOLDER ? 2 : -1;
                    if (part < 0)
                        continue;
                    const typeof(rec[0]) *r = &rec[i];
                    float jx;
                    int h = holder_of(&p, r, id, &jx);
                    if (h < 0 || at[h][part] >= 0) {
                        count++;
                        continue;
                    }
                    at[h][part] = i;
                    float angle = part == 2 ? 0 : -pour_holder_angle(&p, h), x;
                    int ball;
                    pour_holder(&p, h, &x, &ball);
                    int bad = r->angle != angle;
                    if (angle != 0)
                        bad |= !atlas_smooth_tex(r->tex) || r->x != jx || r->y != POUR_JOINT_Y;
                    else
                        bad |= !atlas_tex(r->tex) || r->cx != 0 || r->cy != 0 || r->x != floorf(r->x) ||
                               r->y != floorf(r->y);
                    if (part == 0)
                        bad |= !ball || r->y - r->cy != POUR_PIECE_Y - POUR_PIECE_H / 2 + 1 || r->w != POUR_PIECE_W ||
                               r->h != POUR_PIECE_H;
                    if (part == 1) {
                        int cell = (int)floorf(r->u0 * r->tex->width + 0.01f) - tongs->x;
                        int top = (int)floorf((1 - r->v0) * r->tex->height + 0.01f) - tongs->y;
                        bad |= cell != pour_holder_pose(&p, h) * POUR_TONGS_W || top != 0 ||
                               r->y - r->cy != POUR_TONGS_Y || r->w != POUR_TONGS_W || r->h != POUR_TONGS_H;
                        poses |= 1 << pour_holder_pose(&p, h);
                    }
                    if (part == 2)
                        bad |= r->x != jx - POUR_HOLDER_W / 2 || r->y != POUR_HOLDER_Y;
                    wrong += bad;
                    turned += angle != 0, upright += angle == 0 && part < 2;
                    swung |= angle != 0;
                }
                for (int i = 0; i < p.total; i++) {
                    float x;
                    int ball;
                    if (!pour_holder(&p, i, &x, &ball)) {
                        count += at[i][0] >= 0 || at[i][1] >= 0 || at[i][2] >= 0;
                        continue;
                    }
                    count += (at[i][0] >= 0) != ball || at[i][1] < 0 || at[i][2] < 0;
                    order += (ball && at[i][0] > at[i][1]) || at[i][1] > at[i][2];
                }
                swinging += swung;
            }
        }
    }
    ui_look(pour_look, ART_SCHEME_WEISS, 1, PATTERN_HOUSES);
    char what[320];
    snprintf(what, sizeof what, "SR-TD3c the holders over %d frames (Weiss, Nacht), %d of them swinging: %d draws turned "
             "and %d upright, %d of them off the model (pose, angle, joint, texture, place), %d holders drawn not once "
             "each, %d not piece, tongs, clamp in order; poses seen %#x", frames, swinging, turned, upright, wrong,
             count, order, poses);
    check(swinging > 50 && turned > 0 && upright > 0 && !wrong && !count && !order &&
          poses == (1 << POUR_POSES) - 1, what);
}

/* Where the middle of a piece drawn as r lies (its sprite's middle 1 px over the sprite's, POUR_PIECE_Y) and its
 * angle (degrees, citro2d's), as citro2d turns it about the point it places at r's x, y. */
static void piece_pose(const typeof(rec[0]) *r, float *mx, float *my, float *deg)
{
    float u = POUR_PIECE_W / 2.0f - r->cx, v = POUR_PIECE_H / 2.0f - 1 - r->cy;
    float c = cosf(r->angle), s = sinf(r->angle);
    *mx = r->x + u * c - v * s, *my = r->y + u * s + v * c, *deg = r->angle * 57.29578f;
}

/* FNV-1a over the eight bytes of v */
static uint64_t fnv8(uint64_t h, uint64_t v)
{
    for (int b = 0; b < 8; b++)
        h = (h ^ (v >> (8 * b) & 255)) * 0x100000001B3ull;
    return h;
}

/* AN-3, the piece leaves its holder in the pose the holder holds it in (the review of the joined line: let go while
 * the holder still swings it, it was drawn upright at once, about 2 px aside and 9 degrees turned in one frame): on
 * the install and the badge write, the work reported evenly over 9 s and at once, frames of 16 ms: each piece's middle
 * and angle in its first falling frame against where its holder drew it the frame before, at most 1.5 px apart (the
 * line's way in a frame, 1.4 px at 88 px/s, and the fall's first 0.1 px; whole pixels upright) and 1.5 degrees (what
 * a holder's swing turns in a frame, about 1); some let-gos with the holder turned 2 degrees or more (else nothing of
 * this is met); from POUR_LEAVE_MS after the let-go every falling piece upright. When the pieces land and the ground
 * of each melt's first frame are as at 71cc030 (the same fingerprint computed with that commit's pour.c:
 * 52 landings, 386dd8d5df1937f7): the pose is the drawing's alone. */
static void pour_leave(void)
{
    int lets = 0, turned = 0, apart = 0, late = 0, lands = 0;
    float most_px = 0, most_deg = 0;
    uint64_t h = 0xCBF29CE484222325ull;
    for (int n = 0; n < 2; n++)
        for (int w = 0; w < 2; w++) {
            pour p;
            int seen[POUR_BALLS] = {0}, fell[POUR_BALLS] = {0};
            float was[POUR_BALLS][3] = {{0}};
            int had[POUR_BALLS] = {0};
            pour_start(&p, n ? &POUR_BADGES : &POUR_INSTALL, 1000);
            for (unsigned t = 16; t <= 20000; t += 16) {
                pour_work(&p, 1000 + t, w || t >= 9000 ? 1 : t / 9000.0f, t < 9500 ? POUR_WORKING : POUR_SUCCEEDED);
                for (int i = 0; i < p.gone; i++) {
                    int frame;
                    float ground, alpha;
                    if (!seen[i] && pour_melt(&p, i, &frame, &ground, &alpha)) {
                        uint32_t g;
                        memcpy(&g, &ground, sizeof g);
                        h = fnv8(fnv8(fnv8(h, pour_lands(&p, i)), (uint64_t)frame), g);
                        seen[i] = 1;
                        lands++;
                    }
                }
                pour_draw(&p, n);
                int now_had[POUR_BALLS] = {0};
                float now[POUR_BALLS][3] = {{0}};
                for (int i = 0; i < recs; i++) {
                    float jx;
                    int k = atlas_sprite(&rec[i]) == ART_POUR_PIECE ? holder_of(&p, &rec[i], ART_POUR_PIECE, &jx) : -1;
                    if (k >= 0 && k < POUR_BALLS) {
                        piece_pose(&rec[i], &now[k][0], &now[k][1], &now[k][2]);
                        now_had[k] = 1;
                    }
                }
                for (int i = 0, r = 0; i < p.gone; i++) {
                    float down;
                    int turn;
                    if (!pour_piece(&p, i, &down, &turn))
                        continue;
                    while (r < recs && atlas_sprite(&rec[r]) != ART_POUR_PIECE_TURN)
                        r++;
                    if (r == recs)
                        break;
                    float mx, my, deg;
                    piece_pose(&rec[r++], &mx, &my, &deg);
                    late += p.now >= p.let_go[i] + POUR_LEAVE_MS && deg != 0;
                    if (fell[i]++ || !had[i])
                        continue;
                    float px = hypotf(mx - was[i][0], my - was[i][1]), dd = fabsf(deg - was[i][2]);
                    lets++;
                    turned += fabsf(was[i][2]) >= 2;
                    apart += px > 1.5f || dd > 1.5f;
                    most_px = fmaxf(most_px, px), most_deg = fmaxf(most_deg, dd);
                }
                memcpy(was, now, sizeof was);
                memcpy(had, now_had, sizeof had);
            }
        }
    char what[400];
    snprintf(what, sizeof what, "AN-3 a piece leaves its holder in its pose: %d let-gos (%d with the holder turned 2 "
             "degrees or more), %d off the holder's pose the frame before by more than 1.5 px or 1.5 degrees (most "
             "%.2f px, %.2f degrees), %d falling frames turned POUR_LEAVE_MS after the let-go; %d landings, their "
             "fingerprint %016llx (71cc030: 52, 386dd8d5df1937f7)", lets, turned, apart, most_px, most_deg, late,
             lands, (unsigned long long)h);
    check(lets == lands && turned > 0 && !apart && !late && lands == 52 && h == 0x386dd8d5df1937f7ull, what);
}

/* SR-TD3c: the line at rest (the work half done long ago): nothing of it turned or read smoothed, the line's rows the
 * same a second later, the eyes' passes the same but for their shift; on frames where it swings (the work raised
 * again) the drawing rule holds and both eyes draw what hangs the same but for their shift (the 3D slider up). */
static void pour_holders_still(void)
{
    static float first[SCREEN_H][TOP_W][3];
    for (int n = 0; n < 2; n++) {
        pour p;
        pour_settle(&p, n, 0.5f, POUR_WORKING, POUR_SETTLE_MS);
        pour_draw(&p, n);
        int smooth = 0, line = 0;
        for (int i = 0; i < recs; i++) {
            int id = atlas_sprite(&rec[i]);
            if (id == ART_POUR_PIECE || id == ART_POUR_TONGS || id == ART_POUR_HOLDER)
                line++, smooth += !atlas_tex(rec[i].tex) || rec[i].angle != 0;
        }
        paint_top(1);
        memcpy(first, paint, sizeof first);
        for (uint64_t t = p.now, end = p.now + 1000; t < end;)
            pour_work(&p, t += 16, 0.5f, POUR_WORKING);
        pour_draw(&p, n);
        paint_top(1);
        int moved = 0;
        for (int y = POUR_HOLDER_Y - 4; y < POUR_PIECE_Y + POUR_PIECE_H; y++)
            for (int x = 0; x < TOP_W; x++)
                moved += paint_diff(first, x, y) > 0;
        int eyes = pour_eyes_differ(&p, n, 1);
        char what[200];
        snprintf(what, sizeof what, "SR-TD3c %s at rest: %d of the line's %d draws turned or smoothed, %d pixels of "
                 "the line's rows changed in a second, %d draws differing between the eyes", n ? "the badge write" :
                 "the install", smooth, line, moved, eyes);
        check(!p.swinging && line > 0 && !smooth && !moved && !eyes, what);
        /* the work goes on: the line moves, stops, what hangs swings */
        int frames = 0, differ = 0;
        pour q = p;
        for (unsigned t = 16; t <= 1500; t += 16) {
            pour_work(&q, p.now + t, 0.75f, POUR_WORKING);
            int turned = 0;
            for (int i = 0; i < q.total; i++)
                turned += pour_holder_angle(&q, i) != 0;
            if (!turned || t % 128)
                continue;
            frames++;
            differ += pour_eyes_differ(&q, n, 1) != 0;
            if (frames == 4) {
                pour_draw(&q, n);
                char name[96];
                snprintf(name, sizeof name, "SR-TD3c %s, what hangs swinging", n ? "the badge write" : "the install");
                judge_pour(name);
            }
        }
        snprintf(what, sizeof what, "SR-TD3c %s swinging, the 3D slider up: of %d frames %d with what hangs differing "
                 "between the eyes but for their shift", n ? "the badge write" : "the install", frames, differ);
        check(frames >= 4 && !differ, what);
    }
}

/* The end's glow in every scheme: round the tile (2 to 12 px out from its edge), the frame at the glow's height against
 * the full vessel's frame before it differs by POUR_GLOW_SEEN to POUR_GLOW_MOST on the average (the largest of r, g,
 * b, of 255): tile d's soft glow as on its sheet (C8), 16 to 23 on the light schemes and 35 on Nacht (D3's glow at
 * 72a927b reached 15 to 21 on the light schemes, 34 on Nacht; the glass block's at 51fda6f 50 to 71). */
#define POUR_GLOW_SEEN 15.0f
#define POUR_GLOW_MOST 45.0f
static float pour_glow_seen = POUR_GLOW_SEEN;   /* the least here (test_look.c: less on its test-only look's ground) */
static void pour_glow_check(void)
{
    static float before[SCREEN_H][TOP_W][3];
    static const char *const NAMES[8] = {"Weiss", "Gelb", "Sand", "Minze", "Himmel", "Flieder", "Rose", "Nacht"};
    for (int k = 0; k < ART_SCHEMES; k++) {
        ui_look(pour_look, k, 1, PATTERN_HOUSES);
        pour p;
        pour_settle(&p, 0, 1, POUR_WORKING, POUR_SETTLE_MS);
        pour_draw(&p, 0);
        paint_top(1);
        memcpy(before, paint, sizeof before);
        uint64_t t = p.now;
        while (pour_glow(&p) < 0.98f && t < p.now + 5000)
            pour_work(&p, t += 16, 1, POUR_SUCCEEDED);
        pour_draw(&p, 0);
        paint_top(1);
        float sum = 0;
        int n = 0;
        for (int y = POUR_TILE_Y - 14; y < POUR_TILE_Y + POUR_TILE_H + 14; y++)
            for (int x = POUR_TILE_X - 14; x < POUR_TILE_X + POUR_TILE_W + 14; x++) {
                float sd = box_sd(x + 0.5f, y + 0.5f, POUR_TILE_X, POUR_TILE_Y, POUR_TILE_W, POUR_TILE_H, POUR_TILE_R);
                if (sd > 2 && sd <= 12)
                    sum += paint_diff(before, x, y), n++;
            }
        char what[160];
        snprintf(what, sizeof what, "R9 the vessel's glow in %s: round the tile %.1f on the average (%.0f to %.0f)",
                 k < 8 ? NAMES[k] : "?", n ? sum / n : 0, pour_glow_seen, POUR_GLOW_MOST);
        check(n > 0 && sum / n >= pour_glow_seen && sum / n <= POUR_GLOW_MOST, what);
    }
    ui_look(pour_look, ART_SCHEME_WEISS, 1, PATTERN_HOUSES);
}

/* ---- R20, the carousel's middle pane and its neighbours apart: a pane's frame (R19) measured where the recorded top
 * screen draws it, as the texels of its sprite in the sheet say (their alpha read with smoothing, as the scaled panes
 * are): its card's outer edge is where that alpha passes one half; the shadow under it stays below. */
#define CAROUSEL_GAP 4.25f      /* px clear between the middle pane's card and each neighbour's (C23): 4.5 by
                                   ui_layout.h's numbers (SWARM_STOCK_APART), less where the card's edge passes one half */
#define CAROUSEL_CARD 3.5f      /* the middle pane's card at least this far beyond its picture (CARD_W 4) */

static int pane_rim(const typeof(rec[0]) *r)
{
    if (r->kind != 'I' || !sheet(r->tex))
        return 0;
    static const int RIMS[2] = {ART_PANE_BIG_RIM, ART_PANE_SMALL_RIM};
    for (int k = 0; k < 2; k++) {
        const art_sprite *s = ui_sprite_info(RIMS[k]);
        if (fabsf(r->u0 * r->tex->width - s->x) < 0.01f && fabsf((1 - r->v0) * r->tex->height - s->y) < 0.01f)
            return 1;
    }
    return 0;
}

/* An image's alpha at a point of the screen: its texels' read with smoothing; 0 outside it. */
static float rec_alpha(const typeof(rec[0]) *r, float px, float py)
{
    float u, v;
    rec_uv(r, px, py, &u, &v);
    if (u < 0 || u > 1 || v < 0 || v > 1)
        return 0;
    const C3D_Tex *t = r->tex;
    float fx = (r->u0 + (r->u1 - r->u0) * u) * t->width - 0.5f;
    float fy = (1.0f - (r->v0 + (r->v1 - r->v0) * v)) * t->height - 0.5f;
    int x0 = (int)floorf(fx), y0 = (int)floorf(fy);
    float a = 0;
    for (int q = 0; q < 4; q++) {
        int sx = x0 + (q & 1), sy = y0 + (q >> 1);
        sx = sx < 0 ? 0 : sx >= t->width ? t->width - 1 : sx;
        sy = sy < 0 ? 0 : sy >= t->height ? t->height - 1 : sy;
        uint32_t texel = ((const uint32_t *)t->data)[((sy >> 3) * (t->width >> 3) + (sx >> 3)) * 64 +
                                                     ui_morton(sx & 7, sy & 7)];
        a += (texel & 255) / 255.0f * (q & 1 ? fx - x0 : 1 - (fx - x0)) * (q >> 1 ? fy - y0 : 1 - (fy - y0));
    }
    return a;
}

/* ---- SR-O1, the balls on the line clear of the steps: the lowest pixel any ball covers (its shadow too) and the
 * highest any step's mark (its ring, its ring with the dot, its check) covers (the rings bound the names, which sit centred in their rows), on the
 * recorded frames of a whole run in every scheme, at least POUR_BALLS_CLEAR empty rows apart. */
#define POUR_BALLS_CLEAR 3

/* The rows of the screen the image recorded as r covers with any alpha: *top to *low, inclusive. */
static void rec_rows(const typeof(rec[0]) *r, int *top, int *low)
{
    float x0, y0, x1, y1;
    rec_box(r, &x0, &y0, &x1, &y1);
    for (int py = (int)floorf(y0); py < y1; py++)
        for (int px = (int)floorf(x0); px < x1; px++)
            if (rec_alpha(r, px + 0.5f, py + 0.5f) > 0.5f / 255) {
                *top = py < *top ? py : *top;
                *low = py > *low ? py : *low;
            }
}

static void pour_balls_clear(void)
{
    static const char *const NAMES[8] = {"Weiss", "Gelb", "Sand", "Minze", "Himmel", "Flieder", "Rose", "Nacht"};
    for (int n = 0; n < 2; n++) {
        int least = 1 << 30, ball_low = -1, step_top = 1 << 30, frames = 0, worst = -1;
        for (int k = 0; k < ART_SCHEMES; k++) {
            ui_look(pour_look, k, 1, PATTERN_HOUSES);
            pour p;
            pour_start(&p, n ? &POUR_BADGES : &POUR_INSTALL, 1000);
            for (unsigned t = 16; t <= 14000; t += 16) {
                float level = t < 9000 ? t / 9000.0f : 1;
                pour_work(&p, 1000 + t, level, t < 9500 ? POUR_WORKING : POUR_SUCCEEDED);
                if (t % 160)
                    continue;
                pour_draw(&p, n);
                int low = -1, top = 1 << 30, ball_top = 1 << 30;
                for (int i = 0; i < recs; i++) {
                    int id = atlas_sprite(&rec[i]);
                    if (id == ART_POUR_PIECE)
                        rec_rows(&rec[i], &ball_top, &low);
                    else if (id == ART_POUR_RING || id == ART_POUR_NOW || id == ART_POUR_TICK)
                        rec_rows(&rec[i], &top, &(int){0});
                }
                frames++;
                if (low < 0 || top == 1 << 30)
                    continue;
                ball_low = low > ball_low ? low : ball_low;
                step_top = top < step_top ? top : step_top;
                if (top - low - 1 < least)
                    least = top - low - 1, worst = k;
            }
        }
        char what[220];
        snprintf(what, sizeof what, "SR-O1 %s over %d frames in 8 schemes: the balls down to row %d, the steps from row "
                 "%d, at the least %d rows apart (in %s; at least %d)", n ? "the badge write" : "the install", frames,
                 ball_low, step_top, least, worst >= 0 ? NAMES[worst] : "-", POUR_BALLS_CLEAR);
        check(least < 1 << 30 && least >= POUR_BALLS_CLEAR, what);
    }
    ui_look(pour_look, ART_SCHEME_WEISS, 1, PATTERN_HOUSES);
}

/* The pane's frame's outer edge on row y: its right side's (right) or its left side's, in sixteenths of a pixel. */
static float frame_edge(const typeof(rec[0]) *r, float y, int right)
{
    float x0 = r->k * r->x + r->tx, x1 = x0 + r->k * r->w, step = 1 / 16.0f;
    for (float x = right ? x1 : x0; right ? x > x0 : x < x1; x += right ? -step : step)
        if (rec_alpha(r, x, y) >= 0.5f)
            return x;
    return right ? x0 : x1;
}

/* How far apart the frames of the panes recorded as a (on the left) and b are, the least over the middle half of the
 * rows both stand on (their straight sides); negative: by how much they cross. */
static float frames_apart(int a, int b)
{
    float top = fmaxf(rec[a].k * rec[a].y + rec[a].ty, rec[b].k * rec[b].y + rec[b].ty);
    float low = fminf(rec[a].k * (rec[a].y + rec[a].h) + rec[a].ty, rec[b].k * (rec[b].y + rec[b].h) + rec[b].ty);
    float least = 1e9f;
    for (float y = top + (low - top) / 4; y <= low - (low - top) / 4; y += 1)
        least = fminf(least, frame_edge(&rec[b], y, 0) - frame_edge(&rec[a], y, 1));
    return least;
}

static float rec_middle(int i)
{
    return rec[i].k * (rec[i].x + rec[i].w / 2) + rec[i].tx;
}

static ui_pic carousel_pic(size_t i)
{
    (void)i;
    return (ui_pic){stand_in, -1};
}

/* The list's top screen as main.c draws it, its carousel's selection on cursor, at the time at. */
static void carousel_top(size_t cursor, uint64_t at)
{
    recs = 0;
    watched = 0;
    ui_begin(1.0f);
    ui_f.ms = at;
    ui_top();
    ui_head("SD-Karte", "5 / 9");
    ui_swarm(carousel_pic, 9, cursor, stand_in, "Waldweg", 0);
    ui_foot(FOOT_DOTS_Y, ART_HOME, "2 Designs installiert", MUTED, 0, NULL);
    ui_bottom();
    ui_end();
    watched = 2;
}

/* The panes' frames as recorded, in the order drawn (far to near: the middle one last); how many pictures waited. */
static int carousel_rims(int *rims, int *pictures)
{
    int n = 0;
    *pictures = 0;
    for (int i = 0; i < recs; i++) {
        if (pane_rim(&rec[i]) && n < 16)
            rims[n++] = i;
        *pictures += rec[i].kind == 'I' && (rec[i].tex == &icon_tex || sheet(rec[i].tex));
    }
    return n;
}

/* At rest the middle pane's frame and each neighbour's CAROUSEL_GAP px apart at least, and at five moments of a move
 * the two panes that pass each other in the middle (the one leaving it and the one coming in) as well; the pictures
 * of the panes never fill the order list (order.h: ORDER_MAX). */
static void carousel_apart(void)
{
    memset(stand_in, 0x5a, sizeof stand_in);
    int rims[16], pictures, most = 0;
    uint64_t t = 50000;
    for (int k = 0; k < 4; k++)
        carousel_top(4, t += 16);
    int n = carousel_rims(rims, &pictures), mid = n ? rims[n - 1] : -1, left = -1, right = -1;
    if (getenv("TEST_UI_DUMP"))
        for (int i = 0; i < recs; i++)
            printf("  %c %s %g,%g %gx%g uv %g,%g %s\n", rec[i].kind,
                   atlas_tex(rec[i].tex) ? "atlas" : sheet(rec[i].tex) ? "sheet" : rec[i].tex == &icon_tex ? "icon"
                   : rec[i].tex ? "other" : "-", rec[i].x, rec[i].y, rec[i].w, rec[i].h, rec[i].u0, rec[i].v0,
                   rec[i].text);
    for (int j = 0; j + 1 < n; j++) {
        float x = rec_middle(rims[j]);
        if (x < rec_middle(mid) && (left < 0 || x > rec_middle(left)))
            left = rims[j];
        if (x > rec_middle(mid) && (right < 0 || x < rec_middle(right)))
            right = rims[j];
    }
    float l = left >= 0 ? frames_apart(left, mid) : -1, r = right >= 0 ? frames_apart(mid, right) : -1;
    float card = mid >= 0 ? frame_edge(&rec[mid], GAL_PICTURE_Y + GAL_PICTURE / 2.0f, 1) -
                                (GAL_PICTURE_X + GAL_PICTURE - PANE_BIG_RIM) : -1;
    most = pictures;
    char what[200];
    snprintf(what, sizeof what, "R20 the carousel at rest: %.2f and %.2f px between the middle pane's card (%.2f px "
             "beyond its picture, at least %.2f) and its neighbours' (at least %.2f), %d panes", (double)l, (double)r,
             (double)card, (double)CAROUSEL_CARD, (double)CAROUSEL_GAP, n);
    check(n == 7 && l >= CAROUSEL_GAP && r >= CAROUSEL_GAP && card >= CAROUSEL_CARD, what);
    carousel_top(5, t += 16);           /* the move starts */
    uint64_t from = t;
    for (int m = 1; m <= 5; m++) {
        carousel_top(5, from + (uint64_t)(SWARM_MOVE_MS * m / 6));
        n = carousel_rims(rims, &pictures);
        most = pictures > most ? pictures : most;
        int a = n >= 2 ? rims[n - 1] : -1, b = n >= 2 ? rims[n - 2] : -1;
        if (a >= 0 && rec_middle(a) > rec_middle(b)) {
            int s = a;
            a = b, b = s;
        }
        float apart = a >= 0 ? frames_apart(a, b) : -1;
        snprintf(what, sizeof what, "R20 the carousel %d/6 of a move on: %.2f px between the frames of the two panes "
                 "passing in the middle (at least %.2f), %d panes", m, (double)apart, (double)CAROUSEL_GAP, n);
        check(n == 8 && apart >= CAROUSEL_GAP, what);
    }
    snprintf(what, sizeof what, "R20 the carousel's pictures: at most %d at once of the order list's %d", most,
             ORDER_MAX);
    check(most > 0 && most <= ORDER_MAX, what);
}

/* An image's straight colour at a point of the screen: its nearest texel (0xRRGGBBAA); 0 outside it. */
static uint32_t rec_texel(const typeof(rec[0]) *r, float px, float py)
{
    float x = r->k * r->x + r->tx, y = r->k * r->y + r->ty, w = r->k * r->w, h = r->k * r->h;
    float u = (px - x) / w, v = (py - y) / h;
    if (u < 0 || u > 1 || v < 0 || v > 1)
        return 0;
    const C3D_Tex *t = r->tex;
    int sx = (int)floorf((r->u0 + (r->u1 - r->u0) * u) * t->width);
    int sy = (int)floorf((1.0f - (r->v0 + (r->v1 - r->v0) * v)) * t->height);
    return ((const uint32_t *)t->data)[((sy >> 3) * (t->width >> 3) + (sx >> 3)) * 64 + ui_morton(sx & 7, sy & 7)];
}

/* C23, the frame "Abzug": the carousel's middle pane at rest, the check screen's single tile (ui_gallery without
 * neighbours) and the empty place (ui_gallery_empty) carry the same card, in every scheme: the big class's frame at
 * its own size in one box, and on the middle of the picture's upper edge, from the edge out to CARD_REACH px, opaque
 * and light (each channel CARD_LEAST or more), clear (alpha under one half) a pixel beyond. */
#define CARD_REACH 3
#define CARD_LEAST 0xD8

static void carousel_card(void)
{
    static const char *const NAMES[8] = {"Weiss", "Gelb", "Sand", "Minze", "Himmel", "Flieder", "Rose", "Nacht"};
    memset(stand_in, 0x5a, sizeof stand_in);
    const art_sprite *s = ui_sprite_info(ART_PANE_BIG_RIM);
    float top = GAL_PICTURE_Y + PANE_BIG_RIM, mx = GAL_PICTURE_X + GAL_PICTURE / 2.0f;
    for (int k = 0; k < ART_SCHEMES; k++) {
        ui_look(LOOK_STOCK, k, 1, PATTERN_HOUSES);
        float box[3][4];
        int ok = 1, card = 1;
        for (int shot = 0; shot < 3; shot++) {
            if (shot == 0) {
                carousel_top(4, 90000);
            } else {
                recs = 0;
                watched = 0;
                ui_begin(1.0f);
                ui_f.ms = 90000;
                ui_top();
                ui_head("Design installieren", NULL);
                if (shot == 1)
                    ui_gallery((ui_pic){stand_in, -1}, (ui_pic){NULL, -1}, (ui_pic){NULL, -1}, "Waldweg", 0);
                else
                    ui_gallery_empty();
                ui_bottom();
                ui_end();
                watched = 2;
            }
            int rims[16], pictures, n = carousel_rims(rims, &pictures);
            const typeof(rec[0]) *r = n ? &rec[rims[n - 1]] : NULL;
            if (!r || fabsf(r->k * r->w - s->w) > 0.01f) {
                ok = 0;
                continue;
            }
            box[shot][0] = r->k * r->x + r->tx, box[shot][1] = r->k * r->y + r->ty;
            box[shot][2] = r->k * r->w, box[shot][3] = r->k * r->h;
            for (int d = 0; d < CARD_REACH; d++) {
                uint32_t t = rec_texel(r, mx, top - 0.5f - d);
                card &= (t & 255) >= 250 && (t >> 24) >= CARD_LEAST && (t >> 16 & 255) >= CARD_LEAST &&
                        (t >> 8 & 255) >= CARD_LEAST;
            }
            card &= rec_alpha(r, mx, top - 0.5f - CARD_REACH - 1.0f) < 0.5f;
        }
        for (int shot = 1; ok && shot < 3; shot++)
            for (int c = 0; c < 4; c++)
                ok &= fabsf(box[shot][c] - box[0][c]) < 0.01f;
        char what[200];
        snprintf(what, sizeof what, "C23 the card in %s: the carousel's middle pane, the check screen's tile and the "
                 "empty place in one box at the sprite's size (%s), opaque and light %d px out from the picture (%s)",
                 NAMES[k], ok ? "yes" : "no", CARD_REACH, card ? "yes" : "no");
        check(ok && card, what);
    }
    ui_look(LOOK_STOCK, ART_SCHEME_WEISS, 1, PATTERN_HOUSES);
}

static void frame_begin(void)
{
    recs = 0;
    ui_begin(1.0f);
    ui_top();
    ui_bottom();
}

/* ---- A1: one top screen of each kind, recorded */
static const char *pattern_name(int k)
{
    static const char *const NAMES[3] = {"Plus und Ringe", "Häuschen", "Blasen"};
    return NAMES[k];
}

static void top_screen(int which)
{
    ui_pic none = {NULL, -1};
    static const char *const steps[4] = {"Prüfen", "Katalog", "Design", "Abschluss"};
    static const float mark[4] = {1, 1, 0.5f, 0};
    switch (which) {
    case 0:
        ui_head("SD-Karte", "3 / 5");
        ui_gallery_empty();
        ui_foot(FOOT_DOTS_Y, ART_HOME, "2 Designs installiert", MUTED, 0, NULL);
        break;
    case 1:
        ui_head("Design installieren", NULL);
        ui_gallery((ui_pic){NULL, ART_WARN}, none, none, "Waldweg", 0);
        ui_foot(FOOT_DOTS_Y, ART_HOME, "2 Designs installiert", MUTED, 0, NULL);
        break;
    case 2:
        ui_hero(ART_DESIGNS, "Designs", "HOME-Menü", "Design ändern");
        ui_foot(HERO_FOOT_DOTS_Y, ART_HOME, "2 Designs installiert", MUTED, 0, NULL);
        break;
    case 3:
        ui_head("Badges", NULL);
        ui_detail(NULL, ART_WARN, "Nichts wurde geändert", NULL, INK);
        ui_foot(FOOT_DOTS_Y, -1, NULL, MUTED, 0, "v0.9.3");
        break;
    case 4:
        ui_steps("Wird installiert", "Waldweg", steps, mark, 0);
        ui_dots(FOOT_DOTS_Y);
        break;
    case 5:
        ui_backdrop();
        ui_head("Einstellungen", "Hintergrund");
        ui_choices(3, 0, pattern_name);
        ui_choices_word();
        ui_foot(FOOT_DOTS_Y, -1, NULL, MUTED, 0, "v0.9.3");
        break;
    case 6:
        ui_head("Installiert", NULL);
        ui_done_card(NULL, "Waldweg", "HOME-Menü", "Design ändern");
        ui_foot(DONE_DOTS_Y, ART_HOME, "3 Designs installiert", MUTED, 0, NULL);
        break;
    case 7:
        ui_head("QR-Code erkannt", NULL);
        ui_url_card("https://example.org/badges/Sterne.zip");
        ui_foot(FOOT_DOTS_Y, -1, "/Badges", MUTED, 1, NULL);
        break;
    default:
        ui_start();
        ui_foot(HERO_FOOT_DOTS_Y, ART_HOME, "2 Designs installiert", MUTED, 0, "v0.9.3");
        break;
    }
}

static void judge_top(const char *name)
{
    int patterns = 0, at = -1, text_before = 0, pattern = expect_of(ui_lk->name)->pattern;
    for (int i = 0; i < recs; i++)
        if (pattern ? rec[i].kind == 'I' && rec[i].tex == &pattern_tex && !patterns++ : rec[i].kind == 'I' && at < 0)
            at = i;
    for (int i = 0; i < at; i++)
        text_before += rec[i].kind == 'T';
    int next_atlas = at >= 0 && at + 1 < recs && rec[at + 1].kind == 'I' && atlas_tex(rec[at + 1].tex);
    /* the whole top screen as judge() counts the bottom's: texts after a picture's texture, transparent pieces */
    int after = 0, fallback = 0;
    const C3D_Tex *last = &atlas;
    for (int i = 0; i < recs; i++)
        letters(&last, i, &after, &fallback);
    if (getenv("TEST_UI_DUMP"))
        for (int i = 0; i < recs; i++)
            printf("  %c %s %g,%g %gx%g %s\n", rec[i].kind, atlas_tex(rec[i].tex) ? "atlas" : rec[i].tex == &pattern_tex
                   ? "pattern" : rec[i].tex ? "other" : "-", rec[i].x, rec[i].y, rec[i].w, rec[i].h, rec[i].text);
    char order_what[200];
    snprintf(order_what, sizeof order_what, "A1 %s: %d texts after a picture's texture, %d transparent pieces", name,
             after, fallback);
    check(!after && !fallback, order_what);
    char what[240];
    int band = at >= 0 && rec[at].y == 0 && rec[at].h == SCREEN_H;
    if (!pattern) {             /* the look's own ground first (expect.h, EXPECT_<NAME>_PATTERN 0) */
        snprintf(what, sizeof what, "A1 %s: the first image a ground %s, %d texts before it", name,
                 band ? "over the whole screen" : "NOT over the whole screen", text_before);
        check(band && !text_before, what);
        return;
    }
    snprintf(what, sizeof what, "A1 %s: the pattern drawn %d times (once), %s, %d texts before it, the atlas "
             "right after it %d", name, patterns, band ? "over the whole screen" : "NOT over the whole screen",
             text_before, next_atlas);
    check(patterns == 1 && band && !text_before && next_atlas, what);
}

/* FIX-2: the sounds' top screen. ui_choices_top draws the choices ui_choices centres, at the same x and higher by
 * the difference, the first line's box CARD_PAD below the card's edge, and returns the y under the last line;
 * ui_choices_credits, told that y, stands the last credit line's box CARD_PAD above the card's lower edge */
static const char *off_on_de(int k) { return k ? "An" : "Aus"; }
static int text_at(const char *s, float *x, float *baseline)
{
    for (int i = 0; i < recs; i++)
        if (rec[i].kind == 'T' && !strcmp(rec[i].text, s)) {
            *x = rec[i].x, *baseline = rec[i].y;
            return 1;
        }
    return 0;
}
static void choices_at_top(const char *look)
{
    static const char *const LINES[3] = {"Bedientöne von Kenney (kenney.nl), CC0", "Musik: eins", "Musik: zwei"};
    rect c = ui_card_rect();
    float x[4] = {0}, b[4] = {0}, last_x = 0, last_b = 0;
    recs = 0;
    ui_begin(1.0f);
    ui_top();
    ui_choices(2, 1, off_on_de);
    ui_bottom();
    ui_end();
    int found = text_at("Aus", &x[0], &b[0]) && text_at("An", &x[1], &b[1]);
    recs = 0;
    ui_begin(1.0f);
    ui_top();
    float end = ui_choices_top(2, 1, off_on_de);
    int fits = ui_choices_credits(end, LINES, 3);
    ui_bottom();
    ui_end();
    found &= text_at("Aus", &x[2], &b[2]) && text_at("An", &x[3], &b[3]) && text_at(LINES[2], &last_x, &last_b);
    float up = roundf(c.y + (c.h - 2 * SET_LIST_LH) / 2) - (c.y + CARD_PAD);
    char what[300];
    snprintf(what, sizeof what, "FIX-2 %s: the sounds' choices at the card's top (Aus at %g, centred %g, up by %g), "
             "the y under them %g (%d), the last credit line's baseline %g (%g), the credits fit %d", look, b[2], b[0],
             up, end, (int)(c.y + CARD_PAD + 2 * SET_LIST_LH), last_b,
             ui_baseline(c.y + c.h - CARD_PAD - CREDIT_LH, CREDIT_LH, CREDIT_PX), fits);
    check(found && x[2] == x[0] && x[3] == x[1] && b[0] - b[2] == up && b[1] - b[3] == up &&
              b[2] == ui_baseline(c.y + CARD_PAD, SET_LIST_LH, SET_LIST_PX) && end == c.y + CARD_PAD + 2 * SET_LIST_LH &&
              last_b == ui_baseline(c.y + c.h - CARD_PAD - CREDIT_LH, CREDIT_LH, CREDIT_PX) && fits, what);
}

/* R2 follow-up: the wrench on the settings tab, resting and pressed. At d6c028b it was drawn where the folder symbol
 * is on the corner button (CORNER_PAD across, centred in CORNER_H, one down when pressed); Stock's is one pixel
 * higher and one further left; another look's where its expect_<name>.h says. */
static void wrench(void)
{
    const art_sprite *s = ui_sprite_info(ART_SETTINGS);
    const expect *e = expect_of(ui_lk->name);
    rect r = ui_settings_rect();
    watched = 2;
    for (int down = 0; down < 2; down++) {
        frame_begin();
        ui_settings_button(down);
        ui_end();
        int found = -1;
        for (int i = 0; i < recs; i++)
            if (rec[i].kind == 'I' && rec[i].tex == ui_sprite_tex(ART_SETTINGS, 0) && rec[i].w == s->w &&
                rec[i].h == s->h)
                found = i;
        float x = r.x + e->wrench_x - s->pad[0];
        float y = r.y + e->wrench_y + down - s->pad[1];
        char name[96];
        snprintf(name, sizeof name, "the wrench%s at (%g, %g)%s", found < 0 ? " (not drawn)" : "", x, y,
                 down ? ", held" : "");
        check(found >= 0 && rec[found].x == x && rec[found].y == y, name);
        if (found >= 0 && (rec[found].x != x || rec[found].y != y))
            printf("     drawn at (%g, %g)\n", rec[found].x, rec[found].y);
    }
}

/* O4: under a question nothing of the bar shows beside the panel: after the bar's tabs, keys and labels and before the
 * dim, the screen's ground covers them, opaque once the popup has opened, from the bar's shadow down; what lies above
 * it is not covered */
static void popup_hides_bar(void)
{
    static const char *const paras[1] = {"Set \"Formen\" entfernen?"};
    ui_popup p;
    ui_popup_text(&p, paras, 1);
    watched = 2;
    for (int two = 0; two < 2; two++) {
        frame_begin();
        ui_bar(UI_KEY_B, "Zurück", 0);
        if (two)
            ui_bar_right(UI_KEY_A, "Vorschau", 0);
        int bar = recs;
        ui_popup_draw(&p, "Abbrechen", "Entfernen", -1, -1, 1);
        ui_end();
        float top = SCREEN_H;
        for (int i = 0; i < bar; i++)
            if (rec[i].kind == 'I' && rec[i].y < top)
                top = rec[i].y;
        int cover = -1, dim = -1;
        for (int i = bar; i < recs; i++) {
            if (rec[i].kind != 'R' || rec[i].x > 0 || rec[i].x + rec[i].w < BOTTOM_W)
                continue;
            if (cover < 0 && dim < 0 && rec[i].top >> 24 == 255 && rec[i].low >> 24 == 255 && rec[i].y <= top &&
                rec[i].y + rec[i].h >= SCREEN_H)
                cover = i;
            else if (dim < 0 && rec[i].y <= 0 && rec[i].y + rec[i].h >= SCREEN_H)
                dim = i;
        }
        char name[160];
        snprintf(name, sizeof name, "O4: under a question the bar (%s, from y %g) is covered by the ground before the "
                 "dim, nothing above y %d (cover %d, dim %d)", two ? "both tabs" : "the B tab", top,
                 BAR_Y - BAR_SHADOW, cover, dim);
        check(bar > 0 && cover >= 0 && dim > cover && rec[cover].y >= BAR_Y - BAR_SHADOW, name);
    }
}

/* ---- G2-L, the layout C "Zwei Teile" (C47, C49 to C51, C53) in the look under test, the install (four steps) and
 * the badge write (four, BS), at the start, half way and at the end. ui_pour's own draws of the frame are recorded
 * (layout_draw). A step is its mark and its name and nothing else: in the column's place (right of POUR_LIST_X, under
 * POUR_LIST_Y) every image is one step's mark at its row's place and size, the one its state asks for (to come the
 * ring, under way the ring with the dot, done the check), one per step, and there is no rectangle: no band, no rail.
 * Every step's name stands once beside its mark. The head is one small line from the left edge: while the work runs
 * its symbol and the title, at the end the done title alone, without a check. The end's line: before the end
 * neither its check nor its sentence; at the end the check and right after it the sentence, centred on the screen,
 * and no path line (no house, no chevron). The name (C146): always one line, centred under the tile; a short one and
 * one that just fits drawn whole; one a letter longer, a long one and two with two- and three-byte letters cut at
 * their end with "...", no wider than POUR_NAME_W, what stands before the dots the name's beginning up to a letter's
 * boundary. Nothing of the head,
 * the tile, the name, the marks, the names or the end's line leaves the top screen. With the 3D slider up the texts,
 * the head's symbol, the marks and the end's check stand on the screen's plane in both eyes, the line and its knot
 * and the tile's back and front in front of it at one depth (C140), each eye by the same shift in opposite
 * directions. The drop behind the tile (C147) is drawn once in every look and eye, on the screen's plane: in Prisma
 * the ground's wandering beam of light, the screen drawing none of its own, and two frames after the installing
 * screen the beam is back 2 px behind; in the other looks ui_pour's, and their grounds draw none. */
static int layout_from, layout_to;      /* ui_pour's own records of the frame layout_draw recorded last (one eye) */
static int sun_from[2], sun_pour[2];    /* ... each eye's top screen: where its records begin and ui_pour's do */
static void layout_draw(const pour *p, int n, const char *name, int eyes)
{
    recs = 0;
    watched = eyes ? 3 : 0;
    slider_now = eyes;
    ui_begin(1.0f);
    do {
        sun_from[ui_f.eye] = recs;
        ui_top();
        layout_from = sun_pour[ui_f.eye] = recs;
        ui_pour(p, n ? "Badges installieren" : "Wird installiert", "Installiert", name, n ? NULL : stand_in,
                n ? ART_BADGES : -1, n ? badge_steps : four, "Erfolgreich installiert", NULL);
        ui_bottom();            /* a check that pops is a picture: it waits for the screen's end (order.h) */
        layout_to = recs;
    } while (ui_next_eye());
    ui_end();
    watched = 2;
    slider_now = 0;
}

/* A frame of a top screen without ui_pour (its ground alone), the slider at its top, both eyes recorded. */
static void ground_draw(void)
{
    recs = 0;
    watched = 3;
    slider_now = 1;
    ui_begin(1.0f);
    do {
        sun_from[ui_f.eye] = recs;
        ui_top();
        sun_pour[ui_f.eye] = recs;
        ui_bottom();
    } while (ui_next_eye());
    ui_end();
    watched = 2;
    slider_now = 0;
}

/* C147: how often the frame drawn last drew the drop behind the tile (ART_DROP_SUN) wrong, eye by eye (eyes: both
 * recorded), pour: the installing screen's frame (layout_draw) or another one's (ground_draw). */
static int sun_wrong(int eyes, int pour)
{
    const int prism = expect_of(look_get(pour_look)->name)->ground_sun;  /* by name: the test-only look has none */
    int bad = 0;
    for (int e = 0; e < 1 + eyes; e++) {
        int all = 0, ground = 0, to = e || !eyes ? recs : sun_from[1];
        float shift = 0;
        for (int i = sun_from[e]; i < to; i++)
            if (atlas_sprite(&rec[i]) == ART_DROP_SUN)
                all++, ground += i < sun_pour[e], shift = rec[i].eye ? -rec[i].tx : rec[i].tx;
        if (pour)
            bad += all != 1 || ground != prism || shift != 0;
        else
            bad += all != prism || ground != prism || (prism && shift != roundf(DEPTH_DROPS_BEHIND / 2.0f));
    }
    return bad;
}

/* The name as layout_draw drew it: its lines (texts centred on the tile's middle, the end's sentence not), the last
 * into last, the widest one's width into widest. */
static int name_drawn(const pour *p, int n, const char *name, char *last, size_t size, float *widest)
{
    const float mid = POUR_TILE_X + POUR_TILE_W / 2.0f;
    int lines = 0;
    layout_draw(p, n, name, 0);
    *widest = 0;
    for (int i = layout_from; i < layout_to; i++)
        if (rec[i].kind == 'T' && rec[i].x == mid && strcmp(rec[i].text, "Erfolgreich installiert")) {
            *widest = fmaxf(*widest, ui_text_width(rec[i].text, POUR_NAME_PX));
            snprintf(last, size, "%s", rec[i].text);
            lines++;
        }
    return lines;
}

/* A name too wide, as name_drawn read it: one line, ending in "...", no wider than POUR_NAME_W, the text before the
 * dots the name's beginning and cut on a letter's boundary (the name's next byte starts a letter). */
static int name_cut_wrong(int lines, const char *shown, float widest, const char *name)
{
    size_t len = strlen(shown), keep = len >= 3 ? len - 3 : 0;
    int dots = len >= 3 && !strcmp(shown + keep, "...");
    return lines != 1 || !dots || widest > POUR_NAME_W || strncmp(shown, name, keep) ||
           (keep < strlen(name) && ((unsigned char)name[keep] & 0xC0) == 0x80);
}

static void pour_layout_c(void)
{
    static const char LONG[] = "Waldweg im ersten Morgenlicht nach dem langen Regen am Fluss entlang";
    static const char WIDE[] = "Ährenfeld über Öland für 5 € je Überfahrt am Ufer entlang";
    static const char BYTES[] = "ÄÖÜäöü€ÄÖÜäöü€ÄÖÜäöü€ÄÖÜäöü€ÄÖÜäöü€ÄÖÜäöü€";     /* every cut next to such a letter */
    static const char END[] = "Erfolgreich installiert";
    const float tx = POUR_LIST_X + POUR_MARK + POUR_MARK_GAP, mid = POUR_TILE_X + POUR_TILE_W / 2.0f;
    char fits[sizeof LONG] = "", over[sizeof LONG] = "";  /* LONG's longest beginning that fits; with its next letter */
    for (size_t k = 1; k < sizeof LONG - 1; k++) {
        memcpy(over, LONG, k);
        over[k] = 0;
        if (over[k - 1] != ' ' && ui_text_width(over, POUR_NAME_PX) <= POUR_NAME_W)
            memcpy(fits, over, k + 1);
    }
    size_t end = strlen(fits) + 1;
    while (LONG[end - 1] == ' ')
        end++;
    memcpy(over, LONG, end);
    over[end] = 0;
    for (int n = 0; n < 2; n++) {
        const char *const *steps = n ? badge_steps : four;
        const int count = 4, row = POUR_ROW;
        int marks = 0, wrong = 0, extra = 0, kinds = 0, names = 0, head_bad = 0, end_bad = 0, outside = 0;
        int name_bad = 0, depth_bad = 0, sun_bad = 0;
        pour p;
        for (int m = 0; m < 3; m++) {
            pour_start(&p, n ? &POUR_BADGES : &POUR_INSTALL, 1000);
            for (unsigned t = 16; t <= (m == 2 ? 16000 : 9000); t += 16)
                pour_work(&p, 1000 + t, m == 2 ? (t < 9000 ? t / 9000.0f : 1) : m * 0.5f,
                          m == 2 && t >= 9500 ? POUR_SUCCEEDED : POUR_WORKING);
            layout_draw(&p, n, "Sommerwiese", 0);
            sun_bad += m == 1 ? sun_wrong(0, 1) : 0;
            int head = 0, head_icon = 0, done_head = 0, end_text = -1, end_tick = -1, way_img = 0, name_lines = 0;
            for (int i = layout_from; i < layout_to; i++) {
                const typeof(rec[0]) *r = &rec[i];
                int id = atlas_sprite(r), step = -1;
                float x0 = r->x, y0 = r->y, x1 = r->x, y1 = r->y;
                if (r->kind == 'T') {
                    for (int k = 0; k < count; k++)
                        step = strcmp(r->text, steps[k]) ? step : k;
                    float w = ui_text_width(r->text, step >= 0 ? POUR_ROW_PX : POUR_NAME_PX);
                    if (step >= 0)
                        names++, wrong += r->x != tx;
                    else if (!strcmp(r->text, n ? "Badges installieren" : "Wird installiert"))
                        head++, head_bad += r->x != TOP_PAD + POUR_HEAD_ICON + POUR_HEAD_GAP;
                    else if (!strcmp(r->text, "Installiert"))
                        done_head++, head_bad += r->x != TOP_PAD;
                    else if (!strcmp(r->text, END))
                        end_text = i;
                    else if (!strcmp(r->text, "Sommerwiese"))
                        name_lines++, name_bad += r->x != mid, x0 -= w / 2;
                    else
                        extra++;
                    x1 = x0 + w;
                } else {
                    rec_box(r, &x0, &y0, &x1, &y1);
                    if (x1 > POUR_LIST_X - 2 && y1 > POUR_LIST_Y - 2) {    /* the column's place */
                        int k = ((int)y0 - POUR_LIST_Y) / row, mk = k >= 0 && k < count ? (int)pour_row_mark(&p, k) : -1;
                        int want = mk == 0 ? ART_POUR_RING : mk == 1 ? ART_POUR_NOW : mk == 2 ? ART_POUR_TICK : -1;
                        if (r->kind != 'I' || (id != ART_POUR_RING && id != ART_POUR_NOW && id != ART_POUR_TICK))
                            extra++;
                        else
                            marks++, kinds |= 1 << (id == ART_POUR_RING ? 0 : id == ART_POUR_NOW ? 1 : 2),
                            wrong += id != want || pour_row_mark(&p, k) != mk || x0 != POUR_LIST_X ||
                                     y0 != POUR_LIST_Y + k * row + (row - POUR_MARK) / 2 || x1 - x0 != POUR_MARK ||
                                     y1 - y0 != POUR_MARK;
                    }
                    head_icon += id == ART_POUR_HEAD;
                    head_bad += id == ART_POUR_HEAD && (x0 != TOP_PAD || y0 < POUR_HEAD_Y ||
                                                        y1 > POUR_HEAD_Y + POUR_HEAD_LH);
                    end_tick = id == ART_POUR_TITLE_DONE ? i : end_tick;
                    way_img += id == ART_HOME || id == ART_CHEVRON;
                    if (id != ART_POUR_RING && id != ART_POUR_NOW && id != ART_POUR_TICK && id != ART_POUR_HEAD &&
                        id != ART_POUR_TITLE_DONE && id != ART_POUR_BACK && id != ART_POUR_FRONT && id != ART_POUR_HALO &&
                        id != ART_POUR_FOOT && id != ART_POUR_LIT_RIM && id != ART_POUR_KNOT)
                        continue;
                }
                outside += x0 < 0 || x1 > TOP_W || y0 < 0 || y1 > SCREEN_H;
            }
            marks -= count, names -= count, name_bad += name_lines != 1;
            if (m < 2) {
                head_bad += head != 1 || head_icon != 1 || done_head;
                end_bad += end_text >= 0 || end_tick >= 0 || pour_ending(&p) > 0;
            } else {
                float tick = ui_ew(ART_POUR_TITLE_DONE) + POUR_END_GAP, w = tick + ui_text_width(END, POUR_END_PX);
                head_bad += head || head_icon || done_head != 1;
                /* right after the check: the kit's transparent piece may stand between (letters after the part's
                 * atlas) */
                end_bad += pour_ending(&p) < 1 || end_tick < 0 || end_text != end_tick + 1 + clear_piece(end_tick + 1) ||
                           way_img ||
                           fabsf(rec[end_tick].x + w / 2 - TOP_W / 2.0f) > 0.5f ||
                           rec[end_text].x != rec[end_tick].x + tick;
            }
            if (m != 1)
                continue;
            layout_draw(&p, n, "Sommerwiese", 1);
            float line = 0, tile = 0;
            for (int i = 0; i < recs; i++) {
                int id = atlas_sprite(&rec[i]);
                float shift = rec[i].eye ? -rec[i].tx : rec[i].tx;
                if (rec[i].kind == 'T' || id == ART_POUR_HEAD || id == ART_POUR_RING || id == ART_POUR_NOW ||
                    id == ART_POUR_TICK)
                    depth_bad += shift != 0;
                else if (id == ART_POUR_LINE || id == ART_POUR_KNOT || id == ART_POUR_HOLDER)
                    depth_bad += shift <= 0 || (line && shift != line), line = shift;
                else if (id == ART_POUR_BACK || id == ART_POUR_FRONT)
                    depth_bad += shift <= 0 || (tile && shift != tile), tile = shift;
            }
            depth_bad += !(line > 0 && tile == line);  /* C140: the line at the tile's depth */
            sun_bad += sun_wrong(1, 1);
            ground_draw();
            ground_draw();
            sun_bad += sun_wrong(1, 0);
        }
        char shown[UI_LINE_BYTES + 4];
        float widest = 0;
        int lines = name_drawn(&p, n, fits, shown, sizeof shown, &widest), whole_bad = 0, cut_bad = 0;
        whole_bad += !*fits || lines != 1 || strcmp(shown, fits);
        const char *const too_wide[] = {over, LONG, WIDE, BYTES};
        for (int k = 0; k < 4; k++) {
            lines = name_drawn(&p, n, too_wide[k], shown, sizeof shown, &widest);
            cut_bad += name_cut_wrong(lines, shown, widest, too_wide[k]);
            outside += mid - widest / 2 < 0 || mid + widest / 2 > TOP_W;
        }
        name_bad += whole_bad + cut_bad;
        /* NAME-2: a name's control characters drawn as spaces, one each, with NAME-1's line and cut after it */
        static const char *const CTL[][2] = {
            {"Wald\nweg", "Wald weg"},          {"Wald\r\nweg", "Wald  weg"},     {"Wald\tweg", "Wald weg"},
            {"Wald\xE2\x80\xA8weg", "Wald weg"}, {"Wald\xE2\x80\xA9weg", "Wald weg"}, {"Wald\xC2\x85weg", "Wald weg"},
            {"\x7F\x01" "Ähre\nÖl", "  Ähre Öl"}, {"Ä\nÖ\tü\r€", "Ä Ö ü €"},
        };
        char ctl_long[2][2][4 * sizeof BYTES];     /* LONG with "\n" for its spaces; BYTES with U+2028 between letters */
        snprintf(ctl_long[0][1], sizeof ctl_long[0][1], "%s", LONG);
        snprintf(ctl_long[0][0], sizeof ctl_long[0][0], "%s", LONG);
        for (char *q = ctl_long[0][0]; (q = strchr(q, ' ')); )
            *q = '\n';
        ctl_long[1][0][0] = ctl_long[1][1][0] = 0;
        for (size_t i = 0; BYTES[i]; i += wrap_unit(BYTES + i)) {
            int len = (int)wrap_unit(BYTES + i);
            size_t a = strlen(ctl_long[1][0]), b = strlen(ctl_long[1][1]);
            snprintf(ctl_long[1][0] + a, sizeof ctl_long[1][0] - a, "%s%.*s", i ? "\xE2\x80\xA8" : "", len, BYTES + i);
            snprintf(ctl_long[1][1] + b, sizeof ctl_long[1][1] - b, "%s%.*s", i ? " " : "", len, BYTES + i);
        }
        char ctl_fit[4 * sizeof LONG] = "";       /* fits with U+2028 for its spaces: measured as drawn, still whole */
        for (const char *q = fits; *q; q++) {
            size_t a = strlen(ctl_fit);
            snprintf(ctl_fit + a, sizeof ctl_fit - a, "%s", *q == ' ' ? "\xE2\x80\xA8" : (char[]){*q, 0});
        }
        lines = name_drawn(&p, n, ctl_fit, shown, sizeof shown, &widest);
        int ctl_bad = lines != 1 || strcmp(shown, fits) || widest > POUR_NAME_W, ctl_cut_bad = 0;
        for (size_t k = 0; k < sizeof CTL / sizeof CTL[0]; k++) {
            lines = name_drawn(&p, n, CTL[k][0], shown, sizeof shown, &widest);
            ctl_bad += lines != 1 || strcmp(shown, CTL[k][1]) || widest > POUR_NAME_W;
        }
        for (int k = 0; k < 2; k++) {
            lines = name_drawn(&p, n, ctl_long[k][0], shown, sizeof shown, &widest);
            ctl_cut_bad += name_cut_wrong(lines, shown, widest, ctl_long[k][1]);
        }
        char ctl_what[300];
        snprintf(ctl_what, sizeof ctl_what, "NAME-2 the name under the tile in %s: of %d names with control characters %d not "
                 "drawn on one line with a space for each, within %d px; of 2 too wide %d not one line cut with "
                 "\"...\" on a letter's boundary", n ? "the badge write" : "the install",
                 (int)(sizeof CTL / sizeof CTL[0]) + 1, ctl_bad, POUR_NAME_W, ctl_cut_bad);
        check(!ctl_bad && !ctl_cut_bad, ctl_what);
        char what[800];
        snprintf(what, sizeof what, "G2-L the layout C, %s, at the start, half way and at the end: %d marks too many or "
                 "too few, %d of them or of the names not at their place or not their state's, kinds seen %d of 7, %d "
                 "other images, rectangles or texts in the column's place, %d names too many or too few; the head %d "
                 "times not as it should be; the end's line %d times; the name %d times (C146: one that just fits, \"%s\", "
                 "%d times not whole on one line; of four too wide %d not one line cut with \"...\" on a letter's "
                 "boundary within %d px); %d things leaving the screen; %d depths wrong; the drop behind the tile %d "
                 "times not once on the screen's plane, by the ground in Prisma alone (C147)",
                 n ? "the badge write" : "the install", marks, wrong, kinds, extra, names, head_bad, end_bad, name_bad,
                 fits, whole_bad, cut_bad, POUR_NAME_W, outside, depth_bad, sun_bad);
        check(!marks && !wrong && kinds == 7 && !extra && !names && !head_bad && !end_bad && !name_bad && !outside &&
              !depth_bad && !sun_bad, what);
    }
}

/* NAME-2: a name in the top card's detail with "\n" (where ui_wrap ends a line) or U+2028 is drawn on one line, each
 * as a space */
static void detail_controls(void)
{
    static const char *const NAME[][2] = {{"Wald\nweg", "Wald weg"}, {"Wald\xE2\x80\xA8weg", "Wald weg"}};
    int bad = 0;
    watched = 0;
    for (int k = 0; k < 2; k++) {
        recs = 0;
        ui_begin(1.0f);
        ui_top();
        ui_detail(NULL, ART_WARN, NAME[k][0], NULL, INK);
        ui_end();
        int lines = 0, same = 0;
        for (int i = 0; i < recs; i++)
            if (rec[i].kind == 'T')
                lines++, same += !strcmp(rec[i].text, NAME[k][1]);
        bad += lines != 1 || same != 1;
    }
    char what[160];
    snprintf(what, sizeof what, "NAME-2 a name with a control character in the top card's detail: %d of 2 not on one "
             "line with a space for it", bad);
    check(!bad, what);
}

/* G2-L1c, layout C read on every look's own ground: (1) a step to come is drawn at 0.8 of the alpha of the step under
 * way (was 0.45: under 3:1 on the light grounds); (2) the head changes one after the other, the running title out in
 * the change's first half, the done title in during its second: in no frame of the change are both drawn with an
 * alpha above FAINT. FAINT is 0, the strictest reading: the halves do not overlap, so no tolerance is needed. */
static void pour_head_change(void)
{
    enum { FAINT = 0 };
    for (int n = 0; n < 2; n++) {
        const char *const *steps = n ? badge_steps : four;
        const char *run = n ? "Badges installieren" : "Wird installiert";
        const int count = 4;
        int to_come = 0, dim_bad = 0, frames = 0, both = 0, only_run = 0, only_done = 0, icon_bad = 0;
        pour p;
        pour_start(&p, n ? &POUR_BADGES : &POUR_INSTALL, 1000);
        pour_work(&p, 1400, 0, POUR_WORKING);   /* the first step fully under way */
        layout_draw(&p, n, "Sommerwiese", 0);
        int now_a = -1;
        for (int i = layout_from; i < layout_to; i++)
            if (rec[i].kind == 'T' && !strcmp(rec[i].text, steps[0]))
                now_a = (int)(rec[i].top >> 24);
        for (int i = layout_from; i < layout_to; i++)
            for (int k = 1; k < count; k++)
                if (rec[i].kind == 'T' && !strcmp(rec[i].text, steps[k]) && pour_row_mark(&p, k) == 0)
                    to_come++, dim_bad += now_a != 255 || abs((int)(rec[i].top >> 24) - (int)(0.8f * now_a + 0.5f)) > 1;
        pour_start(&p, n ? &POUR_BADGES : &POUR_INSTALL, 1000);
        for (unsigned t = 16; t <= 16000; t += 16) {
            pour_work(&p, 1000 + t, t < 9000 ? t / 9000.0f : 1, t >= 9500 ? POUR_SUCCEEDED : POUR_WORKING);
            float e = pour_ending(&p);
            if (e <= 0 || e >= 1)
                continue;
            layout_draw(&p, n, "Sommerwiese", 0);
            int a_run = -1, a_done = -1, icon = 0;
            for (int i = layout_from; i < layout_to; i++) {
                if (rec[i].kind == 'T' && !strcmp(rec[i].text, run))
                    a_run = (int)(rec[i].top >> 24);
                else if (rec[i].kind == 'T' && !strcmp(rec[i].text, "Installiert"))
                    a_done = (int)(rec[i].top >> 24);
                icon += rec[i].kind == 'I' && atlas_sprite(&rec[i]) == ART_POUR_HEAD;
            }
            frames++;
            both += a_run > FAINT && a_done > FAINT;
            only_run += a_run > FAINT && a_done < 0;
            only_done += a_done > FAINT && a_run < 0;
            icon_bad += icon != (a_run >= 0);   /* the symbol goes with the running title */
        }
        char what[300];
        snprintf(what, sizeof what, "G2-L1c, %s: %d steps to come (want %d) at 0.8 of the step under way's alpha, %d "
                 "not; the head's change over %d frames: both titles above alpha %d in %d, only the running one in %d, "
                 "only the done one in %d, the symbol apart from its title in %d",
                 n ? "the badge write" : "the install", to_come, count - 1, dim_bad, frames, FAINT, both, only_run,
                 only_done, icon_bad);
        check(to_come == count - 1 && !dim_bad && frames >= 4 && !both && only_run > 0 && only_done > 0 && !icon_bad,
              what);
    }
}

/* every check above with the look of that number (G1: once per look this build holds) */
/* ---- FB-L (C104): the tile 3 px lower, the installing screen's spacing, the bar's stripes */

/* The element box of a pane's rim recorded as r (the image less its pad, scaled as drawn). */
static void fbl_pane_box(const typeof(rec[0]) *r, float *top, float *low, float *mx)
{
    const art_sprite *s = ui_sprite_info(ART_PANE_BIG_RIM), *t = ui_sprite_info(ART_PANE_SMALL_RIM);
    float x0, y0, x1, y1;
    rec_box(r, &x0, &y0, &x1, &y1);
    if (fabsf(r->u0 * r->tex->width - t->x) < 0.01f)
        s = t;
    float ky = (y1 - y0) / s->h, kx = (x1 - x0) / s->w;
    *top = y0 + s->pad[1] * ky, *low = y1 - s->pad[3] * ky, *mx = (x0 + s->pad[0] * kx + x1 - s->pad[2] * kx) / 2;
}

/* (1) The single tile, the carousel at rest and the empty place stand GAL_LOWER (3) px lower than 0.9.3's places
 * (the middle pane's box from y 48, the panes' middles at 88, 93, 96, 101 on the left and 88, 93, 98, 99 on the
 * right), every pane by the same 3 px; the music badge stays on the tile's corner (its middle GAL_BADGE_IN px in
 * from it); the name keeps its rule: the room from the tile's lower edge down to its capitals and from its baseline
 * down to the card's lower edge are equal but for GAL_NAME_UP px up (they differ by 2 * GAL_NAME_UP), in the single
 * tile and the carousel, and the empty place's placeholder stands on the same baseline (rounded). */
static void fbl_gallery_place(void)
{
    static const short OLD_MID[2][SWARM_SIDE + 1] = {{88, 93, 96, 101}, {88, 93, 98, 99}};
    const float cap = FONT_CAP * GAL_NAME_PX / (float)FONT_PX, card_low = GAL_CARD_Y + GAL_CARD_H;
    int tile_bad = 0, pane_bad = 0, badge_bad = 0, name_bad = 0, seen = 0;
    float tile_low = 0, above = 0, below = 0, baseline = 0;
    memset(stand_in, 0x5a, sizeof stand_in);
    for (int shot = 0; shot < 3; shot++) {
        if (shot == 0) {
            carousel_top(4, 90000);
        } else {
            recs = 0;
            watched = 0;
            ui_begin(1.0f);
            ui_f.ms = 90000;
            ui_top();
            ui_head("Design installieren", NULL);
            if (shot == 1)
                ui_gallery((ui_pic){stand_in, -1}, (ui_pic){NULL, -1}, (ui_pic){NULL, -1}, "Waldweg", 1);
            else
                ui_gallery_empty();
            ui_bottom();
            ui_end();
            watched = 2;
        }
        int rims[16], pictures, n = carousel_rims(rims, &pictures);
        float top[16], low[16], mx[16];
        for (int i = 0; i < n; i++)
            fbl_pane_box(&rec[rims[i]], &top[i], &low[i], &mx[i]);
        int mid = n - 1;    /* drawn far to near: the middle pane last */
        tile_bad += n != (shot ? 1 : 2 * SWARM_SIDE + 1) || fabsf(top[mid] - (48 + 3)) > 0.01f ||
                    fabsf(low[mid] - (48 + 3 + GAL_PICTURE)) > 0.01f || GAL_LOWER != 3;
        tile_low = low[mid];
        for (int i = 0; shot == 0 && i < n; i++) {
            int side = mx[i] < mx[mid] ? 0 : 1, k = 0;
            for (int j = 0; j < n; j++)     /* the slot: how many panes stand between it and the middle, and it */
                k += j != mid && (mx[j] - mx[mid]) * (mx[i] - mx[mid]) > 0 &&
                     fabsf(mx[j] - mx[mid]) <= fabsf(mx[i] - mx[mid]);
            pane_bad += k > SWARM_SIDE || fabsf((top[i] + low[i]) / 2 - (OLD_MID[side][k] + 3)) > 0.01f;
        }
        for (int i = 0; i < recs; i++) {
            const typeof(rec[0]) *r = &rec[i];
            if (r->kind == 'T' && !strcmp(r->text, "Waldweg")) {
                seen++;
                baseline = r->y, above = r->y - cap - tile_low, below = card_low - r->y;
                name_bad += fabsf(below - above - 2 * GAL_NAME_UP) > 0.01f;
            } else if (shot == 2 && atlas_sprite(r) == ART_EMPTY_NAME) {
                const art_sprite *s = ui_sprite_info(ART_EMPTY_NAME);
                seen++;
                float placeholder = r->y + s->pad[1] + ui_eh(ART_EMPTY_NAME) - EMPTY_NAME_BELOW;
                name_bad += fabsf(placeholder - roundf(baseline)) > 0.01f;
            } else if (shot == 1 && atlas_sprite(r) == ART_MUSIC_BADGE + ui_glass_want) {
                const art_sprite *s = ui_sprite_info(ART_MUSIC_BADGE + ui_glass_want);
                float cx = r->x + s->pad[0] + ui_ew(ART_MUSIC_BADGE + ui_glass_want) / 2.0f;
                float cy = r->y + s->pad[1] + ui_eh(ART_MUSIC_BADGE + ui_glass_want) / 2.0f;
                seen++;
                badge_bad += fabsf(cx - (GAL_PICTURE_X + GAL_PICTURE - GAL_BADGE_IN)) > 0.5f ||
                             fabsf(cy - (tile_low - GAL_BADGE_IN)) > 0.5f;
            }
        }
    }
    char what[360];
    snprintf(what, sizeof what, "FB-L (1) the tile 3 px lower (GAL_LOWER %d): the tile %d times not at y 51 to 131, %d "
             "panes of the carousel not 3 px under 0.9.3's middles, the music badge %d times off the tile's corner; "
             "the name: %g px from the tile to its capitals, %g px from its baseline to the card's edge (%d times not "
             "differing by 2 * GAL_NAME_UP = %d, the placeholder included); %d of 4 seen",
             GAL_LOWER, tile_bad, pane_bad, badge_bad, above, below, name_bad, 2 * GAL_NAME_UP, seen);
    check(!tile_bad && !pane_bad && !badge_bad && !name_bad && seen == 4, what);
}

/* (2) The installing screen (FB-L, C104): every place of G2-L from the line down, the tile and the steps 4 px higher
 * (the line 52, the holder 49, the joint 55, the tongs 53, a piece 68, the tile 90, the picture 109, the steps 86
 * then); the name 3 px further from the tile (5 then from the tile's edge to its line box) and since C110-L 2 px
 * more, the end's line 4 px further from the screen's lower edge (28 then; C110-F's 2 px less for a two-line name
 * gone again with it, C146). Drawn, at the
 * end of the install and of the badge write: nothing of the line, its holders, tongs and pieces on or within 2 rows
 * of the head's dotted rule; the name's capitals at least 12.5 px under the tile (7.9 at G2-L, 10.9 at FB-L); the
 * gaps between a name and the end's line by the font's real extremes (C110-F, the review of the join): the deepest
 * foot of any letter of a name and the highest top of the end's own letters (FONT_DEEP, FONT_TALL): a short name's
 * and a long one's, both one line since C146, at least 17 px (17.47 at the end's place before C110-F and again
 * since); the end's baseline at least 17.5 px over the screen's lower edge. */
static void fbl_pour_gaps(void)
{
    static const char LONG[] = "Waldweg im ersten Morgenlicht nach dem langen Regen am Fluss entlang";
    static const char END[] = "Erfolgreich installiert";
    int moved = POUR_LINE_Y == 52 - 4 && POUR_HOLDER_Y == 49 - 4 && POUR_JOINT_Y == 55 - 4 && POUR_TONGS_Y == 53 - 4 &&
                POUR_PIECE_Y == 68 - 4 && POUR_TILE_Y == 90 - 4 && POUR_PICTURE_Y == 109 - 4 && POUR_LIST_Y == 86 - 4 &&
                POUR_NAME_Y - (POUR_TILE_Y + POUR_TILE_H) == 5 + 3 + 2 && SCREEN_H - POUR_END_Y == 28 + 4;
    const float cap = FONT_CAP * POUR_NAME_PX / (float)FONT_PX, end_cap = FONT_CAP * POUR_END_PX / (float)FONT_PX;
    /* in cap heights, measured on the font the host renders are set in (SF Rounded Medium, its capitals scaled to
     * FONT_CAP): the deepest foot of the letters the renders know (the cedilla's; "g" 0.272), the highest top of
     * "Erfolgreich installiert" and "Installed successfully" (their "f", "l", "d"; a capital's 1, the font's highest,
     * the grave over "E", 1.299, the end never shows) */
    const float FONT_DEEP = 0.319f, FONT_TALL = 1.003f;
    /* the head's dotted rule alone: its lowest row */
    recs = 0;
    watched = 0;
    ui_begin(1.0f);
    ui_top();
    int rule_from = recs;
    ui_dots(POUR_HEAD_DOTS_Y);
    int rule_to = recs;
    ui_bottom();
    ui_end();
    watched = 2;
    int rule_low = -1;
    for (int i = rule_from; i < rule_to; i++) {
        int t = 1 << 20, l = (int)ceilf(rec[i].y + rec[i].h) - 1;     /* a rectangle: its rows */
        if (rec[i].kind == 'I')
            l = -1, rec_rows(&rec[i], &t, &l);
        rule_low = rec[i].kind != 'T' && l > rule_low ? l : rule_low;
    }
    int bad = 0;
    float line_gap = 1e9f, name_gap = 0, one_gap = 0, long_gap = 0, low_gap = 0;
    for (int n = 0; n < 2; n++) {
        pour p;
        pour_start(&p, n ? &POUR_BADGES : &POUR_INSTALL, 1000);
        for (unsigned t = 16; t <= 16000; t += 16)
            pour_work(&p, 1000 + t, t < 9000 ? t / 9000.0f : 1, t >= 9500 ? POUR_SUCCEEDED : POUR_WORKING);
        for (int long_name = 0; long_name < 2; long_name++) {
            layout_draw(&p, n, long_name ? LONG : "Sommerwiese", 0);
            float name_last = -1, name_first = -1, end_base = -1;
            for (int i = layout_from; i < layout_to; i++) {
                const typeof(rec[0]) *r = &rec[i];
                int id = atlas_sprite(r);
                if (r->kind == 'T' && !strcmp(r->text, END))
                    end_base = r->y;
                else if (r->kind == 'T' && r->x == POUR_TILE_X + POUR_TILE_W / 2.0f)
                    name_first = name_first < 0 ? r->y : name_first, name_last = r->y;
                else if (id == ART_POUR_LINE || id == ART_POUR_KNOT || id == ART_POUR_HOLDER ||
                         id == ART_POUR_TONGS || id == ART_POUR_PIECE_TURN) {
                    int t = 1 << 20, l = -1;
                    rec_rows(r, &t, &l);
                    line_gap = fminf(line_gap, (float)(t - rule_low - 1));
                }
            }
            bad += name_first < 0 || end_base < 0 || name_first != name_last;
            if (!long_name)
                name_gap = name_first - cap - (POUR_TILE_Y + POUR_TILE_H), low_gap = SCREEN_H - end_base,
                one_gap = (end_base - FONT_TALL * end_cap) - (name_last + FONT_DEEP * cap);
            else
                long_gap = (end_base - FONT_TALL * end_cap) - (name_last + FONT_DEEP * cap);
            bad += long_name ? long_gap < 17 : name_gap < 12.5f || one_gap < 17 || low_gap < 17.5f;
        }
    }
    bad += line_gap < 2;
    char what[600];
    snprintf(what, sizeof what, "FB-L (2) the installing screen: every place from the line down 4 px higher, the "
             "name 3 px further from the tile and C110-L's 2 px more, the end 4 px further from the lower edge (%s); "
             "drawn: %g empty rows between the head's rule and the line's things, %.2f px from the tile to the name's "
             "capitals, %.2f px from a short name's deepest foot and %.2f px from a long one's to the end's highest "
             "top (%.3f and %.3f cap heights), %.2f px from the end's baseline to the lower edge (%d wrong)",
             moved ? "yes" : "no", line_gap, name_gap, one_gap, long_gap,
             FONT_DEEP, FONT_TALL, low_gap, bad);
    check(moved && !bad, what);
}

/* (3) The download's bar (FB-L, C104: "making the stripes take the full height/being even ... neat to the edges"),
 * in the look's glass styles, at the smallest fill, a third, two thirds and the whole bar, at each of the stripes'
 * STRIPE_W phases. Per screen pixel the fill's alpha F (against the fill's most opaque texel) and the stripes' S: the
 * stripes are the period P (read off the whole bar's straight middle at that phase, where F is 1, by column from the
 * fill's left end and row) times F, within 2/255, at the ends too (cut by the outline, round or slanted, nothing
 * outside it); P reaches every row the fill has and is as strong in each (its brightest per row within 0.03 of each
 * other: even); the fill stays inside the track's box. C110-L (the fill's left end slanted in Frost and Prisma): in
 * every row of the fill at every width the stripes reach its left edge (its first pixel at least a quarter covered)
 * in some phase. */
static float fbl_texel_alpha(int sx, int sy)
{
    return (((const uint32_t *)atlas.data)[((sy >> 3) * (atlas.width >> 3) + (sx >> 3)) * 64 + ui_morton(sx & 7, sy & 7)] &
            255) / 255.0f;
}

#define FBL_W 336
static void fbl_bar(rect bar, float fraction, unsigned ms, float f[PROGRESS_H][FBL_W], float s[PROGRESS_H][FBL_W],
                    int *x0)
{
    recs = 0;
    watched = 0;
    ui_begin(1.0f);
    ui_f.ms = ms;
    ui_top();
    ui_progress(bar, fraction);
    ui_bottom();
    ui_end();
    watched = 2;
    memset(f, 0, sizeof(float) * PROGRESS_H * FBL_W);
    memset(s, 0, sizeof(float) * PROGRESS_H * FBL_W);
    *x0 = 1 << 20;
    for (int i = 0; i < recs; i++) {
        int id = atlas_sprite(&rec[i]), stripes = id == ART_STRIPES + ui_glass_want;
        if (id != ART_PROGRESS_FILL + ui_glass_want && !stripes)
            continue;
        float bx0, by0, bx1, by1;
        rec_box(&rec[i], &bx0, &by0, &bx1, &by1);
        if (!stripes)
            *x0 = (int)fminf((float)*x0, bx0);
        for (int y = (int)floorf(by0); y < (int)ceilf(by1); y++)
            for (int x = (int)floorf(bx0); x < (int)ceilf(bx1); x++) {
                int col = x - (int)bar.x, row = y - (int)bar.y;
                if (col >= 0 && col < FBL_W && row >= 0 && row < PROGRESS_H) {
                    float a = rec_alpha(&rec[i], x + 0.5f, y + 0.5f);
                    float *to = stripes ? &s[row][col] : &f[row][col];
                    *to = fmaxf(*to, a);
                }
            }
    }
}

static void fbl_stripes(void)
{
    static float f[PROGRESS_H][FBL_W], s[PROGRESS_H][FBL_W], mid_s[PROGRESS_H][STRIPE_W], mid_f[PROGRESS_H];
    static const float FRACTION[4] = {0.001f, 1 / 3.0f, 2 / 3.0f, 1};
    const rect bar = {TOP_PAD + CARD_PAD, 100, TOP_W - 2 * (TOP_PAD + CARD_PAD), PROGRESS_H};
    int styles = pour_look == LOOK_STOCK ? GLASS_COUNT : 1, off = 0, uneven = 0, bare = 0, outside = 0, frames = 0;
    int left_bare = 0, left_rows = 0;
    float worst = 0;
    for (int g = 0; g < styles; g++) {
        char left_has[4][PROGRESS_H] = {{0}}, left_hit[4][PROGRESS_H] = {{0}};
        ui_look(pour_look, ART_SCHEME_WEISS, styles == 1 ? GLASS_SHARD : g, PATTERN_HOUSES);
        frame_begin();          /* the scheme's atlas unpacked */
        ui_end();
        const art_sprite *fs = ui_sprite_info(ART_PROGRESS_FILL + ui_glass_want);
        float full = 0;
        for (int y = 0; y < fs->h; y++)
            for (int x = 0; x < fs->w; x++)
                full = fmaxf(full, fbl_texel_alpha(fs->x + x, fs->y + y));
        for (int phase = 0; phase < STRIPE_W; phase++) {
            unsigned ms = 90000 - 90000 % STRIPE_MS + (unsigned)(phase * STRIPE_MS / STRIPE_W);
            int x0;
            fbl_bar(bar, 1, ms, f, s, &x0);
            float first = -1;
            for (int y = 0; y < PROGRESS_H; y++) {      /* the straight middle of the whole bar, one period */
                float most = 0;
                for (int j = 0; j < STRIPE_W; j++) {
                    int sx = x0 + fs->cap[0] + STRIPE_W + j, c = sx - (int)bar.x;
                    mid_s[y][(sx - x0) % STRIPE_W] = s[y][c];
                    mid_f[y] = f[y][c] / full;
                    most = fmaxf(most, mid_f[y] > 0.2f ? s[y][c] / mid_f[y] : 0);
                }
                bare += mid_f[y] > 0.2f && most < 0.05f;
                if (mid_f[y] >= 0.99f && first < 0)
                    first = most;
                uneven += mid_f[y] >= 0.99f && fabsf(most - first) > 0.03f;
            }
            for (int k = 0; k < 4; k++) {
                fbl_bar(bar, FRACTION[k], ms, f, s, &x0);
                frames++;
                for (int y = 0; y < PROGRESS_H; y++)
                    for (int c = 0; c < FBL_W; c++) {
                        int pc = ((c + (int)bar.x - x0) % STRIPE_W + STRIPE_W) % STRIPE_W;
                        float d = fabsf(s[y][c] * mid_f[y] - mid_s[y][pc] * f[y][c] / full);
                        worst = fmaxf(worst, d);
                        off += d > 2 / 255.0f;
                        outside += f[y][c] == 0 && s[y][c] > 0;
                    }
                outside += x0 < bar.x;
                for (int y = 0; y < PROGRESS_H; y++)
                    for (int c = 0; c < FBL_W; c++)
                        if (f[y][c] >= full / 4) {
                            left_has[k][y] = 1, left_hit[k][y] |= s[y][c] > 0;
                            break;
                        }
            }
        }
        for (int k = 0; k < 4; k++)
            for (int y = 0; y < PROGRESS_H; y++)
                left_rows += left_has[k][y], left_bare += left_has[k][y] && !left_hit[k][y];
    }
    ui_look(pour_look, ART_SCHEME_WEISS, 1, PATTERN_HOUSES);
    char what[420];
    snprintf(what, sizeof what, "FB-L (3) the bar's stripes in %s, %d frames (4 widths, %d phases, %d styles): %d "
             "pixels not the period times the fill's outline (worst %.4f), %d rows of the fill without stripes, %d "
             "rows not as strong as the others, %d pixels or ends outside the fill or the track; C110-L: %d of %d "
             "rows where the stripes never reach the fill's left edge",
             look_get(pour_look)->name, frames, STRIPE_W, styles, off, worst, bare, uneven, outside, left_bare,
             left_rows);
    check(frames == 4 * STRIPE_W * styles && !off && !bare && !uneven && !outside && !left_bare && left_rows,
          what);
}

/* ---- C110-L (C110): the installing name 2 px lower; C146: Frost's and Prisma's bar in their own shapes */

/* The bar of Frost and Prisma (BAR-S, C146 "frost a , prisma b"; until then art.py liq_bars' Shard rod, slanted at
 * both ends, C110-L, which no look draws since), at the smallest fill, fills of 1 to 8 px of the bar's 336 (all drawn
 * at the fill's least width, its two ends), 5 %, a third, two thirds, the whole bar and the sliding piece: per row,
 * where the track's and the fill's alpha first and last reach half their glass's own, to a tenth of a pixel. Each
 * end's shape is its outline's profile: per row of 2 to 9 (clear of the top and bottom rows, where a round end and a
 * point are a pixel or less wide) how far the edge stands in from the edge at the bar's middle (the mean of rows 5 and
 * 6), in px; Frost's ends round (its track a half circle of PROGRESS_R, the lozenge of glass one pixel in, its lower
 * edge the glass's polished side, so its profile is not mirrored top to bottom), Prisma's pointed (the channel and the
 * crystal a point each, 1 px a row: 45 degrees). Measured at BAR-S, the same at every fraction and at both ends to
 * 0.01 px. Every edge (track left and right, fill left and right) at every fraction has the look's measured profile of
 * its glass within C110L_TOL px in every row (the left and the right end alike, so the ends are mirrored and the fill
 * never takes a slant or a flat end); in those rows the fill lies inside the track (its ends at most half a pixel
 * outside the track's); at the whole bar and at the left at every fraction the fill's end stands the look's measured
 * gap in from the track's in the bar's middle rows (within a quarter of a pixel; measured 0.99 to 1.00 in both). With
 * the two looks' profiles swapped, and with the head's Shard rod (603b620's art), the check fails in both looks.
 * C110L_DUMP=1 prints the measured profiles. */
#define C110L_TOL 0.1f
static const struct {
    const char *name;
    float track[8], fill[8], gap;       /* the profile in rows 2 to 9; the fill's end in from the track's */
} C110L_ENDS[] = {
    {"frost", {1.11f, 0.56f, 0.11f, 0, 0, 0.11f, 0.56f, 1.11f}, {1.12f, 0.35f, 0.02f, 0, 0, 0.15f, 0.72f, 1.40f}, 1},
    {"prism", {3, 2, 1, 0, 0, 1, 2, 3}, {3, 2, 1, 0, 0, 1, 2, 3}, 1},
};
#define C110L_M 12              /* columns recorded beside the bar on either side */
#define C110L_W (FBL_W + 2 * C110L_M)
static void c110l_bar(rect bar, float fraction, unsigned ms, float f[PROGRESS_H][C110L_W],
                      float t[PROGRESS_H][C110L_W])
{
    recs = 0;
    watched = 0;
    ui_begin(1.0f);
    ui_f.ms = ms;
    ui_top();
    ui_progress(bar, fraction);
    ui_bottom();
    ui_end();
    watched = 2;
    memset(f, 0, sizeof(float) * PROGRESS_H * C110L_W);
    memset(t, 0, sizeof(float) * PROGRESS_H * C110L_W);
    for (int i = 0; i < recs; i++) {
        int id = atlas_sprite(&rec[i]);
        if (id != ART_PROGRESS_FILL + ui_glass_want && id != ART_PROGRESS + ui_glass_want)
            continue;
        float (*to)[C110L_W] = id == ART_PROGRESS + ui_glass_want ? t : f;
        for (int y = 0; y < PROGRESS_H; y++)
            for (int c = 0; c < C110L_W; c++)
                to[y][c] = fmaxf(to[y][c], rec_alpha(&rec[i], bar.x - C110L_M + c + 0.5f, bar.y + y + 0.5f));
    }
}

/* Where a row first (*l) and last (*r) reaches half the glass's alpha in its middle (halfway between the first and
 * the last column with any), in px from the bar's left edge */
static void c110l_edges(const float *a, float *l, float *r)
{
    int c0 = -1, c1 = -1;
    for (int c = 0; c < C110L_W; c++)
        if (a[c] > 0.05f)
            c0 = c0 < 0 ? c : c0, c1 = c;
    float h = c0 < 0 ? 2 : a[(c0 + c1) / 2] / 2;
    *l = *r = -1e9f;
    for (int c = 1; c < C110L_W && *l < -1e8f; c++)
        if (a[c - 1] < h && a[c] >= h)
            *l = c - 1 + 0.5f + (h - a[c - 1]) / (a[c] - a[c - 1]) - C110L_M;
    for (int c = C110L_W - 1; c > 0 && *r < -1e8f; c--)
        if (a[c] < h && a[c - 1] >= h)
            *r = c - 0.5f + (a[c - 1] - h) / (a[c - 1] - a[c]) - C110L_M;
}

static void c110l_bar_slant(void)
{
    static float f[PROGRESS_H][C110L_W], t[PROGRESS_H][C110L_W];
    static const float FRACTION[] = {0.001f, 1 / 336.0f, 2 / 336.0f, 3 / 336.0f, 4 / 336.0f, 5 / 336.0f, 6 / 336.0f,
                                     7 / 336.0f, 8 / 336.0f, 0.05f, 1 / 3.0f, 2 / 3.0f, 1, -1};
    const char *name = look_get(pour_look)->name;      /* by name: test_look's fake look stands under Prisma's number */
    int at = -1;
    for (int i = 0; i < (int)(sizeof C110L_ENDS / sizeof C110L_ENDS[0]); i++)
        at = strcmp(name, C110L_ENDS[i].name) ? at : i;
    if (at < 0)
        return;
    const rect bar = {TOP_PAD + CARD_PAD, 100, TOP_W - 2 * (TOP_PAD + CARD_PAD), PROGRESS_H};
    ui_look(pour_look, ART_SCHEME_WEISS, GLASS_SHARD, PATTERN_HOUSES);
    frame_begin();
    ui_end();
    int off = 0, outside = 0, gaps = 0, missing = 0;
    float worst = 0, worst_gap = 0;
    for (int k = 0; k < (int)(sizeof FRACTION / sizeof FRACTION[0]); k++) {
        c110l_bar(bar, FRACTION[k], 400, f, t);
        float e[4][PROGRESS_H];       /* track left, track right, fill left, fill right */
        for (int y = 0; y < PROGRESS_H; y++) {
            c110l_edges(t[y], &e[0][y], &e[1][y]);
            c110l_edges(f[y], &e[2][y], &e[3][y]);
        }
        for (int j = 0; j < 4; j++) {
            const float *want = j < 2 ? C110L_ENDS[at].track : C110L_ENDS[at].fill, mid = (e[j][5] + e[j][6]) / 2;
            for (int y = 2; y <= 9; y++) {
                missing += e[j][y] < -1e8f;
                float in = j % 2 ? mid - e[j][y] : e[j][y] - mid, d = fabsf(in - want[y - 2]);
                worst = fmaxf(worst, d);
                off += d > C110L_TOL;
                if (getenv("C110L_DUMP"))
                    printf("c110l %s fraction %g edge %d row %d in %.3f\n", name, FRACTION[k], j, y, in);
            }
        }
        for (int y = 2; y <= 9; y++)
            outside += e[2][y] < e[0][y] - 0.5f || e[3][y] > e[1][y] + 0.5f;
        for (int y = 5; y <= 6; y++)
            for (int j = 0; j < 2; j++) {
                if (j && FRACTION[k] != 1)
                    continue;
                float g = j ? e[1][y] - e[3][y] : e[2][y] - e[0][y];
                if (getenv("C110L_DUMP"))
                    printf("c110l %s fraction %g gap %s row %d %.3f\n", name, FRACTION[k], j ? "right" : "left", y, g);
                if (FRACTION[k] < 0 && !j)
                    continue;               /* the sliding piece stands anywhere */
                worst_gap = fmaxf(worst_gap, fabsf(g - C110L_ENDS[at].gap));
                gaps += fabsf(g - C110L_ENDS[at].gap) > 0.25f;
            }
    }
    ui_look(pour_look, ART_SCHEME_WEISS, 1, PATTERN_HOUSES);
    char what[600];
    snprintf(what, sizeof what, "C110-L/C146 the bar in %s with its own ends (%s), the track too: %d edge rows off the "
             "measured profile by more than %.2f px (worst %.3f), %d rows with the fill outside the track, %d of the "
             "fill's ends not %.1f px in from the track's (worst %.3f off), %d edges not found", name,
             strcmp(name, "frost") ? "pointed" : "round", off, C110L_TOL, worst, outside, gaps, C110L_ENDS[at].gap,
             worst_gap, missing);
    check(!off && !outside && !gaps && !missing, what);
}

/* C110-D (C140: "achso, wenn es da höhen gibt, dann mach es auch auf höhe der kachel. was dann hinter fällt soll 0
 * sein"): the installing screen half way, pieces hanging from the line, drawn as main.c draws it. With the 3D slider at
 * its top, in both eyes and both works: every draw of the line, its knot, holders, tongs and hanging pieces has the
 * tile's shift, roundf(DEPTH_ICON / 2); what ui_pour draws before the head (the circle, the head's rule) has none;
 * the head, the name and the list have none, as at 3c8c914; the line covers x 0 to TOP_W without a gap. With the
 * slider at 0 nothing is shifted and the line is the box 0 to TOP_W it was; the frame's fingerprint (its records,
 * -0 as 0) is printed to be compared with 3c8c914's. C110D_DUMP=1 lists the frame's records. */
static int c110d_from[2], c110d_to[2];
static void c110d_draw(const pour *p, int n, float slider)
{
    recs = 0;
    watched = 3;
    slider_now = slider;
    ui_begin(1.0f);
    do {
        ui_top();
        c110d_from[ui_f.eye] = recs;
        ui_pour(p, n ? "Badges installieren" : "Wird installiert", "Installiert", "Sommerwiese", n ? NULL : stand_in,
                n ? ART_BADGES : -1, n ? badge_steps : four, "Erfolgreich installiert", NULL);
        int eye = ui_f.eye;
        ui_bottom();            /* a check that pops is a picture: it waits for the screen's end (order.h) */
        c110d_to[eye] = recs;
    } while (ui_next_eye());
    ui_end();
    watched = 2;
    slider_now = 0;
}

static void c110d_line_depth(void)
{
    const char *name = look_get(pour_look)->name;
    const float want = roundf(DEPTH_ICON / 2.0f);
    int bad_line = 0, bad_tile = 0, bad_behind = 0, bad_flat = 0, bad_cover = 0, bad_flat0 = 0, seen[6] = {0};
    float tile[2] = {0}, line[2] = {0}, behind[2] = {0}, cover[2][2] = {{0}}, cover0[2] = {0};
    unsigned long sum = 2166136261u;
    for (int n = 0; n < 2; n++) {
        pour p;
        pour_start(&p, n ? &POUR_BADGES : &POUR_INSTALL, 1000);
        for (unsigned t = 16; t <= 9000; t += 16)
            pour_work(&p, 1000 + t, 0.5f, POUR_WORKING);
        for (int s = 1; s >= 0; s--) {
            c110d_draw(&p, n, (float)s);
            for (int e = 0; e < 1 + s; e++) {
                float x0 = 1e9f, x1 = -1e9f, wide = 0;
                int head = 0;
                for (int i = e ? c110d_to[0] : 0; getenv("C110D_DUMP") && i < c110d_to[e]; i++)
                    printf("c110d %s n%d s%d e%d %s %c id %d shift %g at %g,%g %gx%g %s\n", name, n, s, e,
                           i < c110d_from[e] ? "top" : "pour", rec[i].kind, atlas_sprite(&rec[i]),
                           (rec[i].eye ? -rec[i].tx : rec[i].tx) + 0.0f, rec[i].x + 0.0f, rec[i].y, rec[i].w, rec[i].h,
                           rec[i].text);
                for (int i = c110d_from[e]; i < c110d_to[e]; i++) {
                    const typeof(rec[0]) *r = &rec[i];
                    int id = atlas_sprite(r);
                    float shift = r->eye ? -r->tx : r->tx;
                    head |= r->kind == 'T' || id == ART_POUR_HEAD || id == ART_POUR_TITLE_DONE;
                    if (id == ART_POUR_LINE) {
                        float a, b, c, d;
                        rec_box(r, &a, &b, &c, &d);
                        x0 = fminf(x0, a), x1 = fmaxf(x1, c), wide += c - a;
                    }
                    if (!s) {
                        bad_flat0 += r->tx != 0;
                        continue;
                    }
                    if (!head) {
                        bad_behind += shift != 0, seen[0]++;
                        behind[e] = id == ART_DROP_SUN ? shift + 0.0f : behind[e];
                    } else if (id == ART_POUR_LINE || id == ART_POUR_KNOT || id == ART_POUR_HOLDER ||
                               id == ART_POUR_TONGS || id == ART_POUR_PIECE) {
                        bad_line += shift != want, line[e] = shift;
                        seen[id == ART_POUR_LINE ? 1 : id == ART_POUR_KNOT ? 2 : id == ART_POUR_HOLDER ? 3 :
                             id == ART_POUR_TONGS ? 4 : 5]++;
                    } else if (id == ART_POUR_BACK || id == ART_POUR_FRONT) {
                        bad_tile += shift != want, tile[e] = shift;
                    } else if (r->kind == 'T' || id == ART_POUR_HEAD || id == ART_POUR_TITLE_DONE ||
                               id == ART_POUR_RING || id == ART_POUR_NOW || id == ART_POUR_TICK) {
                        bad_flat += shift != 0;
                    }
                }
                if (s)
                    cover[e][0] = x0, cover[e][1] = x1, bad_cover += x0 > 0 || x1 < TOP_W || wide != x1 - x0;
                else
                    cover0[0] = x0, cover0[1] = x1, bad_cover += x0 != 0 || x1 != TOP_W || wide != x1 - x0;
            }
            if (!s)
                for (int i = 0; i < recs; i++) {
                    float v[10] = {rec[i].x, rec[i].y, rec[i].w, rec[i].h, rec[i].u0, rec[i].v0, rec[i].tx, rec[i].ty,
                                   rec[i].angle, (float)rec[i].kind};
                    for (int k = 0; k < 10; k++) {
                        float f = v[k] + 0.0f;
                        uint32_t bits;
                        memcpy(&bits, &f, sizeof bits);
                        sum = ((sum ^ bits) * 16777619u) & 0xffffffffu;
                    }
                    for (const char *c = rec[i].text; *c; c++)
                        sum = ((sum ^ (unsigned char)*c) * 16777619u) & 0xffffffffu;
                }
        }
    }
    char what[600];
    snprintf(what, sizeof what, "C110-D %s, the slider at 1, shifts left/right eye: tile %g/%g, line, knot, holders, "
             "tongs and hanging pieces %g/%g (want %g; draws: line %d, knot %d, holder %d, tongs %d, piece %d), the "
             "layer behind: the circle %g/%g, %d draws in all, %d head, name and list draws off 0; the line covers x %g to %g and %g "
             "to %g of 0 to %d; %d wrong. The slider at 0: %d draws shifted, the line %g to %g, fingerprint %08lx",
             name, tile[0], tile[1], line[0], line[1], want, seen[1], seen[2], seen[3], seen[4], seen[5], behind[0],
             behind[1], seen[0], bad_flat, cover[0][0], cover[0][1], cover[1][0], cover[1][1], TOP_W,
             bad_line + bad_tile + bad_behind + bad_flat + bad_cover, bad_flat0, cover0[0], cover0[1], sum);
    check(!bad_line && !bad_tile && !bad_behind && !bad_flat && !bad_cover && !bad_flat0 && seen[0] && seen[1] &&
          seen[2] && seen[3] && seen[4] && seen[5], what);
}

static void checks_of_look(int id)
{
    printf("-- the look %s\n", look_get(id)->name);
    ui_look(id, 0, 1, 0);
    for (int pass = 0; pass < 2; pass++) {      /* the first frame unpacks the sheets */
        frame_begin();
        ui_end();
    }
    const char *held[2] = {"", ", held"};
    char name[96];
    rect tile = {TILE_X1, DTILE_Y, TILE_W, DTILE_H}, wide = {TILE_X1, WIDE_Y, TILE_X2 + TILE_W - TILE_X1, WIDE_H};
    rect upper = {GO_X, GO_TWO_Y1, GO_W, GO_H}, lower = {GO_X, GO_TWO_Y2, GO_W, GO_H};
    rect keep = {TILE_X1, CHOICE_Y, TILE_W, CHOICE_H}, drop = {TILE_X2, CHOICE_Y, TILE_W, CHOICE_H};
    for (int down = 0; down < 2; down++) {
        int state = down ? UI_DOWN : 0;
        /* each element alone */
        for (int e = 0; e < 4; e++) {
            static const char *const WHAT[4] = {"a tile", "a wide button", "the green button", "the cream button"};
            frame_begin();
            if (e == 0)
                ui_tile(FACE_DTILE, tile, state, ART_SD, "Von SD installieren", DESIGNS_PX, DESIGNS_LH, DESIGNS_GAP,
                        TILE_INK);
            else if (e == 1)
                ui_wide(FACE_WIDE, wide, state, ART_INSTALLED, "Installierte Designs", DESIGNS_PX, DESIGNS_GAP,
                        TILE_INK);
            else if (e == 2)
                ui_go(upper, state, ART_DOWNLOAD, "Installieren");
            else
                ui_alt(lower, state, ART_NO_MUSIC, "Ohne Musik installieren");
            ui_end();
            snprintf(name, sizeof name, "%s%s", WHAT[e], held[down]);
            judge(name, 1);
        }
        /* the Designs menu as main.c draws it, the selection over the first tile */
        frame_begin();
        ui_tile(FACE_DTILE, tile, state, ART_SD, "Von SD installieren", DESIGNS_PX, DESIGNS_LH, DESIGNS_GAP, TILE_INK);
        ui_select(tile, TILE_R);
        ui_tile(FACE_DTILE, (rect){TILE_X2, DTILE_Y, TILE_W, DTILE_H}, state, ART_QR, "QR-Code scannen", DESIGNS_PX,
                DESIGNS_LH, DESIGNS_GAP, TILE_INK);
        ui_wide(FACE_WIDE, wide, state, ART_INSTALLED, "Installierte Designs", DESIGNS_PX, DESIGNS_GAP, TILE_INK);
        ui_end();
        snprintf(name, sizeof name, "the Designs menu with the selection%s", held[down]);
        judge(name, 0);
        /* the check screen's two buttons with the selection, and the Done screen's Keep and Delete */
        frame_begin();
        ui_go(upper, state, ART_DOWNLOAD, "Installieren");
        ui_select(upper, GO_R);
        ui_alt(lower, state, ART_NO_MUSIC, "Ohne Musik installieren");
        ui_tile(FACE_CHOICE, keep, state, ART_KEEP, "Behalten", CHOICE_PX, CHOICE_LH, CHOICE_GAP, TILE_INK);
        ui_tile(FACE_CHOICE, drop, state, ART_TRASH, "Löschen", CHOICE_PX, CHOICE_LH, CHOICE_GAP, TILE_INK);
        ui_end();
        snprintf(name, sizeof name, "two buttons with the selection, two choice tiles%s", held[down]);
        judge(name, 0);
    }
    /* A1: the top screens */
    static const char *const TOPS[9] = {"a list's top", "a check screen's top", "the Designs menu's top",
                                        "a problem's top", "the installing screen's top", "the settings' top",
                                        "the Done screen's top", "a QR code's top", "the start screen"};
    ui_look(id, 0, 1, PATTERN_HOUSES);
    watched = 0;
    for (int k = 0; k < 9; k++) {
        recs = 0;
        ui_begin(1.0f);
        ui_top();
        top_screen(k);
        ui_bottom();
        ui_end();
        judge_top(TOPS[k]);
    }
    choices_at_top(ui_lk->name);
    pour_frames();
    pour_look = id;
    if (id == LOOK_STOCK) {
        loader_top();
        loader_notice();
        black_cover();
        pour_numbers();
    }
    /* the installing screen's tile (C44: every look's, drawn by the shared pour_ui.h from the look's images): the same
     * checks in every look. They compare whole frames of the top screen, so in a look whose ground moves by the
     * clock (A1: Frost's, Nachtglas's, Prisma's) the clock stands for them; the tile's own motion is the model's,
     * by the times the checks hand it. Stock's run as on the tile's own line, the clock running */
    clock_held = id != LOOK_STOCK;
    pour_closed_top();
    pour_inside_check();
    pour_picture_order();
    pour_still();
    pour_full_landing();
    pour_pieces_drawn();
    pour_melt_drawn();
    pour_holders_drawn();
    pour_leave();
    pour_holders_still();
    pour_answer();
    pour_recess();
    pour_glow_check();
    pour_balls_clear();
    pour_layout_c();
    detail_controls();
    pour_head_change();
    fbl_gallery_place();    /* FB-L */
    fbl_pour_gaps();
    fbl_stripes();
    c110l_bar_slant();      /* C110-L */
    c110d_line_depth();     /* C110-D */
    clock_held = 0;
    if (id == LOOK_STOCK) {     /* Stock's own: its carousel's gap, its ground over the bar */
        carousel_apart();
        carousel_card();
        popup_hides_bar();
    }
    wrench();
}

/* FB-E2: what a sign means, this test's own table (not ui.h's rule): the top card's head's warning and folder and the
 * footer's warning, folder and house, and the notes' signs, whatever their size; 0 for an image that is no sign */
static int sign_meaning(int sprite)
{
    switch (sprite) {
    case ART_WARN:
    case ART_WARN_FOOT:
    case ART_INFO_WARN:
        return 1;
    case ART_FOLDER_BIG:
    case ART_FOLDER_FOOT:
        return 2;
    case ART_INFO_SD:
        return 4;
    case ART_HOME_FOOT:
        return 32;
    default:
        return 0;
    }
}

/* The signs among the images recorded, one bit per meaning */
static int signs_recorded(void)
{
    int m = 0;
    for (int i = 0; i < recs; i++)
        m |= sign_meaning(atlas_sprite(&rec[i]));
    return m;
}

/* A frame of a problem's pair of screens: the top card's head with sprite head (-1: none, as a gallery's) and the
 * footer with sprite foot before its line (-1: none), the box under it on the bottom screen, whichever screen is
 * watched */
static float info_pair(int head, int foot, const char *title, const char *text, const char *path, float *h)
{
    recs = 0;
    ui_begin(1.0f);
    ui_top();
    if (head >= 0)
        ui_detail(NULL, head, "Kopf", NULL, INK);
    ui_foot(FOOT_DOTS_Y, foot, "Fuss", MUTED, 0, NULL);
    ui_bottom();
    *h = ui_info_height(title, text, path, 0);
    float y = ui_info_box(title, text, path, 0, 0);
    ui_end();
    return y;
}

/* C135 ("nur text, links zentriert"): the box just drawn at x, y, h high (its records in rec) is text only: no sign
 * (ART_INFO_*) drawn, every line and the path's from the box's inner left edge, the block (the lines and the path's
 * line) centred in the box's height within a pixel, read off the first line's baseline (a box without text: no sign
 * alone). 1 when so; signs the signs drawn, off the room above the block less the room under it. */
static int info_text_only(float x, float y, float h, int *signs, float *off)
{
    int wrong_left = 0;
    float first = -1;
    *signs = 0;
    for (int i = 0; i < recs; i++) {
        int id = atlas_sprite(&rec[i]);
        *signs += id == ART_INFO_WARN || id == ART_INFO_SD;
        if (rec[i].kind == 'T') {
            wrong_left += fabsf(rec[i].x - (x + info_pad_x)) > 0.5f;
            first = first < 0 ? rec[i].y : first;
        }
    }
    if (first < 0) {
        *off = 0;
        return !*signs;
    }
    float top = first - info_text.lh / 2 - FONT_CAP * info_text.px / (float)FONT_PX / 2;
    float th = ui_block_height(&info_text) +
               (info_path[0] ? INFO_PATH_GAP + roundf(INFO_PATH_PX * INFO_LH_PCT / 100.0f) : 0);
    *off = (top - y) - (y + h - top - th);
    return !*signs && !wrong_left && fabsf(*off) <= 1.0f;
}

/* C110-E, C135: the text of a box broken as it would be at its size in the box's whole inner width; 1 when its lines
 * differ (room kept for a sign) */
static int info_room_wrong(const char *text)
{
    static ui_block b;
    float w = info_w - 2 * info_pad_x;
    ui_wrap(&b, text ? text : "", w, info_text.px, info_text.px, 0, 0, INFO_PX * INFO_LH_PCT / 100.0f);
    int differ = b.count != info_text.count;
    for (int i = 0; !differ && i < b.count; i++)
        differ = strcmp(b.line[i], info_text.line[i]) != 0;
    return differ;
}

/* ---- FB-E: the information box (ui_info_box) of every problem, refusal and empty state, in each look: every
 * letter inside the box, nothing after a picture's texture (R7); and a refusing popup's sign above its first
 * letters. C135: each under a top card without a sign in its head, with the warning, with the folder: the box text
 * only (no sign on the bottom screen), from its inner left edge, centred in its height, broken in its whole inner
 * width */
static void info_checks(int id)
{
    static const struct {
        const char *head, *text, *path;
    } CASES[] = {
        {"Kann nicht installiert werden", "Keine lesbare Zip, oder beschädigt.", NULL},
        {"Nicht geschrieben, Sicherung ist zurück",
         "Beim Schreiben der Badge-Daten ist ein Fehler aufgetreten (Schritt 4, Code FFFFFFFF). Die Sicherung wurde "
         "zurückgeschrieben und nachgelesen: Die Badge-Daten des HOME-Menüs sind wie vorher.",
         "/theme-pack/badge-backup/0003"},
        {"Kein WLAN", "WLAN einschalten und erneut scannen.", NULL},
        {"Die Kamera konnte nicht gestartet werden.", "Gehe zurück und versuche es erneut.", NULL},
        {"Leer", "Hier sind keine Designs und keine Ordner", NULL},
        {"Installierte Badges", "Keine Badge-Sets im HOME-Menü", NULL},
        {"/Badges", "Der Ordner /Badges ist nicht auf der SD-Karte", NULL},
        /* INFO_LONG: an install that failed and lost designs, in both languages */
        {"Fehlgeschlagen, Designs verloren",
         "Der Import ist gescheitert (ein AM-Schritt) und wurde abgebrochen. Laut AM sind 2 vorher installierte(s) "
         "Design(s) nicht mehr installiert. Weg zurück: Installiere die CIA des Pakets mit FBI neu "
         "(sdmc:/theme-pack/theme-pack.cia); Designs, die nach dem Bau dieser CIA dazukamen, müssen danach erneut "
         "hinzugefügt werden. Jedes AM-Ergebnis steht in sdmc:/theme-pack/install.log.", NULL},
        {"Failed, themes lost",
         "The import failed (an AM step) and was cancelled. AM lists 2 theme(s) that were installed as no longer "
         "installed. Way back: reinstall the pack CIA with FBI (sdmc:/theme-pack/theme-pack.cia); themes added after "
         "that CIA was built have to be added again. Every AM result is in sdmc:/theme-pack/install.log.", NULL},
    };
    printf("-- FB-E the information box, text only (C135), in %s\n", look_get(id)->name);
    ui_look(id, 0, 1, 0);
    for (int pass = 0; pass < 2; pass++) {
        frame_begin();
        ui_end();
    }
    static const int HEADS[6][2] = {{-1, -1}, {ART_WARN, -1}, {ART_FOLDER_BIG, ART_HOME_FOOT}, {-1, ART_HOME_FOOT},
                                    {-1, ART_FOLDER_FOOT}, {-1, ART_WARN_FOOT}};
    for (size_t c = 0; c < sizeof CASES / sizeof CASES[0]; c++)
        for (int hd = 0; hd < 6; hd++) {
            float h;
            watched = 0;
            info_pair(HEADS[hd][0], HEADS[hd][1], CASES[c].head, CASES[c].text, CASES[c].path, &h);
            int top = signs_recorded();
            watched = 2;
            float y = info_pair(HEADS[hd][0], HEADS[hd][1], CASES[c].head, CASES[c].text, CASES[c].path, &h);
            float w = info_w, x = roundf((BOTTOM_W - w) / 2);
            int bottom = signs_recorded(), texts = 0, outside = 0, sign = 0, after = 0, cut_ = 0;
            const C3D_Tex *last = &atlas;
            for (int i = 0; i < recs; i++) {
                if (rec[i].kind == 'T') {
                    texts++;
                    cut_ += strstr(rec[i].text, "...") != NULL;
                    after += !atlas_tex(last);
                    outside += rec[i].x < x || rec[i].x > x + w || rec[i].y < y || rec[i].y > y + h;
                } else if (rec[i].kind == 'R') {
                    last = &atlas;
                } else if (rec[i].kind == 'I') {
                    last = rec[i].tex;
                    if (atlas_tex(rec[i].tex) && rec[i].w == INFO_ICON && rec[i].h >= INFO_ICON)
                        sign += rec[i].x >= x && rec[i].x + rec[i].w <= x + w && rec[i].y >= y &&
                                rec[i].y + INFO_ICON <= y + h;
                }
            }
            int room_kept = info_room_wrong(CASES[c].text), signs;
            float off;
            int only = info_text_only(x, y, h, &signs, &off);
            char what[400];
            snprintf(what, sizeof what, "FB-E box %zu (%.40s) under head %d, foot %d: %d texts, %d outside the box %g, %g, %g x "
                     "%g; above the bar; %d texts after a picture's texture (R7); %d lines cut; signs on top %d, on "
                     "the bottom %d; C135 text only %d (%d signs in the box, %d of its size, the block %g px off the "
                     "middle, from the inner left edge), room kept %d", c, CASES[c].text, HEADS[hd][0], HEADS[hd][1],
                     texts, outside, x, y, w, h, after, cut_, top, bottom, only, signs, sign, off, room_kept);
            check(texts > 0 && !outside && !sign && !after && !cut_ && y >= 0 && y + h <= BAR_Y - BAR_SHADOW &&
                      (!CASES[c].path || texts >= 2) && !bottom && !room_kept && only, what);
        }
    /* the scan screen's note: the hint and the box under it in the room left above the bar, as main.c's
     * scan_bottom puts them (the hint wrapped as there), in both languages, with a scanned text longer than its
     * line: the box above the bar's shadow, no line of it cut */
    static const char *const SCAN_HINT[2] = {"Hold the QR code in front of the camera", "QR-Code vor die Kamera halten"};
    static const char *const SCAN_WHY[2] = {
        "The QR code holds no web address (http or https). Nothing was downloaded.",
        "Der QR-Code enthält keine Webadresse (http oder https). Es wurde nichts geladen."};
    for (int l = 0; l < 2; l++) {
        ui_block hint;
        ui_wrap(&hint, SCAN_HINT[l], BOTTOM_W - 2 * BODY_PAD, SCAN_PX, SCAN_PX - 3, 2, 0, SCAN_LH);
        const char *line = "Hallo vom Kühlschrank, das ist ein langer Text ohne Ende";
        float room = BAR_Y - BAR_SHADOW - 2 * INFO_AIR, hh = ui_block_height(&hint);
        frame_begin();      /* the height in the frame the box is drawn in, as scan_bottom */
        float box = ui_info_height("QR-Code scannen", SCAN_WHY[l], line, room - hh - SCAN_GAP);
        float top = INFO_AIR + roundf((room - hh - SCAN_GAP - box) / 2) + hh + SCAN_GAP;
        float y = ui_info_box("QR-Code scannen", SCAN_WHY[l], line, top, top + box);
        ui_end();
        int cut_ = 0, texts = 0;
        for (int i = 0; i < recs; i++)
            if (rec[i].kind == 'T') {
                texts++;
                cut_ += strstr(rec[i].text, "...") != NULL && strncmp(rec[i].text, "...", 3);
            }
        int signs;
        float off;
        int only = info_text_only(roundf((BOTTOM_W - info_w) / 2), y, box, &signs, &off);
        char what[240];
        snprintf(what, sizeof what, "FB-E the scan note (language %d): hint %g and box %g px high from %g, %d lines, "
                 "%d cut; C135 text only %d (%d signs, the block %g px off the middle)", l, hh, box, y, texts, cut_,
                 only, signs, off);
        check(y == top && y + box <= BAR_Y - BAR_SHADOW && texts >= 2 && !cut_ && only, what);
    }
    ui_popup p;
    const char *paras[2] = {"Nur das zuletzt hinzugefügte Set kann entfernt werden.", NULL};
    ui_popup_text_signed(&p, paras, 1);
    frame_begin();
    ui_popup_sign(ART_INFO_WARN);
    ui_popup_draw(&p, NULL, "Okay", -1, -1, 1.0f);
    ui_end();
    int sign_at = -1, text_at = -1;
    for (int i = 0; i < recs; i++) {
        if (sign_at < 0 && rec[i].kind == 'I' && atlas_tex(rec[i].tex) && rec[i].w == INFO_ICON &&
            rec[i].h >= INFO_ICON)
            sign_at = i;
        if (text_at < 0 && rec[i].kind == 'T' && !strncmp(rec[i].text, "Nur", 3))
            text_at = i;
    }
    check(sign_at >= 0 && text_at > sign_at && rec[sign_at].y + INFO_ICON <= rec[text_at].y,
          "FB-E a refusing note: its sign drawn above its first line");
    /* FB-E2: the same note under a top card whose head shows the warning: on the pair the warning once */
    int top = 0, bottom = 0;
    for (watched = 0;; watched = 2) {
        recs = 0;
        ui_begin(1.0f);
        ui_top();
        ui_detail(NULL, ART_WARN, "Kopf", NULL, INK);
        ui_bottom();
        ui_popup_sign(ART_INFO_WARN);
        ui_popup_draw(&p, NULL, "Okay", -1, -1, 1.0f);
        ui_end();
        if (watched == 2)
            break;
        top = signs_recorded();
    }
    bottom = signs_recorded();
    int texts = 0;
    for (int i = 0; i < recs; i++)
        texts += rec[i].kind == 'T' && !strncmp(rec[i].text, "Nur", 3);
    check(top == 1 && !bottom && texts == 1,
          "FB-E2 a refusing note under the warning's head: the sign once on the pair");
}

int main(void)
{
    /* D4 of S3-2c: an id of the application's has no sprite until its part is handed over, then the part's; one past
     * the part has none */
    const art_part_look *stock = &ART_APP_PART.looks[0];
    check(!ui_sprite_info(ART_POUR_HALO) && !ui_sprite_info(ART_WORD_TOP) && !ui_sprite_tex(ART_POUR_HALO, 0),
          "before ui_part an id of the application's has no sprite");
    ui_part(&ART_APP_PART);
    check(stock->look == &ART_STOCK && ui_sprite_info(ART_POUR_HALO) == &stock->sprites[ART_POUR_HALO - ART_ALL] &&
              ui_sprite_info(ART_WORD_TOP) == &stock->sheet_sprites[ART_WORD_TOP - ART_ALL - ART_APP_COUNT] &&
              !ui_sprite_info(ART_ALL + ART_APP_COUNT + ART_APP_GLASS_COUNT) && !ui_sprite_info(-1),
          "after ui_part an id of the application's has the part's sprite, one past the part none");
    if (ui_init()) {
        printf("FAIL ui_init\n1 failed\n");
        return 1;
    }
    for (int id = 0; id < LOOK_COUNT; id++)
        if (look_built(id))
            checks_of_look(id);
    for (int id = 0; id < LOOK_COUNT; id++)     /* FB-E */
        if (look_built(id))
            info_checks(id);
    printf("%d failed\n", failed);
    return failed != 0;
}
