// The screens of the approved prototype: Start (Designs, Badges); Designs (install from the SD card, scan a QR
// code, the installed designs); Badges (not built yet); the folder view of the SD card; the chosen theme, with
// Install (and Install without music for a theme with music), or for a theme installed already Delete theme and
// Delete file, each asked in a popup first; installing; done, with the question whether to delete the file the
// theme came from; the QR scan and the download; the designs installed in the pack and one of them, with Delete
// theme; problems. The D-pad and A choose, B goes back, Y goes one folder up, X opens the help on
// the cleanup question, touch works on everything drawn as a button, and every button shows a push before it acts;
// the folder view scrolls by pixels (dragging, with a fling, or gliding after the cursor) under the corner button;
// START leaves from the start screen. A build with DEMO=1 (APP_DEMO) only pretends to install and delete, to show
// those screens in an emulator. Reading a folder, checking a theme, leaving its music out and downloading run on a
// worker thread, so the frames go on; so do a delete and the install, while this thread only draws and does not go
// back to its loop. While such work runs and nothing can be pressed, the loader takes the bottom screen's place
// (job_loader); the install has its own screen.
// Sizes and positions are in ../uikit/include/ui_layout.h and ui/layout.h, the drawing in ../uikit/source/ui.c and ui/app_ui.c.
#include <3ds.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"
#include "camera.h"
#include "console.h"
#include "deletion.h"
#include "designs.h"
#include "download.h"
#include "ease.h"
#include "files.h"
#include "install.h"
#include "layout.h"
#include "motion.h"
#include "plan.h"
#include "popup.h"
#include "preview.h"
#include "progress.h"
#include "scan.h"
#include "settings.h"
#include "store.h"
#include "text.h"
#include "touch.h"
#include "tracks.h"
#include "ui.h"
#include "app_ui.h"
#include "look.h"
#include "looks_built.h"

#define INK UI_COLOR(SC(COL_INK))
#define MUTED UI_COLOR(SC(COL_MUTED))
#define WARN UI_COLOR(SC(COL_ORANGE))
#define OFF_INK UI_COLOR(SC(COL_OFF_INK))
#define TILE_INK UI_COLOR(SC(COL_TILE_INK))

enum screen { SCR_MAIN, SCR_DESIGNS, SCR_BADGES, SCR_LIST, SCR_CONFIRM, SCR_DONE, SCR_PROBLEM, SCR_SCAN, SCR_URL,
              SCR_INSTALLED, SCR_DESIGN, SCR_PREVIEW, SCR_SETTINGS,
              SCR_BADGE_LIST, SCR_BADGE_SET, SCR_BADGE_SETS, SCR_BADGE_ONE, SCR_BADGE_DONE, SCR_BADGE_PROBLEM };

/* What a touch or a key acts on: the bar (or its left and right half), the tiles (or the check screen's two
 * buttons; HIT_FIRST + n is the n-th), the green button, the corner button, the scroll bar, the help button, the
 * popup's button (or its right one) and its left one, the footer's Y tab that opens the preview, the list rows
 * (HIT_ROW0 + i). */
enum hit { HIT_NONE = -1, HIT_LEFT, HIT_RIGHT, HIT_FIRST, HIT_SECOND, HIT_THIRD, HIT_GO, HIT_CORNER, HIT_SCROLL, HIT_HELP,
           HIT_POPUP, HIT_POPUP_LEFT, HIT_PREVIEW, HIT_ROW0 };

/* What the popup shows: the help on the cleanup question, a question before a delete (Cancel and Delete), a note,
 * the question whether to create the folder /Themes (Cancel and Create). */
enum popup_kind { POP_NONE, POP_HELP, POP_ASK_THEME, POP_ASK_FILE, POP_NOTE, POP_ASK_FOLDER, POP_ASK_SET };

static const rect TILE_ONE = {TILE_X1, TILE_Y, TILE_W, TILE_H};
static const rect TILE_TWO = {TILE_X2, TILE_Y, TILE_W, TILE_H};
static const rect DESIGNS_TILES[3] = {{TILE_X1, DTILE_Y, TILE_W, DTILE_H}, {TILE_X2, DTILE_Y, TILE_W, DTILE_H},
                                      {TILE_X1, WIDE_Y, TILE_X2 + TILE_W - TILE_X1, WIDE_H}};
static const rect KEEP_TILE = {TILE_X1, CHOICE_Y, TILE_W, CHOICE_H};
static const rect DELETE_TILE = {TILE_X2, CHOICE_Y, TILE_W, CHOICE_H};
static const rect GO_BUTTON = {GO_X, GO_Y, GO_W, GO_H};
static const rect GO_UPPER = {GO_X, GO_TWO_Y1, GO_W, GO_H};
static const rect GO_LOWER = {GO_X, GO_TWO_Y2, GO_W, GO_H};
static const rect GO_FACTS = {GO_X, GO_FACTS_Y, GO_W, GO_H};
static const rect DELETE_THEME_TILE = {TILE_X1, ACT_TILE_Y, TILE_W, CHOICE_H};
static const rect DELETE_FILE_TILE = {TILE_X2, ACT_TILE_Y, TILE_W, CHOICE_H};
static const rect LIST_AREA = {LIST_X, 0, LIST_W, LIST_BOTTOM};
static const rect SCROLL_AREA = {SCROLL_HIT_X, 0, BOTTOM_W - SCROLL_HIT_X, LIST_BOTTOM};
static const rect TRACK = {SCROLL_X, SCROLL_Y, SCROLL_W, LIST_BOTTOM - SCROLL_Y};
static const rect BODY = {BODY_PAD, BODY_PAD, BOTTOM_W - 2 * BODY_PAD, BAR_Y - 2 * BODY_PAD};

// ---------------------------------------------------------------- state

static int lang, screen, focus;
static int ring;                        /* the cursor ring shows: since a D-pad or circle pad press on this screen */
static int designs_above;               /* the Designs screen: the top tile the wide one was reached from */
static char version[32];               /* the foot: "v0.9.4-beta probe" with room for a longer mark ("beta.12") */
static am_view before, after;
static store_record record_sets[2][2];  /* installed.tmd and .new as read, twice: the pack view points into the set
                                           it was made from (record_set); the pack is read anew into the other */
static int record_set;
static pack_view pv;
static pack_view next_pv;               /* the pack read anew (read_pack), until take_pack makes it the pack */

static scan_list list, read_list;      /* the folder view; the folder job reads into read_list, the view shows list
                                           until the job is done */
static char folder[SCAN_PATH];          /* the folder read last, or being read */
static char list_dir[SCAN_PATH];        /* the folder list shows */
static scroll_view lv;                  /* the list shown (scroll.h): its cursor, selection (none until the D-pad asks)
                                           and scroll; every list's, one at a time (rows_of) */
static scroll_view sv;                  /* the settings rows' scroll (scroll.h); their cursor is focus */
static int sv_loose;                    /* the stylus moved them since the last key: they stay where it left them */
static int list_unread;                 /* the folder view could not be read again after a delete on the check screen */
static int open_selected;               /* the folder view had a selection when the chosen row was opened */
static int select_on_entry;             /* the folder view selects its cursor's row when it opens next */
static int keep_active, keep_selected;  /* a folder read again: put back its scroll, and its selection if the row */
static float keep_scroll;               /* is still there */
static int install_from;                /* where the last install started: SCR_LIST, or SCR_DESIGNS for a download */
static int install_selected;            /* the folder view then: its selection, scroll and the row installed from */
static float install_scroll;
static char install_row[SCAN_NAME];

static design_list designs;             /* the installed designs as listed (designs.h) */
static const pack_theme *designs_from;  /* the pack view's themes its rows point into */
static pack_theme *old_themes;          /* the themes load_pack replaced: kept while the rows may point into them */
enum rows_of { ROWS_FOLDER, ROWS_DESIGNS, ROWS_BADGES, ROWS_SETS };
static int rows_of;                     /* whose rows lv and the list geometry below are: the folder view's, the
                                           installed designs', the Badges part's folder's or its installed sets' */
static uint8_t music_of[PACK_CONTENTS]; /* by content index, MUSIC_*: read once from the content (indices are never
                                           handed out twice, so it stays true) */
static uint32_t music_error[PACK_CONTENTS];
static scan_list card_files;            /* /Themes, read to find each design's file */
static uint16_t design_index;           /* the design the one-design screen shows, and the row to select again */
static uint16_t delete_index;           /* the theme a delete question is about */
static int delete_from;                 /* the screen it was asked on: SCR_CONFIRM or SCR_DESIGN */
static int delete_file_kept;            /* a file of that theme is known to be on the card */

static int confirm_from;                /* where B leads back from the chosen theme */
static char chosen[SCAN_NAME], chosen_dir[SCAN_PATH];
static tp_source file_src;
static tp_package package;
static const tp_source *zip_src;
static int theme_open, chosen_kind;
static tp_theme theme;
static int theme_result = TP_E_ARG;
static install_plan plan;

static int problem_head, problem_ok;    /* the problem screen's header text; its bar says OK instead of Back */
static int note_sign = -1;  /* FB-E: the note's sign (ART_INFO_*) */
static char title[160], body[1600];
static int may_clean;
static char clean_name[SCAN_NAME], clean_dir[SCAN_PATH], done_name[TP_NAME_BYTES];
static uint8_t done_icon[TP_ICON_SIZE];  /* the installed theme's icon, for "Installiert" */
static char note[400];                  /* after a deletion: shown once in the Designs footer */
static int note_warn;
static int popup;                       /* POP_*: what the popup over the bottom screen shows */
static ui_popup pop;
static u64 popup_since;                 /* its first frame: it grows and fades in from then (POPUP_IN_MS) */
static int popup_fresh;                 /* not drawn yet */
static int popup_gone;                  /* the one that just closed, while it fades out (POPUP_OUT_MS): only drawn */
static u64 popup_gone_at;
static int popup_then;                  /* POP_ASK_*: what its answer starts once it has faded out (a delete, the
                                           folder question's job) */
static int popup_yes;                   /* ... and the answer: the right button (the folder question has work for
                                           both) */
static popup_guard guard;               /* the popup takes no touch meant for the screen under it */
static popup_guard screen_guard;        /* after a popup closed: the screen takes no touch meant for the popup */
static int keys_read;                   /* keys_sync read the input: the touch's edges count from the scan before */
static u32 held_before;                 /* ... what that scan had held */
static u32 keys_stale;                  /* keys held since keys_sync: no repeat from them until let go */
static popup_focus pop_focus;           /* the question's cursor: "Abbrechen" first, hidden until a key shows it */
static home_state home;                 /* HOME's SaveData.dat, read when a delete is asked for */
static int no_music_result;             /* the job leaving the music out: tp_theme_drop_bgm's result */

static char url[DOWNLOAD_URL_MAX], scan_note[160];
static char url_shown[3 * DOWNLOAD_URL_MAX + 1];   /* the address as shown: every byte the font would not draw, %XX */
static const uint16_t *camera_image;

static int pressed = HIT_NONE;          /* what shows the push before it acts (a key, or a touch just lifted) */
static int pending = HIT_NONE;          /* what acts once its push has shown */
static touch_state touch = {.target = HIT_NONE};   /* what the stylus presses (touch.h) */
static int thumb_held;                  /* the stylus is on the scroll bar */
static u64 pending_at, touch_since;
static int touch_x, touch_y;

/* Work that runs on the worker thread while the frames go on. */
enum job { JOB_NONE, JOB_FOLDER, JOB_CHECK, JOB_DOWNLOAD, JOB_NO_MUSIC, JOB_DESIGNS, JOB_DELETE_THEME,
           JOB_DELETE_FILE, JOB_THEMES, JOB_INSTALL,
           JOB_BADGES, JOB_BADGE_WRITE };
#define WORKER_STACK 0x40000
static int job;
static Thread worker;
static volatile int job_over, job_cancel;
static volatile float job_fraction;     /* the install's bar, 0 to 1 */
static volatile uint64_t job_done, job_total;
static u64 job_since;                   /* when the job started: its loader comes LOADER_WAIT_MS after */
static u64 job_over_at;                 /* when this thread saw the job's work done (0: not yet) */
static float job_dots, job_over_dots;   /* how far the loader's ring showed in the last frame; and at job_over_at */
static u64 back_since;                  /* when a job handed over under its loader (0: none): what the bottom screen */
static int back_screen;                 /* shows comes out of the background again, on the screen it started from */
static int job_result, root_next;       /* the folder job's result; reading THEMES_DIR: the root if it is missing */
static char job_select[SCAN_NAME];      /* the folder job: put the cursor on this entry */
static int themes_missing;              /* at the start: the folder THEMES_DIR is not on the card */
static int themes_declined;             /* at the start: the user said no to creating it before (THEMES_DECLINED) */
static int themes_for_list;             /* the question was asked for "Von SD installieren": the folder view follows */
static int themes_make, themes_made;    /* the answer; and whether the folder could be created */
static download_result dl;
static float progress_shown;            /* the bar's value on screen, gliding after the real one */
static progress_run bar;                /* the bar of the work under way: never goes back */
static int shown_key = -1;              /* the screen last drawn, and since when */
static u64 shown_since;
static int opened;                      /* a screen opened, or a job ended on one: entered() starts it afresh */

static ui_block block_a;

#define SETTINGS_PATH CARD_DIR "/settings.txt"
static settings prefs;                  /* the settings as the file on the card has them (settings.h) */
static int music_pick;                  /* the menu music: a track's place in tracks.h's list, or TRACK_COUNT for none */
static int look_pick;                   /* the look: its place in LOOKS, the looks this build holds */

static int preview_from;                /* the preview: the screen Y was pressed on, SCR_CONFIRM or SCR_DESIGN */
static int preview_seen;                /* its state as the last frame saw it (preview.h, PREVIEW_*) */
static u64 preview_since;               /* when its wallpapers first showed: their motion counts from here */

// ---------------------------------------------------------------- small helpers

static const char *folder_name(const char *dir)
{
    if (scan_is_root(dir))
        return text(lang, T_SD_CARD);
    return strrchr(dir, '/') + 1;
}

/* The status line: how many themes the HOME Menu lists, behind the house (the symbol returned); or what is wrong
 * with the pack, without a symbol (-1). */
static int status_line(char *out, size_t size)
{
    char unused[8];
    if (pv.state != PACK_OK) {
        text_pack(lang, &pv, out, size, unused, sizeof unused);
        return -1;
    }
    text_installed(lang, (unsigned)pv.usable, out, size);
    return ART_HOME_FOOT;
}

/* A file as it is shown: its folder as the corner button shows it, then its name ("/Themes/name.zip"). */
static void file_shown(char *out, size_t size, const char *dir, const char *name)
{
    const char *in = scan_shown(dir);
    size_t n = strlen(in);
    snprintf(out, size, "%s%s%s", in, n && in[n - 1] == '/' ? "" : "/", name);
}

/* The status line as the top screen's footer. */
static void foot_status(void)
{
    char status[160];
    int house = status_line(status, sizeof status);
    ui_foot(FOOT_DOTS_Y, house, status, MUTED, 0, NULL);
}

/* The host of an address: after "://" and any "user@", up to the port, path, query or fragment. */
static void url_host(const char *u, char *out, size_t size)
{
    const char *p = strstr(u, "://");
    p = p ? p + 3 : u;
    const char *at = NULL;
    for (const char *q = p; *q && *q != '/' && *q != '?' && *q != '#'; q++)
        if (*q == '@')
            at = q;
    if (at)
        p = at + 1;
    size_t n = *p == '[' ? strcspn(p, "]") + 1 : strcspn(p, "/?#:");
    snprintf(out, size, "%.*s", (int)n, p);
}

static int is_pressed(int target)
{
    return pressed == target || (!lv.dragging && !sv.dragging && touch_held(&touch, osGetTime()) == target);
}

static int tile_state(int target)
{
    return is_pressed(target) ? UI_DOWN : 0;
}

/* A lone "A OK" stands on the right tab, where A's action stands on every screen with two tabs (0.9.2); every other
 * lone action (Back, Cancel) on the left one. HIT_LEFT is the one action's tab in both cases. */
static int lone_ok(void)
{
    return (screen == SCR_DONE && !may_clean) || (screen == SCR_PROBLEM && problem_ok);
}

static int bar_down(void)
{
    return is_pressed(HIT_LEFT) ? 0 : is_pressed(HIT_RIGHT) ? 1 : -1;
}

// ---------------------------------------------------------------- the lists' geometry
// Every list, one at a time (rows_of): the folder view, the installed designs, the Badges part's folder and its
// installed sets. The same rows and the same scrolling (scroll.h); each screen draws its rows where row_rect says.

/* The Designs part's lists (their bar has two tabs, the probe watches them). */
static int list_screen(void)
{
    return screen == SCR_LIST || screen == SCR_INSTALLED;
}

/* Every screen that shows a list. */
static int rows_screen(void)
{
    return list_screen() || screen == SCR_BADGE_LIST || screen == SCR_BADGE_SETS;
}

static void badges_rows(size_t *count, size_t *heads);     /* badges_ui.h */

/* The rows of the list rows_of names, folders first; they scroll from the screen's top edge to LIST_BOTTOM, at scroll
 * 0 starting below the corner button, or near the top in the lists without one. */
static scroll_shape shape(void)
{
    scroll_shape s = {0, 0, FOLDER_H, ROW_H, LIST_GAP, LIST_INSET, LIST_BOTTOM, LIST_END_PAD, FOCUS + 1};
    if (rows_of == ROWS_FOLDER) {
        s.count = list.count;
        s.heads = list.folders;
    } else if (rows_of == ROWS_DESIGNS) {
        s.count = designs.count;
    } else {
        badges_rows(&s.count, &s.heads);
    }
    if (rows_of == ROWS_DESIGNS || rows_of == ROWS_SETS)
        s.inset = DESIGNS_INSET;
    return s;
}

static size_t list_count(void)
{
    return shape().count;
}

static float max_scroll(void)
{
    scroll_shape s = shape();
    scroll_area a = scroll_of(&s);
    return scroll_max(&a);
}

static float clamp_scroll(float v)
{
    scroll_shape s = shape();
    scroll_area a = scroll_of(&s);
    return scroll_clamp(&a, v);
}

static rect row_rect(size_t i)
{
    scroll_shape s = shape();
    return (rect){LIST_X, scroll_y(&s, &lv, i), LIST_W, scroll_row_h(&s, i)};
}

/* Where the list should be for the cursor's row to show whole below the corner button, with its ring. */
static float scroll_for_cursor(void)
{
    scroll_shape s = shape();
    return scroll_target(&s, &lv);
}

/* The first row that shows whole below the corner button: where a selection starts. */
static long first_shown(void)
{
    scroll_shape s = shape();
    return scroll_first(&s, &lv);
}

/* A D-pad, circle pad, L or R press: with nothing selected it selects the first row shown whole, else it moves. */
static void list_move(long delta)
{
    scroll_shape s = shape();
    if (scroll_move(&s, &lv, delta))
        sfx(SFX_MOVE);
}

/* Once per frame: the fling slows down, or the list glides to the cursor. */
static void list_motion(void)
{
    scroll_shape s = shape();
    scroll_frame(&s, &lv);
}

// ---------------------------------------------------------------- frames

static u64 last_frame;

/* The bar's value on screen: it glides after the real one, by the clock (PROGRESS_GLIDE_PCT of what is left in each
 * sixtieth of a second, however far apart the frames are); a smaller value starts a new run. */
static u64 glided_at;           /* when the value last moved (0: a new bar, job_start) */

static float glide(float fraction)
{
    static u32 at;              /* once per frame: the right eye's pass gets the same value */
    if (at == ui_frame())
        return progress_shown;
    at = ui_frame();
    u64 now = osGetTime();
    unsigned ms = glided_at && now > glided_at ? (unsigned)(now - glided_at) : 16;
    glided_at = now;
    if (fraction < 0)
        return progress_shown = -1;
    if (fraction < progress_shown - 0.001f || progress_shown < 0 || fraction - progress_shown < PROGRESS_SNAP)
        progress_shown = fraction;
    else
        progress_shown = progress_glide(progress_shown, fraction, PROGRESS_GLIDE_PCT, ms);
    return progress_shown;
}

/* How far the current screen's transition is, 0 to 1, by the clock. Keyed on the screen alone: a job draws over the
 * screen it started from and never starts the transition again. What a job ends on is new even on the same screen (a
 * folder, the rows read again): like a screen that opens it starts afresh, once no job runs, but its transition
 * goes on. The clock runs from the screen's second frame, set one frame back: the first frame is 0 and the second
 * one step on however long the first took (it converts the new screen's icons and lays out its text), so that work
 * never swallows the start of the motion. */
static float entered(void)
{
    static int first_frame;
    u64 now = osGetTime();
    if (screen != shown_key) {
        shown_key = screen;
        shown_since = now;
        opened = 1;
        first_frame = 1;
    } else if (first_frame) {
        shown_since = now - FRAME_MS;
        first_frame = 0;
    }
    if (opened && !job) {
        opened = 0;
        ring = 0;               /* every screen opens without the cursor ring, the folder view without a selection */
        lv.selected = select_on_entry;     /* unless it comes back to a row that had one */
        select_on_entry = 0;
        touch.target = HIT_NONE;            /* a touch begun on the last screen presses nothing here */
        lv.dragging = lv.armed = 0;
    }
    return ease_span(now, shown_since, TRANSITION_MS);
}

/* The look's choices: the colour scheme (ART_SCHEME_*), the style of every glass element (GLASS_*) and the pattern
 * behind the start screen's box. They are the settings' (settings.h), read before the first frame; the numbers are
 * the same on both sides. */
_Static_assert(SETTINGS_STYLES == GLASS_COUNT && SETTINGS_STYLES == PATTERN_COUNT && GLASS_CLEAR == 0 &&
               GLASS_SHARD == 1 && GLASS_PLATIN == 2 && PATTERN_PLUS == 0 && PATTERN_HOUSES == 1 &&
               PATTERN_BUBBLES == 2 && SETTINGS_LOGO_DEFAULT == GLASS_SHARD &&
               SETTINGS_PATTERN_DEFAULT == PATTERN_HOUSES, "the settings' numbers are the drawing layer's");
_Static_assert(SETTINGS_SCHEMES == ART_SCHEMES && SETTINGS_SCHEME_DEFAULT == ART_SCHEME_WEISS &&
               ART_SCHEME_GELB == 1 && ART_SCHEME_SAND == 2 && ART_SCHEME_MINZE == 3 && ART_SCHEME_HIMMEL == 4 &&
               ART_SCHEME_FLIEDER == 5 && ART_SCHEME_ROSE == 6 && ART_SCHEME_NACHT == 7 &&
               T_SCHEME_NIGHT == T_SCHEME_WHITE + ART_SCHEME_NACHT,
               "the settings' schemes are the drawing layer's, in the texts' order");

/* The looks this build holds (make's LOOKS, looks_built.h), Stock first: the choices of the settings row "Stil" */
_Static_assert(SETTINGS_DESIGNS == LOOK_COUNT && SETTINGS_DESIGN_DEFAULT == LOOK_STOCK && LOOK_FROST == 1 &&
               LOOK_NIGHT == 2 && LOOK_PRISM == 3 && T_LOOK_PRISM == T_LOOK_STOCK + LOOK_PRISM,
               "the settings' looks are look.h's, in the texts' order");
#define AS_LOOK(name, NAME) LOOK_##NAME,
static const int LOOKS[LOOKS_HELD] = {LOOKS_BUILT(AS_LOOK)};
/* The log names the look drawn by its word on the card */
#define LOOK_DRAWN settings_design_word(LOOKS[look_pick])

/* What a look changes on the settings screen (G1 plan, B6): whether it draws the start screen's pattern, and so has
 * the row "Hintergrund", and its first colour's name. */
static const struct {
    int pattern;
    int first_colour;
} LOOK_FACTS[LOOK_COUNT] = {
    [LOOK_STOCK] = {1, T_SCHEME_WHITE},
    [LOOK_FROST] = {1, T_SCHEME_WHITE},
    [LOOK_NIGHT] = {1, T_SCHEME_WHITE},
    [LOOK_PRISM] = {0, T_SCHEME_PEARL},
};

/* The look drawn: the one chosen, which is Stock when the card names one this build does not hold (settings.h). */
static int choice_look(void)
{
    return LOOKS[look_pick >= 0 && look_pick < LOOKS_HELD ? look_pick : 0];
}

/* Stock has no glass: its images stand in the Shard style's place (art.py --design stock), whatever the card says */
static int choice_glass(void)
{
    return GLASS_SHARD;
}

static int choice_pattern(void)
{
    return settings_pattern();
}

/* Before a frame is drawn the drawing layer is told the choices, so that no frame shows another look, scheme, glass
 * or pattern and the house's pictures are not unpacked twice. A look or scheme chosen on the settings screen so
 * shows in the very next frame, on that screen. */
static void tell_look(void)
{
    ui_look(choice_look(), settings_scheme(), choice_glass(), choice_pattern());
}

/* A glass element's image in the chosen style: the three styles' images stand in a row (art.py). */
_Static_assert(ART_DESIGNS_PLATIN == ART_DESIGNS + GLASS_PLATIN && ART_BADGES_PLATIN == ART_BADGES + GLASS_PLATIN,
               "a symbol's styles stand in a row");
static int in_glass(int clear_id)
{
    int style = choice_glass();
    return clear_id + (style > 0 && style < GLASS_COUNT ? style : 0);
}

/* The installing screen: when its first frame was drawn (0: not yet; it comes in from then as every screen does),
 * when its picture was first full (0: not yet), the time its last frame showed, and how its frames went, for the
 * log. */
static u64 install_since, install_full_at, install_last;
static unsigned install_frames, install_slowest;
/* the installing screen D3 (R9): the picture of the install (pour.h) and how the install is, as far as known */
static pour install_pour;
static int install_how;

/* C53 ("man könnte den loesch screen unten anzeigen nachdem es fertig geladen hat"): "Installiert" right after an
 * install, and a set's Done screen after its install (badges_ui.h), show no new picture: their top screen goes on
 * with the installing screen's end (install_top, badges_work_top), the tile full and glowing, the steps done, and
 * only the bottom screen changes (draw). The picture's model follows its clock tick by tick (pour.c), and these
 * screens stay as long as the user likes: the end's clock goes on with the frames drawn and stands while none is
 * (HOME, the lid), since hours to catch up would hold the first frame back. */
#define END_STEP_MS 100         /* the most the end's clock goes on from one frame to the next */
static int done_end;            /* "Installiert" follows an install: its top is the installing screen's end */
static u64 end_at, end_seen;    /* the end's clock, and when its last frame was drawn */

static void end_begin(void)
{
    end_at = end_seen = osGetTime();
}

/* Once per frame. */
static u64 end_now(void)
{
    u64 now = osGetTime(), gone = now - end_seen;
    end_at += gone < END_STEP_MS ? gone : END_STEP_MS;
    end_seen = now;
    return end_at;
}

/* Before an install's first frame. */
static void install_screen_start(void)
{
    install_since = install_full_at = install_last = 0;
    install_frames = install_slowest = 0;
    pour_start(&install_pour, &POUR_INSTALL, osGetTime());
    install_how = POUR_WORKING;
    progress_shown = 0;
    glided_at = 0;
}

/* The installing screen's top: the picture of the install (pour.h) with the theme's picture and name and the four
 * steps. "Installiert" goes on with it after an install (done_top), when the theme is closed: its name and icon are
 * the copies run_install took (done_name, done_icon). */
static void install_top(void)
{
    const char *steps[4] = {text(lang, T_STEP_CHECK), text(lang, T_STEP_CATALOGUE), text(lang, T_STEP_THEME),
                            text(lang, T_STEP_FINISH)};
    ui_pour(&install_pour, text(lang, T_INSTALLING), text(lang, T_INSTALLED), done_name, done_icon, -1, steps,
            text(lang, T_INSTALL_SUCCESS), NULL);
}

/* The installing picture once a frame (pour_work), with the drop for the pieces that landed by its clock since the last
 * frame: one for all of them, as a frame shows them together. While no frame is drawn nothing sounds, and nothing once
 * a failed step froze the picture. */
static void pour_frame(pour *p, u64 now, float level, int how)
{
    int landed = p->landed;
    pour_work(p, now, level, how);
    if (p->landed > landed)
        sfx(SFX_DROP);
}

/* The installing screen from the install's bar alone (progress.h): the picture of the install (pour.h). */
static void frame_install(float fraction)
{
    /* the two warnings side by side in one box, centred */
    const rect box = {(BOTTOM_W - SAFE_BOX_W) / 2, (SCREEN_H - SAFE_BOX_H) / 2, SAFE_BOX_W, SAFE_BOX_H};
    const float half = (SAFE_BOX_W - 2 * SAFE_BOX_PAD) / 2.0f;
    u64 now = osGetTime();
    if (!install_since)
        install_since = now;
    /* the picture follows the install's four steps, a quarter each, from the bar alone (progress.h) */
    static const float quarters[4] = {1, 2, 3, 4};
    pour_frame(&install_pour, now, progress_line_end(fraction, 0, quarters) / 4, install_how);
    if (!install_full_at && pour_full(&install_pour))
        install_full_at = now;
    ui_begin(ease_span(now, install_since, TRANSITION_MS));
    do {
        glide(fraction);
        ui_top();
        install_top();
        ui_bottom();
        ui_card_glass(box);
        ui_stack((rect){box.x + SAFE_BOX_PAD, box.y, half, box.h}, ART_POWER_OFF, text(lang, T_DONT_SWITCH_OFF),
                 SAFE_PX, SAFE_LH, SAFE_GAP, INK);
        ui_stack((rect){box.x + SAFE_BOX_PAD + half, box.y, half, box.h}, ART_SD_OFF, text(lang, T_DONT_REMOVE_SD),
                 SAFE_PX, SAFE_LH, SAFE_GAP, INK);
    } while (ui_next_eye());
    ui_end();
    last_frame = osGetTime();
    if (install_frames++ && last_frame - install_last > install_slowest)
        install_slowest = (unsigned)(last_frame - install_last);
    install_last = last_frame;
}

/* After the install, when nothing is written any more: how the installing screen's frames went, as a motion line. */
static void install_screen_log(void)
{
    console_log("motion: installing screen: %u frames in %llu ms, slowest %u ms", install_frames,
                (unsigned long long)(install_last - install_since), install_slowest);
}

enum { TICK_NONE, TICK_MEASURE };
static volatile int tick_mode;

/* Reading the chosen file while the install measures it, on its worker (the check before shows no bar). The bar
 * counts the bytes read (progress.h), so it never jumps to the end and starts again. */
static void file_tick(void *ctx, uint64_t read, uint64_t size)
{
    (void)ctx;
    if (tick_mode == TICK_MEASURE)
        job_fraction = progress_keep(&bar, progress_install(STEP_MEASURE, progress_passes(read, size, 1)));
}

/* After an install AM called a success (R9): the picture catches up with the install (POUR_TRAIL_MS for a step done
 * at once, POUR_CATCH_MS for a whole install), then the full box glows for POUR_END_MS; never longer, whatever the
 * picture does. The next screen is "Installiert", nothing in between: its top goes on with this picture (done_end). */
static void install_shown_done(void)
{
    u64 until = osGetTime() + POUR_CATCH_MS + POUR_END_MS;
    install_how = POUR_SUCCEEDED;
    while (!pour_over(&install_pour) && osGetTime() < until)
        frame_install(1.0f);
}

/* The demonstration build only: the worker waits about 3 s, as demo_install slows the install, so the loader of a
 * check or a delete can be seen. */
static void demo_hold(void)
{
#ifdef APP_DEMO
    svcSleepThread(3000000000LL);
#endif
}

/* On the install's worker: its progress (progress_install) for the frames this thread draws meanwhile; when each
 * step began goes to the log, to see where the line stands still on the console (AM calls that report no progress). */
static int logged_step;
static u64 install_began;

static void install_progress_show(void *ctx, int step, uint64_t done, uint64_t total)
{
    (void)ctx;
    if (step != logged_step) {
        console_log("install step %d at %llu ms", step, (unsigned long long)(osGetTime() - install_began));
        logged_step = step;
    }
    tick_mode = step == STEP_MEASURE ? TICK_MEASURE : TICK_NONE;
    job_fraction = progress_keep(&bar, progress_install(step, total ? (float)done / (float)total : 0));
}

// ---------------------------------------------------------------- the worker thread

/* Runs fn on a thread one step below this one's priority, on the same core: it works while this thread waits for
 * the screen, and this thread draws as soon as a frame is due. No graphics and no input on the worker; it only
 * writes the job's variables, which this thread reads once job_over is set, and never what the screens draw from.
 * Only the jobs run by job_here write to the log (this thread then draws and does nothing else); the others leave
 * what they found for their *_done. If no thread can be made, fn runs here, blocking as before.
 * The same core on a New 3DS too: threadCreate's core -2 is the process's default core (the exheader's
 * IdealProcessor, 0 here), core 1 takes one thread only after APT_SetAppCpuTimeLimit and gives it a share of the
 * system core, core 2 needs the exheader's permission for it and core 3 is not for applications (libctru 2.7.0,
 * 3ds/thread.h). A second core would make the two threads run at the same moment, which the job's plain variables
 * and the unlocked fs and am sessions are not built for, to win the little time this thread does not wait. */
static void job_start(int kind, ThreadFunc fn)
{
    s32 prio = 0x30;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    job_over = job_cancel = 0;
    job_fraction = 0;
    job_done = job_total = 0;
    progress_shown = 0;
    glided_at = 0;
    progress_start(&bar);
    job = kind;
    job_since = osGetTime();
    job_over_at = 0;
    job_dots = job_over_dots = 0;
    audio_work(1);              /* the working loop: every job begins it here and ends it in job_ended (the install
                                   in run_install) */
    touch.target = HIT_NONE;            /* a touch begun before presses nothing (the download's Cancel) */
    lv.dragging = lv.armed = 0;
    worker = threadCreate(fn, NULL, WORKER_STACK, prio + 1 > 0x3F ? 0x3F : prio + 1, -2, false);
    if (!worker) {
        fn(NULL);
        job_over_at = osGetTime();      /* done before a frame of it was drawn: its loader never comes */
    }
}

static void draw(void);
static int job_loader(void);
static void loader_now(float *cover, float *dots);
static void motion_told(u64 frame_before);

/* Whether the job's work is done and its loader has left; until then the job goes on and nothing can be pressed. A
 * job done before its loader came is left at once. One that showed it is left on the bare background, when what the
 * bottom screen showed has gone wholly and the ring has faded: then nothing jumps, whatever comes next. */
static int job_left(void)
{
    float cover, dots;
    if (!job_over)
        return 0;
    if (job_loader() < 0)
        return 1;
    if (!job_over_at) {
        job_over_at = osGetTime();
        job_over_dots = job_dots;       /* what the last frame showed, not what the clock says now */
    }
    loader_now(&cover, &dots);
    return job_over_dots <= 0 || (cover >= 1 && dots <= 0);
}

/* The job is left (job_left). If its loader had come, the bottom screen is the bare background now: on the screen
 * the job started from, what shows next comes out of it again (loader_now); a new screen comes in by its own
 * transition, which begins on the background too. */
static void job_ended(void)
{
    if (job_over_dots > 0) {
        back_since = osGetTime();
        back_screen = screen;
    }
    if (job != JOB_INSTALL)     /* the install's ends in run_install, with its done or error sound */
        audio_work(0);
    job = JOB_NONE;
    pressed = HIT_NONE;
    opened = 1;
}

/* Runs fn on the worker and draws frames here (frame) until it is over and its loader has left: for work that
 * changes the pack or the card (a delete, the install). This thread does not go back to the main loop meanwhile:
 * aptMainLoop is not called and no input is read, so HOME, the lid, START and an
 * order to close wait until the work is done, as they did while such work ran on this thread. Only the worker writes
 * to the log meanwhile, so its lines stay whole and in order; where it writes to the pack or the card, the caller
 * makes the log strict first, so every line is on the card before the next call is made. */
static void keys_sync(void);

static void job_here(int kind, ThreadFunc fn, void (*frame)(void))
{
    job_start(kind, fn);
    while (!job_left()) {
        u64 frame_before = last_frame;
        frame();
        motion_told(frame_before);
    }
    if (worker) {
        threadJoin(worker, U64_MAX);
        threadFree(worker);
        worker = NULL;
    }
    job_ended();
    keys_sync();
}

static void folder_work(void *arg)
{
    (void)arg;
    job_result = scan_folder(folder, &pv, &read_list, NULL, NULL);
    job_over = 1;
}

/* How long each step of a check took (0.9.2: opening a file was slow): the core says when its work changes its kind
 * (tp_set_phase) and files.c when a read of the file begins and ends; the ticks since the last change are booked to
 * the step that ends there. Only the worker counts; check_done writes the line to the log. */
enum { CHECK_OTHER, CHECK_READ, CHECK_CIA, CHECK_UNZIP, CHECK_LZ11, CHECK_HASH, CHECK_PLAN, CHECK_STEPS };
static u64 check_ticks[CHECK_STEPS], check_since, check_bytes;
static int check_step;

static void check_enter(int step)
{
    u64 now = svcGetSystemTick();
    check_ticks[check_step] += now - check_since;
    check_since = now;
    check_step = step;
}

static void check_phase(void *ctx, int phase)
{
    (void)ctx;
    check_enter(phase == TP_PHASE_UNZIP ? CHECK_UNZIP : phase == TP_PHASE_LZ11 ? CHECK_LZ11
                : phase == TP_PHASE_HASH ? CHECK_HASH : CHECK_OTHER);
}

static void check_reading(void *ctx, int begin)
{
    static int back;            /* the step the read interrupts */
    (void)ctx;
    if (begin)
        back = check_step;
    check_enter(begin ? CHECK_READ : back);
}

static void check_work(void *arg)
{
    (void)arg;
    char path[SCAN_PATH + SCAN_NAME], fallback[SCAN_NAME];
    scan_path(path, sizeof path, chosen_dir, chosen);
    scan_fallback(fallback, sizeof fallback, chosen);
    memset(check_ticks, 0, sizeof check_ticks);
    check_step = CHECK_OTHER;
    check_since = svcGetSystemTick();
    tp_set_phase(check_phase, NULL);
    theme_result = files_open_source(path, &file_src, file_tick, NULL) ? TP_E_READ : TP_OK;
    check_bytes = theme_result == TP_OK ? file_src.size : 0;
    if (theme_result == TP_OK)
        files_around_reads(&file_src, check_reading, NULL);
    check_enter(CHECK_CIA);     /* a CIA's content 0 hashed; next to nothing for a zip */
    if (theme_result == TP_OK)
        theme_result = tp_theme_in(&file_src, 1, &package, &zip_src, &chosen_kind);
    check_enter(CHECK_OTHER);
    if (theme_result == TP_OK)
        theme_result = tp_theme_open(&theme, zip_src, fallback);
    check_enter(CHECK_PLAN);
    if (theme_result == TP_OK)
        plan_install(&plan, &pv, &theme);
    check_enter(CHECK_OTHER);
    if (file_src.ctx)
        files_around_reads(&file_src, NULL, NULL);
    tp_set_phase(NULL, NULL);
    demo_hold();
    job_over = 1;
}

/* The installed designs, on the worker, for the pack as view holds it: whether each content holds music, for those
 * not read before, then /Themes for their files; scan_folder's result. */
static int designs_read(const pack_view *view)
{
    for (size_t k = 0; k < view->count; k++) {
        uint16_t index = view->themes[k].index;
        if (view->themes[k].usable && music_of[index] == MUSIC_UNKNOWN)
            music_of[index] = (uint8_t)console_content_music(index, &music_error[index]);
    }
    return scan_folder(THEMES_DIR, view, &card_files, NULL, NULL);
}

static void designs_work(void *arg)
{
    (void)arg;
    job_result = designs_read(&pv);
    job_over = 1;
}

/* "Ohne Musik installieren": the open theme without bgm.bcstm, its body's music byte cleared (tp_theme_drop_bgm). */
static void no_music_work(void *arg)
{
    (void)arg;
    no_music_result = tp_theme_drop_bgm(&theme);
    job_over = 1;
}

static int download_tick(void *ctx, uint64_t done, uint64_t total)
{
    (void)ctx;
    job_done = done;
    job_total = total;
    return job_cancel;
}

static void download_work(void *arg)
{
    (void)arg;
    download_progress progress = {NULL, download_tick};
    download(url, PART_PATH, THEMES_DIR, CA_PATH, console_user_agent(), &progress, &dl);
    job_over = 1;
}

// ---------------------------------------------------------------- the pack

static int no_archive(void *ctx, uint8_t *buf, size_t cap, size_t *size)
{
    (void)ctx, (void)buf, (void)cap, (void)size;
    return -1;
}

static int no_icon(void *ctx, uint16_t index, uint8_t icon[TP_ICON_SIZE])
{
    (void)ctx, (void)index, (void)icon;
    return -1;
}

/* AM, the record and the catalogue read anew into next_pv and the record set the pack does not use. It changes
 * nothing the screens draw from, so a delete's worker runs it too. */
static void read_pack(void)
{
    store_record *records = record_sets[record_set ^ 1];
    console_read_am(&before);
    console_log_view("now", &before);
    store_read(CARD_DIR, records);
    catalogue_reader cat = {NULL, no_archive, no_icon};
    int mounted = before.ok && before.exists && console_open_catalogue(&cat) == 0;
    pack_assess(&next_pv, &before, records, store_mark(CARD_DIR), &cat);
    if (mounted)
        console_close_catalogue();
    console_log("pack: state %d, detail %s, record %s (%s, %s), %u theme records, %u installed, mark %u, top %u",
                next_pv.state, tp_result_name(next_pv.detail),
                next_pv.tmd ? (next_pv.from_staged ? STORE_STAGED : STORE_FILE) : "none",
                records[0].present ? "installed.tmd present" : "installed.tmd missing",
                records[1].present ? ".new present" : "no .new", (unsigned)next_pv.count, (unsigned)next_pv.usable,
                next_pv.mark, next_pv.top);
    for (size_t k = 0; k < next_pv.count; k++)
        console_log("   theme %3u  %s  %s", next_pv.themes[k].index,
                    next_pv.themes[k].usable ? "installed" : "deleted  ", next_pv.themes[k].name);
}

/* The pack read_pack read becomes the pack: on this thread, between two frames. */
static void take_pack(void)
{
    /* the installed designs' rows point into the themes replaced here: they are kept until the next time, so rows
     * made before stay readable until they are made anew */
    if (designs_from == old_themes)
        designs.count = 0;
    free(old_themes);
    old_themes = pv.themes;
    pv = next_pv;
    memset(&next_pv, 0, sizeof next_pv);
    record_set ^= 1;
}

/* At the start: the pack, and whether the folder /Themes is there (themes_start). */
static void pack_work(void *arg)
{
    (void)arg;
    read_pack();
    themes_missing = !files_exists(THEMES_DIR);
    themes_declined = files_exists(THEMES_DECLINED);
}

/* The pack read at the start, before any frame (ST-B: nothing shows until it is read, so no frame stands still). It
 * runs on a thread of its own with the worker's stack, as it always ran (ST-B2): this thread's stack is libctru's
 * default __stacksize__ of 32 KB, which read_pack was never measured against. That thread is joined at once: no job,
 * no frame, no input meanwhile; if it cannot be made, the read runs here, as job_start runs a job. Then as after a
 * job (job_ended, job_here): the screen opens afresh, and a key held meanwhile counts only once it is let go and
 * pressed again. */
static void load_pack(void)
{
    s32 prio = 0x30;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    Thread reader = threadCreate(pack_work, NULL, WORKER_STACK, prio + 1 > 0x3F ? 0x3F : prio + 1, -2, false);
    if (reader) {
        threadJoin(reader, U64_MAX);
        threadFree(reader);
    } else {
        pack_work(NULL);
    }
    opened = 1;
    keys_sync();
    take_pack();
}

static void close_theme(void)
{
    if (theme_open)
        tp_theme_close(&theme);
    if (file_src.ctx)
        files_close_source(&file_src);
    theme_open = 0;
    zip_src = NULL;
    theme_result = TP_E_ARG;
}

static void show_problem(int head, const char *problem_title, const char *problem_body, int ok)
{
    problem_head = head;
    problem_ok = ok;
    snprintf(title, sizeof title, "%s", problem_title);
    snprintf(body, sizeof body, "%s", problem_body);
    screen = SCR_PROBLEM;
    sfx(SFX_ERROR);
}

// ---------------------------------------------------------------- actions

static void go_main(int which)
{
    focus = which;
    screen = SCR_MAIN;
}

static void go_designs(int which)
{
    designs_above = 0;          /* up from the wide tile: the left one, until the D-pad came down from another */
    close_theme();
    camera_stop();
    scan_free(&list);
    list_unread = 0;
    may_clean = 0;
    popup = POP_NONE;
    focus = which;
    screen = SCR_DESIGNS;
}

/* Both ways in need a pack that can take a theme; otherwise the reason. */
static int pack_ready(void)
{
    if (pv.state == PACK_OK)
        return 1;
    text_pack(lang, &pv, title, sizeof title, body, sizeof body);
    problem_head = T_DESIGNS;
    problem_ok = 0;
    screen = SCR_PROBLEM;
    return 0;
}

/* Lists folder on the worker; the cursor will be on the entry named select (the folder one came up from, or the row
 * the folder view comes back to). select may point into the list, so it is copied: the list is replaced when the job
 * is done and shows until then. */
static void read_folder(const char *select)
{
    snprintf(job_select, sizeof job_select, "%s", select ? select : "");
    scan_free(&read_list);
    job_start(JOB_FOLDER, folder_work);
}

/* Back from the check screen to the folder view, as it was (a delete on the check screen has read it again; if that
 * could not be done, the reason instead of a view that is no longer true). */
static void back_to_list(void)
{
    if (list_unread) {
        list_unread = 0;
        show_problem(T_SD_CARD, text_result(lang, TP_E_NOMEM), "", 0);
        return;
    }
    select_on_entry = open_selected;
    screen = SCR_LIST;
}

static void take_folder(void);
static void open_popup(int kind);
static void show_note(int sign, const char *head, const char *text_body);

/* The folder /Themes (0.9.2, item 20). This application stands in for the one that used to make that folder, so when
 * it is missing it offers to create it: at the start, unless the user said no at an earlier start (themes_start),
 * and whenever "Von SD installieren" finds it missing (folder_done), whatever was said before. Yes creates that one
 * folder and nothing else; no leaves a mark on the card, so the next start does not ask, and "Von SD installieren"
 * shows the card's root instead, as it did. Both answers are carried out on the worker (a write to the card right
 * when the popup closes would cost frames). */
static void ask_themes(int for_list)
{
    const char *paras[2] = {text(lang, T_ASK_THEMES), text(lang, T_ASK_THEMES_NOTE)};
    themes_for_list = for_list;
    ui_popup_text(&pop, paras, 2);
    open_popup(POP_ASK_FOLDER);
}

/* At the start, after the pack is read (pack_work looked on the card) and before any input. */
static void themes_start(void)
{
    if (themes_missing && !themes_declined)
        ask_themes(0);
}

static void themes_work(void *arg)
{
    (void)arg;
    if (themes_make) {
        themes_made = files_make_dir(THEMES_DIR) == FILES_OK;
        if (themes_made)
            remove(THEMES_DECLINED);
    } else {
        files_write(THEMES_DECLINED, (const uint8_t *)"", 0);
    }
    job_over = 1;
}

static void answer_themes(int yes)
{
    themes_make = yes;
    job_start(JOB_THEMES, themes_work);
}

static void themes_done(void)
{
    int for_list = themes_for_list;
    themes_for_list = 0;
    if (themes_make)
        console_log("%s %s", THEMES_DIR, themes_made ? "created" : "could NOT be created");
    else
        console_log("%s not created: the user said no (%s)", THEMES_DIR, THEMES_DECLINED);
    if (themes_make && !themes_made) {
        show_note(ART_INFO_SD, text(lang, T_THEMES_NOT_CREATED), NULL);
        return;
    }
    if (!for_list)
        return;
    snprintf(folder, sizeof folder, "%s", themes_make ? THEMES_DIR : CARD_ROOT);
    read_folder(NULL);
}

/* The folder as just read, for the log. */
static void folder_log(void)
{
    console_log("%s: %u folders, %u themes, %u other files%s", folder, (unsigned)read_list.folders,
                (unsigned)(read_list.count - read_list.folders), (unsigned)read_list.other,
                read_list.missing ? " (no such folder)" : "");
}

static void folder_done(void)
{
    if (job_result) {
        scan_free(&read_list);
        show_problem(T_SD_CARD, text_result(lang, TP_E_NOMEM), "", 0);
        return;
    }
    folder_log();
    if (read_list.missing && root_next) {
        root_next = 0;
        ask_themes(1);          /* the folder is needed now: asked, also when it was declined at a start */
        return;
    }
    take_folder();
    screen = SCR_LIST;
}

/* The folder as read (read_list) becomes the folder view's list: the cursor on the entry job_select names; with
 * keep_active the scroll as it was, and that row selected again if it was and is still there. */
static void take_folder(void)
{
    scan_free(&list);
    list = read_list;
    memset(&read_list, 0, sizeof read_list);
    list_unread = 0;
    snprintf(list_dir, sizeof list_dir, "%s", folder);
    root_next = 0;
    rows_of = ROWS_FOLDER;
    lv.cursor = 0;
    int found = 0;
    for (size_t i = 0; job_select[0] && i < list.count && !found; i++)
        if (!strcmp(list.items[i].file, job_select)) {
            lv.cursor = i;
            found = 1;
        }
    lv.fling = 0;
    lv.dragging = 0;
    if (keep_active) {
        keep_active = 0;
        lv.scroll = clamp_scroll(keep_scroll);
        select_on_entry = keep_selected && found;
        if (select_on_entry)
            lv.scroll = scroll_for_cursor();
    } else {
        lv.scroll = 0;
        lv.scroll = scroll_for_cursor();
    }
    lv.follow = 0;
}

/* The folder view read again by a delete's worker, where it is: the same scroll, and the row that was opened
 * selected again if it had been and is still there. 0, or -1 with the problem's screen when it could not be read. */
static int take_folder_again(void)
{
    if (job_result) {
        scan_free(&read_list);
        show_problem(T_SD_CARD, text_result(lang, TP_E_NOMEM), "", 0);
        return -1;
    }
    keep_active = 1;
    keep_scroll = lv.scroll;
    keep_selected = open_selected;
    snprintf(job_select, sizeof job_select, "%s", lv.cursor < list.count ? list.items[lv.cursor].file : "");
    take_folder();
    return 0;
}

static void open_list(void)
{
    if (!pack_ready())
        return;
    snprintf(folder, sizeof folder, "%s", THEMES_DIR);
    root_next = 1;
    read_folder(NULL);
}

static void folder_up(void)
{
    char child[SCAN_NAME];
    if (scan_up(folder, child, sizeof child) == 0)
        read_folder(child);
}

/* The installed designs read on the worker (designs_work); the cursor will be on design select (0: none). */
static void read_designs(uint16_t select)
{
    design_index = select;
    scan_free(&card_files);
    job_start(JOB_DESIGNS, designs_work);
}

static void designs_done(void)
{
    char line[160];
    designs_list(&designs, &pv, music_of);
    designs_files(&designs, &card_files);
    designs_from = pv.themes;
    free(old_themes);
    old_themes = NULL;
    console_log("installed designs: %u of %u theme records, room for %u; /Themes %s, %u themes",
                (unsigned)designs.count, (unsigned)pv.count, designs_room(&pv),
                job_result ? "NOT read (no memory)" : "read", (unsigned)card_files.count);
    for (size_t i = 0; i < designs.count; i++) {
        const design_row *r = &designs.rows[i];
        if (r->music == MUSIC_UNKNOWN)
            snprintf(line, sizeof line, "music unknown (FS %08" PRIX32 ")", music_error[r->index]);
        else
            snprintf(line, sizeof line, "%s", r->music == MUSIC_YES ? "music" : "no music");
        console_log("   theme %3u  %s, file %s  %s", r->index, line, r->file[0] ? r->file : "-", r->theme->name);
    }
    scan_free(&card_files);
    rows_of = ROWS_DESIGNS;
    lv.cursor = 0;
    int found = 0;
    for (size_t i = 0; design_index && i < designs.count && !found; i++)
        if (designs.rows[i].index == design_index) {
            lv.cursor = i;
            found = 1;
        }
    lv.fling = 0;
    lv.dragging = 0;
    if (keep_active) {
        keep_active = 0;
        lv.scroll = clamp_scroll(keep_scroll);
        select_on_entry = keep_selected && found;
        if (select_on_entry)
            lv.scroll = scroll_for_cursor();
    } else {
        lv.scroll = 0;
    }
    lv.follow = 0;
    screen = SCR_INSTALLED;
}

/* "Installierte Designs": also with a full pack (its themes can only be deleted from here or in HOME); a pack that
 * was not read whole shows why instead. */
static void open_designs(void)
{
    if (!designs_readable(&pv)) {
        text_pack(lang, &pv, title, sizeof title, body, sizeof body);
        problem_head = T_INSTALLED_DESIGNS;
        problem_ok = 0;
        screen = SCR_PROBLEM;
        return;
    }
    read_designs(0);
}

static const design_row *shown_design(void)
{
    for (size_t i = 0; i < designs.count; i++)
        if (designs.rows[i].index == design_index)
            return &designs.rows[i];
    return NULL;
}

/* One installed design's button: under the box of facts, which has a second row when the design's file is still in
 * /Themes (the box and the button then stand where the check screen's do). */
static rect design_tile(void)
{
    const design_row *d = shown_design();
    return (rect){(BOTTOM_W - TILE_W) / 2, d && d->file[0] ? ACT_TILE_Y : DESIGN_TILE_Y, TILE_W, CHOICE_H};
}

static void open_design(size_t i)
{
    if (i >= designs.count)
        return;
    open_selected = lv.selected;
    design_index = designs.rows[i].index;
    focus = 0;
    screen = SCR_DESIGN;
}

/* Back from one design to the list, as it was. */
static void back_to_designs(void)
{
    select_on_entry = open_selected;
    screen = SCR_INSTALLED;
}

/* The chosen theme dir/name, read in full on the worker (a CIA's content 0 hashed), then the plan. */
static void open_confirm(const char *name, const char *dir, int from)
{
    close_theme();
    snprintf(chosen, sizeof chosen, "%s", name);
    snprintf(chosen_dir, sizeof chosen_dir, "%s", dir);
    confirm_from = from;
    tick_mode = TICK_NONE;
    job_start(JOB_CHECK, check_work);
}

static void guard_screen(void);

/* A key counts where it went down after the last scan, and job_here's work and the install's end hold read none: so
 * where one ends and where a screen whose buttons act or a question shows (guard_screen, open_popup), the keys are
 * read, and one held then came down before and is no new press; it acts once let go and pressed again. The touch
 * keeps its own guard (popup.h): its state and edges still count from the scan before (stylus_held, keys_scan). */
static void keys_sync(void)
{
    if (!keys_read)
        held_before = hidKeysHeld();
    hidScanInput();
    keys_read = 1;
    keys_stale = hidKeysHeld() & ~KEY_TOUCH;
}

static int stylus_held(void)
{
    return ((keys_read ? held_before : hidKeysHeld()) & KEY_TOUCH) != 0;
}

/* This frame's input; after keys_sync the touch's edges as if it had not read */
static void keys_scan(u32 *down, u32 *held, u32 *up)
{
    hidScanInput();
    *down = hidKeysDown(), *held = hidKeysHeld(), *up = hidKeysUp();
    if (keys_read) {
        *down = (*down & ~KEY_TOUCH) | (*held & ~held_before & KEY_TOUCH);
        *up = (*up & ~KEY_TOUCH) | (held_before & ~*held & KEY_TOUCH);
    }
    keys_read = 0;
    keys_stale &= *held;
}

static void check_done(void)
{
    theme_open = theme_result == TP_OK;
    if (!theme_open && file_src.ctx)
        files_close_source(&file_src);
    console_log("chosen %s/%s: kind %d, %s; music file %d, music byte %d, warnings %u; plan %d (index %u, duplicate "
                "of %u, %s)", chosen_dir, chosen, chosen_kind, tp_result_name(theme_result),
                theme_open && theme.has_bgm, theme_open && theme.use_bgm, theme_open ? theme.warnings : 0,
                theme_open ? plan.state : -1, theme_open ? plan.index : 0, theme_open ? plan.duplicate : 0,
                theme_open ? tp_result_name(plan.detail) : "-");
    unsigned ms[CHECK_STEPS], total = 0;
    for (int k = 0; k < CHECK_STEPS; k++)
        total += ms[k] = (unsigned)((double)check_ticks[k] / CPU_TICKS_PER_MSEC);
    console_log("check times: %u ms for %llu bytes: read %u, CIA hash %u, unzip %u, LZ11 %u, RomFS hash %u, plan %u, "
                "other %u", total, (unsigned long long)check_bytes, ms[CHECK_READ], ms[CHECK_CIA], ms[CHECK_UNZIP],
                ms[CHECK_LZ11], ms[CHECK_HASH], ms[CHECK_PLAN], ms[CHECK_OTHER]);
    focus = 0;
    screen = SCR_CONFIRM;
    guard_screen();             /* Installieren opens where a folder view's rows were */
}

static void open_item(size_t i)
{
    if (i >= list.count)
        return;
    const scan_item *it = &list.items[i];
    if (!it->folder) {
        open_selected = lv.selected;
        open_confirm(it->file, folder, SCR_LIST);
        return;
    }
    if (strlen(folder) + 1 + strlen(it->file) >= sizeof folder)
        return;
    char inner[SCAN_PATH];
    scan_path(inner, sizeof inner, folder, it->file);
    snprintf(folder, sizeof folder, "%s", inner);
    read_folder(NULL);
}

static int can_install(void)
{
    return theme_open && theme_result == TP_OK && plan.state == PLAN_OK;
}

/* A theme whose music HOME plays: the check screen offers to install it without (a zip whose body leaves the music
 * off plays none either way, and keeps its one button and the warning). */
static int music_choice(void)
{
    return can_install() && theme.has_bgm && theme.use_bgm;
}

/* A theme HOME lists already: the check screen offers to delete it from the pack, or its file from the card. */
static int installed_already(void)
{
    return theme_open && theme_result == TP_OK && plan.state == PLAN_DUPLICATE;
}

/* Every popup opens here (its text laid out before): its guard, the cursor on the left button, and the touch that
 * may still be down forgotten, so lifting it presses nothing. */
static void open_popup(int kind)
{
    popup = kind;
    sfx(SFX_POPUP);
    popup_fresh = 1;
    popup_gone = popup_then = POP_NONE;
    guard_arm(&guard, stylus_held());
    focus_open(&pop_focus);
    touch.target = HIT_NONE;
    keys_sync();
}

/* How far the open popup has opened, 0 to 1 by the clock; 0 before its first frame. */
static float popup_opened(u64 now)
{
    if (popup_fresh)
        return 0;
    return ease_span(now, popup_since, POPUP_IN_MS);
}

/* Every popup closes here, the same way back: the screen's guard from now, its cursor hidden (the first key only
 * shows it again) and nothing pressed or pending on it; the screen may have changed under the popup. It fades out
 * from as far as it had opened: one closed before it was all there does not jump to its full size first. */
static void close_popup(void)
{
    u64 now = osGetTime();
    popup_gone = popup;
    popup_gone_at = now - (u64)((1.0f - popup_opened(now)) * POPUP_OUT_MS);
    popup = POP_NONE;
    guard_arm(&screen_guard, stylus_held());
    guard_from(&screen_guard, now);
    ring = 0;
    lv.selected = 0;
    touch.target = HIT_NONE;
    pending = pressed = HIT_NONE;
}

/* A screen whose button acts (installs, deletes, removes) opens where the stylus may be down: the end of a
 * job run by job_here (no input is read during it, nor during the install's end hold), a question, a check screen
 * after its read. As after a popup closed, it takes a touch only POPUP_GUARD_MS after now and none that is down now
 * (popup.h): no press begun before it showed acts, nor a key held since then (keys_sync). */
static void guard_screen(void)
{
    guard_arm(&screen_guard, stylus_held());
    guard_from(&screen_guard, osGetTime());
    keys_sync();
}

/* sign: a refusal's or a failure's (ART_INFO_*), above the text as on the problem screens; -1 none (a success) */
static void show_note(int sign, const char *head, const char *text_body)
{
    const char *paras[2] = {head, text_body};
    int count = text_body && text_body[0] ? 2 : 1;
    note_sign = sign;
    if (sign < 0)
        ui_popup_text(&pop, paras, count);
    else if (!ui_popup_text_signed(&pop, paras, count))
        note_sign = -1;         /* the sign never costs a sentence (ui_popup_text_signed) */
    open_popup(POP_NOTE);
}

#ifdef APP_DEMO
/* The demonstration build (make app DEMO=1, never delivered): the emulator's AM refuses an install, so this walks
 * through the install's steps with short pauses and reports success, without calling AM and without writing to the
 * card. It shows the screens after an install. */
static void demo_install(const install_progress *progress, install_result *res)
{
    /* the line takes 1.2 s or more from each mark to the next (the four steps end with STEP_MEASURE, STEP_CONTENT0,
     * STEP_CONTENT and STEP_COMMIT2; the others report no progress): a mark's motion (STEP_MOVE_MS), then more than
     * one breath of the step under way (STEP_PULSE_MS); Stock's balls (R9) flow for 1.6 s or more in each of the
     * first three, and wait over the box through the commit. 8 s in all */
    static const struct {
        int step, ms;
    } steps[] = {{STEP_MEASURE, 2000},
                 {STEP_STAGE, 100}, {STEP_TITLE1, 200}, {STEP_TMD, 200}, {STEP_CONTENT0, 1600},
                 {STEP_COMMIT1, 200}, {STEP_RECORD, 100}, {STEP_TITLE2, 100}, {STEP_CONTEXT, 100}, {STEP_CONTENT, 2400},
                 {STEP_COMMIT2, 1200}};
    for (size_t k = 0; k < sizeof steps / sizeof steps[0]; k++)
        for (int part = 0; part <= steps[k].ms / 10; part++) {
            progress->show(progress->ctx, steps[k].step, (uint64_t)part, (uint64_t)(steps[k].ms / 10));
            svcSleepThread(10000000LL);
        }
    memset(res, 0, sizeof *res);
    res->done = res->wrote = res->tmd_accepted = res->committed1 = res->record_ok = 1;
    progress->show(progress->ctx, STEP_DONE, 1, 1);
}
#endif

/* AM's view right before the first write: the record must still match it and nothing may be pending. It is also
 * the view the judgement compares with, so a theme deleted in HOME meanwhile does not count as lost. */
static int still_same(install_result *res)
{
    tp_difference d;
    console_read_am(&before);
    console_log_view("before the install", &before);
    if (before.ok && before.exists && before.pending == PENDING_NO &&
        tp_tmd_check(pv.tmd, pv.tmd_size, before.version, before.list, before.count, &d) == TP_OK)
        return 1;
    memset(res, 0, sizeof *res);
    res->failed = STEP_MEASURE;
    res->detail = TP_E_STALE;
    console_log("The pack changed since it was read: nothing is written");
    return 0;
}

/* After an install or its refusal, the pack read anew (read_pack): the theme and the folder view closed, that pack
 * taken, then "Installiert" or the problem. */
static void finish(int done)
{
    close_theme();
    scan_free(&list);
    take_pack();
    focus = 0;
    screen = done ? SCR_DONE : SCR_PROBLEM;
    guard_screen();             /* "Löschen" deletes the file */
    sfx(done ? SFX_DONE : SFX_ERROR);
}

/* What the install's worker leaves for this thread. */
static install_result install_res;
static judgement install_j;

/* The install on the worker (0.9.2), so the frames go on during AM's calls: install_run with the console's importer
 * and the same arguments as when it was called on this thread, then what followed it there in the same order: AM's
 * view afterwards, the judgement, the outcome's line, and the pack read anew. Every line of it goes to the card at
 * once (run_install makes the log strict). */
static void install_work(void *arg)
{
    (void)arg;
    install_progress progress = {NULL, install_progress_show};
#ifdef APP_DEMO
    demo_install(&progress, &install_res);
#else
    importer imp;
    console_importer(&imp);
    install_run(&plan, &theme, &imp, CARD_DIR, &progress, &install_res);
#endif
    tick_mode = TICK_NONE;
    console_log("install: done %d, failed at step %d (%s), wrote %d, TMD accepted %d, phase 1 committed %d, record "
                "renamed %d", install_res.done, install_res.failed, tp_result_name(install_res.detail),
                install_res.wrote, install_res.tmd_accepted, install_res.committed1, install_res.record_ok);
    console_read_am(&after);
    console_log_view("after", &after);
#ifdef APP_DEMO
    install_j = (judgement){OUT_DONE, 1, 0, 1, PENDING_NO};
#else
    install_judge(&before, &after, &plan, &install_res, &install_j);
#endif
    text_outcome(lang, &install_j, &install_res, theme.name, title, sizeof title, body, sizeof body);
    console_log("outcome %d (lost %u, new installed %d, pending %d): %s | %s", install_j.outcome,
                (unsigned)install_j.lost, install_j.new_installed, install_j.pending, title, body);
    read_pack();
    job_over = 1;
}

static void install_frame(void)
{
    frame_install(job_fraction);
}

/* "Installieren". AM's view is read once more here, right before the first write. Then the install runs on the
 * worker while this thread draws the installing screen and nothing else: it does not go back to the main loop until
 * the worker is done (job_here), so aptMainLoop is not called and no key is read, and HOME and sleep are disallowed
 * for that time, as while the install ran on this thread. So START, HOME, the lid and an order to close cannot
 * interrupt it: they wait until it is over. */
static void run_install(void)
{
    may_clean = 0;
    problem_head = T_INSTALL_THEME;
    problem_ok = 1;
    console_log_strict(1);      /* from AM's view before the first write to the pack read after it */
    if (!still_same(&install_res)) {
        judgement j = {OUT_NOTHING_WRITTEN, 1, 0, 0, PENDING_NO};
        text_outcome(lang, &j, &install_res, theme.name, title, sizeof title, body, sizeof body);
        read_pack();
        finish(0);
        console_log_strict(0);
        return;
    }
    aptSetHomeAllowed(false);
    aptSetSleepAllowed(false);
    console_log("== Install \"%s\" from %s/%s as theme %u%s: title version %u -> %u, content 0 ID %08" PRIX32
                ", catalogue top %u", theme.name, chosen_dir, chosen, plan.index,
                theme.body_mem ? " without music" : "", pv.version, plan.delta.version, plan.delta.content0_id,
                plan.top);
    install_from = confirm_from;        /* the folder view is freed by finish(); after it comes back as it was */
    install_selected = open_selected;
    install_scroll = lv.scroll;
    snprintf(install_row, sizeof install_row, "%s", chosen);
    snprintf(done_name, sizeof done_name, "%s", theme.name);    /* the installing screen's and "Installiert"'s */
    memcpy(done_icon, theme.icon, sizeof done_icon);
    install_screen_start();
    logged_step = -1;
    install_began = osGetTime();
    files_count_restart(&file_src);     /* measuring is its own pass over the file the check read */
    frame_install(0);           /* the installing screen from the first moment, also if no thread can be made */
    job_here(JOB_INSTALL, install_work, install_frame);
    may_clean = install_may_clean(&install_j, &install_res);
    snprintf(clean_name, sizeof clean_name, "%s", chosen);
    snprintf(clean_dir, sizeof clean_dir, "%s", chosen_dir);
    if (install_j.outcome == OUT_DONE) {
        install_shown_done();
        done_end = 1;           /* "Installiert" goes on with this top */
        end_begin();
    }
    aptSetHomeAllowed(true);
    aptSetSleepAllowed(true);
    audio_work(0);              /* the working loop goes on through the marks' last stay; the sound in finish closes it */
    finish(install_j.outcome == OUT_DONE);
    console_log_strict(0);
    install_screen_log();
#ifdef APP_DEMO
    pv.usable++;            /* the status line counts the theme the demonstration pretended to install */
#endif
}

/* Deletes the one file the installed theme came from, after the user said so. */
static void clean_up(void)
{
    char path[SCAN_PATH + SCAN_NAME];
    scan_path(path, sizeof path, clean_dir, clean_name);
#ifdef APP_DEMO
    int ok = 1;
    console_log("cleanup: %s not deleted (demonstration)", path);
#else
    console_log_strict(1);
    int ok = remove(path) == 0;
    console_log("cleanup: %s %s", path, ok ? "deleted" : "NOT deleted");
    console_log_strict(0);
#endif
    snprintf(note, sizeof note, text(lang, ok ? T_DELETED : T_DELETE_FAILED), clean_name);
    note_warn = !ok;
    may_clean = 0;
}

/* After "Installiert" (and its cleanup question): back where the install started. From the folder view: that folder
 * read again where it was, so the theme's row shows it installed (after "Löschen" the row is gone and nothing is
 * selected), the cleanup's note over it; from a download: the Designs screen on "QR-Code scannen", the note in its
 * footer as before. */
static void after_install(void)
{
    if (install_from != SCR_LIST) {
        go_designs(1);
        return;
    }
    char cleaned[sizeof note];
    int cleaned_warn = note_warn;
    snprintf(cleaned, sizeof cleaned, "%s", note);
    note[0] = 0;
    may_clean = 0;
    popup = POP_NONE;
    if (cleaned[0])
        show_note(cleaned_warn ? ART_INFO_WARN : -1, cleaned, NULL);
    keep_active = 1;
    keep_scroll = install_scroll;
    keep_selected = install_selected;
    read_folder(install_row);
}

/* The help on the cleanup question: where the file is, what Keep and Delete do; laid out once, here. */
static void open_help(void)
{
    const char *const rest[] = {text(lang, T_HELP_KEEP), text(lang, T_HELP_DELETE)};
    int fit = ui_popup_fit(&pop, text(lang, T_HELP_FILE), scan_shown(clean_dir), rest, 3);
    console_log("help: text at %d px%s", pop.para[0].px, fit ? "" : ", cut to fit");
    open_popup(POP_HELP);
}

/* "Ohne Musik installieren": the music is left out on the worker, then the same install as "Installieren". */
static void start_no_music(void)
{
    snprintf(done_name, sizeof done_name, "%s", theme.name);   /* drawn while the worker changes the theme */
    memcpy(done_icon, theme.icon, sizeof done_icon);
    tick_mode = TICK_NONE;
    job_start(JOB_NO_MUSIC, no_music_work);
}

static void no_music_done(void)
{
    console_log("without music: %s, body %u bytes%s", tp_result_name(no_music_result), (unsigned)theme.body_size,
                theme.body_mem ? ", its music byte cleared" : "");
    if (no_music_result != TP_OK) {
        show_note(ART_INFO_WARN, text(lang, T_CANNOT_INSTALL), text_result(lang, no_music_result));
        return;
    }
    plan_install(&plan, &pv, &theme);   /* the same theme and index; the install measures the new content */
    if (plan.state == PLAN_OK)
        run_install();
}

/* The name HOME lists for the theme a plan names, else the zip's (on the check screen). */
static void deleted_name(const deletion_plan *dp, char *out, size_t size)
{
    snprintf(out, size, "%s", dp->theme ? dp->theme->name : theme_open ? theme.name : "?");
}

/* "Design löschen" for theme index, on the check screen of its file (from: SCR_CONFIRM, the file is on the card) or
 * on the one-design screen (SCR_DESIGN, file_kept: a file of it is in /Themes): HOME's data now, then the plan on
 * the pack as read; the question, or why not. */
static void ask_delete_theme(uint16_t index, int from, int file_kept)
{
    char name[TP_NAME_BYTES], head[200], why[600];
    deletion_plan dp;
    delete_index = index;
    delete_from = from;
    delete_file_kept = file_kept;
    console_read_home(&home);
    deletion_plan_make(&dp, &pv, &before, &home, TP_TITLE_ID, index);
    deleted_name(&dp, name, sizeof name);
    console_log("delete theme %u (\"%s\") asked: plan %d", index, name, dp.state);
    if (dp.state != DEL_OK) {
        text_deletion(lang, dp.state, name, head, sizeof head, why, sizeof why);
        show_note(ART_INFO_WARN, head, why);
        return;
    }
    snprintf(head, sizeof head, text(lang, T_ASK_DELETE_THEME), name);
    const char *paras[2] = {head, text(lang, file_kept ? T_ASK_DELETE_THEME_NOTE : T_ASK_DELETE_THEME_NO_FILE)};
    ui_popup_text(&pop, paras, 2);
    open_popup(POP_ASK_THEME);
}

/* What a delete's worker leaves for this thread. */
static struct {
    int ran;                    /* a theme: the plan held right before, AM was asked and the pack read again */
    int ok;                     /* a file: it is gone */
    char head[200], why[800];   /* a theme: the outcome, for the note */
    int gone;                   /* FB-E: a theme: the outcome is DEL_DONE (its note has no sign) */
} del;

/* On the worker, after Delete in the popup: AM's view and HOME's data read again right before the one AM call, the
 * outcome judged and logged; then the pack read anew (for take_pack) and with it what the screen after the delete
 * shows: the installed designs, or the folder view the check screen came from. So the list is true the moment it
 * shows again, and nothing is read after the loader has gone. */
static void delete_theme_work(void *arg)
{
    (void)arg;
    char name[TP_NAME_BYTES];
    deletion_plan dp;
    del.ran = 0;
    console_read_am(&before);
    console_log_view("before the delete", &before);
    console_read_home(&home);
    deletion_plan_make(&dp, &pv, &before, &home, TP_TITLE_ID, delete_index);
    deleted_name(&dp, name, sizeof name);
    if (dp.state != DEL_OK) {
        console_log("delete theme %u refused: plan %d, nothing deleted", dp.index, dp.state);
        text_deletion(lang, dp.state, name, del.head, sizeof del.head, del.why, sizeof del.why);
        job_over = 1;
        return;
    }
    console_log("== Delete theme %u \"%s\" of %016llX", dp.index, name, TP_TITLE_ID);
    uint32_t error = 0;
    demo_hold();
#ifdef APP_DEMO
    int r = 0;
    console_log("delete: nothing deleted (demonstration)");
#else
    deleter d;
    console_deleter(&d);
    int r = deletion_run(&dp, &d, &error);
#endif
    console_read_am(&after);
    console_log_view("after the delete", &after);
    deletion_judgement j;
#ifdef APP_DEMO
    j = (deletion_judgement){DEL_DONE, 0, 1};
#else
    deletion_judge(&before, &after, &pv, &dp, &j);
#endif
    text_deleted(lang, &j, r == 1, error, delete_file_kept, name, del.head, sizeof del.head, del.why,
                 sizeof del.why);
    del.gone = j.outcome == DEL_DONE;
    console_log("delete: AM %s %08" PRIX32 ", outcome %d (lost %u, record %s): %s | %s", r ? "FAILED" : "ok", error,
                j.outcome, (unsigned)j.lost, j.record_ok ? "matches" : "DOES NOT MATCH", del.head, del.why);
    read_pack();
#ifdef APP_DEMO
    for (size_t k = 0; k < next_pv.count; k++)   /* as if AM had deleted it, to show the screens afterwards */
        if (next_pv.themes[k].index == dp.index && next_pv.themes[k].usable) {
            next_pv.themes[k].usable = 0;
            next_pv.usable--;
        }
#endif
    del.ran = 1;
    job_result = 0;
    if (delete_from == SCR_DESIGN)
        job_result = designs_read(&next_pv);
    else if (confirm_from == SCR_LIST && !(job_result = scan_folder(folder, &next_pv, &read_list, NULL, NULL)))
        folder_log();
    job_over = 1;
}

/* "Design löschen" confirmed: the delete on the worker, the loader in the bottom screen's place; HOME and the lid
 * wait. Then, in one step: the pack as read anew, and where it was asked, the installed designs
 * without the theme, or the check screen planned anew (the theme can be installed again once it is gone) over a
 * folder view that no longer shows it installed; the outcome in a note. */
static void run_delete_theme(void)
{
    scan_free(&read_list);
    scan_free(&card_files);
    aptSetHomeAllowed(false);
    aptSetSleepAllowed(false);
    console_log_strict(1);
    job_here(JOB_DELETE_THEME, delete_theme_work, draw);
    console_log_strict(0);
    aptSetHomeAllowed(true);
    aptSetSleepAllowed(true);
    show_note(del.ran && del.gone ? -1 : ART_INFO_WARN, del.head, del.why);
    if (!del.ran)
        return;
    take_pack();
    if (delete_from == SCR_DESIGN) {
        keep_active = 1;        /* the list where it was, without the design */
        keep_scroll = lv.scroll;
        keep_selected = open_selected;
        design_index = delete_index;
        designs_done();
        return;
    }
    if (confirm_from == SCR_LIST && job_result) {
        scan_free(&read_list);
        list_unread = 1;        /* said when the folder view is asked for (back_to_list) */
    } else if (confirm_from == SCR_LIST) {
        take_folder_again();
    }
    plan_install(&plan, &pv, &theme);
    focus = 0;
}

/* "Datei löschen": the question, with the file's folder (cut at its beginning when long) and its name. */
static void ask_delete_file(void)
{
    char ask[SCAN_NAME + 80];
    snprintf(ask, sizeof ask, text(lang, T_ASK_DELETE_FILE), chosen);
    const char *const rest[] = {ask, text(lang, T_ASK_DELETE_FILE_NOTE)};
    ui_popup_fit(&pop, text(lang, T_ASK_DELETE_FILE_WHERE), scan_shown(chosen_dir), rest, 3);
    open_popup(POP_ASK_FILE);
}

/* On the worker, after Delete in the popup: the one file, closed first (its theme stays as it is for the check
 * screen, which is drawn until the loader has taken its place; run_delete_file closes it), then the folder view it
 * was chosen in, read without it. */
static void delete_file_work(void *arg)
{
    (void)arg;
    char path[SCAN_PATH + SCAN_NAME];
    scan_path(path, sizeof path, chosen_dir, chosen);
    demo_hold();
    files_close_source(&file_src);
#ifdef APP_DEMO
    del.ok = 1;
    console_log("file delete: %s not deleted (demonstration)", path);
#else
    del.ok = remove(path) == 0;
    console_log("file delete: %s %s", path, del.ok ? "deleted" : "NOT deleted");
#endif
    job_result = confirm_from == SCR_LIST ? scan_folder(folder, &pv, &read_list, NULL, NULL) : 0;
    if (confirm_from == SCR_LIST && !job_result)
        folder_log();
    job_over = 1;
}

/* "Datei löschen" confirmed: on the worker, the loader in the bottom screen's place; then back where the check
 * screen came from, the folder view already without the file, with the outcome. */
static void run_delete_file(void)
{
    scan_free(&read_list);
    console_log_strict(1);
    job_here(JOB_DELETE_FILE, delete_file_work, draw);
    console_log_strict(0);
    close_theme();
    char done[SCAN_NAME + 80];
    snprintf(done, sizeof done, text(lang, del.ok ? T_DELETED : T_DELETE_FAILED), chosen);
    if (confirm_from != SCR_LIST) {
        go_designs(1);
        snprintf(note, sizeof note, "%s", done);
        note_warn = !del.ok;
        return;
    }
    show_note(del.ok ? -1 : ART_INFO_WARN, done, NULL);
    if (take_folder_again())
        return;
    screen = SCR_LIST;
}

#include "badges_ui.h"

static void start_scan(void)
{
    if (!bqr && !pack_ready())
        return;
    scan_note[0] = 0;
    camera_image = NULL;
    if (camera_start()) {
        show_problem(T_SCAN_QR, text(lang, T_CAMERA_FAILED), "", 0);
        return;
    }
    screen = SCR_SCAN;
}

static void download_done(void);

/* Before anything is sent: is the wireless on? Without it no request starts, and the screen says so instead of
 * curl's connect error. When AC cannot tell, the download goes ahead as before. */
static void run_download(void)
{
    if (console_wifi() == 0) {
        show_problem(T_QR_FOUND, text(lang, T_NO_WIFI), text(lang, T_NO_WIFI_HINT), 0);
        return;
    }
    aptSetHomeAllowed(false);
    aptSetSleepAllowed(false);
    if (console_net_start()) {  /* no job is started: the download's end, with the reason, at once */
        memset(&dl, 0, sizeof dl);
        dl.status = DL_NETWORK;
        snprintf(dl.error, sizeof dl.error, "socInit");
        dl.rcvbuf_before = dl.rcvbuf_after = -1;
        download_done();
        return;
    }
    job_start(JOB_DOWNLOAD, bqr ? badges_download_work : download_work);
}

static void download_done(void)
{
    console_net_stop();
    aptSetHomeAllowed(true);
    aptSetSleepAllowed(true);
    console_log("download %s: status %d, HTTP %ld, %llu bytes, kind %d, %s, %s, file %s", url, dl.status, dl.http,
                (unsigned long long)dl.bytes, dl.kind, tp_result_name(dl.detail), dl.error, dl.file);
    /* bytes per millisecond are KB/s with 1 KB = 1000 bytes */
    console_log("download: %llu bytes in %u ms (%llu KB/s), first byte after %u ms, %u pieces, largest %u, %u ms in "
                "the file, rcvbuf %d -> %d, file buffer %u", (unsigned long long)dl.bytes, dl.body_ms,
                (unsigned long long)(dl.body_ms ? dl.bytes / dl.body_ms : 0), dl.first_ms, dl.pieces, dl.largest,
                dl.file_ms, dl.rcvbuf_before, dl.rcvbuf_after, dl.file_buffer);
    if (bqr) {
        badges_download_done();
        return;
    }
    if (dl.status == DL_OK) {
        open_confirm(dl.file, THEMES_DIR, SCR_DESIGNS);
        return;
    }
    char why[600];
    text_download(lang, dl.status, dl.http, dl.detail, dl.error, why, sizeof why);
    show_problem(T_QR_FOUND, text(lang, T_DOWNLOAD_FAILED), why, 0);
}

/* The job is over: the worker is joined and the flow goes on here. */
static void job_finish(void)
{
    if (worker) {
        threadJoin(worker, U64_MAX);
        threadFree(worker);
        worker = NULL;
    }
    int kind = job;
    job_ended();
    switch (kind) {
    case JOB_FOLDER: folder_done(); break;
    case JOB_CHECK: check_done(); break;
    case JOB_DOWNLOAD: download_done(); break;
    case JOB_NO_MUSIC: no_music_done(); break;
    case JOB_DESIGNS: designs_done(); break;
    case JOB_THEMES: themes_done(); break;
    case JOB_BADGES: badges_job_done(); break;
    default: break;
    }
}

/* The camera stops while the application is in the background or the console sleeps, and starts again after. */
static void on_apt(APT_HookType hook, void *param)
{
    (void)param;
    if (screen != SCR_SCAN)
        return;
    if (hook == APTHOOK_ONSUSPEND || hook == APTHOOK_ONSLEEP) {
        camera_stop();
        camera_image = NULL;
    } else if (hook == APTHOOK_ONRESTORE || hook == APTHOOK_ONWAKEUP) {
        if (camera_start())
            show_problem(T_SCAN_QR, text(lang, T_CAMERA_FAILED), "", 0);
    }
}

static void poll_camera(void)
{
    const uint16_t *frame;
    if (!camera_frame(&frame))
        return;
    camera_image = frame;
    char found[DOWNLOAD_URL_MAX];
    if (!camera_qr(frame, found, sizeof found))
        return;
    if (!download_url_ok(found)) {
        text_shown(scan_note, sizeof scan_note, found);
        return;
    }
    snprintf(url, sizeof url, "%s", found);
    text_shown(url_shown, sizeof url_shown, url);
    console_log("QR code: %s", url);
    sfx(SFX_CONFIRM);
    camera_stop();
    camera_image = NULL;
    screen = SCR_URL;
}

// ---------------------------------------------------------------- the preview (preview.h)

/* A preview is on offer: on the check screen of a file that was read, and on an installed design's screen. The
 * footer then is two tabs, B back and Y preview, and Y starts it. */
static int can_preview(void)
{
    return (screen == SCR_CONFIRM && theme_open && theme_result == TP_OK) || (screen == SCR_DESIGN && shown_design());
}

static void open_preview(void)
{
    preview_from = screen;
    preview_seen = PREVIEW_LOADING;
    preview_since = osGetTime();        /* its loader comes LOADER_WAIT_MS after, as a job's */
    audio_work(1);              /* its reading is a load as a job's is */
    if (screen == SCR_CONFIRM)
        preview_open_theme(&theme);
    else
        preview_open_content(design_index);
    screen = SCR_PREVIEW;
}

/* Back to the screen it was opened on, which is as it was. */
static void close_preview(void)
{
    preview_close();
    preview_seen = PREVIEW_OFF;
    screen = preview_from;
}

/* Once per frame while the preview's screen shows. The frame its reading ends: the wallpapers' textures go to the
 * drawing, the music has started (preview_poll), the log gets its line and the screen fades in anew. */
static int preview_step(void)
{
    int state = preview_poll();
    if (state == preview_seen)
        return state;
    preview_seen = state;
    audio_work(0);
    char line[320];
    preview_describe(line, sizeof line);
    console_log("%s", line);
    const tp_body *walls = preview_walls();
    for (int bottom = 0; walls && bottom < 2; bottom++) {
        const tp_wall *w = bottom ? &walls->bottom : &walls->top;
        if (w->kind == TP_WALL_TEXTURE && ui_wall_set(bottom, preview_texels(bottom), w->width))
            console_log("preview: no texture for the %s screen's wallpaper", bottom ? "bottom" : "top");
    }
    if (state == PREVIEW_FAILED)
        sfx(SFX_ERROR);
    preview_since = shown_since = osGetTime();
    return state;
}

// ---------------------------------------------------------------- the settings (settings.h, tracks.h)

/* At start, before the first frame: the settings file read. A missing or damaged one gives the defaults, a track
 * the application is not built with the default track; no screen says anything about it. The start screen's logo
 * and pattern are asked for through settings_logo and settings_pattern from the first frame on. */
static void settings_start(void)
{
    int read = settings_load(SETTINGS_PATH, &prefs, TRACK_LIST[TRACK_DEFAULT].file);
    music_pick = prefs.music[0] ? track_find(prefs.music) : TRACK_COUNT;
    if (music_pick < 0) {
        music_pick = TRACK_DEFAULT;
        snprintf(prefs.music, sizeof prefs.music, "%s", TRACK_LIST[TRACK_DEFAULT].file);
    }
    look_pick = 0;
    for (int i = 0; i < LOOKS_HELD; i++)
        if (LOOKS[i] == prefs.design)
            look_pick = i;
    settings_apply(&prefs);
    console_log("settings: %s; look %s, colour %d, menu music %s, interface sounds %s, logo %d, pattern %d",
                read ? "read" : "defaults (no file, or not one this reads)", LOOK_DRAWN, settings_scheme(),
                music_pick < TRACK_COUNT ? TRACK_LIST[music_pick].file : "off", prefs.sounds ? "on" : "off",
                settings_logo(), settings_pattern());
}

/* Then, once the pack is read: sound starts as the settings say; the menu music only once the first frame shows
 * (start_black). */
static void sound_start(void)
{
    audio_log = console_log;
    audio_init();
    audio_sounds(prefs.sounds);
}

/* The start (ST-B), after the settings and the APT hook: nothing is drawn until the pack is read and the sound is
 * ready, then the start screen comes in once, out of black (start_black). When main() began and each step was
 * done, for the log. */
enum { START_MAIN, START_UI, START_PACK, START_SOUND, START_STEPS };
static u64 start_at[START_STEPS];
static int start_frames = -1;           /* frames drawn since start_up; -1: no start under way, no black */

static void start_up(void)
{
    load_pack();
    start_at[START_PACK] = osGetTime();
    sound_start();
    start_at[START_SOUND] = osGetTime();
    go_main(0);
    themes_start();
    start_frames = 0;
    last_frame = osGetTime();   /* the first frame's gap (motion_told) counts from here, not from 0 */
}

/* The black over both screens at the start, after ui_begin: whole on the first frame, then gone within
 * START_BLACK_MS, eased as a screen comes in. Its clock runs from the second frame, set one frame back, as
 * entered()'s: the first frame unpacks the scheme's atlas and the glass sheet, and that never eats the clearing. By
 * the second frame's ui_begin the first one is on the screens: the menu music begins and the start's times go to
 * the log, once. Afterwards nothing brings it back (a return from HOME or sleep, a job, a screen, a popup). */
static float start_black(void)
{
    static u64 since;
    if (start_frames < 0)
        return 0;
    u64 now = osGetTime();
    if (start_frames++ == 0)
        return 1;
    if (start_frames == 2) {
        since = now - FRAME_MS;
        audio_menu(music_pick < TRACK_COUNT ? music_pick : -1);
        console_log("start: ui_init %llu ms, pack %llu ms, sound %llu ms, first frame shown %llu ms",
                    (unsigned long long)(start_at[START_UI] - start_at[START_MAIN]),
                    (unsigned long long)(start_at[START_PACK] - start_at[START_MAIN]),
                    (unsigned long long)(start_at[START_SOUND] - start_at[START_MAIN]),
                    (unsigned long long)(now - start_at[START_MAIN]));
    }
    float black = 1 - ease_out(ease_span(now, since, START_BLACK_MS));
    if (black <= 0)
        start_frames = -1;
    return black;
}

/* The settings screen's rows, one table: what a row is called, how many choices it has, where the chosen one is
 * kept, how a choice is shown, and what a change does at once besides going to the card (NULL: nothing here; every
 * frame asks settings.h for the scheme, the glass and the pattern, tell_look). A further setting is one more entry
 * here, with its texts and, to be kept on the card, its field and key in settings.c; the screen holds five rows
 * (see below). */
typedef struct setting {
    int label;
    int count;
    int *value;
    const char *(*name)(int choice);
    void (*changed)(void);
} setting;

static const char *music_name(int choice)
{
    static char line[160];
    if (choice >= TRACK_COUNT)
        return text(lang, T_NO_MUSIC);
    snprintf(line, sizeof line, "%s, %s", TRACK_LIST[choice].title, TRACK_LIST[choice].author);
    return line;
}

static const char *switch_name(int choice)
{
    return text(lang, choice ? T_ON : T_OFF);
}

static const char *scheme_name(int choice)
{
    return text(lang, choice ? T_SCHEME_WHITE + choice : LOOK_FACTS[choice_look()].first_colour);
}

static const char *look_name(int choice)
{
    return text(lang, T_LOOK_STOCK + LOOKS[choice]);
}

static const char *pattern_name(int choice)
{
    return text(lang, T_PATTERN_PLUS + choice);
}

/* The track chosen plays at once from its start; "no music" ends it. */
static void music_changed(void)
{
    snprintf(prefs.music, sizeof prefs.music, "%s", music_pick < TRACK_COUNT ? TRACK_LIST[music_pick].file : "");
    audio_menu(music_pick < TRACK_COUNT ? music_pick : -1);
}

static void sounds_changed(void)
{
    audio_sounds(prefs.sounds);
}

/* The look chosen is drawn from the next frame on (tell_look), and the card names it. */
static void look_changed(void)
{
    prefs.design = LOOKS[look_pick];
}

static const setting SETTINGS[] = {
    {T_LOOK, LOOKS_HELD, &look_pick, look_name, look_changed},     /* first: the cursor stays when the rows change */
    {T_SCHEME, SETTINGS_SCHEMES, &prefs.scheme, scheme_name, NULL},
    {T_MENU_MUSIC, TRACK_COUNT + 1, &music_pick, music_name, music_changed},
    {T_SOUNDS, 2, &prefs.sounds, switch_name, sounds_changed},
    {T_PATTERN, SETTINGS_STYLES, &prefs.pattern, pattern_name, NULL},
};
#define SETTINGS_ALL ((int)(sizeof SETTINGS / sizeof *SETTINGS))
/* What the screen holds (ui_layout.h, SET_*): SET_SHOWN rows above the bar's shadow (more scroll), a row's choices on
 * the top screen's card. A track too many stops the build here instead of being drawn over the card's edge. */
_Static_assert(SET_Y + SET_SHOWN * (SET_ROW_H(SET_SHOWN) + SET_GAP) - SET_GAP <= BAR_Y - BAR_SHADOW,
               "the settings screen shows SET_SHOWN rows whole above the bar's shadow");
_Static_assert((TRACK_COUNT + 1) * SET_LIST_LH <= CARD_H && SETTINGS_STYLES * SET_LIST_LH <= CARD_H &&
               LOOK_COUNT * SET_LIST_LH <= CARD_H &&
               (SETTINGS_SCHEMES + 1) / 2 * SET_LIST_LH <= CARD_H,
               "the top screen's card holds no more choices (the schemes in two columns)");
_Static_assert(CARD_PAD + 2 * SET_LIST_LH + CREDIT_ROOM_MIN + (1 + TRACK_COUNT) * CREDIT_LH + CARD_PAD <= CARD_H,
               "below the sounds' two choices the card holds the interface sounds' credit and each track's");

/* Row i of a settings screen of rows rows at scroll 0 (layout.h, SET_ROW_H; more than SET_SHOWN rows are as high as
 * SET_SHOWN): its face and its touch area. */
static rect settings_row(int i, int rows)
{
    int h = SET_ROW_H(rows < SET_SHOWN ? rows : SET_SHOWN);
    return (rect){SET_X, SET_Y + i * (h + SET_GAP), SET_W, h};
}

/* The rows shown: the look's only in a build that holds more than one, the background's only in a look that draws
 * the pattern; how many, and the i-th of them. */
static int setting_shown(const setting *s)
{
    if (s->value == &look_pick)
        return LOOKS_HELD > 1;
    return s->value != &prefs.pattern || LOOK_FACTS[choice_look()].pattern;
}

static int rows_shown(void)
{
    int n = 0;
    for (int k = 0; k < SETTINGS_ALL; k++)
        n += setting_shown(&SETTINGS[k]);
    return n;
}

static const setting *setting_at(int i)
{
    for (int k = 0; k < SETTINGS_ALL; k++)
        if (setting_shown(&SETTINGS[k]) && i-- == 0)
            return &SETTINGS[k];
    return &SETTINGS[SETTINGS_ALL - 1];
}

/* The settings rows as a list (scroll.h): from SET_Y down to the lower edge of the SET_SHOWN-th row, so SET_SHOWN of
 * them show whole at every stop and more scroll by the row (a row and its gap). */
static scroll_shape settings_shape(void)
{
    int n = rows_shown();
    rect last = settings_row(SET_SHOWN - 1, n);
    return (scroll_shape){(size_t)n, 0, last.h, last.h, SET_GAP, SET_Y, last.y + last.h, 0, 0};
}

static rect settings_rect(int i)
{
    scroll_shape s = settings_shape();
    return (rect){SET_X, scroll_y(&s, &sv, (size_t)i), SET_W, scroll_row_h(&s, (size_t)i)};
}

static int settings_whole(int i)
{
    scroll_shape s = settings_shape();
    return scroll_whole(&s, &sv, (size_t)i);
}

/* Where a touch may drag the rows (scroll.h, as a list's): their width, from the screen's top edge (they scroll under
 * it) to the lower edge of the SET_SHOWN-th row at scroll 0. */
static rect settings_area(void)
{
    scroll_shape s = settings_shape();
    return (rect){SET_X, 0, SET_W, s.h};
}

/* The rows scroll at once as far as the cursor's row needs to show whole, and no further than the rows go (fewer rows
 * after a change of the look: back towards 0); a fling after a drag stops. */
static void settings_follow(void)
{
    scroll_shape s = settings_shape();
    scroll_area a = scroll_of(&s);
    sv.cursor = (size_t)focus;
    sv.scroll = scroll_clamp(&a, scroll_target(&s, &sv));
    sv.fling = 0;
}

static void open_settings(void)
{
    focus = 0;
    sv = (scroll_view){0};
    sv_loose = 0;
    screen = SCR_SETTINGS;
}

/* Row i goes on to its next choice (dir 1) or back to the one before (-1), round its end. What the change does
 * happens at once, and the file on the card follows every change. */
static void setting_step(int i, int dir)
{
    const setting *s = setting_at(i);
    *s->value = (*s->value + dir + s->count) % s->count;
    if (s->changed)
        s->changed();
    settings_apply(&prefs);
    int saved = !settings_save(SETTINGS_PATH, &prefs);
    console_log("settings: look %s, colour %d, menu music %s, interface sounds %s, logo %d, pattern %d%s",
                LOOK_DRAWN, settings_scheme(), music_pick < TRACK_COUNT ? TRACK_LIST[music_pick].file : "off",
                prefs.sounds ? "on" : "off",
                settings_logo(), settings_pattern(), saved ? "" : "; NOT saved (they hold until the application ends)");
}

/* The sound of a button's push: the bar's B and a question's "Abbrechen" go back, everything else confirms, also
 * the bar where it says OK. */
static int push_sound(int target)
{
    if (target == HIT_POPUP_LEFT)
        return SFX_BACK;
    if (target != HIT_LEFT || popup)
        return SFX_CONFIRM;
    return screen == SCR_DONE || (screen == SCR_PROBLEM && problem_ok) ? SFX_CONFIRM : SFX_BACK;
}

// ---------------------------------------------------------------- input

static int bar_halves(void)
{
    return (list_screen() && list_count()) || (screen == SCR_CONFIRM && can_install() && !music_choice()) ||
           screen == SCR_URL;
}

/* How many buttons the D-pad moves between, HIT_FIRST and on (0: none of this kind); B goes back (on Done: keeps the
 * file). */
static int tiles(void)
{
    if (screen == SCR_DESIGNS)
        return 3;
    if (screen == SCR_DESIGN)
        return 1;
    return screen == SCR_MAIN || (screen == SCR_DONE && may_clean) ||
           (screen == SCR_CONFIRM && (music_choice() || installed_already())) ? 2 : 0;
}

/* The row of list item i the stylus is on, else HIT_NONE. Below LIST_BOTTOM the rows show around the tabs (in the
 * gap between them and above their round corners) and take touches there too; target_at asks the tabs first. */
static int row_at(int x, int y)
{
    if (!ui_hit((rect){LIST_X, 0, LIST_W, SCREEN_H}, x, y))
        return HIT_NONE;
    for (size_t i = 0; i < list_count(); i++)
        if (ui_hit(row_rect(i), x, y))
            return HIT_ROW0 + (int)i;
    return HIT_NONE;
}

static int screen_target_at(int x, int y)
{
    switch (screen) {
    case SCR_MAIN:
        if (ui_hit(ui_settings_rect(), x, y))
            return HIT_CORNER;      /* the settings button, above the first tile (they do not meet since 0.9.2) */
        return ui_hit(TILE_ONE, x, y) ? HIT_FIRST : ui_hit(TILE_TWO, x, y) ? HIT_SECOND : HIT_NONE;
    case SCR_SETTINGS:
        if (ui_tab_hit(0, x, y))
            return HIT_NONE;        /* the tab over a row that scrolls under it (target_at: HIT_LEFT) */
        for (int i = 0; i < rows_shown(); i++)
            if (ui_hit(settings_rect(i), x, y))
                return HIT_ROW0 + i;
        return HIT_NONE;
    case SCR_DESIGNS:
        for (int k = 0; k < 3; k++)
            if (ui_hit(DESIGNS_TILES[k], x, y))
                return HIT_FIRST + k;
        return HIT_NONE;
    case SCR_DESIGN:
        return ui_hit(design_tile(), x, y) ? HIT_FIRST : HIT_NONE;
    case SCR_DONE:
        if (!may_clean)
            return HIT_NONE;
        if (ui_hit(ui_help_rect(), x, y))
            return HIT_HELP;
        return ui_hit(KEEP_TILE, x, y) ? HIT_FIRST : ui_hit(DELETE_TILE, x, y) ? HIT_SECOND : HIT_NONE;
    case SCR_CONFIRM:
        if (installed_already())
            return ui_hit(DELETE_THEME_TILE, x, y) ? HIT_FIRST : ui_hit(DELETE_FILE_TILE, x, y) ? HIT_SECOND : HIT_NONE;
        if (music_choice())
            return ui_hit(GO_UPPER, x, y) ? HIT_FIRST : ui_hit(GO_LOWER, x, y) ? HIT_SECOND : HIT_NONE;
        return can_install() && ui_hit(GO_BUTTON, x, y) ? HIT_GO : HIT_NONE;
    case SCR_URL:
        return ui_hit(GO_BUTTON, x, y) ? HIT_GO : HIT_NONE;
    case SCR_LIST:
        if (ui_hit(ui_corner_rect(scan_shown(folder)), x, y))
            return scan_is_root(folder) ? HIT_NONE : HIT_CORNER;   /* never the row under the button */
        if (ui_hit(SCROLL_AREA, x, y))
            return max_scroll() > 0 ? HIT_SCROLL : HIT_NONE;
        return row_at(x, y);
    case SCR_INSTALLED:
        if (ui_hit(SCROLL_AREA, x, y))
            return max_scroll() > 0 ? HIT_SCROLL : HIT_NONE;
        return row_at(x, y);
    default:
        return badges_screen() ? badges_target_at(x, y) : HIT_NONE;
    }
}

/* Two tabs first, as they are drawn; around them what the screen draws there (a list's rows, else nothing). With one
 * action the screen's own buttons first (the Done screen's tiles reach into the tab's place, which it leaves empty),
 * then the one tab as it is drawn, on the start screen the bar across the screen. */
static int target_at(int x, int y)
{
    if (can_preview())      /* B back and Y preview, whatever the screen's own footer was */
        return ui_tab_hit(0, x, y) ? HIT_LEFT : ui_tab_hit(1, x, y) ? HIT_PREVIEW : screen_target_at(x, y);
    if (bar_halves())
        return ui_tab_hit(0, x, y) ? HIT_LEFT : ui_tab_hit(1, x, y) ? HIT_RIGHT : screen_target_at(x, y);
    int hit = screen_target_at(x, y);
    if (hit != HIT_NONE || (screen == SCR_DONE && may_clean))
        return hit;
    if (screen == SCR_MAIN)
        return ui_hit(ui_bar_wide_rect(), x, y) ? HIT_LEFT : HIT_NONE;
    return ui_tab_hit(lone_ok(), x, y) ? HIT_LEFT : HIT_NONE;
}

/* The thumb follows the stylus: its middle under the touch. */
static void scroll_bar_to(int y)
{
    scroll_shape s = shape();
    scroll_thumb_to(&s, &lv, (float)y, TRACK.y, TRACK.h);
}

/* The folder views' corner button floats over the rows: a touch on it drags nothing. */
static int on_corner(int x, int y)
{
    if (screen == SCR_LIST)
        return ui_hit(ui_corner_rect(scan_shown(folder)), x, y);
    return screen == SCR_BADGE_LIST && ui_hit(ui_corner_rect(badges_shown(bdir)), x, y);
}

/* The push: target shows held for PRESS_MS (counted from since, the touch's start), then acts. */
static void push(int target, u64 since)
{
    pressed = pending = target;
    pending_at = since + PRESS_MS;
    sfx(push_sound(target));
}

/* What a tap, A, B or Y does once its push has shown. Returns 0 to leave the application. */
static int activate(int target)
{
    if (target == HIT_POPUP_LEFT) {
        int kind = popup;
        close_popup();
        if (kind == POP_ASK_FOLDER)
            popup_then = kind, popup_yes = 0;
        return 1;
    }
    if (target == HIT_POPUP) {  /* a question's "Löschen" or "Erstellen": the delete or the folder's job starts
                                   when the question has faded out (handle_input), not in its last frame, which
                                   would cut the fade off; the folder question's "Abbrechen" above likewise */
        int kind = popup;
        close_popup();
        if (kind == POP_ASK_THEME || kind == POP_ASK_FILE || kind == POP_ASK_FOLDER || kind == POP_ASK_SET)
            popup_then = kind, popup_yes = 1;
        return 1;
    }
    if (target == HIT_PREVIEW) {
        if (can_preview())
            open_preview();
        return 1;
    }
    if (badges_screen())
        return badges_activate(target);
    switch (screen) {
    case SCR_MAIN:
        if (target == HIT_LEFT)
            return 0;
        if (target == HIT_CORNER)
            open_settings();
        if (target == HIT_FIRST) {
            note[0] = 0;
            go_designs(0);
        } else if (target == HIT_SECOND) {
            badges_open(0);
        }
        break;
    case SCR_DESIGNS:
        if (target == HIT_LEFT || target == HIT_FIRST || target == HIT_SECOND || target == HIT_THIRD)
            note[0] = 0;
        if (target == HIT_LEFT) {
            go_main(0);
        } else if (target == HIT_FIRST) {
            focus = 0;
            open_list();
        } else if (target == HIT_SECOND) {
            focus = 1;
            start_scan();
        } else if (target == HIT_THIRD) {
            focus = 2;
            open_designs();
        }
        break;
    case SCR_INSTALLED:
        if (target == HIT_LEFT) {
            go_designs(2);
        } else if (target == HIT_RIGHT) {     /* as A: with nothing selected it only selects */
            long c = (long)lv.cursor;
            int open = select_next(&lv.selected, &c, SELECT_A, 0, first_shown(), (long)designs.count);
            lv.cursor = (size_t)c;
            lv.follow = 1;
            if (open)
                open_design(lv.cursor);
        } else if (target >= HIT_ROW0 && (size_t)(target - HIT_ROW0) < designs.count) {
            lv.cursor = (size_t)(target - HIT_ROW0);
            open_design(lv.cursor);
        }
        break;
    case SCR_DESIGN:
        if (target == HIT_LEFT) {
            back_to_designs();
        } else if (target == HIT_FIRST) {
            const design_row *d = shown_design();
            if (d)
                ask_delete_theme(d->index, SCR_DESIGN, d->file[0] != 0);
        }
        break;
    case SCR_LIST:
        if (target == HIT_LEFT)
            go_designs(0);
        else if (target == HIT_CORNER)
            folder_up();
        else if (target == HIT_RIGHT) {     /* as A: with nothing selected it only selects */
            long c = (long)lv.cursor;
            int open = select_next(&lv.selected, &c, SELECT_A, 0, first_shown(), (long)list.count);
            lv.cursor = (size_t)c;
            lv.follow = 1;
            if (open)
                open_item(lv.cursor);
        }
        else if (target >= HIT_ROW0 && (size_t)(target - HIT_ROW0) < list.count) {
            lv.cursor = (size_t)(target - HIT_ROW0);
            open_item(lv.cursor);
        }
        break;
    case SCR_CONFIRM:
        if (target == HIT_LEFT) {
            close_theme();
            if (confirm_from == SCR_LIST)
                back_to_list();
            else
                go_designs(1);
        } else if (installed_already()) {
            if (target == HIT_FIRST)
                ask_delete_theme(plan.duplicate, SCR_CONFIRM, 1);
            else if (target == HIT_SECOND)
                ask_delete_file();
        } else if (music_choice()) {
            if (target == HIT_FIRST)
                run_install();
            else if (target == HIT_SECOND)
                start_no_music();
        } else if ((target == HIT_GO || target == HIT_RIGHT) && can_install()) {
            run_install();
        }
        break;
    case SCR_DONE:
        if (may_clean && target == HIT_HELP) {
            open_help();
            break;
        }
        if (may_clean && target == HIT_SECOND)
            clean_up();
        if (target == HIT_FIRST || target == HIT_SECOND || target == HIT_LEFT)
            after_install();    // keeping the file is the default: A on Keep, B and the Keep tile all keep it
        break;
    case SCR_PROBLEM:
        if (target == HIT_LEFT && bqr)
            badges_open(1);
        else if (target == HIT_LEFT)
            go_designs(focus);
        break;
    case SCR_SCAN:
        if (target == HIT_LEFT && bqr)
            badges_open(1);
        else if (target == HIT_LEFT)
            go_designs(1);
        break;
    case SCR_URL:
        if (target == HIT_LEFT)
            start_scan();
        else if (target == HIT_GO || target == HIT_RIGHT)
            run_download();
        break;
    case SCR_PREVIEW:
        if (target == HIT_LEFT)
            close_preview();
        break;
    case SCR_SETTINGS:
        if (target == HIT_LEFT)
            go_main(0);
        else if (target >= HIT_ROW0 && target < HIT_ROW0 + rows_shown()) {
            focus = target - HIT_ROW0;
            if (settings_whole(focus))      /* a row cut at the edge is only scrolled in */
                setting_step(focus, 1);
            settings_follow();
        }
        break;
    }
    return 1;
}

/* The settings screen's keys: up and down through the rows; left and right change the row's choice at once, A (as
 * a tap) goes on to its next one; B goes back. The first press only shows the cursor, as everywhere, and brings its
 * row in whole (a drag may have taken it out). Called every frame on the screen: a fling after a drag goes on here,
 * as a list's in list_motion; the cursor's row is kept whole except while and after the stylus moves the rows, until
 * the next key. */
static int settings_input(u32 down, u64 now)
{
    scroll_shape s = settings_shape();
    scroll_frame(&s, &sv);
    int was = focus;
    if (!ring && (down & (KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT | KEY_A))) {
        ring = 1;
        sv_loose = 0;
        settings_follow();
        sfx(SFX_MOVE);
        return 1;
    }
    if (down & KEY_UP)
        focus = focus > 0 ? focus - 1 : 0;
    else if (down & KEY_DOWN)
        focus = focus < rows_shown() - 1 ? focus + 1 : focus;
    if (down & (KEY_LEFT | KEY_RIGHT))
        setting_step(focus, down & KEY_RIGHT ? 1 : -1);
    if (down & (KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT))
        sv_loose = 0;
    if (!sv_loose && !sv.dragging)
        settings_follow();
    if (focus != was || (down & (KEY_LEFT | KEY_RIGHT)))
        sfx(SFX_MOVE);          /* after the change: switching the sounds on is heard, switching them off is not */
    if (down & KEY_A)
        push(HIT_ROW0 + focus, now);
    else if (down & KEY_B)
        push(HIT_LEFT, now);
    return 1;
}

static void read_touch(void)
{
    touchPosition t;
    hidTouchRead(&t);
    touch_x = t.px;
    touch_y = t.py;
}

/* While a job runs: only the download listens, to B and to a tap on its tab. */
static void job_input(void)
{
    u32 down, held, up;
    keys_scan(&down, &held, &up);
    if (job != JOB_DOWNLOAD)
        return;
    u64 now = osGetTime();
    int cancel = 0;
    if (down & KEY_TOUCH) {
        read_touch();
        touch_down(&touch, ui_tab_hit(0, touch_x, touch_y) ? HIT_LEFT : HIT_NONE, 0, 0, touch_x, touch_y, now);
    } else if (held & KEY_TOUCH) {
        read_touch();
        touch_move(&touch, touch_x, touch_y, ui_tab_hit(0, touch_x, touch_y));
    } else if (up & KEY_TOUCH) {
        cancel = touch_up(&touch, ui_tab_hit(0, touch_x, touch_y)) == HIT_LEFT;           /* lifted on the tab */
    }
    if (cancel || (down & KEY_B)) {
        pressed = HIT_LEFT;
        job_cancel = 1;
    }
}

static int popup_asks(void)
{
    return popup == POP_ASK_THEME || popup == POP_ASK_FILE || popup == POP_ASK_FOLDER || popup == POP_ASK_SET;
}

/* The popup's button under the stylus: the one across, or Cancel (left) and Delete (right). */
static int popup_target(int x, int y)
{
    if (!popup_asks())
        return ui_hit(ui_popup_button(0, 0), x, y) ? HIT_POPUP : HIT_NONE;
    return ui_hit(ui_popup_button(1, 0), x, y) ? HIT_POPUP_LEFT : ui_hit(ui_popup_button(1, 1), x, y) ? HIT_POPUP
                                                                                                         : HIT_NONE;
}

/* While the popup is open only it listens (popup.h): a tap on a button, once the guard lets the touch through; B
 * cancels or closes; in a question A presses the button under the cursor, which starts on "Abbrechen", and its keys
 * too wait for the guard time. */
static int popup_input(u32 down, u32 held, u32 up, u64 now)
{
    if (down & KEY_TOUCH) {
        read_touch();
        touch_since = now;
        pop_focus.ring = 0;     /* a touch hides the cursor, as everywhere */
        touch_down(&touch, guard_takes(&guard, now) ? popup_target(touch_x, touch_y) : HIT_NONE, 0, 0, touch_x,
                   touch_y, now);
    } else if (held & KEY_TOUCH) {
        read_touch();
        touch_move(&touch, touch_x, touch_y, touch.target >= 0 && popup_target(touch_x, touch_y) == touch.target);
    } else if (up & KEY_TOUCH) {
        guard_lift(&guard);
        int target = touch_up(&touch, touch.target >= 0 && popup_target(touch_x, touch_y) == touch.target);
        if (target != HIT_NONE) {
            push(target, touch_since);
            return 1;
        }
    }
    if (popup_asks() && (popup_fresh || now < guard.from + POPUP_GUARD_MS))
        return 1;               /* no key answers before a touch may: a press while it opens is not kept */
    int key = down & KEY_B ? POPUP_KEY_B : down & KEY_A ? POPUP_KEY_A : down & (KEY_LEFT | KEY_UP) ? POPUP_KEY_LEFT
              : down & (KEY_RIGHT | KEY_DOWN) ? POPUP_KEY_RIGHT : -1;
    int which = key < 0 ? -1 : popup_key(&pop_focus, popup_asks(), key);
    if (key >= 0 && which < 0)
        sfx(SFX_MOVE);          /* the cursor showed or moved */
    if (which >= 0)
        push(popup_asks() && which == 0 ? HIT_POPUP_LEFT : HIT_POPUP, now);
    return 1;
}

/* The keys of every list. With nothing selected the first move or A selects the first row shown whole (L and R page
 * first) and does nothing else; then the D-pad and the circle pad move the selection a row, left and L, right and R
 * a page; A opens the row, Y goes up a folder where the corner button can, B goes back. */
static int list_keys(u32 down, u32 repeat, u64 now)
{
    scroll_shape s = shape();
    float page = s.h - s.inset;
    long rows = (long)(page / (s.row_h + s.gap));
    if (s.count && !lv.selected && (down & (KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT | KEY_L | KEY_R | KEY_A))) {
        if (down & (KEY_L | KEY_R)) {
            lv.scroll = clamp_scroll(lv.scroll + page * (down & KEY_L ? -1 : 1));
            lv.fling = 0;
        }
        list_move(0);
        return 1;
    }
    if (repeat & KEY_UP)
        list_move(-1);
    if (repeat & KEY_DOWN)
        list_move(1);
    if (repeat & (KEY_LEFT | KEY_L))
        list_move(-rows);
    if (repeat & (KEY_RIGHT | KEY_R))
        list_move(rows);
    if ((down & KEY_A) && s.count) {
        push(HIT_ROW0 + (int)lv.cursor, now);
        return 1;
    }
    int can_up = screen == SCR_LIST ? !scan_is_root(folder) : screen == SCR_BADGE_LIST && !scan_is_root(bdir);
    if ((down & KEY_Y) && can_up) {
        push(HIT_CORNER, now);
        return 1;
    }
    if (down & KEY_B)
        push(HIT_LEFT, now);
    return 1;
}

static int handle_input(void)
{
    u32 down, held, up;
    keys_scan(&down, &held, &up);
    u32 repeat = hidKeysDownRepeat() & ~keys_stale;
    u64 now = osGetTime();
    if (pending != HIT_NONE) {
        if (down & KEY_TOUCH)               /* a second touch while an action is under way presses nothing */
            touch_down(&touch, HIT_NONE, 0, 1, touch_x, touch_y, now);
        if (up & KEY_TOUCH) {
            lv.dragging = lv.armed = thumb_held = 0;   /* the stylus left during the push: no drag goes on after it */
            sv.dragging = sv.armed = 0;
            guard_lift(&guard);
            guard_lift(&screen_guard);
        }
        if (now < pending_at)
            return 1;
        int target = pending;
        pending = pressed = HIT_NONE;
        return activate(target);
    }
    if (screen == SCR_PREVIEW && preview_step() == PREVIEW_LOADING)
        return 1;               /* nothing listens while a preview is read */
    if (popup)
        return popup_input(down, held, up, now);
    if (popup_then != POP_NONE) {       /* the question fades out; nothing else is taken until its answer is carried out */
        if (up & KEY_TOUCH)
            guard_lift(&screen_guard);
        if (now - popup_gone_at < POPUP_OUT_MS)
            return 1;
        int kind = popup_then;
        popup_then = POP_NONE;
        if (kind == POP_ASK_THEME)
            run_delete_theme();
        else if (kind == POP_ASK_FILE)
            run_delete_file();
        else if (kind == POP_ASK_SET)
            badges_remove();
        else
            answer_themes(popup_yes);
        return 1;
    }
    if ((down & KEY_TOUCH) && !guard_takes(&screen_guard, now)) {
        read_touch();           /* came down too soon after a popup closed: it presses, drags and selects nothing */
        touch_down(&touch, HIT_NONE, 0, 1, touch_x, touch_y, now);
        lv.dragging = lv.armed = sv.dragging = sv.armed = thumb_held = 0;
    } else if (down & KEY_TOUCH) {
        read_touch();
        ring = 0;
        int hit = target_at(touch_x, touch_y), rows = rows_screen(), set = screen == SCR_SETTINGS;
        if (rows && (ui_hit(LIST_AREA, touch_x, touch_y) || ui_hit(SCROLL_AREA, touch_x, touch_y) || hit >= HIT_ROW0))
            lv.selected = 0;               /* a touch on the list or the scroll bar: no row is selected */
        touch_since = now;
        /* the settings rows are one more list for the touch, on their own view sv (no bar, no corner button) */
        int in_list = (rows && (hit == HIT_CORNER || hit >= HIT_ROW0)) || (set && hit >= HIT_ROW0);
        int moving = (rows && scroll_moving(&lv)) || (set && scroll_moving(&sv));  /* still gliding: stopped, no press */
        thumb_held = hit == HIT_SCROLL;
        touch_down(&touch, thumb_held ? HIT_NONE : hit, in_list, moving && in_list, touch_x, touch_y, now);
        if (hit >= HIT_FIRST && hit < HIT_FIRST + tiles())
            focus = hit - HIT_FIRST;
        scroll_shape s = shape();
        scroll_grab(&s, &lv, touch_y, rows && ui_hit(LIST_AREA, touch_x, touch_y) && !on_corner(touch_x, touch_y));
        if (set) {
            scroll_shape ss = settings_shape();
            scroll_grab(&ss, &sv, touch_y, ui_hit(settings_area(), touch_x, touch_y) && hit != HIT_LEFT);
        }
        if (thumb_held)
            scroll_bar_to(touch_y);
    } else if (held & KEY_TOUCH) {
        read_touch();
        touch_move(&touch, touch_x, touch_y, touch.target >= 0 && target_at(touch_x, touch_y) == touch.target);
        scroll_shape s = shape();
        if (rows_screen() && thumb_held)
            scroll_bar_to(touch_y);
        else if (rows_screen())
            scroll_drag(&s, &lv, touch_y);
        else if (screen == SCR_SETTINGS) {
            scroll_shape ss = settings_shape();
            scroll_drag(&ss, &sv, touch_y);
        }
    } else if (up & KEY_TOUCH) {
        guard_lift(&screen_guard);
        int target = touch_up(&touch, touch.target >= 0 && target_at(touch_x, touch_y) == touch.target);
        thumb_held = 0;
        int was_drag = scroll_release(&lv);
        if (screen == SCR_SETTINGS && scroll_release(&sv))
            was_drag = sv_loose = 1;
        if (target != HIT_NONE && !was_drag) {
            push(target, touch_since);
            return 1;
        }
    }

    if (screen == SCR_MAIN && (down & KEY_START))
        return 0;
    if (screen == SCR_DONE && may_clean && (down & KEY_X)) {
        push(HIT_HELP, now);
        return 1;
    }
    if (can_preview() && (down & KEY_Y)) {
        push(HIT_PREVIEW, now);
        return 1;
    }
    if (screen == SCR_MAIN && (down & KEY_Y)) {     /* Y is the corner button's key, as in the folder view */
        push(HIT_CORNER, now);
        return 1;
    }
    if (screen == SCR_SETTINGS)
        return settings_input(down, now);
    if (rows_screen())
        return list_keys(down, repeat, now);
    if (badges_screen())
        return badges_input(down, now);
    /* The ring is hidden until the D-pad or the circle pad is used. Its first press, and an A press while it
     * is hidden, only show it on the current item: nothing unseen is moved or started. */
    u32 dirs = KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT;
    int n = tiles();
    int has_item = n || (screen == SCR_CONFIRM && can_install()) || screen == SCR_URL;
    if (has_item && !ring && (down & (dirs | KEY_A))) {
        ring = 1;
        sfx(SFX_MOVE);
        return 1;
    }
    int focus_was = focus;
    if (n) {
        if (n == 3) {           /* two tiles over the wide one: left and right above, down to it, up to where from */
            if (focus < 2 && (down & KEY_LEFT))
                focus = 0;
            if (focus < 2 && (down & KEY_RIGHT))
                focus = 1;
            if (focus < 2 && (down & KEY_DOWN)) {
                designs_above = focus;
                focus = 2;
            } else if (focus == 2 && (down & KEY_UP)) {
                focus = designs_above;
            }
        } else {
            if (down & (KEY_LEFT | KEY_UP))
                focus = 0;
            if (down & (KEY_RIGHT | KEY_DOWN))
                focus = n - 1;
        }
        if (focus != focus_was)
            sfx(SFX_MOVE);
        if (down & KEY_A)
            push(HIT_FIRST + focus, now);
        else if ((down & KEY_B) && screen != SCR_MAIN)
            push(screen == SCR_DONE ? HIT_FIRST : HIT_LEFT, now);
        return 1;
    }
    switch (screen) {
    case SCR_CONFIRM:
        if ((down & KEY_A) && can_install()) {
            push(HIT_GO, now);
            return 1;
        }
        break;
    case SCR_URL:
        if (down & KEY_A) {
            push(HIT_GO, now);
            return 1;
        }
        break;
    case SCR_PROBLEM:
    case SCR_DONE:
        if (down & KEY_A) {
            push(HIT_LEFT, now);
            return 1;
        }
        break;
    default:
        break;
    }
    if (down & KEY_B)
        push(HIT_LEFT, now);
    return 1;
}

// ---------------------------------------------------------------- drawing
// Each screen in two parts, its top and its bottom screen: while a job runs both go on showing the screen it started
// from, until the loader takes the bottom screen's place (draw, job_loader); the download has its own.

/* The selection's corners over the button the cursor is on, at[focus]: drawn after all of the screen's buttons (and
 * before its bar), so a neighbour drawn later never covers them where they reach past their button. */
static void draw_cursor(const rect *at, int count, int radius)
{
    if (ring && focus >= 0 && focus < count)
        ui_select(at[focus], radius);
}

static void main_top(void)
{
    char status[160];
    int house = status_line(status, sizeof status);
    ui_start();
    ui_foot(HERO_FOOT_DOTS_Y, house, status, MUTED, 0, version);
}

static void main_bottom(void)
{
    ui_tile(FACE_TILE, TILE_ONE, tile_state(HIT_FIRST), in_glass(ART_DESIGNS), text(lang, T_DESIGNS), MAIN_PX, MAIN_LH,
            TILE_GAP, TILE_INK);
    int badges = in_glass(ART_BADGES);
    ui_tile(FACE_TILE, TILE_TWO, tile_state(HIT_SECOND) | (badges == ART_BADGES_SHARD ? UI_AMBER : 0), badges,
            text(lang, T_BADGES), MAIN_PX, MAIN_LH, TILE_GAP, TILE_INK);
    ui_settings_button(is_pressed(HIT_CORNER));     /* before the cursor, whose corner on the first tile reaches over it */
    draw_cursor((const rect[]){TILE_ONE, TILE_TWO}, 2, TILE_R);
    ui_bar_wide(UI_KEY_START, text(lang, T_EXIT), is_pressed(HIT_LEFT));
}

static void designs_top(void)
{
    char status[160];
    int house = status_line(status, sizeof status);
    ui_hero(in_glass(ART_DESIGNS), text(lang, T_DESIGNS), text(lang, T_HOME_MENU), text(lang, T_CHANGE_THEME));
    if (note[0])
        ui_foot(HERO_FOOT_DOTS_Y, -1, note, note_warn ? WARN : MUTED, 0, NULL);
    else
        ui_foot(HERO_FOOT_DOTS_Y, house, status, MUTED, 0, NULL);
}

static void designs_bottom(void)
{
    ui_tile(FACE_DTILE, DESIGNS_TILES[0], tile_state(HIT_FIRST), ART_SD, text(lang, T_FROM_SD), DESIGNS_PX,
            DESIGNS_LH, DESIGNS_GAP, TILE_INK);
    ui_tile(FACE_DTILE, DESIGNS_TILES[1], tile_state(HIT_SECOND), ART_QR, text(lang, T_SCAN_QR), DESIGNS_PX,
            DESIGNS_LH, DESIGNS_GAP, TILE_INK);
    ui_wide(FACE_WIDE, DESIGNS_TILES[2], tile_state(HIT_THIRD), ART_INSTALLED, text(lang, T_INSTALLED_DESIGNS),
            DESIGNS_PX, DESIGNS_GAP, TILE_INK);
    draw_cursor(DESIGNS_TILES, 3, TILE_R);
    ui_bar(UI_KEY_B, text(lang, T_BACK), is_pressed(HIT_LEFT));
}

/* The picture of list item i for the top screen's gallery: its icon, the folder, or the warning when unreadable. */
static ui_pic pic_of(size_t i)
{
    if (i >= list.count)
        return (ui_pic){NULL, -1};
    const scan_item *it = &list.items[i];
    if (it->folder)
        return (ui_pic){NULL, ART_FOLDER_HERO};
    if (it->result != TP_OK)
        return (ui_pic){NULL, ART_WARN};
    return (ui_pic){it->icon, -1};
}

static void draw_scrollbar(void)
{
    scroll_shape s = shape();
    float at, size;
    scroll_thumb(&s, &lv, &at, &size);
    if (max_scroll() > 0)
        ui_scrollbar(TRACK, at, size, thumb_held);
}

/* The rows are drawn by pixels from the screen's top edge to its lower edge: the corner button and the bar, with its
 * shadow, are drawn over them, so rows show around the bar's round corners. */
static int row_drawn(size_t i)
{
    rect r = row_rect(i);
    return r.y + r.h + FACE_PAD_LOW >= 0 && r.y <= SCREEN_H;
}

/* The selected row's corners, once all rows are drawn (the next row would cover its lower ones) and before the
 * corner button, the scroll bar and the bar, which cover them as they cover the rows. */
static void draw_row_cursor(void)
{
    if (lv.selected && lv.cursor < list_count() && row_drawn(lv.cursor))
        ui_select(row_rect(lv.cursor), ROW_R);
}

static void list_top(void)
{
    char pos[48];
    snprintf(pos, sizeof pos, "%u / %u%s", (unsigned)lv.cursor + 1, (unsigned)list.count, list.more ? "+" : "");
    ui_head(text(lang, T_SD_CARD), list.count && lv.selected ? pos : NULL);
    /* the footer is the status line in every state, from the first frame (the path is in the corner button, and
     * a row shows itself whether it is installed) */
    char status[160];
    int house = status_line(status, sizeof status);
    ui_foot(FOOT_DOTS_Y, house, status, MUTED, 0, NULL);
    if (!list.count) {
        ui_detail(NULL, ART_FOLDER_BIG, folder_name(list_dir), NULL, INK);
        return;
    }
    if (!lv.selected) {
        ui_gallery_empty();
        return;
    }
    const scan_item *it = &list.items[lv.cursor];
    if (!it->folder && it->result != TP_OK) {
        ui_detail(NULL, ART_WARN, it->name, text_result(lang, it->result), WARN);
        return;
    }
    ui_swarm(pic_of, list.count, lv.cursor, list.items, it->folder ? it->file : it->name, !it->folder && it->has_bgm);
}

static void list_bottom(void)
{
    for (size_t i = 0; i < list.count; i++) {
        if (!row_drawn(i))
            continue;
        rect r = row_rect(i);
        const scan_item *it = &list.items[i];
        int state = is_pressed(HIT_ROW0 + (int)i) ? UI_DOWN : 0;
        if (it->folder) {
            ui_row_folder(r, state, it->file);
            continue;
        }
        char sub[96] = "";
        uint32_t color = MUTED;
        int installed = 0, music = 0;
        if (it->result != TP_OK) {
            snprintf(sub, sizeof sub, "%s", text(lang, T_UNREADABLE));
            color = WARN;
        } else if (it->in_pack && it->in_pack_usable) {
            installed = 1;
            snprintf(sub, sizeof sub, "%s", text(lang, T_INSTALLED_ALREADY));
        } else {
            music = it->has_bgm;
            snprintf(sub, sizeof sub, "%s", music ? text(lang, T_MUSIC) : "");
        }
        ui_row_theme(r, state, it->result == TP_OK ? it->icon : NULL, it->name, sub, music, color, installed);
    }
    draw_row_cursor();
    ui_corner(scan_shown(list_dir), !scan_is_root(list_dir), is_pressed(HIT_CORNER));
    draw_scrollbar();
    if (!list.count)
        ui_bar(UI_KEY_B, text(lang, T_BACK), is_pressed(HIT_LEFT));
    else
        ui_bar2(text(lang, T_BACK), text(lang, lv.selected && list.items[lv.cursor].folder ? T_OPEN : T_SELECT),
                bar_down());
    /* the empty folder's note in the information box (FB-E), after the corner and the bar */
    if (!list.count) {
        char other[24], line[SCAN_PATH + SCAN_NAME + 80];
        snprintf(other, sizeof other, "%u%s", (unsigned)list.other, list.more ? "+" : "");
        snprintf(line, sizeof line, "%s", text(lang, T_EMPTY_FOLDER));
        if (list.other) {
            size_t n = strlen(line);
            snprintf(line + n, sizeof line - n, ".\n");
            n = strlen(line);
            snprintf(line + n, sizeof line - n, text(lang, T_OTHER_FILES), other);
        }
        ui_info_box(folder_name(list_dir), line, NULL, LIST_INSET, LIST_BOTTOM);
    }
}

/* The footer of the screens a preview starts from: B back alone, or with a preview on offer B back and Y preview. */
static void detail_bar(void)
{
    if (can_preview())
        ui_bar_y(text(lang, T_BACK), text(lang, T_PREVIEW), is_pressed(HIT_LEFT) ? 0 : is_pressed(HIT_PREVIEW) ? 1 : -1);
    else
        ui_bar(UI_KEY_B, text(lang, T_BACK), is_pressed(HIT_LEFT));
}

static ui_pic design_pic(size_t i)
{
    return i < designs.count ? (ui_pic){designs.rows[i].theme->icon, -1} : (ui_pic){NULL, -1};
}

static int design_music(const design_row *d)
{
    return d->music == MUSIC_YES;
}

/* The footer of the installed designs: the status line; and only when the pack can take fewer than
 * DESIGNS_ROOM_LOW more, how many, as a warning (a full pack's status line says so itself). Deleted designs keep
 * their place in the pack, so the room does not grow with a delete. */
static void designs_foot(void)
{
    char status[160], room[64];
    int house = status_line(status, sizeof status);
    unsigned low = designs_room_low(&pv);
    ui_foot(FOOT_DOTS_Y, house, status, MUTED, 0, NULL);
    if (low) {
        snprintf(room, sizeof room, text(lang, T_ROOM), low);
        ui_foot_warn(room);
    }
}

static void installed_top(void)
{
    char pos[48];
    snprintf(pos, sizeof pos, "%u / %u", (unsigned)lv.cursor + 1, (unsigned)designs.count);
    ui_head(text(lang, T_INSTALLED_DESIGNS), designs.count && lv.selected ? pos : NULL);
    designs_foot();
    if (!designs.count || !lv.selected)
        ui_gallery_empty();
    else
        ui_swarm(design_pic, designs.count, lv.cursor, designs.rows, designs.rows[lv.cursor].theme->name,
                 design_music(&designs.rows[lv.cursor]));
}

static void installed_bottom(void)
{
    for (size_t i = 0; i < designs.count; i++) {
        if (!row_drawn(i))
            continue;
        const design_row *d = &designs.rows[i];
        int state = is_pressed(HIT_ROW0 + (int)i) ? UI_DOWN : 0;
        int music = design_music(d);
        ui_row_theme(row_rect(i), state, d->theme->icon, d->theme->name, music ? text(lang, T_MUSIC) : "", music,
                     MUTED, 0);
    }
    draw_row_cursor();
    draw_scrollbar();
    if (!designs.count)
        ui_bar(UI_KEY_B, text(lang, T_BACK), is_pressed(HIT_LEFT));
    else
        ui_bar2(text(lang, T_BACK), text(lang, T_SELECT), bar_down());
    /* the empty list's note in the information box (FB-E), after the bar */
    if (!designs.count)
        ui_info_box(text(lang, T_INSTALLED_DESIGNS), text(lang, T_NO_THEMES_INSTALLED), NULL, 0,
                    LIST_BOTTOM);
}

/* One installed design: where HOME lists it, in the box of facts, and "Design löschen" (no file name: the pack's
 * design is the subject). */
static void design_top(void)
{
    const design_row *d = shown_design();
    ui_head(text(lang, T_INSTALLED_DESIGNS), NULL);
    ui_pic none = {NULL, -1};
    if (d)
        ui_gallery((ui_pic){d->theme->icon, -1}, none, none, d->theme->name, design_music(d));
    designs_foot();
}

static void design_bottom(void)
{
    char line[160];
    const design_row *d = shown_design();
    if (d) {
        char path[SCAN_PATH + SCAN_NAME];
        rect tile = design_tile();
        snprintf(line, sizeof line, text(lang, T_DESIGN_IN_HOME), (unsigned)d->index);
        if (d->file[0]) {
            file_shown(path, sizeof path, THEMES_DIR, d->file);
            ui_facts((const ui_fact[]){{ART_FACT_HOME, line, 0}, {ART_FACT_FILE, path, 1}}, 2, ACT_FACTS_Y);
        } else {
            ui_facts((const ui_fact[]){{ART_FACT_HOME, line, 0}}, 1, DESIGN_FACTS_Y);
        }
        ui_tile(FACE_CHOICE, tile, tile_state(HIT_FIRST), ART_DELETE_THEME, text(lang, T_DELETE_THEME), CHOICE_PX,
                CHOICE_LH, CHOICE_GAP, TILE_INK);
        draw_cursor(&tile, 1, TILE_R);
    }
    detail_bar();
}

static void confirm_top(void)
{
    char line[160];
    ui_head(text(lang, T_INSTALL_THEME), NULL);
    if (theme_result != TP_OK) {
        ui_detail(NULL, ART_WARN, chosen, NULL, INK);
        ui_foot(FOOT_DOTS_Y, -1, text(lang, T_CANNOT_INSTALL), WARN, 0, NULL);
        return;
    }
    ui_pic none = {NULL, -1};
    ui_gallery((ui_pic){theme.icon, -1}, none, none, theme.name, theme.has_bgm);
    if (plan.state == PLAN_OK || plan.state == PLAN_DUPLICATE)
        ui_foot(FOOT_DOTS_Y, status_line(line, sizeof line), line, MUTED, 0, NULL);
    else
        ui_foot(FOOT_DOTS_Y, -1, text(lang, T_CANNOT_INSTALL), WARN, 0, NULL);
}

/* The check screen's two buttons for a theme with music; also while its music is left out (the theme changes on the
 * worker then, so nothing of it is read here). */
static void music_choice_bottom(void)
{
    ui_go(GO_UPPER, tile_state(HIT_FIRST), ART_DOWNLOAD, text(lang, T_INSTALL));
    ui_alt(GO_LOWER, tile_state(HIT_SECOND), ART_NO_MUSIC, text(lang, T_INSTALL_NO_MUSIC));
    draw_cursor((const rect[]){GO_UPPER, GO_LOWER}, 2, GO_R);
    detail_bar();
}

static void confirm_bottom(void)
{
    char line[160], path[SCAN_PATH + SCAN_NAME];
    if (installed_already()) {
        snprintf(line, sizeof line, text(lang, T_INSTALLED_AS), (unsigned)plan.duplicate);
        file_shown(path, sizeof path, chosen_dir, chosen);
        ui_facts((const ui_fact[]){{ART_FACT_CHECK, line, 0}, {ART_FACT_FILE, path, 1}}, 2, ACT_FACTS_Y);
        ui_tile(FACE_CHOICE, DELETE_THEME_TILE, tile_state(HIT_FIRST), ART_DELETE_THEME, text(lang, T_DELETE_THEME),
                CHOICE_PX, CHOICE_LH, CHOICE_GAP, TILE_INK);
        ui_tile(FACE_CHOICE, DELETE_FILE_TILE, tile_state(HIT_SECOND), ART_DELETE_FILE, text(lang, T_DELETE_FILE),
                CHOICE_PX, CHOICE_LH, CHOICE_GAP, TILE_INK);
        draw_cursor((const rect[]){DELETE_THEME_TILE, DELETE_FILE_TILE}, 2, TILE_R);
        detail_bar();
        return;
    }
    if (music_choice()) {
        music_choice_bottom();
        return;
    }
    if (can_install()) {
        ui_go(GO_BUTTON, tile_state(HIT_GO), ART_DOWNLOAD, text(lang, T_INSTALL));
        int warn = theme.warnings & TP_WARN_BGM_UNUSED ? T_WARN_BGM_UNUSED
                   : theme.warnings & TP_WARN_BGM_MISSING ? T_WARN_BGM_MISSING : -1;
        if (warn >= 0) {
            ui_wrap(&block_a, text(lang, warn), BOTTOM_W - 2 * BODY_PAD, BODY_PX, BODY_MIN_PX, 0,
                    BAR_Y - GO_NOTE_Y - 4, BODY_PX * BODY_LH_PCT / 100.0f);
            ui_block_draw(&block_a, BOTTOM_W / 2.0f, GO_NOTE_Y, WARN, UI_CENTER);
        }
        if (ring)
            ui_select(GO_BUTTON, GO_R);
        detail_bar();           /* was B back and A install: A and the green button install as before */
        return;
    }
    char reason[600];
    if (theme_result != TP_OK)
        snprintf(reason, sizeof reason, "%s", text_result(lang, theme_result));
    else
        text_plan(lang, &plan, reason, sizeof reason);
    detail_bar();               /* the reason after the bar, in the information box (FB-E) */
    ui_info_box(text(lang, T_CANNOT_INSTALL), reason, NULL, 0, 0);
}

static void done_top(void)
{
    if (done_end) {             /* right after an install: the installing screen's end goes on (draw) */
        install_top();
        return;
    }
    char status[160];
    int house = status_line(status, sizeof status);
    ui_head(text(lang, T_INSTALLED), NULL);
    ui_done_card(done_icon, done_name, text(lang, T_HOME_MENU), text(lang, T_CHANGE_THEME));
    ui_foot(DONE_DOTS_Y, house, status, MUTED, 0, NULL);
}

static void done_bottom(void)
{
    if (!may_clean) {
        ui_bar_right(UI_KEY_A, text(lang, T_OK), is_pressed(HIT_LEFT));
        ui_way_pill(text(lang, T_HOME_MENU), text(lang, T_CHANGE_THEME));
        return;
    }
    char path[SCAN_PATH + SCAN_NAME];
    file_shown(path, sizeof path, clean_dir, clean_name);
    ui_facts((const ui_fact[]){{ART_FACT_FILE, path, 1}}, 1, ui_help_low() + DONE_FACTS_AIR);
    /* the question after the card: no letters directly on the ground, which a look may draw from a texture (kit.h) */
    ui_text_fit(text(lang, T_DELETE_ASK), BOTTOM_W / 2.0f, ui_baseline(ASK_Y, ASK_LH, ASK_PX),
                BOTTOM_W - 2 * (BOTTOM_W - HELP_X + 4), ASK_PX, INK, UI_CENTER, 0);
    ui_tile(FACE_CHOICE, KEEP_TILE, tile_state(HIT_FIRST), ART_KEEP, text(lang, T_KEEP), CHOICE_PX, CHOICE_LH,
            CHOICE_GAP, TILE_INK);
    ui_tile(FACE_CHOICE, DELETE_TILE, tile_state(HIT_SECOND), ART_TRASH, text(lang, T_DELETE), CHOICE_PX, CHOICE_LH,
            CHOICE_GAP, TILE_INK);
    draw_cursor((const rect[]){KEEP_TILE, DELETE_TILE}, 2, TILE_R);
    ui_help(is_pressed(HIT_HELP));
}

/* Over whatever screen is drawn (not while a job runs): the help, a note, or a question before a delete. It grows
 * and fades in by the clock; the one that just closed fades out the same way back, taking no input (the screen
 * under it has it again, behind its guard). */
static void draw_popup(void)
{
    u64 now = osGetTime();
    int kind = popup, held = -1, ring_at = -1;
    float shown;
    if (kind == POP_NONE) {
        if (popup_gone == POP_NONE || now - popup_gone_at >= POPUP_OUT_MS) {
            popup_gone = POP_NONE;
            return;
        }
        kind = popup_gone;
        shown = 1.0f - ease_span(now, popup_gone_at, POPUP_OUT_MS);
    } else {
        if (popup_fresh)
            popup_since = now - FRAME_MS;       /* its first frame is one step in, not an empty one */
        popup_fresh = 0;
        guard_from(&guard, now);
        shown = popup_opened(now);
        held = is_pressed(HIT_POPUP_LEFT) ? 0 : is_pressed(HIT_POPUP) ? 1 : -1;
        ring_at = popup_asks() && pop_focus.ring ? pop_focus.at : -1;
    }
    int asks = kind == POP_ASK_THEME || kind == POP_ASK_FILE || kind == POP_ASK_FOLDER || kind == POP_ASK_SET;
    ui_popup_tone(asks && kind != POP_ASK_FOLDER);
    ui_popup_sign(kind == POP_NOTE ? note_sign : -1);
    ui_popup_draw(&pop, asks ? text(lang, T_CANCEL) : NULL,
                  kind == POP_ASK_SET ? badge_text(lang, BT_REMOVE)
                                      : text(lang, kind == POP_ASK_FOLDER ? T_CREATE : asks ? T_DELETE : T_OKAY),
                  held, ring_at, shown);
}

static void problem_top(void)
{
    ui_head(text(lang, problem_head), NULL);
    ui_detail(NULL, ART_WARN, title, NULL, INK);
    ui_foot(FOOT_DOTS_Y, -1, NULL, MUTED, 0, version);
}

static void problem_bottom(void)
{
    if (problem_ok)
        ui_bar_right(UI_KEY_A, text(lang, T_OK), is_pressed(HIT_LEFT));
    else
        ui_bar(UI_KEY_B, text(lang, T_BACK), is_pressed(HIT_LEFT));
    /* the text after the bar, in the information box (FB-E); a problem without one says to try again */
    ui_info_box(title, body[0] ? body : text(lang, T_TRY_AGAIN), NULL, 0, 0);
}

static void scan_top(void)
{
    ui_camera(camera_image);
    ui_brackets();
}

static void scan_bottom(void)
{
    ui_wrap(&block_a, text(lang, T_SCAN_HINT), BODY.w, SCAN_PX, SCAN_PX - 3, 2, 0, SCAN_LH);
    float h = ui_block_height(&block_a), box = 0, top;
    char why[300];
    if (scan_note[0]) {         /* the hint and the message's box under it, without the QR sign (FB-E): the box's own
                                 * sign stands for it, and all three do not fit above the bar */
        float room = BAR_Y - BAR_SHADOW - 2 * INFO_AIR;
        text_download(lang, DL_BAD_URL, 0, 0, NULL, why, sizeof why);
        box = ui_info_height(text(lang, T_SCAN_QR), why, scan_note, room - h - SCAN_GAP);
        top = INFO_AIR + roundf((room - h - SCAN_GAP - box) / 2);
    } else {
        top = roundf((BAR_Y - h - SCAN_ICON - SCAN_GAP) / 2);
        ui_sprite(ART_QR_SCAN, (BOTTOM_W - SCAN_ICON) / 2, top);
        top += SCAN_ICON + SCAN_GAP;
    }
    if (scan_note[0]) {         /* the box first: its sign is the atlas's, so the hint's letters follow it (R7) */
        float under = top + ui_block_height(&block_a) + SCAN_GAP;
        ui_info_box(text(lang, T_SCAN_QR), why, scan_note, under, under + box);
    }
    ui_block_draw(&block_a, BOTTOM_W / 2.0f, top, INK, UI_CENTER);
    ui_bar(UI_KEY_B, text(lang, T_BACK), is_pressed(HIT_LEFT));
}

static void url_top(void)
{
    ui_head(text(lang, T_QR_FOUND), NULL);
    ui_url_card(url_shown);
    ui_foot(FOOT_DOTS_Y, ART_FOLDER_FOOT, scan_shown(bqr ? BADGE_SETS_DIR : THEMES_DIR), MUTED, 1, NULL);
}

static void url_bottom(void)
{
    ui_go(GO_BUTTON, tile_state(HIT_GO), ART_DOWNLOAD, text(lang, T_DOWNLOAD));
    if (ring)
        ui_select(GO_BUTTON, GO_R);
    ui_bar2(text(lang, T_BACK), text(lang, T_LOAD), bar_down());
}

/* The preview: what it is of, for the wait and for a failure. */
static const char *preview_name(const uint8_t **icon)
{
    const design_row *d = preview_from == SCR_DESIGN ? shown_design() : NULL;
    *icon = d ? d->theme->icon : preview_from == SCR_CONFIRM && theme_open ? theme.icon : NULL;
    return d ? d->theme->name : preview_from == SCR_CONFIRM && theme_open ? theme.name : "";
}

/* One screen's wallpaper over all of it: the texture's part for this moment (still, gliding, or a flip book), a
 * colour, or nothing of the theme's own, which a line says on the application's background. */
static void preview_wall(const tp_wall *w, int bottom)
{
    static u32 at;              /* the time once per frame: both eyes see the same moment of a moving wallpaper */
    static u64 now;
    if (at != ui_frame()) {
        at = ui_frame();
        now = osGetTime();
    }
    float width = bottom ? BOTTOM_W : TOP_W;
    if (w->kind == TP_WALL_TEXTURE)
        ui_wall(bottom, tp_wall_x(w, (int)width, now - preview_since));
    else if (w->kind == TP_WALL_COLOR)
        ui_wall_color(w->color_top, w->color);
    else
        ui_text_fit(text(lang, T_PREVIEW_NO_WALL), width / 2, SCREEN_H / 2.0f, width - 2 * BODY_PAD, BODY_PX, MUTED,
                    UI_CENTER, 0);
}

static void preview_top(void)
{
    const tp_body *walls = preview_walls();
    if (walls) {
        preview_wall(&walls->top, 0);
        return;
    }
    const uint8_t *icon;
    const char *name = preview_name(&icon);
    ui_head(text(lang, T_PREVIEW), NULL);
    if (preview_seen == PREVIEW_FAILED) {
        ui_detail(icon, ART_WARN, name, text(lang, T_PREVIEW_FAILED), WARN);
    } else {                    /* while it is read: the design, no bar; the loader is on the bottom screen */
        ui_gallery((ui_pic){icon, -1}, (ui_pic){NULL, -1}, (ui_pic){NULL, -1}, name, 0);
        foot_status();
    }
}

static void preview_bottom(void)
{
    const tp_body *walls = preview_walls();
    if (walls)
        preview_wall(&walls->bottom, 1);
    if (preview_seen != PREVIEW_LOADING)
        ui_bar(UI_KEY_B, text(lang, T_BACK), is_pressed(HIT_LEFT));
    else                        /* the loader as a job's: after LOADER_WAIT_MS, within LOADER_FADE_MS; the bottom
                                   screen holds nothing else meanwhile */
        ui_loader(text(lang, T_PREVIEW_LOADING), NULL, 1.0f,
                  ease_out(ease_span(osGetTime(), preview_since + LOADER_WAIT_MS, LOADER_FADE_MS)));
}

/* Under the sounds' choices, which end at choices_end, whose sounds the application plays (FIX-1): the interface
 * sounds' maker, then every track of the build from its line in tracks.h, so a track added there cannot miss its
 * credit. */
static void settings_credits(float choices_end)
{
    char tracks[TRACK_COUNT][160];
    const char *lines[1 + TRACK_COUNT] = {text(lang, T_SOUNDS_CREDIT)};
    for (int k = 0; k < TRACK_COUNT; k++) {
        const track *t = &TRACK_LIST[k];
        text_track_credit(lang, t->title, t->author, t->licence, tracks[k], sizeof tracks[k]);
        lines[1 + k] = tracks[k];
    }
    ui_choices_credits(choices_end, lines, 1 + TRACK_COUNT);
}

/* The settings. Top: the row the cursor is on (the first until it moves), all its choices with the one that holds
 * checked; the sounds' with their credits. Bottom: a row a setting, its name over the choice that holds. */
static void settings_top(void)
{
    const setting *s = setting_at(focus >= 0 && focus < rows_shown() ? focus : 0);
    if (s->value == &prefs.pattern)
        ui_backdrop();
    ui_head(text(lang, T_SETTINGS), text(lang, s->label));
    if (s->value == &prefs.sounds)     /* FIX-2: the choices at the card's top, the credits at its lower end */
        settings_credits(ui_choices_top(s->count, *s->value, s->name));
    else
        ui_choices(s->count, *s->value, s->name);
    if (s->value == &prefs.logo)
        ui_choices_word();
    ui_foot(FOOT_DOTS_Y, -1, NULL, MUTED, 0, version);
}

static void settings_bottom(void)
{
    for (int i = 0; i < rows_shown(); i++) {
        const setting *s = setting_at(i);
        char at[24];
        snprintf(at, sizeof at, "%d / %d", *s->value + 1, s->count);
        ui_row_setting(settings_rect(i), is_pressed(HIT_ROW0 + i) ? UI_DOWN : 0, text(lang, s->label),
                       s->name(*s->value), at);
    }
    if (ring && focus >= 0 && focus < rows_shown())
        ui_select(settings_rect(focus), ROW_R);
    ui_bar(UI_KEY_B, text(lang, T_BACK), is_pressed(HIT_LEFT));
}

static const struct {
    void (*top)(void), (*bottom)(void);
} SCREENS[] = {
    [SCR_MAIN] = {main_top, main_bottom},
    [SCR_DESIGNS] = {designs_top, designs_bottom},
    [SCR_BADGES] = {badges_top, badges_bottom},
    [SCR_LIST] = {list_top, list_bottom},
    [SCR_CONFIRM] = {confirm_top, confirm_bottom},
    [SCR_DONE] = {done_top, done_bottom},
    [SCR_PROBLEM] = {problem_top, problem_bottom},
    [SCR_SCAN] = {scan_top, scan_bottom},
    [SCR_URL] = {url_top, url_bottom},
    [SCR_INSTALLED] = {installed_top, installed_bottom},
    [SCR_DESIGN] = {design_top, design_bottom},
    [SCR_PREVIEW] = {preview_top, preview_bottom},
    [SCR_SETTINGS] = {settings_top, settings_bottom},
    [SCR_BADGE_LIST] = {badges_list_top, badges_list_bottom},
    [SCR_BADGE_SET] = {badges_set_top, badges_set_bottom},
    [SCR_BADGE_SETS] = {badges_sets_top, badges_sets_bottom},
    [SCR_BADGE_ONE] = {badges_one_top, badges_one_bottom},
    [SCR_BADGE_DONE] = {badges_done_top, badges_done_bottom},
    [SCR_BADGE_PROBLEM] = {badges_problem_top, badges_problem_bottom},
};

static void screen_bottom(void)
{
    SCREENS[screen].bottom();
}

/* The jobs during which nothing can be pressed, and the line under the loader's ring; -1: the download, which shows
 * its progress and keeps its Cancel. While such a job runs the bottom screen does not stand there as if it could be
 * used ("sonst hat man das Gefühl man kann weiter drücken/scrollen"): once the job has run for LOADER_WAIT_MS, what
 * the bottom screen shows goes away within LOADER_FADE_MS and the loader takes its place; a job over sooner shows no
 * loader, so a short load does not flash. The top screen stays as it is, and nothing shows progress: one loader, not
 * two. How the loader leaves again: loader_now. */
static int job_loader(void)
{
    switch (job) {
    case JOB_FOLDER: return T_READING_FOLDER;
    case JOB_DESIGNS: return T_READING_PACK;
    case JOB_CHECK: return T_CHECKING_THEME;
    case JOB_NO_MUSIC: return T_LEAVING_OUT_MUSIC;
    case JOB_DELETE_THEME: return T_DELETING_THEME;
    case JOB_DELETE_FILE: return T_DELETING_FILE;
    case JOB_THEMES: return themes_make ? T_CREATING_FOLDER : T_READING_FOLDER;
    case JOB_BADGES: return badges_loader();
    case JOB_BADGE_WRITE: return bremove ? T_REMOVE_SET : -1;     /* a removal as a delete; its line: job_line */
    default: return -1;
    }
}

/* The loader this frame, by the clock. cover: how far what the bottom screen shows has gone back into the
 * background; dots: how far the loader's ring itself shows. While a job with a loader works, both rise together once it has
 * run for LOADER_WAIT_MS. Once its work is done the ring fades within LOADER_OUT_MS while the cover goes on to the
 * whole; then the job is left (job_left). After that, on the screen the job started from, the cover goes back, so
 * what shows there now comes out of the background; on another screen it is gone at once, for that screen comes in
 * by its own transition. Every way is eased as a screen's content coming in (ease.h, ease_out). */
static void loader_now(float *cover, float *dots)
{
    u64 now = osGetTime();
    *cover = *dots = 0;
    if (back_since && (screen != back_screen || now - back_since >= LOADER_FADE_MS))
        back_since = 0;
    if (back_since)
        *cover = 1 - ease_out(ease_span(now, back_since, LOADER_FADE_MS));
    if (!job || job_loader() < 0 || (job_over_at && job_over_dots <= 0))
        return;                 /* no loader; or the job was done before its loader came: nothing of it shows */
    float in = ease_out(ease_span(now, job_since + LOADER_WAIT_MS, LOADER_FADE_MS));
    *dots = job_over_at ? job_over_dots * (1 - ease_out(ease_span(now, job_over_at, LOADER_OUT_MS))) : in;
    if (in > *cover)
        *cover = in;
}

/* A job's own top screen, where it has one; 0: the screen the job started from goes on (draw). The download's is its
 * progress, gliding, the stripes moving; while a theme's music is left out the theme changes on the worker, so its
 * picture and name come from the copies start_no_music made, as the check screen shows them. */
static int job_top(void)
{
    switch (job) {
    case JOB_DOWNLOAD: {
        char host[256], amount[64];
        uint64_t done = job_done, total = job_total;
        url_host(url_shown, host, sizeof host);
        if (total)
            snprintf(amount, sizeof amount, "%.1f / %.1f MiB", (double)done / (1 << 20), (double)total / (1 << 20));
        else
            snprintf(amount, sizeof amount, "%.1f MiB", (double)done / (1 << 20));
        ui_head(text(lang, T_DOWNLOADING), NULL);
        ui_busy_card(NULL, ART_GLOBE_SMALL, host, glide(total ? progress_keep(&bar, (float)done / (float)total) : -1.0f),
                     amount);
        ui_foot(FOOT_DOTS_Y, ART_FOLDER_FOOT, scan_shown(bqr ? BADGE_SETS_DIR : THEMES_DIR), MUTED, 1, NULL);
        return 1;
    }
    case JOB_NO_MUSIC:
        ui_head(text(lang, T_INSTALL_THEME), NULL);
        ui_gallery((ui_pic){done_icon, -1}, (ui_pic){NULL, -1}, (ui_pic){NULL, -1}, done_name, 1);
        foot_status();
        return 1;
    default:
        return 0;
    }
}

/* A job's own bottom screen, where it has one; 0: the screen the job started from (draw), until the loader has taken
 * its place. The download's is its Cancel; the check that follows a download (still on the QR screen) cannot be
 * cancelled, but its bar stays as it was, never held, and goes back into the background as the loader comes in, as
 * every bottom screen leaves when a job begins (0.9.3; 0.9.2 dropped it in one frame); while a theme's music is left
 * out, the two buttons without reading the theme. */
static int job_bottom(void)
{
    switch (job) {
    case JOB_DOWNLOAD:
        ui_bar(UI_KEY_B, text(lang, T_CANCEL), is_pressed(HIT_LEFT));
        return 1;
    case JOB_NO_MUSIC:
        music_choice_bottom();
        return 1;
    case JOB_CHECK:
    case JOB_BADGES:
        if (screen != SCR_URL)
            return 0;
        ui_bar(UI_KEY_B, text(lang, T_CANCEL), 0);
        return 1;
    default:
        return 0;
    }
}

/* The line under the loader's ring: the job's; while a set is removed the step under way (badges_work_line). */
static const char *job_line(void)
{
    return job == JOB_BADGE_WRITE ? badges_work_line() : text(lang, job_loader());
}

/* The notice under that line while a job writes the theme pack or HOME's badge files: a design's delete, a set's
 * removal (writing its backup back included); the jobs that read, make a folder or delete a file on the card show
 * none. */
static const char *job_notice(void)
{
    return job == JOB_DELETE_THEME || (job == JOB_BADGE_WRITE && bremove) ? text(lang, T_DONT_SWITCH_OFF) : NULL;
}

/* "Installiert" after an install, a set's Done screen after its install: the top screen is the installing screen's
 * end, going on. */
static int end_stays(void)
{
    return (screen == SCR_DONE && done_end) || (screen == SCR_BADGE_DONE && !bremove);
}

static void draw(void)
{
    float cover, dots;
    loader_now(&cover, &dots);
    if (job)
        job_dots = dots;
    tell_look();
    if (screen != SCR_DONE)
        done_end = 0;           /* "Installiert" was left: the Done card again, until an install ends on it */
    float in = entered();
    if (end_stays()) {
        /* the picture goes on by the end's clock; nothing comes in on the top screen, which stands as the
         * installing screen left it: only the bottom screen's content comes out of the background, as far faded at
         * first as a new screen's (ui.c, scene) */
        pour_frame(screen == SCR_DONE ? &install_pour : &bpour, end_now(), 1.0f, POUR_SUCCEEDED);
        float left = TRANSITION_FADE_PCT / 100.0f * (1 - ease_out(in));
        if (left > cover)
            cover = left;
        in = 1.0f;
    }
    ui_begin(in);
    ui_black(start_black());
    do {
        ui_top();
        if (!job || !job_top())
            SCREENS[screen].top();
        ui_bottom();
        if (!job || !job_bottom())
            screen_bottom();
        if (cover > 0 || dots > 0)
            ui_loader(dots > 0 ? job_line() : NULL, job_notice(), cover, dots);
        if (!job)
            draw_popup();
        else
            popup_gone = POP_NONE;      /* a job's frames show no popup: none fades on after them */
    } while (ui_next_eye());
    ui_end();
    last_frame = osGetTime();
}

/* How each change of screen and each popup's opening and closing went, a line each for the log (motion.h): told
 * after every frame of the main loop and of job_here. The line goes into the log's buffer once this thread may write
 * (motion_log); the buffer goes to the card only when nothing moves, so the log itself costs no frame of what it
 * measures. */
#define MOTION_MS 300           /* a movement is watched this long: longer than a transition or a popup's lasts */
#define MOTION_LINES 4          /* lines that can wait for motion_log: a delete's frames end at most two movements */
static motion moving;
static char moving_what[96], moved_line[MOTION_LINES][240];
static int moved_lines;

static const char *name_in(const char *const names[], size_t count, int k)
{
    return k >= 0 && (size_t)k < count && names[k] ? names[k] : "?";
}

static void motion_told(u64 frame_before)
{
    static const char *const screens[] = {
        [SCR_MAIN] = "start", [SCR_DESIGNS] = "designs", [SCR_BADGES] = "badges", [SCR_LIST] = "folder",
        [SCR_CONFIRM] = "check", [SCR_DONE] = "done", [SCR_PROBLEM] = "problem", [SCR_SCAN] = "scan",
        [SCR_URL] = "address", [SCR_INSTALLED] = "installed", [SCR_DESIGN] = "design", [SCR_PREVIEW] = "preview",
        [SCR_SETTINGS] = "settings",
    };
    static const char *const popups[] = {
        [POP_HELP] = "help", [POP_ASK_THEME] = "delete theme", [POP_ASK_FILE] = "delete file", [POP_NOTE] = "note",
        [POP_ASK_FOLDER] = "create folder", [POP_ASK_SET] = "remove set",
    };
    static int was_screen = -1, was_popup;
    char what[96] = "";
    size_t n = 0;
    if (was_screen >= 0 && screen != was_screen)
        n += (size_t)snprintf(what, sizeof what, "screen %s to %s", name_in(screens, sizeof screens / sizeof *screens,
                                                                            was_screen),
                              name_in(screens, sizeof screens / sizeof *screens, screen));
    if (popup != was_popup && was_popup)
        n += (size_t)snprintf(what + n, sizeof what - n, "%spopup %s closed", n ? ", " : "",
                              name_in(popups, sizeof popups / sizeof *popups, was_popup));
    if (popup != was_popup && popup && n < sizeof what)
        snprintf(what + n, sizeof what - n, "%spopup %s opened", n ? ", " : "",
                 name_in(popups, sizeof popups / sizeof *popups, popup));
    was_screen = screen;
    was_popup = popup;
    motion_result r;
    if (motion_frame(&moving, last_frame, (unsigned)(last_frame - frame_before), what[0] ? MOTION_MS : 0, &r) &&
        moved_lines < MOTION_LINES)
        snprintf(moved_line[moved_lines++], sizeof moved_line[0], "motion: %s: first frame after %u ms, %u frames in "
                 "%u ms, slowest %u ms%s", moving_what, r.first, r.frames, r.ms, r.slowest,
                 r.cut ? " (cut short by the next)" : "");
    if (what[0])
        snprintf(moving_what, sizeof moving_what, "%s", what);
}

static void motion_log(void)
{
    for (int k = 0; k < moved_lines; k++)
        console_log("%s", moved_line[k]);
    moved_lines = 0;
}

#ifdef APP_PROBE
#include "probe.h"

/* The probe build (make app PROBE=1, never delivered; the white flicker): while one of the screens below stands still
 * every frame must come out the same, on both screens of the console (probe.h). Still: no key or touch, no job, no
 * popup, a list with nothing selected and its scroll unchanged, for PROBE_STILL frames; on the bottom screen also no
 * cursor ring on a menu or the settings (it breathes), on the top screen the 3D slider unchanged (it moves the
 * views). Of the top screen the left eye's frame is read: the right eye's is drawn by the same calls, only shifted.
 * The pattern behind the top screens and the start screen's house and word stand still in this build (ui.c,
 * widget.h's MOVING_MS). A change of a render target or of a framebuffer goes to the log, the two frames' images to
 * sdmc:/theme-pack/flicker-<n>-{bottom,top}-{target,fb}-{a,b}.raw (a before, b after; at most 4 times); the count
 * for each screen and screen of the console every 600 still frames compared, and at the end. R7: the menus and the
 * settings too (their tiles' and rows' symbols are the glass sheet's, the Designs menu's letters flickered on the
 * console); A1: the top screens, since the moving pattern lies behind every one of them. */
static const struct {
    int screen;
    const char *name;
} PROBED[] = {
    {SCR_LIST, "folder view"}, {SCR_INSTALLED, "installed designs"}, {SCR_MAIN, "main menu"},
    {SCR_DESIGNS, "Designs menu"}, {SCR_BADGES, "Badges menu"}, {SCR_SETTINGS, "settings"},
};
#define PROBED_COUNT ((int)(sizeof PROBED / sizeof *PROBED))
static const char *const PROBE_SIDE[2] = {"bottom", "top"};     /* ui_probe's side: 0 the bottom screen, 1 the top */
static unsigned probe_still[2], probe_dumps;
static probe_tally probe_tallies[PROBED_COUNT][2];

/* The fit probe (review of the join): once at the start, the tightest texts of the information box and of the
 * note laid out with the console's font (ui_info_probe, ui_note_probe), nothing drawn: each install outcome with a
 * long text, with a pending import, 12 lost and the step whose text is longest, and the lost designs' note with 12
 * lost; a theme name of 64 two-byte letters (TP_NAME_BYTES); in English and German (the text functions take the
 * language; the setting is not changed). */
static void fit_probe(void)
{
    static const struct {
        int out;
        const char *name;
    } OUTS[] = {{OUT_DONE_UNCONFIRMED, "unconfirmed"}, {OUT_CANCELLED_KEPT, "cancelled-kept"},
                {OUT_PHASE2_FAILED_KEPT, "phase2-failed-kept"}, {OUT_LOST, "lost"}, {OUT_UNKNOWN, "unknown"}};
    static const char *const LANG_NAME[2] = {"en", "de"};
    static char fp_name[64 * 2 + 1], fp_title[200], fp_body[1600], fp_best[1600], fp_best_title[200];
    static char fp_head[200], fp_why[800];
    for (int fp_i = 0; fp_i < 64; fp_i++)
        memcpy(fp_name + 2 * fp_i, "\xC3\x84", 2);
    ui_fit fp_fit;
    for (int fp_l = 0; fp_l < 2; fp_l++) {
        for (size_t fp_o = 0; fp_o < sizeof OUTS / sizeof *OUTS; fp_o++) {
            judgement fp_j = {OUTS[fp_o].out, OUTS[fp_o].out != OUT_UNKNOWN, 12, 0, PENDING_YES};
            install_result fp_res;
            memset(&fp_res, 0, sizeof fp_res);
            fp_res.detail = TP_E_NOMEM;
            fp_best[0] = 0;
            for (int fp_step = STEP_NONE; fp_step <= STEP_DONE; fp_step++) {
                fp_res.failed = fp_step;
                text_outcome(fp_l, &fp_j, &fp_res, fp_name, fp_title, sizeof fp_title, fp_body, sizeof fp_body);
                if (strlen(fp_body) > strlen(fp_best)) {
                    snprintf(fp_best, sizeof fp_best, "%s", fp_body);
                    snprintf(fp_best_title, sizeof fp_best_title, "%s", fp_title);
                }
            }
            ui_info_probe(fp_best_title, fp_best, &fp_fit);
            console_log("fit probe %s %s: layout %d, %d px, %d lines, cut %d", OUTS[fp_o].name, LANG_NAME[fp_l],
                        fp_fit.layout, fp_fit.px, fp_fit.lines, fp_fit.cut);
        }
        deletion_judgement fp_dj = {DEL_LOST, 12, 0};
        text_deleted(fp_l, &fp_dj, 0, 0, 1, fp_name, fp_head, sizeof fp_head, fp_why, sizeof fp_why);
        ui_note_probe(fp_head, fp_why, &fp_fit);
        console_log("fit probe note-designs-lost %s: layout %d (1 with the sign, 2 without), %d px, %d lines, cut %d",
                    LANG_NAME[fp_l], fp_fit.layout, fp_fit.px, fp_fit.lines, fp_fit.cut);
    }
}

/* The screen shown among PROBED, else -1. */
static int probed(void)
{
    for (int i = 0; i < PROBED_COUNT; i++)
        if (screen == PROBED[i].screen)
            return i;
    return -1;
}

static void probe_save(const char *what, int n, int side, char ab, const void *data, size_t size)
{
    char path[80];
    snprintf(path, sizeof path, CARD_DIR "/flicker-%d-%s-%s-%c.raw", n, PROBE_SIDE[side], what, ab);
    FILE *f = fopen(path, "wb");
    if (f) {
        fwrite(data, 1, size, f);
        fclose(f);
    }
}

static void probe_count(int on, int side)
{
    const probe_tally *t = &probe_tallies[on][side];
    console_log("flicker probe, look %s, %s, %s screen: %u still frames compared, render target changed %u times, "
                "framebuffer %u times", LOOK_DRAWN, PROBED[on].name, PROBE_SIDE[side], t->compared, t->target_changes,
                t->fb_changes);
}

static void flicker_probe(void)
{
    static float last_scroll = -1, last_slider = -1;
    static int last_screen = -1;
    int on = probed();
    float slider = ui_probe_slider();
    int calm = on >= 0 && screen == last_screen && !job && popup == POP_NONE && !lv.selected &&
               pressed == HIT_NONE && pending == HIT_NONE && !hidKeysHeld() && lv.scroll == last_scroll;
    int still[2] = {calm && (list_screen() || !ring), calm && slider == last_slider};
    last_scroll = lv.scroll;
    last_slider = slider;
    last_screen = screen;
    ui_probe_arm(on >= 0);
    for (int side = 0; side < 2; side++) {
        ui_probe_frame fresh = {0}, older = {0};
        int read = ui_probe(side, &fresh, &older);
        int changed = probe_compare(&probe_still[side], on >= 0 ? &probe_tallies[on][side] : NULL, still[side], read,
                                    older.target_crc, fresh.target_crc, older.fb_crc, fresh.fb_crc);
        if (changed < 0)
            continue;
        const probe_tally *t = &probe_tallies[on][side];
        int target = changed & 1, fb = changed >> 1;
        if (changed && t->target_changes + t->fb_changes <= 100)
            console_log("flicker probe, look %s, %s, %s screen: frame %u, still for %u frames: render target %s "
                        "(%08lX, %08lX), framebuffer %s (%08lX, %08lX)", LOOK_DRAWN, PROBED[on].name, PROBE_SIDE[side],
                        (unsigned)ui_frame(), probe_still[side], target ? "CHANGED" : "same",
                        (unsigned long)older.target_crc, (unsigned long)fresh.target_crc, fb ? "CHANGED" : "same",
                        (unsigned long)older.fb_crc, (unsigned long)fresh.fb_crc);
        if (changed && probe_dumps < 4) {
            int n = (int)probe_dumps++;
            probe_save("target", n, side, 'a', older.target, older.target_bytes);
            probe_save("target", n, side, 'b', fresh.target, fresh.target_bytes);
            probe_save("fb", n, side, 'a', older.fb, older.fb_bytes);
            probe_save("fb", n, side, 'b', fresh.fb, fresh.fb_bytes);
            console_log("flicker probe: images saved as " CARD_DIR "/flicker-%d-%s-*.raw", n, PROBE_SIDE[side]);
        }
        if (t->compared % 600 == 0)
            probe_count(on, side);
    }
}
#endif

/* The main loop's frame, after its input: drawn, told to the motion log, and the log's lines to the card when
 * nothing moves. */
static void loop_frame(void)
{
#ifdef APP_PROBE
    flicker_probe();
#endif
    u64 frame_before = last_frame;
    draw();
    motion_told(frame_before);
    motion_log();
    if (ui_stats_alarm()) {
        char line[256];
        ui_stats(line, sizeof line);
        console_log("graphics near a limit: %s", line);
    }
    if (!moving.active)
        console_log_flush();
}

/* the foot's version: "v0.9.4-beta", " demo" or " probe" behind it in those builds; pre: the mark, "" for a release */
static void version_set(const char *pre)
{
    char numbers[24];
    text_version(numbers, sizeof numbers, APP_VERSION_MAJOR, APP_VERSION_MINOR, APP_VERSION_MICRO, pre);
#if defined APP_DEMO
    snprintf(version, sizeof version, "v%s demo", numbers);
#elif defined APP_PROBE
    snprintf(version, sizeof version, "v%s probe", numbers);
#else
    snprintf(version, sizeof version, "v%s", numbers);
#endif
}

int main(void)
{
    start_at[START_MAIN] = osGetTime();
    console_speed_up();         /* first: everything after it runs faster on a New 3DS */
    ui_part(&ART_APP_PART);
    int art = ui_init();
    start_at[START_UI] = osGetTime();
    version_set(APP_VERSION_PRE);
    lang = console_language();
    console_log_open();
    if (art)
        console_log("the interface's images could not be unpacked");
#ifdef APP_PROBE
    fit_probe();
#endif
    settings_start();
    hidSetRepeatParameters(24, 5);   // in frames: held for 0.4 s, the list moves 12 rows a second
    aptHookCookie cookie;
    aptHook(&cookie, on_apt, NULL);
    start_up();
    while (console_main_loop()) {
        if (job) {
            if (job_left())
                job_finish();
            else
                job_input();
        } else if (!handle_input()) {
            break;
        }
        if (!job && screen == SCR_SCAN)
            poll_camera();
        if (!job && rows_screen())
            list_motion();
        loop_frame();
    }
    if (worker) {
        job_cancel = 1;
        threadJoin(worker, U64_MAX);
        threadFree(worker);
    }
    aptUnhook(&cookie);
    preview_close();
    audio_exit();
    camera_stop();
    console_net_stop();
    close_theme();
    scan_free(&list);
    scan_free(&read_list);
    pack_free(&pv);
    free(old_themes);
    char graphics[256];
    ui_stats(graphics, sizeof graphics);
    motion_log();
    console_log("graphics: %s", graphics);
#ifdef APP_PROBE
    for (int i = 0; i < PROBED_COUNT; i++)
        for (int side = 0; side < 2; side++)
            probe_count(i, side);
#endif
    console_log("==== end");
    console_log_close();
    ui_exit();
    return 0;
}
