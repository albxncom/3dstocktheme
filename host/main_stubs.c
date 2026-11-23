/* For host/test_main.c: ui.c, app_ui.c, console.c and camera.c as no-ops, so main.c runs on the Mac. What a touch hits
 * comes from uikit/source/hits.c, the console's own code; the corner button's width depends on the font, so it is fixed
 * here. */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <zlib.h>
#include <3ds.h>
#include "ui.h"
#include "app_ui.h"
#include "layout.h"
#include "console.h"
#include "camera.h"

int stub_verbose;

/* What the top screen showed, frame by frame, a letter each, a repeat left out: S the start screen (also its
 * loading frame), 0 to 4 installing with that many steps green, D "Installiert". */
char stub_tops[4096];
static void top_was(char c)
{
    size_t n = strlen(stub_tops);
    if ((!n || stub_tops[n - 1] != c) && n + 1 < sizeof stub_tops)
        stub_tops[n] = c, stub_tops[n + 1] = 0;
}

/* The bottom screen frame by frame, for host/test_main.c: how many things were drawn on it and the transition's
 * value; the places of the rows and buttons ('e'), of the selection ('s') and of what covers them (the corner
 * button, the scroll bar, the bar, the popup: 'o') in drawing order; whether the top screen showed work under way.
 * stub_frame_done, if set, is called at the end
 * of every frame, also of those drawn inside an action (a delete under its loader, the install). */
float stub_entered;
int stub_bottom_things, stub_busy_top;    /* stub_busy_top: the top screen showed work under way */
int stub_loader;                          /* the loader was drawn (0.9.2) */
char stub_loader_line[120];               /* ... with this line under its ring ("" none) */
char stub_loader_notice[120];             /* ... and this notice under the line ("" none) */
char stub_pill_home[64], stub_pill_change[64];   /* the Done screen's pill (DN): its way as drawn this frame ("" none) */
int stub_pill_bottom;                           /* ... drawn on the bottom screen */
int stub_top_loader;                      /* a loader's ring showed on the top screen */
ui_pic stub_gallery_pic;                  /* the gallery drawn last: its middle pane's picture, */
char stub_gallery_name[120];              /* ... and the name under it ("" none this frame) */
#define STUB_MARKS 64
char stub_mark_kind[STUB_MARKS];
rect stub_mark_at[STUB_MARKS];
int stub_marks;
void (*stub_frame_done)(void);
void (*stub_each_frame)(void);            /* after it, always: the test's clockwork (a held job comes nearer its end) */
static int on_bottom;
static void drew(void) { stub_bottom_things += on_bottom; }
/* G1-R5: the frame's pieces on both screens in drawing order, each a letter, its screen and where it stands (for a
 * piece without a rect of its own: y, or the loader's cover and ring); the texts and the pictures are left out */
#define STUB_PIECES 64
char stub_piece_kind[STUB_PIECES];
int stub_piece_bottom[STUB_PIECES];
rect stub_piece_at[STUB_PIECES];
int stub_pieces;
static void piece(char kind, rect r)
{
    if (stub_pieces < STUB_PIECES)
        stub_piece_kind[stub_pieces] = kind, stub_piece_bottom[stub_pieces] = on_bottom, stub_piece_at[stub_pieces++] = r;
}
static void mark(char kind, rect r)
{
    drew();
    piece(kind, r);
    if (on_bottom && stub_marks < STUB_MARKS)
        stub_mark_kind[stub_marks] = kind, stub_mark_at[stub_marks++] = r;
}

static int look_told;           /* ui_look was called for the frame being drawn */
int stub_glass_drawn, stub_glass_untold;    /* glass elements the drawing layer drew, and those in a frame not told
                                             * the look first */
static void glass_drawn(void) { stub_glass_drawn++; stub_glass_untold += !look_told; }

rect ui_corner_rect(const char *path) { (void)path; return (rect){0, 0, 120, CORNER_H}; }
rect ui_card_rect(void) { return (rect){0, 0, 0, 0}; }
int ui_popup_fit(ui_popup *p, const char *first, const char *path, const char *const rest[], int count)
{ (void)first; (void)path; (void)rest; p->count = count; return 1; }
char stub_popup_para[UI_POPUP_PARAS][600];     /* the paragraphs of the popup laid out last (ui_popup_text) */
int ui_popup_text(ui_popup *p, const char *const paras[], int count)
{
    for (int k = 0; k < UI_POPUP_PARAS; k++)
        snprintf(stub_popup_para[k], sizeof stub_popup_para[k], "%s", k < count && paras[k] ? paras[k] : "");
    p->count = count;
    return 1;
}
float stub_popup_shown = -1;     /* how far the popup drawn last had opened; -1: none this frame */
const char *stub_popup_left, *stub_popup_right;    /* its buttons as drawn last (left NULL: one button across) */
int stub_popup_tone = -1, stub_popup_removes = -1;     /* told for the next popup (ui_popup_tone); the popup drawn last's */
void ui_popup_tone(int removes) { stub_popup_tone = removes; }
void ui_popup_draw(const ui_popup *p, const char *left, const char *right, int held, int ring, float shown)
{ piece('P', (rect){0, 0, 0, 0}); (void)p; (void)held; (void)ring; stub_popup_left = left; stub_popup_right = right; stub_popup_shown = shown; stub_popup_removes = stub_popup_tone; stub_popup_tone = 0; mark('o', (rect){0, 0, BOTTOM_W, SCREEN_H}); }

int ui_init(void) { return 0; }
void ui_exit(void) {}
void ui_part(const art_part *part) { (void)part; }
/* FB-E2: the signs the top screen drew this frame, in the head and the footer (the sprites, for the tests' own
 * meanings), and their motifs for ui_sign_once as ui.c tells them */
unsigned ui_top_signs;
int stub_top_glyph[8], stub_top_glyphs;
static void top_glyph(int sprite)
{
    ui_top_signs |= ui_motif(sprite);
    if (stub_top_glyphs < 8)
        stub_top_glyph[stub_top_glyphs++] = sprite;
}
void stub_frame_reset(void);
float stub_black;               /* the start's black over the frame, as told (ui_black); 0: none */
void ui_begin(float entered)
{
    stub_entered = entered;
    stub_bottom_things = stub_marks = on_bottom = stub_busy_top = stub_loader = stub_top_loader = stub_pieces = 0;
    stub_loader_line[0] = stub_loader_notice[0] = stub_gallery_name[0] = stub_pill_home[0] = stub_pill_change[0] = 0;
    stub_popup_shown = -1;
    stub_black = 0;
    ui_top_signs = 0;
    stub_top_glyphs = 0;
    stub_frame_reset();
}
void ui_black(float amount) { stub_black = amount; }
int ui_next_eye(void) { return 0; }
void ui_top(void) { on_bottom = 0; }
void ui_bottom(void) { on_bottom = 1; }
void ui_end(void)
{
    on_bottom = look_told = 0;
    if (stub_frame_done)
        stub_frame_done();
    if (stub_each_frame)
        stub_each_frame();
}
int ui_stats_alarm(void) { return 0; }
void ui_stats(char *out, size_t size) { if (size) out[0] = 0; }
u32 ui_frame(void) { static u32 f; return ++f; }
float ui_baseline(float top, float lh, int px) { (void)px; return top + lh; }
float stub_fit_baseline;        /* where the text fitted last stands */
void ui_text_fit(const char *s, float x, float baseline, float w, int px, uint32_t color, int align, int from_end)
{ (void)s; (void)x; (void)w; (void)px; (void)color; (void)align; (void)from_end; stub_fit_baseline = baseline; drew(); }
int ui_wrap(ui_block *b, const char *s, float w, int px, int min_px, int max_lines, float max_h, float lh)
{ (void)s; (void)w; (void)min_px; (void)max_lines; (void)max_h; b->px = px; b->lh = lh; b->count = 0; return 1; }
float ui_block_height(const ui_block *b) { (void)b; return 0; }
int stub_bottom_blocks;          /* FB-E: wrapped blocks drawn on the bottom screen (the test sets it to 0) */
void ui_block_draw(const ui_block *b, float x, float top, uint32_t color, int align)
{ (void)b; (void)x; (void)top; (void)color; (void)align; drew(); stub_bottom_blocks += on_bottom; }
void ui_sprite(int id, float x, float y) { (void)id; (void)x; (void)y; drew(); }
void ui_sprite_box(int id, rect r) { (void)id; (void)r; drew(); }
void ui_camera(const uint16_t *frame) { (void)frame; }
/* how far it covers what the bottom screen shows and how far its ring shows, as given (ui.c eases both) */
float stub_loader_cover, stub_loader_shown;
void ui_loader(const char *line, const char *notice, float cover, float shown)
{
    piece('L', (rect){cover, shown, 0, 0});
    if (!on_bottom) {
        stub_top_loader = stub_busy_top = shown > 0;
        return;
    }
    stub_loader = 1;
    snprintf(stub_loader_line, sizeof stub_loader_line, "%s", line ? line : "");
    snprintf(stub_loader_notice, sizeof stub_loader_notice, "%s", line && notice ? notice : "");
    stub_loader_cover = cover;
    stub_loader_shown = shown;
    if (cover > 0 || shown > 0)
        mark('o', (rect){0, 0, BOTTOM_W, SCREEN_H});
}
int stub_hero_symbol = -1;      /* the big symbol of the section screen drawn last */
void ui_hero(int symbol, const char *t, const char *h, const char *c) { piece('h', (rect){0, 0, 0, 0}); (void)t; (void)h; (void)c; stub_hero_symbol = symbol; }
void ui_head(const char *l, const char *r) { (void)l; (void)r; piece('H', (rect){0, 0, 0, 0}); }
char stub_foot[200];             /* the footer's text as last drawn */
char stub_foot_warn[80];         /* the warning at the footer's right end, "" when the footer drawn last had none */
void ui_foot(float d, int s, const char *l, uint32_t c, int f, const char *r)
{
    (void)c; (void)f; (void)r;
    piece('F', (rect){0, d, 0, 0});
    if (s >= 0)
        top_glyph(s);
    stub_foot_warn[0] = 0;
    snprintf(stub_foot, sizeof stub_foot, "%s", l ? l : "");
}
void ui_dots(float y) { piece('d', (rect){0, y, 0, 0}); }
void ui_foot_warn(const char *r) { piece('W', (rect){0, 0, 0, 0}); top_glyph(ART_WARN_FOOT); snprintf(stub_foot_warn, sizeof stub_foot_warn, "%s", r); }
void ui_busy_card(const uint8_t *i, int s, const char *n, float f, const char *l) { piece('b', (rect){0, 0, 0, 0}); (void)i; (void)s; (void)n; (void)f; (void)l; stub_busy_top = 1; }
const char *stub_done_home;     /* the Done card's way into the HOME Menu as drawn last (NULL: none) */
void ui_done_card(const uint8_t *i, const char *n, const char *h, const char *c) { piece('D', (rect){0, 0, 0, 0}); (void)i; (void)n; (void)c; stub_done_home = h; top_was('D'); glass_drawn(); }
void ui_way_pill(const char *h, const char *c)
{
    snprintf(stub_pill_home, sizeof stub_pill_home, "%s", h);
    snprintf(stub_pill_change, sizeof stub_pill_change, "%s", c);
    stub_pill_bottom = on_bottom;
    piece('P', (rect){0, PILL_Y, 0, PILL_H});
    drew();
}
int stub_glass = -1, stub_pattern = -1;         /* what the start screen was last told to draw */
int stub_scheme = -1;                           /* the colour scheme the drawing layer was last told */
int stub_look = -1;                             /* ... and the look */
const uint32_t *ui_colours = ART_STOCK_COLOUR[ART_SCHEME_WEISS];      /* ... whose colours SC() gives from then on */
int stub_start_drawn;           /* how often the start screen's top was drawn */
void ui_look(int l, int s, int g, int p)
{
    stub_look = l;
    stub_scheme = s;
    stub_glass = g;
    stub_pattern = p;
    look_told = 1;
    ui_colours = ART_STOCK.colour[s >= 0 && s < ART_SCHEMES ? s : ART_SCHEME_WEISS];
}
void ui_start(void) { top_was('S'); stub_start_drawn++; glass_drawn(); }
void ui_gallery(ui_pic a, ui_pic b, ui_pic c, const char *n, int m)
{
    (void)b; (void)c; (void)m;
    piece('G', (rect){0, 0, 0, 0});
    stub_gallery_pic = a;
    snprintf(stub_gallery_name, sizeof stub_gallery_name, "%s", n ? n : "");
    glass_drawn();
}
void ui_gallery_empty(void) { piece('g', (rect){0, 0, 0, 0}); glass_drawn(); }
/* the carousel as last drawn: its list, count, cursor and the middle pane's picture */
const void *stub_swarm_list;
size_t stub_swarm_count, stub_swarm_cursor;
ui_pic stub_swarm_middle;
void ui_swarm(ui_pic (*pic)(size_t i), size_t count, size_t cursor, const void *list, const char *name, int music)
{
    (void)name; (void)music;
    stub_swarm_list = list, stub_swarm_count = count, stub_swarm_cursor = cursor, stub_swarm_middle = pic(cursor);
    glass_drawn();
}
/* the installing screen as last drawn: each step's mark (0 to come, 1 under way, 2 done, between them in motion),
 * where the line ends, and how many frames of it were drawn; the top screen's letter is how many marks are past
 * "under way" */
float stub_step_mark[4], stub_line_end;
int stub_steps_frames;
void ui_steps(const char *t, const char *n, const char *const s[4], const float mark[4], float e)
{
    (void)t; (void)n; (void)s;
    int past = 0;
    for (int k = 0; k < 4; k++) {
        stub_step_mark[k] = mark[k];
        past += mark[k] > 1;
    }
    stub_line_end = e;
    stub_steps_frames++;
    top_was((char)('0' + past));
}
/* the installing screen (R9 D3): the picture as last drawn, and how many frames of it; what it was given then (the
 * Done screen's top after an install is this picture with the same, G2-L2b) */
const pour *stub_pour;
int stub_pour_frames;
struct stub_pour_given {
    const char *title, *done_title, *home, *step0;
    const uint8_t *icon;
    int symbol;
    char name[96], what[96];
} stub_pour_given;
void ui_pour(const pour *p, const char *t, const char *d, const char *n, const uint8_t *i, int s, const char *const st[],
             const char *h, const char *w)
{
    stub_pour_given = (struct stub_pour_given){.title = t, .done_title = d, .home = h, .step0 = st[0], .icon = i,
                                               .symbol = s};
    snprintf(stub_pour_given.name, sizeof stub_pour_given.name, "%s", n ? n : "");
    snprintf(stub_pour_given.what, sizeof stub_pour_given.what, "%s", w ? w : "");
    piece('p', (rect){0, 0, 0, 0});
    stub_pour = p;
    stub_pour_frames++;
    stub_busy_top = 1;          /* the work under way, as ui_busy_card's */
    top_was('p');
}
void ui_url_card(const char *u) { (void)u; }
void ui_brackets(void) {}
int stub_tile_symbol[2] = {-1, -1};        /* the symbols of the two tiles drawn last (by x: left, right) */
void ui_tile(int k, rect r, int s, int y, const char *l, int px, int lh, int g, uint32_t i) { (void)k; (void)s; (void)l; (void)px; (void)lh; (void)g; (void)i; stub_tile_symbol[r.x > BOTTOM_W / 2 - r.w / 2] = y; mark('e', r); }
void ui_wide(int k, rect r, int s, int y, const char *l, int px, int g, uint32_t i) { (void)k; (void)s; (void)y; (void)l; (void)px; (void)g; (void)i; mark('e', r); }
void ui_stack(rect r, int s, const char *l, int px, int lh, int g, uint32_t i) { (void)s; (void)l; (void)px; (void)lh; (void)g; (void)i; piece('k', r); drew(); }
/* the box of facts drawn last: its rows' symbols, lines and whether a line is cut at its beginning */
int stub_fact_rows, stub_fact_symbol[UI_FACTS], stub_fact_end[UI_FACTS];
char stub_fact_text[UI_FACTS][600];
float stub_fact_y;               /* where the box of facts drawn last stands */
void ui_facts(const ui_fact *rows, int count, float y)
{
    piece('A', (rect){0, y, (float)count, 0});
    stub_fact_y = y;
    stub_fact_rows = count;
    for (int k = 0; k < count && k < UI_FACTS; k++) {
        stub_fact_symbol[k] = rows[k].symbol;
        stub_fact_end[k] = rows[k].from_end;
        snprintf(stub_fact_text[k], sizeof stub_fact_text[k], "%s", rows[k].text);
    }
    drew();
}
void ui_go(rect r, int s, int i, const char *l) { (void)s; (void)i; (void)l; mark('e', r); }
void ui_alt(rect r, int s, int i, const char *l) { (void)s; (void)i; (void)l; mark('e', r); }
int stub_bar_right, stub_bar_down;      /* the frame's one action was drawn on the right tab; it was drawn held */
void ui_bar(const char *k, const char *l, int d) { (void)k; (void)l; stub_bar_right = 0; stub_bar_down = d; mark('o', ui_bar_rect()); }
void ui_bar_right(const char *k, const char *l, int d) { (void)k; (void)l; stub_bar_right = 1; stub_bar_down = d; mark('o', (rect){BOTTOM_W - TAB_W, BAR_Y, TAB_W, BAR_H}); }
void ui_bar_wide(const char *k, const char *l, int d) { (void)k; (void)l; (void)d; mark('o', ui_bar_wide_rect()); }
void ui_bar2(const char *l, const char *r, int d) { (void)l; (void)r; (void)d; mark('o', ui_bar_wide_rect()); }
void ui_corner(const char *p, int e, int d) { (void)e; (void)d; mark('o', ui_corner_rect(p)); }
void ui_help(int d) { (void)d; mark('o', ui_help_rect()); }
float ui_help_low(void) { return HELP_Y + (HELP_SIZE + HELP_FACE) / 2; }      /* every look's */
void ui_row_folder(rect r, int s, const char *n) { (void)s; (void)n; mark('e', r); }
void ui_scrollbar(rect r, float a, float s, int h) { (void)a; (void)s; (void)h; mark('o', r); }
void ui_select(rect r, int radius) { (void)radius; mark('s', r); }
void ui_select_corners(rect r, const int radius[4]) { (void)radius; mark('s', r); }
void ui_detail(const uint8_t *i, int s, const char *n, const char *c, uint32_t k) { piece('x', (rect){0, 0, 0, 0}); if (!i) top_glyph(s); (void)n; (void)c; (void)k; }
void ui_row_theme(rect r, int s, const uint8_t *i, const char *n, const char *b, int m, uint32_t c, int x) { (void)s; (void)i; (void)n; (void)b; (void)m; (void)c; (void)x; mark('e', r); }

int camera_start(void) { return 0; }
void camera_stop(void) {}
int camera_frame(const uint16_t **frame) { (void)frame; return 0; }
int camera_qr(const uint16_t *frame, char *text, size_t size) { (void)frame; (void)text; (void)size; return 0; }

void console_log_open(void) {}
/* The log for host/test_main.c: the last line that begins with "motion:" and how many there were; how many lines
 * went to the card at once (the log strict) and how many waited; how often waiting lines were written to the card
 * (stub_flushes); every line is also the test's business (stub_log_hook). */
char stub_motion_line[256];
int stub_motion_lines, stub_strict, stub_strict_lines, stub_loose_lines, stub_flushes;
static int log_waits;
void (*stub_log_hook)(const char *line);
void console_log(const char *fmt, ...)
{
    char line[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    if (!strncmp(line, "motion:", 7)) {
        stub_motion_lines++;
        snprintf(stub_motion_line, sizeof stub_motion_line, "%s", line);
    }
    stub_strict_lines += stub_strict;
    stub_loose_lines += !stub_strict;
    log_waits |= !stub_strict;
    if (stub_log_hook)
        stub_log_hook(line);
    if (stub_verbose)
        fprintf(stderr, "  log: %s\n", line);
}
void console_log_flush(void)
{
    stub_flushes += log_waits;
    log_waits = 0;
}
void console_log_strict(int on)
{
    stub_strict = on;
    if (on)
        console_log_flush();
}
void console_log_close(void) {}
void console_log_view(const char *when, const am_view *v) { (void)when; (void)v; }
/* The pack as the console would report it, set by the test (all zero: AM does not answer): AM's view, the installed
 * catalogue (ContentInfoArchive_EUR_en.bin and the icons by index), HOME's data; the deleter marks the content as
 * no longer installed in that view, as AM's list does after HOME's own Delete. */
am_view fake_am = {0};
const uint8_t *fake_archive;
size_t fake_archive_size;
#define FAKE_ICONS 8                /* indices up to 7 */
uint8_t fake_icons[FAKE_ICONS][TP_ICON_SIZE] = {{0}};
home_state fake_home = {0};
int fake_deletes;

void console_read_am(am_view *v) { *v = fake_am; }

static int fake_read_archive(void *ctx, uint8_t *buf, size_t cap, size_t *size)
{
    (void)ctx;
    if (!fake_archive || fake_archive_size > cap)
        return -1;
    memcpy(buf, fake_archive, fake_archive_size);
    *size = fake_archive_size;
    return 0;
}

static int fake_read_icon(void *ctx, uint16_t index, uint8_t icon[TP_ICON_SIZE])
{
    (void)ctx;
    if (index >= FAKE_ICONS)
        return -1;
    memcpy(icon, fake_icons[index], TP_ICON_SIZE);
    return 0;
}

int console_open_catalogue(catalogue_reader *reader)
{
    if (!fake_archive)
        return -1;
    *reader = (catalogue_reader){NULL, fake_read_archive, fake_read_icon};
    return 0;
}
void console_close_catalogue(void) {}
/* Whether a content holds music: set by the test per index (MUSIC_*); fake_music_reads counts the reads. */
int fake_music[PACK_CONTENTS];
int fake_music_reads;
int console_content_music(uint16_t index, uint32_t *error)
{
    fake_music_reads++;
    *error = fake_music[index] == MUSIC_UNKNOWN ? 0xC8804478u : 0;
    return fake_music[index];
}
/* An importer whose every call succeeds unless the test says otherwise; when an install's second commit went
 * through, fake_installed (set by the test) gets the TMD it imported, to make AM's view and the catalogue what the
 * console would then report. A ticket it takes (a first install) is on the console from then on (fake_am.ticket).
 * fake_imp_lands: the failing call is a commit that AM carries out all the same, as store.h allows for: fake_landed
 * (set by the test) gets the TMD of that import before the commit answers with its error.
 * 0.9.2: it writes down every call in its order with what it was given (fake_imp_trace, a line each; a content's
 * bytes as each write's length and the CRC-32 of the content so far), so a test can hold two installs against each
 * other. fake_imp_fail: the call with this number fails (1 is the first; cancel is not counted), 0: none.
 * fake_imp_guard, if set, is asked at every call whether it may be made now; fake_imp_unguarded counts the noes. */
void (*fake_installed)(const uint8_t *tmd, size_t size);
void (*fake_landed)(const uint8_t *tmd, size_t size);
char fake_imp_trace[16384];
int fake_imp_calls, fake_imp_fail, fake_imp_unguarded, fake_imp_lands;
int (*fake_imp_guard)(void);
static uint8_t fake_tmd[0x4000];
static size_t fake_tmd_size;
static int fake_commits;
static uLong content_crc;

static void imp_note(const char *fmt, ...)
{
    size_t n = strlen(fake_imp_trace);
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(fake_imp_trace + n, sizeof fake_imp_trace - n, fmt, ap);
    va_end(ap);
}

static int imp_called(void)
{
    fake_imp_calls++;
    if (fake_imp_guard && !fake_imp_guard())
        fake_imp_unguarded++;
    return fake_imp_fail && fake_imp_calls == fake_imp_fail ? -1 : 0;
}

static int imp_title_begin(void *ctx)
{
    (void)ctx;
    imp_note("title_begin\n");
    return imp_called();
}
static int imp_tmd(void *ctx, const uint8_t *tmd, size_t size)
{
    (void)ctx;
    imp_note("tmd %u %08lx\n", (unsigned)size, (unsigned long)crc32(0L, tmd, (uInt)size));
    if (size > sizeof fake_tmd)
        return -1;
    memcpy(fake_tmd, tmd, size);
    fake_tmd_size = size;
    return imp_called();
}
static int imp_contexts(void *ctx, const uint16_t *indices, size_t count)
{
    (void)ctx;
    imp_note("contexts %u %u\n", (unsigned)count, count ? indices[0] : 0);
    return imp_called();
}
static int imp_begin(void *ctx, uint16_t index)
{
    (void)ctx;
    content_crc = crc32(0L, Z_NULL, 0);
    imp_note("content_begin %u\n", index);
    return imp_called();
}
static int imp_write(void *ctx, const void *buf, size_t len)
{
    (void)ctx;
    content_crc = crc32(content_crc, buf, (uInt)len);
    imp_note("write %u %08lx\n", (unsigned)len, (unsigned long)content_crc);
    return imp_called();
}
static int imp_finish(void *ctx)
{
    (void)ctx;
    imp_note("content_finish\n");
    return imp_called();
}
static int imp_commit(void *ctx)
{
    (void)ctx;
    imp_note("commit\n");
    if (imp_called()) {
        if (fake_imp_lands && fake_landed)
            fake_landed(fake_tmd, fake_tmd_size);
        return -1;
    }
    if (++fake_commits == 2 && fake_installed)
        fake_installed(fake_tmd, fake_tmd_size);
    return 0;
}
static void imp_cancel(void *ctx)
{
    (void)ctx;
    imp_note("cancel\n");
}
static int imp_ticket(void *ctx, const uint8_t *ticket, size_t size)
{
    (void)ctx;
    imp_note("ticket %u %08lx\n", (unsigned)size, (unsigned long)crc32(0L, ticket, (uInt)size));
    if (imp_called())
        return -1;
    fake_am.ticket = 1;
    return 0;
}

void console_importer(importer *imp)
{
    fake_commits = 0;
    *imp = (importer){NULL, imp_title_begin, imp_tmd, imp_contexts, imp_begin, imp_write, imp_finish, imp_commit,
                      imp_cancel, imp_ticket};
}
void console_read_home(home_state *h) { *h = fake_home; }

static uint32_t fake_delete(void *ctx, uint64_t title_id, uint16_t index)
{
    (void)ctx;
    if (title_id != TP_TITLE_ID || index == 0)
        return 0xFFFFFFFFu;
    fake_deletes++;
    for (size_t j = 0; j < fake_am.count; j++)
        if (fake_am.list[j].index == index)
            fake_am.usable[j] = 0;
    return 0;
}

void console_deleter(deleter *d) { *d = (deleter){NULL, fake_delete}; }
int console_language(void) { return 0; }
void console_speed_up(void) {}
int console_wifi(void) { return 1; }
int fake_net_fail;                  /* the network cannot be started */
int console_net_start(void) { return fake_net_fail ? -1 : 0; }
void console_net_stop(void) {}
const char *console_user_agent(void) { return "x"; }

/* ---- the audio lead's, 0.9.2: audio.c as counters, the preview's drawing as marks, content.c as the test sets it.
 * preview.c itself is the application's (Makefile, MAIN_SRC). */
#include <stdlib.h>
#include "audio.h"
#include "bcstm.h"
#include "content.h"

int stub_sfx[SFX_COUNT];            /* how often each interface sound played */
int stub_sfx_last = -1;
int stub_menu_paused;               /* the menu music is paused */
int stub_theme_music;               /* a theme's music plays */
int stub_theme_starts;              /* how often one was started */
const uint8_t *stub_theme_file;     /* the file playing, to see that it is still there */
void (*audio_log)(const char *fmt, ...);
void audio_init(void) {}
void audio_exit(void) {}
int audio_ready(void) { return 0; }
extern int stub_sounds_on, stub_work_depth;
extern int stub_work_ends;
int stub_close_depth = -1;          /* work under way when the done or error sound last played (0: none), */
int stub_close_ends = -1;           /* and how many works had ended by then */
void sfx(int id)
{
    if (id == SFX_DONE || id == SFX_ERROR) {
        stub_close_depth = stub_work_depth;
        stub_close_ends = stub_work_ends;
    }
    if (!stub_sounds_on)
        return;
    if (id >= 0 && id < SFX_COUNT)
        stub_sfx[id]++;
    stub_sfx_last = id;
}
int audio_theme_play(const uint8_t *bcstm, size_t size)
{
    bcstm_stream s;
    stub_menu_paused = 1;
    stub_theme_music = 0;
    stub_theme_file = NULL;
    if (!bcstm)
        return 0;
    if (bcstm_open(&s, bcstm, size))
        return -1;
    stub_theme_music = 1;
    stub_theme_starts++;
    stub_theme_file = bcstm;
    return 0;
}
void audio_theme_stop(void)
{
    stub_theme_music = stub_menu_paused = 0;
    stub_theme_file = NULL;
}
/* The settings and the working loop: the menu music's track as last set (-2: never), how often it was set, whether
 * the interface sounds are on (sfx counts only then, as audio.c plays only then), and work begun and ended. */
int stub_menu_track = -2, stub_menu_sets, stub_sounds_on = 1;
int stub_work_depth, stub_work_begins, stub_work_ends;
void audio_menu(int which) { stub_menu_track = which; stub_menu_sets++; }
void audio_sounds(int on) { stub_sounds_on = on; }
void audio_work(int on)
{
    stub_work_depth += on ? 1 : -1;
    if (on)
        stub_work_begins++;
    else
        stub_work_ends++;
}

/* An installed content's files as the test sets them (copies are handed out, as content.c does); body NULL: the
 * content cannot be read. fake_content_reads counts the calls, fake_content_index is the index last asked for. */
const uint8_t *fake_content_body, *fake_content_music;
size_t fake_content_body_size, fake_content_music_size;
int fake_content_reads, fake_content_index;
int content_read(uint16_t index, uint8_t **body, size_t *body_size, uint8_t **music, size_t *music_size,
                 uint32_t *error)
{
    fake_content_reads++;
    fake_content_index = index;
    *body = *music = NULL;
    *body_size = *music_size = 0;
    *error = 0;
    if (!fake_content_body) {
        *error = 0xC8804478u;
        return -1;
    }
    *body = malloc(fake_content_body_size);
    memcpy(*body, fake_content_body, fake_content_body_size);
    *body_size = fake_content_body_size;
    if (fake_content_music) {
        *music = malloc(fake_content_music_size);
        memcpy(*music, fake_content_music, fake_content_music_size);
        *music_size = fake_content_music_size;
    }
    return 0;
}

/* The wallpapers: what was handed over per screen (the width, the first texel), and what a frame drew: 'w' a
 * texture at stub_wall_x, 'c' a colour, 0 nothing. stub_bar_y: the frame drew the footer with the Y tab. */
int stub_wall_width[2], stub_wall_sets;
unsigned stub_wall_first[2];
char stub_wall_drawn[2];
float stub_wall_x[2];
uint8_t stub_wall_rgb[2][6];
int stub_bar_y;
extern int stub_settings_button, stub_choices;
void stub_frame_reset(void) { stub_wall_drawn[0] = stub_wall_drawn[1] = 0; stub_bar_y = 0; stub_settings_button = stub_choices = 0; }
int ui_wall_set(int bottom, const uint8_t *tiled, int width)
{
    stub_wall_sets++;
    stub_wall_width[bottom] = width;
    stub_wall_first[bottom] = tiled[0] | (unsigned)tiled[1] << 8;
    return 0;
}
void ui_wall(int bottom, float x) { drew(); stub_wall_drawn[bottom] = 'w'; stub_wall_x[bottom] = x; }
void ui_wall_color(const uint8_t upper[3], const uint8_t lower[3])
{
    drew();
    stub_wall_drawn[on_bottom] = 'c';
    memcpy(stub_wall_rgb[on_bottom], upper, 3);
    memcpy(stub_wall_rgb[on_bottom] + 3, lower, 3);
}
void ui_bar_y(const char *l, const char *r, int d) { (void)l; (void)r; (void)d; stub_bar_y = 1; mark('o', ui_bar_rect()); }

/* the settings button and the settings screen as the last frame drew it: its rows (stub_row_*: name, the choice
 * that holds, the note) and the top screen's list of one row's choices (stub_list_*, the chosen one's place) */
#define STUB_CHOICES 8
int stub_settings_button, stub_choices, stub_list_count, stub_list_chosen;
char stub_row_label[STUB_CHOICES][64], stub_row_value[STUB_CHOICES][96], stub_row_note[STUB_CHOICES][24];
char stub_list_name[STUB_CHOICES][96];
void stub_frame_reset(void);
void ui_settings_button(int down) { (void)down; stub_settings_button = 1; mark('o', ui_settings_rect()); }
void ui_row_setting(rect r, int state, const char *label, const char *value, const char *note)
{
    (void)state;
    if (stub_choices < STUB_CHOICES) {
        snprintf(stub_row_label[stub_choices], sizeof stub_row_label[0], "%s", label);
        snprintf(stub_row_value[stub_choices], sizeof stub_row_value[0], "%s", value);
        snprintf(stub_row_note[stub_choices++], sizeof stub_row_note[0], "%s", note);
    }
    mark('e', r);
}
void ui_choices(int count, int chosen, const char *(*name)(int choice))
{
    stub_list_count = count;
    stub_list_chosen = chosen;
    for (int k = 0; k < count && k < STUB_CHOICES; k++)
        snprintf(stub_list_name[k], sizeof stub_list_name[0], "%s", name(k));
}
int stub_word_shown, stub_backdrop_shown;     /* the settings screen's glass word and pattern, frames drawn */
void ui_choices_word(void) { stub_word_shown++; glass_drawn(); }
/* FIX-2: the choices at the card's top, recorded as ui_choices's: frames drawn with them, the y it returned */
int stub_choices_top;
float stub_choices_end;
float ui_choices_top(int count, int chosen, const char *(*name)(int choice))
{
    stub_choices_top++;
    ui_choices(count, chosen, name);
    return stub_choices_end = CARD_Y + CARD_PAD + count * SET_LIST_LH;
}
void ui_backdrop(void) { stub_backdrop_shown++; }
/* FIX-1: the credits under the choices, as last drawn: how many lines (0: none since stub_credits was cleared) and
 * where the choices ended (FIX-2) */
int stub_credits;
char stub_credit[STUB_CHOICES][160];
float stub_credits_from;
int ui_choices_credits(float choices_end, const char *const lines[], int n)
{
    stub_credits_from = choices_end;
    stub_credits = n;
    for (int k = 0; k < n && k < STUB_CHOICES; k++)
        snprintf(stub_credit[k], sizeof stub_credit[0], "%s", lines[k]);
    return 1;
}
void ui_text(const char *s, float x, float baseline, int px, uint32_t color, int align)
{ (void)s; (void)x; (void)baseline; (void)px; (void)color; (void)align; drew(); }

/* installing, 0.9.3 (round 12) */
void ui_card_glass(rect r) { piece('c', r); drew(); }

/* FB-E: the information box (text only since C135) and the popup's sign, each as ui.c draws it (FB-E2:
 * ui_sign_once, against the top's signs in ui_top_signs) */
int stub_info_boxes;            /* boxes drawn on the bottom screen (the test sets it to 0) */
char stub_info_text[600];
float ui_info_height(const char *head, const char *text, const char *path, float room)
{ (void)head; (void)text; (void)path; (void)room; return 60; }
float ui_info_box(const char *head, const char *text, const char *path, float top, float bottom)
{
    (void)head; (void)path; (void)bottom;
    piece('i', (rect){0, top, 0, 60});
    if (on_bottom) {
        stub_info_boxes++;
        snprintf(stub_info_text, sizeof stub_info_text, "%s", text ? text : "");
    }
    drew();
    return top;
}
int stub_popup_sign = -1;       /* the sign of the popup drawn last (ui_popup_sign comes right before its draw) */
void ui_popup_sign(int symbol) { stub_popup_sign = ui_sign_once(symbol); }
int ui_popup_text_signed(ui_popup *p, const char *const paras[], int count) { return ui_popup_text(p, paras, count); }
