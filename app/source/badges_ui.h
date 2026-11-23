/* The Badges part's screens, included once by main.c (before start_scan): they use its state, its jobs and its
 * drawing helpers, and main.c calls in through badges_screen, badges_open, badges_input, badges_target_at,
 * badges_activate, badges_job_done, badges_loader, the SCREENS entries and, for the QR code's way,
 * bqr, badges_download_work and badges_download_done. What the screens do with HOME's files is badge_flow.c's; the
 * console's side of it is badge_console.c.
 *
 *   SCR_BADGES          the part: Von SD installieren, QR-Code scannen, Installierte Badges; the footer counts the
 *                       collection
 *   (SCR_SCAN, SCR_URL) the QR code's way is the Designs part's camera and link screens and its download (bqr set):
 *                       the file goes to /Badges, then to the same check screen as a set from the SD card
 *   SCR_BADGE_LIST      a folder of /Badges: subfolders to open, sets to choose
 *   SCR_BADGE_SET       the chosen set: Installieren, or why it cannot be installed
 *   SCR_BADGE_SETS      the sets in the HOME Menu
 *   SCR_BADGE_ONE       one of them, as the Designs part's one design (R15): its facts and one tile to remove it;
 *                       the tile asks the question when the set can be removed (the last set appended here), else
 *                       an alert says why not
 *   SCR_BADGE_DONE      a write that read back as meant; B, A or its tab lead back into the part
 *   SCR_BADGE_PROBLEM   a write or a read that did not work, and what is left on the card; B, A or its tab lead back
 *
 * A write runs in job_here: an install with its own frame (the picture of the write, and the notice not to switch
 * off or take out the SD card), a removal as a design's delete (C43: the set's screen goes on, the loader takes the
 * bottom screen's place, main.c draw); HOME and sleep are not allowed meanwhile, no input is read and the main
 * loop's turn with the system does not come round, so nothing leaves it: what the system asks for meanwhile (HOME
 * after a POWER press, the end) waits until the write is over. Then HOME and sleep are allowed again, whatever the
 * outcome, as after a design's install, and the console is not restarted (R13, decided by the console test of
 * 2026-11-15: HOME took an appended set without a restart and kept it over one). */

#include "badge_console.h"
#include "badge_download.h"
#include "badge_text.h"

static void push(int target, u64 since);
static void draw_cursor(const rect *at, int count, int radius);
static void start_scan(void);
static int row_at(int x, int y);                /* the lists are main.c's (rows_of, lv) */
static int row_drawn(size_t i);
static void draw_row_cursor(void);
static void draw_scrollbar(void);

enum { BJ_LIST, BJ_SETS, BJ_SCAN, BJ_LOAD };   /* what the badge job reads */

static badge_store bstore;
static badge_listing blist, blist_read;         /* the collection as shown; the job reads into blist_read */
static int blist_valid;
static badge_entries bents, bents_read;         /* the folder shown; the job reads into bents_read */
static char bdir[BADGE_PATH], bdir_read[BADGE_PATH];
static char bselect[BADGE_ENTRY_NAME];          /* the folder read: put the cursor on this entry */
static badge_source bsrc;                       /* the chosen set, read whole (the check screen and the write) */
static badge_peek bpeek;
static int bplan, bsrc_loaded, bsrc_result, bsrc_kind;
static char bsrc_path[BADGE_PATH];
static uint32_t bone;                           /* the installed set shown */
static int bjob;                                /* BJ_*: what the badge job reads */
static int bremove, bwriting;                   /* the write under way removes; a write is under way */
static int bremoved;                            /* FB-E: a removal is done: the list read again shows its note */
static badge_flow_result bres;
static char bname[BADGE_ENTRY_NAME];            /* the set the work screen and the Done screen show */
static uint8_t bicon[BADGE_ICON_BYTES];
static int bicon_set;
static volatile int bstep;
static u64 bwork_since;
static char btitle[160], bbody[1600];
static int bqr;                                 /* the QR code's way runs for this part (scan, link, download) */
static int bset_from;                           /* where the check screen goes back to: SCR_BADGE_LIST, SCR_BADGES */
static int bopen_focus;                         /* the part's tile the cursor will be on once it is entered */
_Static_assert(BT_STEP_VERIFY - BT_STEP_READ + 1 == 4 && BSTEP_VERIFY - BSTEP_READ + 1 == 6,
               "the list's four steps are the write's six stages in pairs, in its order (pour.c POUR_BADGES)");
static pour bpour;                              /* the work screen (R9 D3): the write's picture (pour.h) */
static int bhow;                                /* ... and how the write is, as far as this thread knows */
#ifdef APP_DEMO
static int bdemo_step;                          /* the demonstration build: the step it last drew out */
#endif

static int badges_screen(void)
{
    return screen == SCR_BADGES || screen == SCR_BADGE_LIST || screen == SCR_BADGE_SET ||
           screen == SCR_BADGE_SETS || screen == SCR_BADGE_ONE || screen == SCR_BADGE_DONE ||
           screen == SCR_BADGE_PROBLEM;
}

static void badges_log(void *ctx, const char *line)
{
    (void)ctx;
    console_log("%s", line);
}

/* The write's steps as one bar: the backup is most of the time (16 MB to the card and back). */
static void badges_step(void *ctx, int step, uint64_t done, uint64_t total)
{
    static const float FROM[BSTEP_DONE + 1] = {0, 0.08f, 0.45f, 0.78f, 0.82f, 0.9f, 0, 1};
    static const float TO[BSTEP_DONE + 1] = {0.08f, 0.45f, 0.78f, 0.82f, 0.9f, 1, 1, 1};
    (void)ctx;
    float part = total ? (float)done / (float)total : 0;
    bstep = step;
#ifdef APP_DEMO
    /* the demonstration build writes to a pretended HOME at once: each step is drawn out so the screen can be seen */
    static const int DEMO_MS[BSTEP_DONE + 1] = {1200, 3000, 2600, 600, 1200, 1400, 0, 0};
    if (step != bdemo_step && DEMO_MS[step]) {
        bdemo_step = step;
        for (int k = 0; k <= DEMO_MS[step] / 10; k++) {
            job_fraction = FROM[step] + (TO[step] - FROM[step]) * (float)k / (float)(DEMO_MS[step] / 10);
            svcSleepThread(10000000LL);
        }
    }
#endif
    job_fraction = FROM[step] + (TO[step] - FROM[step]) * (part > 1 ? 1 : part);
}

static const badge_env BENV = {&bstore, NULL, BADGE_APP_DIR, badges_log, badges_step, NULL};

// ---------------------------------------------------------------- reading, on the worker

static void badges_read_work(void *arg)
{
    (void)arg;
    switch (bjob) {
    case BJ_LIST:
    case BJ_SETS:
        badge_listing_free(&blist_read);
        badge_flow_list(&BENV, &blist_read);
        break;
    case BJ_SCAN:
        badge_entries_free(&bents_read);
        badge_scan(bdir_read, &bents_read, NULL, NULL);
        break;
    case BJ_LOAD:
        badge_source_free(&bsrc);
        bsrc_result = badge_source_load(bsrc_path, bsrc_kind, &bsrc);
        if (!bsrc_result)
            badge_source_peek(&bsrc, &bpeek);
        else
            memset(&bpeek, 0, sizeof bpeek), bpeek.result = bsrc_result;
        badge_listing_free(&blist_read);
        badge_flow_list(&BENV, &blist_read);
        break;
    }
    job_over = 1;
}

static void badges_read(int what)
{
    bjob = what;
    job_start(JOB_BADGES, badges_read_work);
}

/* The loader's line while the badge job reads. */
static int badges_loader(void)
{
    return bjob == BJ_SCAN ? T_READING_FOLDER : bjob == BJ_LOAD ? T_CHECKING_SET : T_READING_BADGES;
}

static void badges_take_listing(void)
{
    badge_listing_free(&blist);
    blist = blist_read;
    memset(&blist_read, 0, sizeof blist_read);
    blist_valid = 1;
}

static void badges_enter(void);

/* The rows of the Badges part's lists (main.c's shape): the folder's entries, folders first, or the installed sets. */
static void badges_rows(size_t *count, size_t *heads)
{
    *count = rows_of == ROWS_BADGES ? bents.count : blist_valid ? blist.col.sets : 0;
    *heads = rows_of == ROWS_BADGES ? bents.folders : 0;
}

/* One of these lists afresh: the cursor on row at, the list where that row shows, nothing selected. */
static void badges_rows_from(int of, size_t at)
{
    rows_of = of;
    lv = (scroll_view){.cursor = at};
    lv.scroll = scroll_for_cursor();
}

static void badges_job_done(void)
{
    switch (bjob) {
    case BJ_LIST:
        badges_take_listing();
        badges_enter();
        break;
    case BJ_SETS:
        badges_take_listing();
        badges_rows_from(ROWS_SETS, 0);
        screen = SCR_BADGE_SETS;
        if (bremoved) {         /* as a design's delete ends: the list without the set, the note over it */
            char head[BADGE_ENTRY_NAME + 40];
            snprintf(head, sizeof head, badge_text(lang, BT_REMOVED_HEAD), bname);
            bremoved = 0;
            show_note(-1, head, badge_text(lang, BT_REMOVED_NOTE));
        }
        break;
    case BJ_SCAN: {
        badge_entries_free(&bents);
        bents = bents_read;
        memset(&bents_read, 0, sizeof bents_read);
        snprintf(bdir, sizeof bdir, "%s", bdir_read);
        size_t at = 0;
        while (bselect[0] && at < bents.count && strcmp(bents.items[at].file, bselect))
            at++;
        badges_rows_from(ROWS_BADGES, at < bents.count ? at : 0);
        screen = SCR_BADGE_LIST;
        break;
    }
    case BJ_LOAD:
        bqr = 0;                /* a downloaded set's check was read on the link screen, which names /Badges */
        badges_take_listing();
        bsrc_loaded = 1;
        bplan = badge_flow_plan(&blist, &bsrc, &bpeek);
        screen = SCR_BADGE_SET;
        guard_screen();         /* Installieren opens where the list's rows were */
        break;
    }
}

/* The part from the start screen, or back to it: the collection is read anew for its footer, on the screen the way
 * in starts from, and the part comes in when it is read (R11: entered first, its buttons came in, went back under
 * the read's loader when the look at HOME's files took longer than LOADER_WAIT_MS, and came in a second time; as a
 * folder is read on the screen it is opened from). The screen left stays as it is meanwhile: the camera's last
 * picture goes (its frames are freed), the chosen set is let go only when the part comes in. */
static void badges_open(int which)
{
    static int store_made;
    if (!store_made) {
        console_badge_store(&bstore);
        store_made = 1;
    }
    bopen_focus = which;
    camera_stop();              /* back from the QR code's camera screen, as go_designs */
    camera_image = NULL;
    badges_read(BJ_LIST);
}

static void badges_enter(void)
{
    camera_stop();              /* should the camera have started again under the read (back from HOME) */
    camera_image = NULL;
    focus = bopen_focus;
    screen = SCR_BADGES;
    bqr = 0;
    badge_source_free(&bsrc);
    bsrc_loaded = 0;
}

/* A folder of the card, read on the worker; the cursor will be on the entry named select (the folder one came up
 * from), else on the first. */
static void badges_scan(const char *dir, const char *select)
{
    snprintf(bdir_read, sizeof bdir_read, "%s", dir);
    snprintf(bselect, sizeof bselect, "%s", select ? select : "");
    badges_read(BJ_SCAN);
}

// ---------------------------------------------------------------- the QR code's way

/* The download job's work when the QR code was scanned here (main.c's run_download starts it): the set goes to
 * /Badges, kept only if the core takes it. */
static void badges_download_work(void *arg)
{
    (void)arg;
    download_progress progress = {NULL, download_tick};
    badge_download(url, PART_PATH, BADGE_SETS_DIR, CA_PATH, console_user_agent(), &progress, &dl);
    job_over = 1;
}

/* After it (main.c's download_done): the set's check screen, as for a set chosen on the SD card; else the Designs
 * part's problem screen, which leads back here. */
static void badges_download_done(void)
{
    if (dl.status == DL_OK) {
        snprintf(bsrc_path, sizeof bsrc_path, "%s/%s", BADGE_SETS_DIR, dl.file);
        bsrc_kind = dl.kind;
        bset_from = SCR_BADGES;
        badges_read(BJ_LOAD);
        return;
    }
    char why[600];
    if (dl.status == DL_NOT_THEME) {
        snprintf(why, sizeof why, "%s", badge_text(lang, BT_NOT_A_SET));
    } else if (dl.status == DL_BAD_THEME) {     /* detail is a badge_result here, not the tp_result logged before */
        snprintf(why, sizeof why, "%s", badge_text_result(lang, dl.detail));
        console_log("badges: the download is no set the core takes: %s", badge_result_name(dl.detail));
    } else {
        text_download(lang, dl.status, dl.http, dl.detail, dl.error, why, sizeof why);
    }
    show_problem(T_QR_FOUND, text(lang, T_DOWNLOAD_FAILED), why, 0);
}

// ---------------------------------------------------------------- the write

static void badges_write_work(void *arg)
{
    (void)arg;
    if (bremove)
        badge_flow_remove_last(&BENV, &bres);
    else
        badge_flow_append(&BENV, &bsrc, &bres);
    job_over = 1;
}

/* The installing screen's top: the set and the picture of its write with the four steps. The set's Done screen goes
 * on with it after an install (badges_done_top; main.c, done_end). */
static void badges_work_top(void)
{
    const char *steps[4];
    for (int k = 0; k < 4; k++)
        steps[k] = badge_text(lang, BT_STEP_READ + k);
    ui_pour(&bpour, badge_text(lang, BT_INSTALL_BADGES), text(lang, T_INSTALLED), bname, bicon_set ? bicon : NULL,
            in_glass(ART_BADGES), steps, text(lang, T_INSTALL_SUCCESS), NULL);
}

/* The installing screen: the set and the picture of the write on top, the notice below (as a design's install's). */
static void badges_work_frame(void)
{
    const rect box = {(BOTTOM_W - SAFE_BOX_W) / 2, (SCREEN_H - SAFE_BOX_H) / 2, SAFE_BOX_W, SAFE_BOX_H};
    const float half = (SAFE_BOX_W - 2 * SAFE_BOX_PAD) / 2.0f;
    u64 now = osGetTime();
    if (!bwork_since)
        bwork_since = now;
    /* the picture follows the six stages by their shares (badges_step), shown as four steps (POUR_BADGES); writing
     * the backup back is a failure */
    pour_frame(&bpour, now, job_fraction, bstep == BSTEP_RESTORE ? POUR_FAILED : bhow);
    ui_begin(ease_span(now, bwork_since, TRANSITION_MS));
    do {
        ui_top();
        badges_work_top();
        ui_bottom();
        ui_card_glass(box);
        ui_stack((rect){box.x + SAFE_BOX_PAD, box.y, half, box.h}, ART_POWER_OFF, text(lang, T_DONT_SWITCH_OFF),
                 SAFE_PX, SAFE_LH, SAFE_GAP, INK);
        ui_stack((rect){box.x + SAFE_BOX_PAD + half, box.y, half, box.h}, ART_SD_OFF, text(lang, T_DONT_REMOVE_SD),
                 SAFE_PX, SAFE_LH, SAFE_GAP, INK);
    } while (ui_next_eye());
    ui_end();
    last_frame = osGetTime();
}

/* The line under the loader while a set is removed: the step under way (main.c, job_line). */
static const char *badges_work_line(void)
{
    return badge_text(lang, BT_WORK_READ + (bstep < BSTEP_DONE ? bstep : BSTEP_VERIFY));
}

/* One write, never two: the flow has its own guard, and this thread does not come back to the main loop before it
 * is over (job_here), so no input reaches a screen meanwhile. */
static void badges_write(int remove)
{
    if (bwriting)
        return;
    bwriting = 1;
    bremove = remove;
    bstep = BSTEP_READ;
    bwork_since = 0;
    console_log_strict(1);
    aptSetHomeAllowed(false);
    aptSetSleepAllowed(false);
    if (!bremove) {
        pour_start(&bpour, &POUR_BADGES, osGetTime());
        bhow = POUR_WORKING;
    }
#ifdef APP_DEMO
    bdemo_step = -1;
#endif
    if (bremove) {              /* as a design's delete: the set's screen goes on, the loader comes on the bottom one */
        job_here(JOB_BADGE_WRITE, badges_write_work, draw);
    } else {
        badges_work_frame();
        job_here(JOB_BADGE_WRITE, badges_write_work, badges_work_frame);
    }
    if (!bremove && bres.outcome == BFLOW_DONE) {   /* the picture catches up, then the box glows (as the install's) */
        u64 until = osGetTime() + POUR_CATCH_MS + POUR_END_MS;
        bhow = POUR_SUCCEEDED;
        snprintf(bname, sizeof bname, "%s", bres.name);     /* from the end on: the Done screen's top goes on with it */
        while (!pour_over(&bpour) && osGetTime() < until)
            badges_work_frame();
        end_begin();
    }
    aptSetHomeAllowed(true);
    aptSetSleepAllowed(true);
    guard_screen();             /* Zurück on either screen after it */
    if (bres.outcome == BFLOW_DONE && bremove) {    /* FB-E: back in the list, read again, with a note */
        snprintf(bname, sizeof bname, "%s", bres.name);
        bremoved = 1;
        badges_read(BJ_SETS);
        sfx(SFX_DONE);
    } else if (bres.outcome == BFLOW_DONE) {
        snprintf(bname, sizeof bname, "%s", bres.name);
        screen = SCR_BADGE_DONE;
        sfx(SFX_DONE);
    } else {
        badge_text_outcome(lang, &bres, btitle, sizeof btitle, bbody, sizeof bbody);
        screen = SCR_BADGE_PROBLEM;
        sfx(SFX_ERROR);
    }
    console_log_strict(0);
    badge_source_free(&bsrc);
    bsrc_loaded = 0;
    bwriting = 0;
}

static void badges_append(void)
{
    if (!bsrc_loaded || bplan != BPLAN_OK)
        return;
    snprintf(bname, sizeof bname, "%s", bsrc.key);
    memcpy(bicon, bpeek.icon, sizeof bicon);
    bicon_set = 1;
    badges_write(0);
}

static void badges_remove(void)
{
    if (!blist_valid || bone >= blist.col.sets)
        return;
    snprintf(bname, sizeof bname, "%s", blist.col.set[bone].name);
    badges_write(1);
}

// ---------------------------------------------------------------- the lists

static const char *badges_shown(const char *path)
{
    const char *colon = strchr(path, ':');
    return colon && colon[1] == '/' ? colon + 1 : path;
}

// ---------------------------------------------------------------- input

static rect badges_remove_rect(void)
{
    return (rect){(BOTTOM_W - TILE_W) / 2, ACT_TILE_Y, TILE_W, CHOICE_H};    /* main.c's design_tile with a file */
}

static int badges_one_shown(void)
{
    return blist_valid && bone < blist.col.sets;
}

static const char *badges_why_not(void);
static void badges_ask(void);

static int badges_can_remove(void)
{
    return blist_valid && bone < blist.col.sets && blist.col.set[bone].removable && blist.undo && !blist.manifest_bad;
}

/* What a touch at x, y is on (main.c's screen_target_at asks here for these screens). */
static int badges_target_at(int x, int y)
{
    switch (screen) {
    case SCR_BADGES:
        for (int i = 0; i < 3; i++)
            if (ui_hit(DESIGNS_TILES[i], x, y))
                return HIT_FIRST + i;
        return HIT_NONE;
    case SCR_BADGE_LIST:
        if (ui_hit(ui_corner_rect(badges_shown(bdir)), x, y))
            return scan_is_root(bdir) ? HIT_NONE : HIT_CORNER;     /* never the row under the button */
        /* fall through */
    case SCR_BADGE_SETS:
        if (ui_tab_hit(0, x, y))
            return HIT_LEFT;        /* the tab before the row drawn around it, as main.c's lists */
        if (ui_hit(SCROLL_AREA, x, y))
            return max_scroll() > 0 ? HIT_SCROLL : HIT_NONE;
        return row_at(x, y);
    case SCR_BADGE_SET:
        return bplan == BPLAN_OK && ui_hit(GO_FACTS, x, y) ? HIT_GO : HIT_NONE;
    case SCR_BADGE_ONE:
        return badges_one_shown() && ui_hit(badges_remove_rect(), x, y) ? HIT_GO : HIT_NONE;
    default:
        return HIT_NONE;
    }
}

/* The keys of these screens but the lists, whose keys are every list's (main.c's list_keys); main.c's handle_input
 * hands them over, as the settings screen's. The first press of the D-pad or A only shows the cursor, as everywhere. */
static int badges_input(u32 down, u64 now)
{
    u32 dirs = KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT;
    if (!ring && (down & (dirs | KEY_A)) && screen != SCR_BADGE_DONE && screen != SCR_BADGE_PROBLEM &&
        (screen != SCR_BADGE_SET || bplan == BPLAN_OK)) {      /* Installieren too, as the Designs part's check */
        ring = 1;
        sfx(SFX_MOVE);
        return 1;
    }
    switch (screen) {
    case SCR_BADGES: {
        int was = focus;
        if (down & KEY_DOWN)
            focus = 2;
        else if (down & KEY_UP && focus == 2)
            focus = 0;
        else if (down & KEY_LEFT && focus == 1)
            focus = 0;
        else if (down & KEY_RIGHT && focus == 0)
            focus = 1;
        if (focus != was)
            sfx(SFX_MOVE);
        if (down & KEY_A)
            push(HIT_FIRST + focus, now);
        break;
    }
    case SCR_BADGE_SET:
        if ((down & KEY_A) && bplan == BPLAN_OK)
            push(HIT_GO, now);
        break;
    case SCR_BADGE_ONE:
        if ((down & KEY_A) && badges_one_shown())
            push(HIT_GO, now);
        break;
    case SCR_BADGE_DONE:
    case SCR_BADGE_PROBLEM:
        if (down & KEY_A) {
            push(HIT_LEFT, now);
            return 1;
        }
        break;
    }
    if (down & KEY_B)
        push(HIT_LEFT, now);
    return 1;
}

/* What a push does on these screens once it has shown (main.c's activate hands them over). */
static int badges_activate(int target)
{
    switch (screen) {
    case SCR_BADGES:
        if (target == HIT_LEFT) {
            go_main(1);
        } else if (target == HIT_FIRST) {
            focus = 0;
            badges_scan(BADGE_SETS_DIR, NULL);
        } else if (target == HIT_SECOND) {
            focus = 1;
            bqr = 1;
            start_scan();
        } else if (target == HIT_THIRD) {
            focus = 2;
            badges_read(BJ_SETS);
        }
        break;
    case SCR_BADGE_LIST:
        if (target == HIT_LEFT) {
            screen = SCR_BADGES;
            focus = 0;
        } else if (target == HIT_CORNER) {      /* up a folder, to the card's root at most */
            char up[BADGE_PATH], child[BADGE_ENTRY_NAME];
            snprintf(up, sizeof up, "%s", bdir);
            if (scan_up(up, child, sizeof child) == 0)
                badges_scan(up, child);
        } else if (target >= HIT_ROW0 && (size_t)(target - HIT_ROW0) < bents.count) {
            const badge_entry *e = &bents.items[target - HIT_ROW0];
            char path[BADGE_PATH];
            lv.cursor = (size_t)(target - HIT_ROW0);
            open_selected = lv.selected;
            if (strlen(bdir) + 1 + strlen(e->file) >= sizeof path)
                break;
            scan_path(path, sizeof path, bdir, e->file);
            if (e->folder) {
                badges_scan(path, NULL);
            } else {
                snprintf(bsrc_path, sizeof bsrc_path, "%s", path);
                bsrc_kind = e->kind;
                bset_from = SCR_BADGE_LIST;
                badges_read(BJ_LOAD);
            }
        }
        break;
    case SCR_BADGE_SET:
        if (target == HIT_LEFT && bset_from == SCR_BADGES) {
            badges_open(1);
        } else if (target == HIT_LEFT) {
            badge_source_free(&bsrc);
            bsrc_loaded = 0;
            select_on_entry = open_selected;
            screen = SCR_BADGE_LIST;
        } else if (target == HIT_GO) {
            badges_append();
        }
        break;
    case SCR_BADGE_SETS:
        if (target == HIT_LEFT) {
            screen = SCR_BADGES;
            focus = 2;
        } else if (target >= HIT_ROW0 && (size_t)(target - HIT_ROW0) < list_count()) {
            bone = (uint32_t)(target - HIT_ROW0);
            lv.cursor = bone;
            open_selected = lv.selected;
            screen = SCR_BADGE_ONE;
        }
        break;
    case SCR_BADGE_ONE:
        if (target == HIT_LEFT) {
            select_on_entry = open_selected;
            screen = SCR_BADGE_SETS;
        } else if (target == HIT_GO && badges_can_remove()) {
            guard_screen();
            badges_ask();       /* main.c's question, as the design's: its Entfernen removes once it has faded out */
            open_popup(POP_ASK_SET);
        } else if (target == HIT_GO && badges_one_shown()) {
            show_note(ART_INFO_WARN, badges_why_not(), NULL);     /* why not only here, never on the screen (R15) */
        }
        break;
    case SCR_BADGE_DONE:
    case SCR_BADGE_PROBLEM:
        if (target == HIT_LEFT)
            badges_open(0);
        break;
    }
    return 1;
}

// ---------------------------------------------------------------- drawing

static void badges_status_foot(float dots_y)
{
    char status[160];
    if (!blist_valid)
        return;
    int ok = badge_text_status(lang, &blist, status, sizeof status);
    ui_foot(dots_y, -1, status, ok ? MUTED : WARN, 0, NULL);
}

static void badges_top(void)
{
    ui_hero(in_glass(ART_BADGES), text(lang, T_BADGES), NULL, NULL);
    badges_status_foot(HERO_FOOT_DOTS_Y);
}

static void badges_bottom(void)
{
    ui_tile(FACE_DTILE, DESIGNS_TILES[0], tile_state(HIT_FIRST), ART_SD, text(lang, T_FROM_SD), DESIGNS_PX,
            DESIGNS_LH, DESIGNS_GAP, TILE_INK);
    ui_tile(FACE_DTILE, DESIGNS_TILES[1], tile_state(HIT_SECOND), ART_QR, text(lang, T_SCAN_QR), DESIGNS_PX,
            DESIGNS_LH, DESIGNS_GAP, TILE_INK);
    ui_wide(FACE_WIDE, DESIGNS_TILES[2], tile_state(HIT_THIRD), ART_INSTALLED, badge_text(lang, BT_INSTALLED_BADGES),
            DESIGNS_PX, DESIGNS_GAP, TILE_INK);
    draw_cursor(DESIGNS_TILES, 3, TILE_R);
    ui_bar(UI_KEY_B, text(lang, T_BACK), is_pressed(HIT_LEFT));
}

static ui_pic badges_entry_pic(size_t i)
{
    if (i >= bents.count)
        return (ui_pic){NULL, -1};
    const badge_entry *e = &bents.items[i];
    if (e->folder)
        return (ui_pic){NULL, ART_FOLDER_HERO};
    return e->peek.result ? (ui_pic){NULL, ART_WARN} : (ui_pic){e->peek.icon, -1};
}

static ui_pic badges_set_pic(size_t i)
{
    if (!blist_valid || i >= blist.col.sets || !blist.icons)
        return (ui_pic){NULL, -1};
    return (ui_pic){blist.icons[i], -1};
}

/* The top screen of a list: the selected row's picture, or nothing chosen while no row is selected (every list). */
static void badges_list_top(void)
{
    char pos[48];
    snprintf(pos, sizeof pos, "%u / %u", (unsigned)lv.cursor + 1, (unsigned)bents.count);
    ui_head(text(lang, T_SD_CARD), bents.count && lv.selected ? pos : NULL);    /* as the Designs part's list */
    if (!bents.count || !lv.selected || lv.cursor >= bents.count)
        ui_gallery_empty();
    else
        ui_swarm(badges_entry_pic, bents.count, lv.cursor, bents.items, bents.items[lv.cursor].file, 0);
    ui_foot(FOOT_DOTS_Y, ART_FOLDER_FOOT, badges_shown(bdir), MUTED, 1, NULL);
}

static void badges_list_bottom(void)
{
    char sub[48];
    for (size_t i = 0; i < bents.count; i++) {
        if (!row_drawn(i))
            continue;
        rect r = row_rect(i);
        const badge_entry *e = &bents.items[i];
        int state = is_pressed(HIT_ROW0 + (int)i) ? UI_DOWN : 0;
        if (e->folder) {
            ui_row_folder(r, state, e->file);
        } else if (e->peek.result) {
            ui_row_theme(r, state, NULL, e->file, badge_text_result(lang, e->peek.result), 0, WARN, 0);
        } else {
            badge_text_count(lang, e->peek.count, sub, sizeof sub);
            ui_row_theme(r, state, e->peek.icon, e->file, sub, 0, MUTED, 0);
        }
    }
    draw_row_cursor();
    ui_corner(badges_shown(bdir), !scan_is_root(bdir), is_pressed(HIT_CORNER));
    draw_scrollbar();
    ui_bar(UI_KEY_B, text(lang, T_BACK), is_pressed(HIT_LEFT));
    /* the empty folder's note in the information box (FB-E), after the corner and the bar */
    if (!bents.count)
        ui_info_box(badges_shown(bdir),
                    badge_text(lang, bents.missing ? BT_FOLDER_MISSING : BT_EMPTY_FOLDER), NULL, LIST_INSET,
                    LIST_BOTTOM);
}

static void badges_set_top(void)
{
    ui_head(badge_text(lang, BT_INSTALL_BADGES), NULL);
    if (bpeek.result) {
        ui_detail(NULL, ART_WARN, bsrc.key[0] ? bsrc.key : badges_shown(bsrc_path), NULL, INK);
        ui_foot(FOOT_DOTS_Y, -1, text(lang, T_CANNOT_INSTALL), WARN, 0, NULL);
        return;
    }
    ui_pic none = {NULL, -1};
    ui_gallery((ui_pic){bpeek.icon, -1}, none, none, bsrc.key, 0);
    if (bplan == BPLAN_OK)
        badges_status_foot(FOOT_DOTS_Y);
    else
        ui_foot(FOOT_DOTS_Y, -1, text(lang, T_CANNOT_INSTALL), WARN, 0, NULL);
}

static void badges_set_bottom(void)
{
    char count[48], reason[600];
    if (bplan == BPLAN_OK) {
        badge_text_count(lang, bpeek.count, count, sizeof count);
        ui_facts((const ui_fact[]){{ART_FACT_CHECK, count, 0}, {ART_FACT_FILE, badges_shown(bsrc_path), 1}}, 2,
                 ACT_FACTS_Y);
        ui_go(GO_FACTS, tile_state(HIT_GO), ART_DOWNLOAD, text(lang, T_INSTALL));
        if (ring)
            ui_select(GO_FACTS, GO_R);
    }
    ui_bar(UI_KEY_B, text(lang, T_BACK), is_pressed(HIT_LEFT));
    /* the reason after the bar, in the information box (FB-E) */
    if (bplan != BPLAN_OK) {
        badge_text_plan(lang, bplan, &blist, &bpeek, reason, sizeof reason);
        ui_info_box(text(lang, T_CANNOT_INSTALL), reason, NULL, 0, 0);
    }
}

static void badges_sets_top(void)
{
    ui_head(badge_text(lang, BT_INSTALLED_BADGES), NULL);
    if (!blist_valid || !blist.col.sets || !lv.selected || lv.cursor >= blist.col.sets)
        ui_gallery_empty();
    else
        ui_swarm(badges_set_pic, blist.col.sets, lv.cursor, blist.col.set, blist.col.set[lv.cursor].name, 0);
    badges_status_foot(FOOT_DOTS_Y);
}

static void badges_sets_bottom(void)
{
    char sub[48];
    for (size_t i = 0; blist_valid && i < blist.col.sets; i++) {
        if (!row_drawn(i))
            continue;
        rect r = row_rect(i);
        badge_text_count(lang, blist.col.set[i].count, sub, sizeof sub);
        ui_row_theme(r, is_pressed(HIT_ROW0 + (int)i) ? UI_DOWN : 0, blist.icons ? blist.icons[i] : NULL,
                     blist.col.set[i].name, sub, 0, MUTED, 0);
    }
    draw_row_cursor();
    draw_scrollbar();
    ui_bar(UI_KEY_B, text(lang, T_BACK), is_pressed(HIT_LEFT));
    /* the reason or the note after the bar, in the information box (FB-E) */
    if (blist_valid && blist.state != BLIST_OK) {
        char why[600], status[160];
        badge_text_plan(lang, BPLAN_COLLECTION, &blist, &bpeek, why, sizeof why);
        badge_text_status(lang, &blist, status, sizeof status);
        size_t said = strlen(status);
        const char *rest = why;
        if (said && !strncmp(why, status, said) && why[said] == '.' && why[said + 1]) {   /* the footer says it */
            rest = why + said + 1;
            while (*rest == ' ')
                rest++;
        }
        ui_info_box(badge_text(lang, BT_INSTALLED_BADGES), rest, NULL, 0, LIST_BOTTOM);
    } else if (!blist_valid || !blist.col.sets) {
        ui_info_box(badge_text(lang, BT_INSTALLED_BADGES), badge_text(lang, BT_NO_SETS), NULL, 0,
                    LIST_BOTTOM);
    }
}

/* Why the set shown cannot be removed. */
static const char *badges_why_not(void)
{
    const badge_set_info *s = &blist.col.set[bone];
    if (blist.manifest_bad)
        return badge_text_result(lang, BADGE_E_MANIFEST);
    if (bone + 1 != blist.col.sets)
        return badge_text(lang, BT_ONLY_LAST);
    if (!s->mine)
        return badge_text(lang, BT_NOT_OURS);
    if (!s->removable)
        return badge_text_result(lang, blist.col.remove);
    return badge_text(lang, BT_NO_UNDO);
}

static void badges_one_top(void)
{
    ui_head(badge_text(lang, BT_INSTALLED_BADGES), NULL);
    if (!blist_valid || bone >= blist.col.sets) {
        ui_gallery_empty();
        return;
    }
    ui_pic none = {NULL, -1};
    ui_gallery(badges_set_pic(bone), none, none, blist.col.set[bone].name, 0);
    badges_status_foot(FOOT_DOTS_Y);
}

static void badges_ask(void)
{
    char ask[300], what[400];
    snprintf(ask, sizeof ask, badge_text(lang, BT_ASK_REMOVE), blist.col.set[bone].name);
    snprintf(what, sizeof what, badge_text(lang, BT_ASK_REMOVE_NOTE), (unsigned)blist.col.set[bone].count);
    const char *paras[2] = {ask, what};
    ui_popup_text(&pop, paras, 2);
}

static void badges_one_bottom(void)
{
    char count[48];
    if (!blist_valid || bone >= blist.col.sets) {
        ui_bar(UI_KEY_B, text(lang, T_BACK), is_pressed(HIT_LEFT));
        return;
    }
    badge_text_count(lang, blist.col.set[bone].count, count, sizeof count);
    ui_facts((const ui_fact[]){{ART_FACT_HOME, blist.col.set[bone].name, 0}, {ART_FACT_CHECK, count, 0}}, 2,
             ACT_FACTS_Y);
    ui_tile(FACE_CHOICE, badges_remove_rect(), tile_state(HIT_GO), ART_DELETE_THEME, text(lang, T_REMOVE_SET),
            CHOICE_PX, CHOICE_LH, CHOICE_GAP, TILE_INK);
    if (ring)
        ui_select(badges_remove_rect(), TILE_R);
    ui_bar(UI_KEY_B, text(lang, T_BACK), is_pressed(HIT_LEFT));
}

static void badges_done_top(void)
{
    badges_work_top();          /* after an install: the installing screen's end goes on (main.c, draw) */
}

static void badges_done_bottom(void)
{
    ui_bar(UI_KEY_B, text(lang, T_BACK), is_pressed(HIT_LEFT));
    ui_way_pill(text(lang, T_HOME_MENU), badge_text(lang, BT_PLACE_BADGES));
}

static void badges_problem_top(void)
{
    ui_head(text(lang, T_BADGES), NULL);
    ui_detail(NULL, ART_WARN, btitle, NULL, INK);
    ui_foot(FOOT_DOTS_Y, -1, NULL, MUTED, 0, version);
}

static void badges_problem_bottom(void)
{
    /* the bar before the text, in the information box (FB-E) */
    ui_bar(UI_KEY_B, text(lang, T_BACK), is_pressed(HIT_LEFT));
    ui_info_box(btitle, bbody[0] ? bbody : text(lang, T_TRY_AGAIN), NULL, 0, 0);
}
