/* app/source/main.c itself on the Mac (included, so its statics are reachable), with a simulated touch screen, keys
 * and clock, one frame of the main loop per step (input, then drawing), 16 ms for every frame drawn. It checks what reaches
 * remove() and what starts an install when the stylus or the keys come at the wrong moment: a double tap on a button
 * that opens or closes a popup, a touch held through it, keys pressed over and over. Written by the reviewer of 0.8.1
 * (who found the cases) and kept here so they stay; the drawing, AM and the camera are no-ops (host/main_stubs.c),
 * libctru is host/fake/3ds.h, what a touch hits is uikit/source/hits.c.
 *
 *   test_main ZIP_A ZIP_B [-v]   prints one line per check and "N failed"; exit 1 if any failed; -v also prints the
 *                                log. ZIP_A and ZIP_B: two theme zips with different names, the pack's themes
 */
#define _DARWIN_C_SOURCE           /* mkdtemp on macOS */
#define main app_main
#include "main.c"
#include "art_of.h"
#undef main
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>
#include "turn.h"
#include "swarm.h"

#define FAKE_ICONS 8                /* as host/main_stubs.c */
extern int stub_verbose;
extern am_view fake_am;
extern const uint8_t *fake_archive;
extern size_t fake_archive_size;
extern uint8_t fake_icons[FAKE_ICONS][TP_ICON_SIZE];
extern home_state fake_home;
extern int fake_deletes;
extern char stub_foot[200];
extern void (*fake_installed)(const uint8_t *tmd, size_t size);
extern char fake_imp_trace[16384];
extern int fake_imp_calls, fake_imp_fail, fake_imp_unguarded;
extern int (*fake_imp_guard)(void);
extern int fake_music[PACK_CONTENTS];
extern int fake_music_reads;
extern char stub_tops[4096];
extern int stub_bottom_things, stub_marks, stub_busy_top, stub_loader;
extern float stub_loader_cover, stub_loader_shown;
extern char stub_loader_line[120], stub_loader_notice[120];
extern float stub_step_mark[4], stub_line_end, stub_entered;
extern int stub_steps_frames, stub_pour_frames;
extern const pour *stub_pour;
extern char stub_motion_line[256];
extern int stub_motion_lines, stub_strict_lines, stub_loose_lines, stub_flushes;
extern void (*stub_log_hook)(const char *line);
extern int fake_net_fail;
extern char stub_mark_kind[];
extern rect stub_mark_at[];
extern void (*stub_frame_done)(void);
extern void (*stub_each_frame)(void);
extern float stub_popup_shown, stub_entered;
extern int stub_scheme;
extern int stub_glass, stub_pattern, stub_start_drawn, stub_glass_drawn, stub_glass_untold, stub_hero_symbol;
extern int stub_tile_symbol[2], stub_bar_right, stub_bar_down;
extern float stub_fact_y;
extern char stub_foot_warn[80];
extern int stub_fact_rows, stub_fact_symbol[UI_FACTS], stub_fact_end[UI_FACTS];
extern char stub_fact_text[UI_FACTS][600];
extern char stub_popup_para[UI_POPUP_PARAS][600];
extern const char *stub_popup_left, *stub_popup_right;
extern int stub_popup_removes, stub_look;

/* ---- the fake libctru */
static u64 sim_now = 1000000;
static u32 sim_keys, cur, old, k_down, k_up;
static int sim_x, sim_y;
static int threads_started, last_thread_job;
static size_t last_thread_stack;    /* the stack the last thread was made with */
static int on_thread;               /* 1 while a thread's function runs (not where a refused one runs) */
static int thread_runs;             /* how often a thread's function ran */
static int join_record_set = -1;    /* the pack's record set when the last thread was joined (take_pack flips it) */
static int threads_fail;          /* no thread can be made: the job runs where it was started */
static u64 threads_fail_ms;
static int worker_frames;           /* 0: a job runs to its end at once; else it ends after that many frames */
static ThreadFunc held_job;
static void *held_arg;
static int held_frames;
/* the repeat as libctru's hid.c (2.7.0, and its build in devkitpro/devkitarm:20260610): its defaults until set; a
 * change of the held keys starts the delay again and repeats what came down; when the count runs out, every held key
 * repeats and the interval starts; it is counted in scans, and reading it clears it */
static u32 rep_delay = 30, rep_interval = 15, rep_count = 30, k_repeat;
#define REPEAT_DELAY 24             /* main.c's hidSetRepeatParameters(24, 5) */
#define REPEAT_INTERVAL 5
void hidScanInput(void)
{
    old = cur; cur = sim_keys; k_down = cur & ~old; k_up = old & ~cur;
    if (rep_delay) {
        if (cur != old)
            rep_count = rep_delay, k_repeat = k_down;
        if (--rep_count == 0)
            rep_count = rep_interval, k_repeat = cur;
    }
}
u32 hidKeysDown(void) { return k_down; }
u32 hidKeysHeld(void) { return cur; }
u32 hidKeysUp(void) { return k_up; }
u32 hidKeysDownRepeat(void) { u32 r = k_repeat; k_repeat = 0; return r; }
void hidTouchRead(touchPosition *t) { t->px = (u16)sim_x; t->py = (u16)sim_y; }
void hidSetRepeatParameters(u32 a, u32 b) { rep_delay = rep_count = a; rep_interval = b; k_repeat = 0; }
u64 osGetTime(void) { return sim_now; }
/* every look at the tick clock is 1 ms after the last: each stretch the check books (main.c, check_enter) is 1 ms */
static u64 sim_ticks;
u64 svcGetSystemTick(void) { return sim_ticks += 268112; }
bool aptMainLoop(void) { return true; }
static int apt_home = 1, apt_sleep = 1;     /* what the application last allowed */
static int loops;                           /* how often the main loop came round (frame) */
static int home_off_loops;                  /* ... when HOME was last disallowed */
void aptSetHomeAllowed(bool a)
{
    apt_home = a;
    if (!a)
        home_off_loops = loops;
}
void aptSetSleepAllowed(bool a) { apt_sleep = a; }
void aptHook(aptHookCookie *c, aptHookFn fn, void *p) { (void)c; (void)fn; (void)p; }
void aptUnhook(aptHookCookie *c) { (void)c; }
Thread threadCreate(ThreadFunc fn, void *arg, size_t stack, int prio, int core, bool detached)
{
    (void)prio; (void)core; (void)detached;
    threads_started++;
    last_thread_job = job;
    last_thread_stack = stack;
    if (threads_fail) {
        sim_now += threads_fail_ms;     /* the work then runs where the job was started, and takes this long */
        return NULL;
    }
    if (worker_frames) {
        held_job = fn, held_arg = arg, held_frames = worker_frames;
        return (Thread)1;
    }
    on_thread = 1;
    fn(arg);                    /* the job runs to its end at once; the next frame finishes it */
    on_thread = 0;
    thread_runs++;
    return (Thread)1;
}
static void run_held_job(void)
{
    ThreadFunc fn = held_job;
    held_job = NULL;
    if (fn) {
        on_thread = 1;
        fn(held_arg);
        on_thread = 0;
        thread_runs++;
    }
}
/* after every frame drawn, in the main loop or inside an action (a delete draws its frames itself until its job is
 * over; the install): 16 ms have passed (frame_ms: more, for a console that loses frames), and a held job is one
 * frame nearer its end */
static u64 frame_ms = 16;
static void held_tick(void)
{
    sim_now += frame_ms;
    if (held_job && --held_frames <= 0)
        run_held_job();
}
Result threadJoin(Thread t, u64 ns)
{
    (void)t; (void)ns;
    run_held_job();
    join_record_set = record_set;
    return 0;
}
void threadFree(Thread t) { (void)t; }
Result svcGetThreadPriority(s32 *prio, Handle h) { (void)h; *prio = 0x30; return 0; }
void svcSleepThread(int64_t ns) { (void)ns; }

/* DN: the frames whose bottom screen showed the Done screen's pill, and those of them on a screen without it: only
 * "Installiert" without the question and a set's Done screen after its install have it */
extern char stub_pill_home[64], stub_pill_change[64];
extern int stub_pill_bottom;
static int pill_frames, pill_elsewhere;
static void pill_watch(void)
{
    if (!stub_pill_home[0])
        return;
    pill_frames++;
    pill_elsewhere += !stub_pill_bottom || !((screen == SCR_DONE && !may_clean) || (screen == SCR_BADGE_DONE && !bremove));
}

/* ---- one frame of the main loop */
static int frame(void)
{
    int go = 1;
    loops++;
    if (job) {
        if (job_left())
            job_finish();
        else
            job_input();
    } else {
        go = handle_input();
    }
    if (!job && rows_screen())          /* as the main loop: the list glides after the cursor */
        list_motion();
    loop_frame();
    pill_watch();
    return go;
}
static void frames_for(u64 ms) { u64 end = sim_now + ms; while (sim_now < end) frame(); }
static void stylus(int x, int y) { sim_x = x; sim_y = y; sim_keys |= KEY_TOUCH; }
static void lift(void) { sim_keys &= ~KEY_TOUCH; }
/* a tap: down for hold ms */
static void tap(int x, int y, u64 hold) { stylus(x, y); frames_for(hold); lift(); frame(); }
static void key(u32 k) { sim_keys |= k; frame(); sim_keys &= ~k; frame(); }

/* ---- fixtures */
static char dir[512], victim[600];
static void make_victim(const char *name)
{
    snprintf(victim, sizeof victim, "%s/%s", dir, name);
    FILE *f = fopen(victim, "wb");
    fputs("zip", f);
    fclose(f);
}
static int victim_exists(void) { struct stat st; return stat(victim, &st) == 0; }

static void reset_app(void)
{
    sim_keys = 0; cur = old = 0; hidSetRepeatParameters(REPEAT_DELAY, REPEAT_INTERVAL); frame(); frame();
    popup = popup_gone = popup_then = POP_NONE; pending = pressed = HIT_NONE; touch.target = HIT_NONE; ring = 0; focus = 0;
    job = JOB_NONE; job_over = 0; threads_started = 0;
}

/* the check screen of a theme installed already, chosen from the Designs screen; the file at victim */
static void confirm_installed(const char *name)
{
    reset_app();
    make_victim(name);
    snprintf(chosen_dir, sizeof chosen_dir, "%s", dir);
    snprintf(chosen, sizeof chosen, "%s", name);
    confirm_from = SCR_DESIGNS;
    memset(&theme, 0, sizeof theme);
    snprintf(theme.name, sizeof theme.name, "Victim");
    theme_open = 1; theme_result = TP_OK;
    memset(&plan, 0, sizeof plan);
    plan.state = PLAN_DUPLICATE; plan.duplicate = 5;
    screen = SCR_CONFIRM;
    frames_for(500);
}

/* "Installiert" with the cleanup question; the file at victim */
static void done_with_cleanup(const char *name)
{
    reset_app();
    make_victim(name);
    snprintf(clean_dir, sizeof clean_dir, "%s", dir);
    snprintf(clean_name, sizeof clean_name, "%s", name);
    may_clean = 1;
    screen = SCR_DONE;
    frames_for(500);
}

/* ---- a pack as the console would report it: themes 2 (ZIP_A's) and 3 (ZIP_B's) of 0004008C00009809, and `extra`
 *      more ("Theme 4" on, up to index 7), its record in sdmc:/theme-pack/installed.tmd under the working folder, its
 *      catalogue in the fake console */
static void put32(uint8_t *p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> 8 * i); }

static uint8_t archive[8 + 0xC8 * FAKE_ICONS];
static char names[FAKE_ICONS][0x41];
static uint8_t theme_a_icon[TP_ICON_SIZE];
static char theme_a_name[0x41];

/* The catalogue up to index top from names[] and fake_icons[]: what content 0 holds. */
static void make_archive(int top)
{
    memset(archive, 0, sizeof archive);
    put32(archive, 1);
    put32(archive + 4, (uint32_t)top);
    for (int i = 0; i <= top; i++) {
        uint8_t *e = archive + 8 + 0xC8 * i;
        memcpy(e, names[i], strlen(names[i]));
        put32(e + 0xC0, (uint32_t)i);
        put32(e + 0xC4, i != 0);
    }
    fake_archive = archive;
    fake_archive_size = 8 + 0xC8 * ((size_t)top + 1);
}

/* An install went through on the fake console: AM lists the new TMD's contents (installed, but a theme deleted
 * before stays deleted), content 0 lists the new theme, which in these cases is always ZIP_A's. */
static void installed(const uint8_t *tmd, size_t size)
{
    uint64_t title_id;
    uint16_t tmd_version;
    size_t count;
    am_view was = fake_am;
    if (tp_tmd_read(tmd, size, &title_id, &tmd_version, &count) != TP_OK)
        exit(3);
    fake_am.version = tmd_version;
    fake_am.count = count;
    int top = 0;
    for (size_t k = 0; k < count; k++) {
        tp_content c;
        tp_tmd_record(tmd, size, k, &c);
        fake_am.list[k] = (tp_am_content){c.index, c.id, c.size};
        fake_am.usable[k] = 1;
        for (size_t j = 0; j < was.count; j++)
            if (was.list[j].index == c.index)
                fake_am.usable[k] = was.usable[j];
        top = c.index > top ? c.index : top;
    }
    if (top >= FAKE_ICONS) {    /* the fake console names FAKE_ICONS themes: a case that installs past them (a mark
                                   left on the card by the cases before it) must make its pack anew first */
        fprintf(stderr, "the fake console has no room for theme %d\n", top);
        exit(3);
    }
    snprintf(names[top], sizeof names[top], "%s", theme_a_name);
    memcpy(fake_icons[top], theme_a_icon, TP_ICON_SIZE);
    make_archive(top);
}

static int file_sink(void *ctx, const void *buf, size_t len) { return fwrite(buf, 1, len, ctx) == len ? 0 : -1; }

static int file_read(void *ctx, uint64_t offset, void *buf, size_t len)
{
    return fseek(ctx, (long)offset, SEEK_SET) || fread(buf, 1, len, ctx) != len ? -1 : 0;
}

static void peek(const char *path, tp_theme *t)
{
    FILE *f = fopen(path, "rb");
    fseek(f, 0, SEEK_END);
    tp_source src = {f, (uint64_t)ftell(f), file_read};
    if (!f || tp_theme_peek(t, &src, "x") != TP_OK) {
        fprintf(stderr, "cannot read %s\n", path);
        exit(3);
    }
    fclose(f);
}

static void copy_to(const char *from, const char *to)
{
    char cmd[2048];
    snprintf(cmd, sizeof cmd, "cp '%s' '%s'", from, to);
    if (system(cmd))
        exit(3);
}

static void make_pack(const char *zip_a, const char *zip_b, int extra)
{
    static tp_theme a, b;
    peek(zip_a, &a);
    peek(zip_b, &b);
    mkdir("sdmc:", 0755);
    mkdir("sdmc:/theme-pack", 0755);
    int n = 3 + extra;
    tp_content recs[FAKE_ICONS] = {{0, 0, 0, 0x1000, {1}}, {2, 2, 0x4000, 0x2000, {2}}, {3, 3, 0x4000, 0x3000, {3}}};
    for (int k = 3; k < n; k++)
        recs[k] = (tp_content){(uint32_t)(k + 1), (uint16_t)(k + 1), 0x4000, 0x1000u * (uint64_t)(k + 1),
                               {(uint8_t)(k + 1)}};
    FILE *f = fopen("sdmc:/theme-pack/installed.tmd", "wb");
    tp_sink sink = {f, file_sink};
    if (!f || tp_tmd(TP_TITLE_ID, 0, recs, (size_t)n, &sink) != TP_OK || fclose(f))
        exit(3);
    snprintf(names[0], sizeof names[0], "Dummy");
    snprintf(names[1], sizeof names[1], "(removed)");
    snprintf(names[2], sizeof names[2], "%s", a.name);
    snprintf(names[3], sizeof names[3], "%s", b.name);
    for (int k = 4; k < n + 1; k++)
        snprintf(names[k], sizeof names[k], "Theme %d", k);
    snprintf(theme_a_name, sizeof theme_a_name, "%s", a.name);
    memcpy(theme_a_icon, a.icon, TP_ICON_SIZE);
    memset(fake_icons, 0x11, sizeof fake_icons);
    memcpy(fake_icons[2], a.icon, TP_ICON_SIZE);
    memcpy(fake_icons[3], b.icon, TP_ICON_SIZE);
    make_archive(n);
    fake_installed = installed;
    memset(&fake_am, 0, sizeof fake_am);
    fake_am.ok = fake_am.exists = 1;
    fake_am.count = (size_t)n;
    for (int k = 0; k < n; k++) {
        fake_am.list[k] = (tp_am_content){recs[k].index, recs[k].id, recs[k].size};
        fake_am.usable[k] = 1;
    }
    fake_am.pending = PENDING_NO;
    memset(&fake_home, 0, sizeof fake_home);
    fake_home.ok = 1;               /* nothing of the pack in use or in the shuffle */
}

/* the folder view of sdmc:/Themes, opened from the Designs screen with keys, nothing selected */
static void open_folder_view(void)
{
    reset_app();
    load_pack();
    go_designs(0);
    frames_for(300);
    key(KEY_A);                     /* shows the ring on "Von SD installieren" */
    frames_for(200);
    key(KEY_A);
    frames_for(500);
}

/* the D-pad until the cursor is on file: the first press selects the first row shown whole, then up or down */
static int select_row(const char *file)
{
    for (int i = 0; i < 40; i++) {
        if (lv.selected && lv.cursor < list.count && !strcmp(list.items[lv.cursor].file, file))
            return 1;
        size_t at = 0;
        while (at < list.count && strcmp(list.items[at].file, file))
            at++;
        if (at == list.count)
            return 0;
        key(lv.selected && at < lv.cursor ? KEY_DUP : KEY_DDOWN);
        frames_for(150);
    }
    return 0;
}

/* the installed designs, opened from the Designs screen with keys: A shows the ring, down to the wide tile, A */
static void open_installed_view(void)
{
    reset_app();
    load_pack();
    go_designs(0);
    frames_for(300);
    key(KEY_A); frames_for(200);
    key(KEY_DDOWN); frames_for(150);
    key(KEY_A); frames_for(500);
}

/* the row of the installed design with content index, or -1 */
static long design_row_of(uint16_t index)
{
    for (size_t i = 0; i < designs.count; i++)
        if (designs.rows[i].index == index)
            return (long)i;
    return -1;
}

/* the one-design screen of index, all of the pack installed again first */
static void design_screen(uint16_t index)
{
    reset_app();
    for (size_t j = 0; j < fake_am.count; j++)
        fake_am.usable[j] = 1;
    load_pack();
    designs_list(&designs, &pv, music_of);
    designs_from = pv.themes;
    rows_of = ROWS_DESIGNS;
    design_index = index;
    open_selected = 0;
    screen = SCR_DESIGN;
    frames_for(500);
}

/* ---- 0.9.1: the bottom screen frame by frame (host/main_stubs.c), from trace_start on: the screen and the job,
 *      how many things and how many rows or buttons were drawn on it, when the transition started, whether the top
 *      screen showed a job's bar; 0.9.2: whether the loader was drawn and how far, the list's rows, and how far the
 *      screen had come in */
typedef struct { int screen, job, things, elements; u64 since; int busy, selections; size_t rows; float cover, shown, entered; } traced_frame;
#define TRACE_MAX 4096
static traced_frame trace[TRACE_MAX];
static int traced;
static u64 trace_since;

static int marks_drawn(char kind)
{
    int e = 0;
    for (int k = 0; k < stub_marks; k++)
        e += stub_mark_kind[k] == kind;
    return e;
}

static void trace_frame(void)
{
    if (traced < TRACE_MAX)
        trace[traced++] = (traced_frame){screen, job, stub_bottom_things, marks_drawn('e'), shown_since, stub_busy_top,
                                         marks_drawn('s'), list_count(), stub_loader ? stub_loader_cover : 0,
                                         stub_loader ? stub_loader_shown : 0, stub_entered};
}

static void trace_start(void)
{
    traced = 0;
    trace_since = shown_since;
    stub_frame_done = trace_frame;
}

/* What the trace saw: frames whose bottom screen was empty; frames that started the transition (a transition's first
 * frame is the one drawn at 0; its clock is set again at the second frame, which is no start), and of those the ones
 * on the screen the frame before showed (a job restarting it); frames while a job ran. */
typedef struct { int empty, restarts, restarts_same, job_frames; } trace_sum;
static trace_sum trace_check(void)
{
    trace_sum t = {0, 0, 0, 0};
    for (int k = 0; k < traced; k++) {
        t.empty += trace[k].things == 0;
        t.job_frames += trace[k].job != JOB_NONE;
        if (trace[k].entered == 0.0f) {
            t.restarts++;
            t.restarts_same += k && trace[k].screen == trace[k - 1].screen;
        }
    }
    stub_frame_done = NULL;
    return t;
}

/* The job frames of the trace: all on screen s with at least `elements` rows or buttons drawn on the bottom screen
 * (under the loader, once it has come); how many, 0 if one is not. */
static int job_frames_show(int s, int elements)
{
    int n = 0;
    for (int k = 0; k < traced; k++)
        if (trace[k].job != JOB_NONE) {
            if (trace[k].screen != s || trace[k].elements < elements)
                return 0;
            n++;
        }
    return n;
}

/* 0.9.2, what the trace saw of the loader, whose cover takes the bottom screen's content away and whose ring shows
 * in its place. runs: how often a job began (one load, never a second one after it); loader: job frames with the
 * loader; before: the job's frames before the first of them; stray: frames with the ring but no job, or with a
 * job's bar on the top screen; jumps: frames in which the cover or the ring moved further than their curve allows
 * in one frame of 16 ms (ease.h, ease_out: at most twice the even step; the cover going from whole to nothing at a
 * change of screen is that screen's transition taking over on the bare background, not a jump); bare: at the job's last frame the content was gone and the ring all but faded;
 * back: frames after the job in which the cover was still going back; after: the first frame after the job (-1:
 * none). */
typedef struct { int runs, loader, before, stray, jumps, bare, back, after; } loader_sum;
static loader_sum loader_check(void)
{
    loader_sum l = {0, 0, 0, 0, 0, 0, 0, -1};
    const float step = 2 * 16.0f / LOADER_FADE_MS + 0.001f, ring_step = 2 * 16.0f / LOADER_OUT_MS + 0.001f;
    const float gone = 0.99f, faded = 0.03f;    /* the curves' last frames: 0.992 or more of the cover, 0.02 or less */
    for (int k = 0; k < traced; k++) {
        const traced_frame *f = &trace[k], *p = k ? &trace[k - 1] : NULL;
        int in_job = f->job != JOB_NONE, was_job = p && p->job != JOB_NONE;
        l.runs += in_job && !was_job;
        l.loader += in_job && (f->cover > 0 || f->shown > 0);
        l.before += in_job && !l.loader;
        l.stray += (f->shown > 0 && !in_job) || f->busy;
        if (p) {
            int taken_over = f->screen != p->screen && p->cover >= gone && p->shown <= faded && f->cover == 0;
            l.jumps += !taken_over && fabsf(f->cover - p->cover) > step;
            l.jumps += fabsf(f->shown - p->shown) > ring_step;
        }
        if (in_job)
            l.bare = f->cover >= gone && f->shown <= faded;
        if (!in_job && was_job)
            l.after = k;
        l.back += !in_job && f->cover > 0;
    }
    return l;
}
/* the job frames a loader waits before it comes: those drawn up to LOADER_WAIT_MS after the job began, 16 ms apart */
#define WAIT_FRAMES (LOADER_WAIT_MS / 16 + 1)

/* how many entries a folder of the fake card holds */
static int entries(const char *path)
{
    int n = 0;
    DIR *d = opendir(path);
    for (struct dirent *e; d && (e = readdir(d));)
        n += strcmp(e->d_name, ".") && strcmp(e->d_name, "..");
    if (d)
        closedir(d);
    return n;
}

/* The last frame's bottom screen: one selection, at want, drawn after every row and button and before what covers
 * them (the corner button, the scroll bar, the bar, the popup). */
static int selection_last(rect want)
{
    int s = -1;
    for (int k = 0; k < stub_marks; k++)
        if (stub_mark_kind[k] == 's') {
            if (s >= 0)
                return 0;
            s = k;
        }
    if (s < 0)
        return 0;
    for (int k = 0; k < stub_marks; k++)
        if ((k > s && stub_mark_kind[k] == 'e') || (k < s && stub_mark_kind[k] == 'o'))
            return 0;
    rect r = stub_mark_at[s];
    return r.x == want.x && r.y == want.y && r.w == want.w && r.h == want.h;
}

/* A pixel's alpha in the atlas as art.py's tiled() stores it: 8x8 tiles, Z order inside, A first. */
static int atlas_alpha(const uint8_t *atlas, int x, int y)
{
    int k = ((((y >> 3) * (ART_ATLAS_W >> 3) + (x >> 3)) << 6) | (x & 1) | ((y & 1) << 1) | ((x & 2) << 1) |
             ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3));
    return atlas[4 * k];
}

static const scan_item *row(const char *file)
{
    for (size_t i = 0; i < list.count; i++)
        if (!strcmp(list.items[i].file, file))
            return &list.items[i];
    return NULL;
}

/* 0.9.2: lines written to the log while a delete's job runs but not at once to the card (there must be none: only
 * the job writes then, and the log is strict); and every "motion:" line, each cut to its first 80 characters (AN:
 * the installing screen's line is 64 long once it shows for 10 s or more) */
extern int stub_strict;
static int loose_in_delete;
static char motion_lines[1024];
static void watch_log(const char *line)
{
    loose_in_delete += (job == JOB_DELETE_THEME || job == JOB_DELETE_FILE) && !stub_strict;
    if (!strncmp(line, "motion:", 7)) {
        size_t n = strlen(motion_lines);
        snprintf(motion_lines + n, sizeof motion_lines - n, "%.80s\n", line);
    }
}

/* ---- 0.9.2, F7: an install through the application against install_run called here, as 0.9.1 called it */

/* the record files of the fake card as one line: each file's size and CRC-32 */
static void store_digest(char *out, size_t size)
{
    static const char *const files[] = {"installed.tmd", "installed.tmd.new", "installed.mark"};
    size_t n = 0;
    out[0] = 0;
    for (int k = 0; k < 3 && n < size; k++) {
        static uint8_t buf[0x10000];
        char path[128];
        snprintf(path, sizeof path, "sdmc:/theme-pack/%s", files[k]);
        FILE *f = fopen(path, "rb");
        size_t got = f ? fread(buf, 1, sizeof buf, f) : 0;
        if (f)
            fclose(f);
        n += (size_t)snprintf(out + n, size - n, "%s %s %u %08lx; ", files[k], f ? "is" : "missing", (unsigned)got,
                              (unsigned long)crc32(0L, buf, (uInt)got));
    }
}

static void store_copy(const char *from, const char *to)
{
    char cmd[512];
    snprintf(cmd, sizeof cmd, "rm -rf '%s' && cp -R '%s' '%s'", to, from, to);
    if (system(cmd))
        exit(3);
}

/* AM may be called only while HOME and sleep are held off and the main loop has not come round since they were
 * (the install disallows them before its first write and stays in that turn of the loop): then no key was read and
 * aptMainLoop was not asked, so nothing could interrupt it. A held worker runs many frames later; an install that
 * went back to the main loop meanwhile would have let it come round by then. */
static int install_guarded(void)
{
    return !apt_home && !apt_sleep && loops == home_off_loops && job == JOB_INSTALL;
}

static void quiet_progress(void *ctx, int step, uint64_t done, uint64_t total)
{
    (void)ctx, (void)step, (void)done, (void)total;
}

/* the log's lines while the install's job runs: all at once to the card, and in the order of 0.9.1 */
static char install_log[4096];
static int loose_in_install;
static void watch_install_log(const char *line)
{
    if (job != JOB_INSTALL)
        return;
    loose_in_install += !stub_strict;
    size_t n = strlen(install_log);
    snprintf(install_log + n, sizeof install_log - n, "%.24s\n", line);
}

/* 0.9.2, case 43: the footers of the frames drawn while a theme is checked that are not the status line */
static char foot_want[160];
static int foot_frames, foot_other;
static void foot_frame(void)
{
    if (job != JOB_CHECK)
        return;
    foot_frames++;
    foot_other += strcmp(stub_foot, foot_want) != 0;
}

/* 0.9.3, case 83: the scheme and the colours each frame was drawn with, as the drawing layer had them at its end */
static int look_frames, look_scheme = -1, look_mismatch;
static const uint32_t *look_colours;
static void look_frame(void)
{
    look_frames++;
    look_scheme = stub_scheme;
    look_colours = ui_colours;
    look_mismatch += stub_scheme != settings_scheme() || ui_colours != ART_STOCK.colour[settings_scheme()];
}

/* A pixel of an image texture as art.py's tiled() stores it (8x8 tiles, Z order inside; A, B, G, R): c 0 its alpha,
 * 1 blue, 2 green, 3 red. */
static int texel(const uint8_t *tex, int width, int x, int y, int c)
{
    int k = ((((y >> 3) * (width >> 3) + (x >> 3)) << 6) | (x & 1) | ((y & 1) << 1) | ((x & 2) << 1) |
             ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3));
    return tex[4 * k + c];
}

/* 0.9.3, case 75: the WCAG 2 contrast ratio of two colours given as 0xRRGGBB */
static double wcag_luminance(uint32_t c)
{
    double y = 0;
    static const double share[3] = {0.2126, 0.7152, 0.0722};
    for (int k = 0; k < 3; k++) {
        double v = ((c >> (16 - 8 * k)) & 255) / 255.0;
        y += share[k] * (v <= 0.04045 ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4));
    }
    return y;
}

static double wcag_contrast(uint32_t a, uint32_t b)
{
    double ya = wcag_luminance(a), yb = wcag_luminance(b);
    return ((ya > yb ? ya : yb) + 0.05) / ((ya > yb ? yb : ya) + 0.05);
}

/* a sprite's pixels in two unpacked atlases: how many differ */
static int sprite_differs(const uint8_t *one, const uint8_t *two, int id)
{
    const art_sprite *sp = art_of(&ART_STOCK, id);
    int n = 0;
    for (int y = 0; y < sp->h; y++)
        for (int x = 0; x < sp->w; x++) {
            int differs = 0;
            for (int c = 0; c < 4; c++)
                differs |= texel(one, ART_ATLAS_W, sp->x + x, sp->y + y, c) != texel(two, ART_ATLAS_W, sp->x + x, sp->y + y, c);
            n += differs;
        }
    return n;
}

static int fails, checks;
#define EXPECT(cond, ...) do { int ok_ = (cond); printf("%s  ", ok_ ? "ok  " : "FAIL"); printf(__VA_ARGS__); printf("\n"); checks++; if (!ok_) fails++; } while (0)

/* ---- 0.9.2, the audio lead's (cases 50 to 59): the sounds as host/main_stubs.c counts them, what the preview
 *      handed to the drawing and drew, an installed content's files as the test sets them; synthetic themes
 *      (host/synth.h) */
#include "synth.h"
extern int stub_sfx[SFX_COUNT], stub_sfx_last, stub_menu_paused, stub_theme_music, stub_theme_starts;
extern const uint8_t *stub_theme_file;
extern const uint8_t *fake_content_body, *fake_content_music;
extern size_t fake_content_body_size, fake_content_music_size;
extern int fake_content_reads, fake_content_index;
extern int stub_wall_width[2], stub_wall_sets, stub_bar_y;
extern unsigned stub_wall_first[2];
extern char stub_wall_drawn[2];
extern float stub_wall_x[2];
extern uint8_t stub_wall_rgb[2][6];
extern int stub_menu_track, stub_menu_sets, stub_sounds_on, stub_work_depth, stub_work_begins, stub_work_ends;
extern int stub_settings_button, stub_choices, stub_list_count, stub_list_chosen, stub_close_depth, stub_close_ends;
extern char stub_row_label[][64], stub_row_value[][96], stub_row_note[][24], stub_list_name[][96];

/* the application's start as main() runs it from the settings on: the file read before the first frame, the pack and
 * sound (start_up), then its first two frames (the menu music begins on the second, ST-B) */
static void start_again(void)
{
    settings_start();
    start_up();
    frame();
    frame();
}

/* ST-B: the start's frames as drawn: how many, whether the pack was taken by then, the first two's black, transition,
 * screen and music; the most black since st_max was cleared; the "start:" lines written */
extern float stub_black;
static int st_frames, st_untaken, st_screen[2], st_sets[2], st_lines, st_slow_ms;
static float st_black[2], st_entered[2], st_max;
static void st_frame(void)
{
    st_untaken += next_pv.count != 0 || next_pv.tmd != NULL;
    if (st_frames < 2) {
        st_black[st_frames] = stub_black, st_entered[st_frames] = stub_entered;
        st_screen[st_frames] = screen, st_sets[st_frames] = stub_menu_sets;
    }
    if (st_frames++ == 0)
        sim_now += (u64)st_slow_ms;         /* the first frame of all is slow: the atlas and the glass sheet unpack */
    if (stub_black > st_max)
        st_max = stub_black;
}
/* ST-B2: the pack's reads (read_pack's "pack: " line), and how many of them ran on a thread */
static int st_reads, st_reads_on_thread;
static void st_log(const char *line)
{
    st_lines += !strncmp(line, "start: ", 7);
    if (!strncmp(line, "pack: state ", 12)) {
        st_reads++;
        st_reads_on_thread += on_thread;
    }
}

/* the settings file as it lies on the card, read by the application's own reader */
static settings on_card(void)
{
    settings s;
    if (!settings_load(SETTINGS_PATH, &s, "none.bcstm"))
        snprintf(s.music, sizeof s.music, "not read");
    return s;
}

/* the deepest work was while frames were drawn (an install draws its frames from inside one call), and installing
 * frames drawn without work under way once it had begun (the first is drawn before the job starts) */
static int work_deepest, steps_unworked, steps_seen_work;
static void watch_work(void)
{
    if (stub_work_depth > work_deepest)
        work_deepest = stub_work_depth;
    if (stub_pour_frames != steps_seen_work) {
        steps_seen_work = stub_pour_frames;
        /* G2-L2b: "Installiert" goes on with the picture after the work's end; its frames are not the installing
         * screen's */
        steps_unworked += work_deepest > 0 && stub_work_depth == 0 && screen != SCR_DONE;
    }
}

static void write_file(const char *path, const uint8_t *data, size_t size)
{
    FILE *f = fopen(path, "wb");
    if (!f || fwrite(data, 1, size, f) != size || fclose(f))
        exit(3);
}

/* how often sound id played since the last call for it */
static int played(int id)
{
    static int seen[SFX_COUNT];
    int n = stub_sfx[id] - seen[id];
    seen[id] = stub_sfx[id];
    return n;
}

static void forget_sounds(void)
{
    for (int id = 0; id < SFX_COUNT; id++)
        played(id);
}

/* G2-L2b: the Done screen after an install goes on with the installing screen's end on its top screen.
 * The last frame's top screen held a piece of kind (main_stubs.c: 'p' the installing screen's picture, 'D' the Done
 * card) */
extern char stub_piece_kind[64];        /* main_stubs.c's STUB_PIECES */
extern int stub_piece_bottom[64], stub_pieces;
extern rect stub_piece_at[64];
extern struct stub_pour_given {
    const char *title, *done_title, *home, *step0;
    const uint8_t *icon;
    int symbol;
    char name[96], what[96];
} stub_pour_given;
static int top_has(char kind)
{
    for (int k = 0; k < stub_pieces; k++)
        if (!stub_piece_bottom[k] && stub_piece_kind[k] == kind)
            return 1;
    return 0;
}

/* a frame's top screen as the stub saw it: its pieces in order, the transition's value, the picture's model and what
 * it was given; and the model's clock and glow, the bottom screen's cover */
typedef struct {
    int pieces;
    char kind[64];
    rect at[64];
    float entered, glow, cover;
    const pour *model;
    struct stub_pour_given given;
    u64 clock;
} top_shot;

static void top_keep(top_shot *t)
{
    memset(t, 0, sizeof *t);
    for (int k = 0; k < stub_pieces; k++)
        if (!stub_piece_bottom[k])
            t->kind[t->pieces] = stub_piece_kind[k], t->at[t->pieces++] = stub_piece_at[k];
    t->entered = stub_entered;
    t->cover = stub_loader ? stub_loader_cover : 0;
    if (top_has('p')) {
        t->model = stub_pour;
        t->given = stub_pour_given;
        t->clock = stub_pour->now;
        t->glow = pour_glow(stub_pour);
    }
}

/* the same but for the clock (and the glow that goes by it) and the bottom screen */
static int top_same(const top_shot *a, const top_shot *b)
{
    return a->pieces == b->pieces && !memcmp(a->kind, b->kind, sizeof a->kind) && !memcmp(a->at, b->at, sizeof a->at) &&
           a->entered == b->entered && a->model == b->model && a->given.title == b->given.title &&
           a->given.done_title == b->given.done_title && a->given.home == b->given.home &&
           a->given.step0 == b->given.step0 && a->given.icon == b->given.icon && a->given.symbol == b->given.symbol &&
           !strcmp(a->given.name, b->given.name) && !strcmp(a->given.what, b->given.what);
}

/* every frame from watch_end_begin on: the last one before the Done screen end_screen, the first one on it, and of
 * those on it: how many, how many with the picture and how many with the Done card on top, the lowest transition
 * value, the glow's lowest and its biggest step from frame to frame, the bottom screen's cover rising anywhere */
static int end_screen;
static top_shot end_last, end_first, end_now_shot;
static int end_frames, end_pours, end_cards, end_cover_rose;
static float end_entered_low, end_glow_low, end_glow_step;
static void watch_end(void)
{
    if (screen != end_screen) {
        top_keep(&end_last);
        return;
    }
    top_shot was = end_now_shot;
    top_keep(&end_now_shot);
    if (!end_frames++) {
        end_first = end_now_shot;
        end_entered_low = end_glow_low = 1;
    } else {
        float step = fabsf(end_now_shot.glow - was.glow);
        if (step > end_glow_step)
            end_glow_step = step;
        end_cover_rose |= end_now_shot.cover > was.cover;
    }
    end_pours += top_has('p');
    end_cards += top_has('D');
    if (end_now_shot.entered < end_entered_low)
        end_entered_low = end_now_shot.entered;
    if (end_now_shot.glow < end_glow_low)
        end_glow_low = end_now_shot.glow;
}

static void watch_end_begin(int done_screen)
{
    end_screen = done_screen;
    end_frames = end_pours = end_cards = end_cover_rose = 0;
    end_glow_step = 0;
    stub_frame_done = watch_end;
}

/* the last thing the bottom screen drew is the bar, across it (ui_bar, or the two tabs) */
static int bar_drawn_last(void)
{
    rect at = ui_bar_rect();
    return stub_marks > 0 && stub_mark_kind[stub_marks - 1] == 'o' && stub_mark_at[stub_marks - 1].y == at.y &&
           stub_mark_at[stub_marks - 1].w == at.w;
}

/* SND-1 (C163): every frame from drops_begin on, against the installing picture's model: the pieces that landed by its
 * clock since the frame before and the drops heard in that frame. Right is one drop in a frame in which pieces
 * landed (one for all of them) and none in any other; with the sounds off none at all. drops_ramp: while the job
 * of that kind is held (worker_frames), its level rises by this much a frame, as a worker's bar would, so that pieces
 * fall and land during the work; drops_mute: the settings' "Töne" are off; drops_fail_screen: from the first frame
 * on it (the work failed) the pieces still in the air are counted, and every drop heard after. */
static const pour *drops_model;
static int drops_job, drops_fail_screen, drops_mute;
static float drops_ramp;
static u64 drops_clock, drops_late;     /* the clock of the frame before; the longest from a landing to its frame */
static int drops_was, drops_heard, drops_pieces, drops_frames, drops_shared, drops_wrong, drops_before_first;
static int drops_failed, drops_in_air, drops_after_fail;
static u64 drops_first;
static void drops_watch(void)
{
    if (drops_mute && stub_sounds_on) {     /* as the settings' switch does it */
        prefs.sounds = 0;
        sounds_changed();
    }
    if (drops_ramp > 0 && job == drops_job && !job_over && job_fraction < 0.95f)
        job_fraction += drops_ramp;
    int heard = stub_sfx[SFX_DROP] - drops_was, landed = 0;
    drops_was = stub_sfx[SFX_DROP];
    for (int i = 0; i < drops_model->gone; i++) {
        u64 at = pour_lands(drops_model, i);
        if (at > drops_clock && at <= drops_model->now) {
            landed++;
            if (drops_model->now - at > drops_late)
                drops_late = drops_model->now - at;
            if (!drops_first || at < drops_first)
                drops_first = at;
        }
    }
    if (heard && (!drops_first || drops_model->now < drops_first))
        drops_before_first++;
    drops_clock = drops_model->now;
    drops_heard += heard;
    drops_pieces += landed;
    drops_frames += landed > 0;
    drops_shared += landed > 1 ? landed - 1 : 0;
    drops_wrong += heard != (landed > 0 && !drops_mute);
    if (drops_failed)
        drops_after_fail += heard;
    else if (drops_fail_screen && screen == drops_fail_screen) {
        drops_failed = 1;
        for (int i = 0; i < drops_model->gone; i++)
            drops_in_air += pour_lands(drops_model, i) > drops_model->now;
    }
}

static void drops_begin(const pour *model)
{
    drops_model = model;
    drops_clock = model->now;
    drops_late = drops_first = 0;
    drops_was = stub_sfx[SFX_DROP];
    drops_heard = drops_pieces = drops_frames = drops_shared = drops_wrong = drops_before_first = 0;
    drops_failed = drops_in_air = drops_after_fail = 0;
    stub_frame_done = drops_watch;
}

static void drops_end(void)
{
    stub_frame_done = NULL;
    drops_ramp = 0;
    drops_fail_screen = 0;
    if (drops_mute) {
        drops_mute = 0;
        prefs.sounds = 1;
        sounds_changed();
    }
}

/* 66: an install of a.zip from its check screen, its work taking install_hold frames (20); when (1: during the install's job, 2:
 * during the end hold after it, when no input is read either) the stylus comes down at x, y, or keys are held, and
 * stays so until "Installiert" shows. 1 when it showed. */
static int done_when, done_x, done_y, done_frames, done_seen_job;
static int install_hold = 20;
static u32 done_keys;
static void done_tick(void)
{
    held_tick();
    done_seen_job |= job == JOB_INSTALL;
    int now_put = (done_when == 1 && job == JOB_INSTALL && ++done_frames == 5) ||
                  (done_when == 2 && done_seen_job && job == JOB_NONE && screen == SCR_CONFIRM);
    if (!now_put)
        return;
    done_when = 0;
    if (done_keys)
        sim_keys |= done_keys;
    else
        stylus(done_x, done_y);
}
static int install_held(const char *zip_a, const char *zip_b, int when, int x, int y, u32 keys)
{
    remove("sdmc:/theme-pack/installed.mark");  /* the installs before left the mark high */
    make_pack(zip_a, zip_b, 0);
    for (size_t j = 0; j < fake_am.count; j++)      /* theme 2 (a.zip's) deleted in HOME: a.zip can be installed */
        fake_am.usable[j] = fake_am.list[j].index != 2;
    mkdir("sdmc:/Themes", 0755);
    copy_to(zip_a, "sdmc:/Themes/a.zip");
    close_theme();
    reset_app();
    load_pack();
    go_designs(0);
    frames_for(200);
    open_confirm("a.zip", "sdmc:/Themes", SCR_DESIGNS);
    for (int i = 0; i < 60 && screen != SCR_CONFIRM; i++)
        frame();
    frames_for(POPUP_GUARD_MS + 100);
    done_when = when, done_x = x, done_y = y, done_keys = keys, done_frames = done_seen_job = 0;
    worker_frames = install_hold;
    stub_each_frame = done_tick;
    tap((int)(GO_X + GO_W / 2), (int)(GO_Y + GO_H / 2), 48);
    for (int i = 0; i < 400 && screen != SCR_DONE; i++)
        frame();
    stub_each_frame = held_tick;
    worker_frames = 0;
    return screen == SCR_DONE && may_clean;
}

/* G1-R5b: a key comes down in the 5th frame of a job of kind through_job (run by job_here or by the main loop) and
 * stays down */
static int through_job, through_frames;
static u32 through_key;
static void through_tick(void)
{
    held_tick();
    if (job == through_job && ++through_frames == 5)
        sim_keys |= through_key;
}

/* ... the job taking 20 frames: start it with start (a key, or a tap at x, y), run up to ms */
static void key_through(int kind, u32 k, u32 start, u64 ms)
{
    through_job = kind, through_key = k, through_frames = 0;
    worker_frames = 20;
    stub_each_frame = through_tick;
    key(start);
    frames_for(ms);
    stub_each_frame = held_tick;
    worker_frames = 0;
}

/* G1-R5b: a key held from before a screen or a question showed: let go after a while, then pressed again; acted: what
 * the press did then (shown false before it) */
static int still_until_again(u32 k, int (*acted)(void))
{
    frames_for(POPUP_GUARD_MS + 300);
    int held = (sim_keys & k) != 0, still = !acted();
    sim_keys &= ~k;
    frames_for(300);
    still = still && !acted();
    key(k);
    frames_for(800);
    return held && still && acted();
}

static int r5b_target;
static int left_target(void) { return screen != r5b_target || ring; }
static int note_closed(void) { return popup != POP_NOTE; }
static int ask_closed(void) { return popup == POP_NONE && popup_then == POP_NONE; }

/* G1-R7: a question opened by A: A pressed and let go, then the frames up to its first one drawn; how far it had
 * opened there (-1: none came) */
static float ask_opened(int kind)
{
    sim_keys |= KEY_A;
    frame();
    sim_keys &= ~KEY_A;
    for (int i = 0; i < 40 && (popup != kind || popup_fresh); i++)
        frame();
    return popup == kind && !popup_fresh ? stub_popup_shown : -1;
}

static int r7_kind;
static int r7_ring(void) { return popup == r7_kind && pop_focus.ring; }

/* G1-R7: a key acts on a question only from POPUP_GUARD_MS after its first frame, as a touch does; a press while it
 *     opens is not kept. to: up to the A that opens it; removed: what it removed since to */
static void keys_wait_for_question(const char *what, int kind, void (*to)(void), int (*removed)(void))
{
    r7_kind = kind;
    to();
    float at = ask_opened(kind);
    key(KEY_DRIGHT);
    key(KEY_DRIGHT);
    sim_keys |= KEY_A;
    frame();
    int none = popup == kind && pending == HIT_NONE && !pop_focus.ring && !removed();
    sim_keys &= ~KEY_A;
    frames_for(600);
    EXPECT(at > 0 && at < 1.0f && none && popup == kind && !pop_focus.ring && !removed(), "G1-R7 %s: D-pad right "
           "twice and A as the question opens (from %.2f): nothing pressed, no cursor, still asked (popup %d, ring %d, "
           "%d removed)", what, (double)at, popup, pop_focus.ring, removed());
    key(KEY_DRIGHT);
    int left = popup == kind && pop_focus.ring && pop_focus.at == 0;
    key(KEY_DRIGHT);
    int right = popup == kind && pop_focus.ring && pop_focus.at == 1;
    key(KEY_A);
    frames_for(800);
    EXPECT(left && right && popup != kind && removed() == 1, "G1-R7 %s: once it is open the same keys act: the cursor "
           "on Abbrechen, then on the right button, A removes once (left %d, right %d, %d removed)", what, left, right,
           removed());
    to();
    ask_opened(kind);
    key(KEY_B);
    frames_for(600);
    int stayed = popup == kind && !removed();
    key(KEY_B);
    frames_for(400);
    EXPECT(stayed && popup == POP_NONE && popup_then == POP_NONE && !removed(), "G1-R7 %s: B as the question opens "
           "does not cancel; once it is open B cancels (stayed %d, popup %d)", what, stayed, popup);
    to();
    ask_opened(kind);
    sim_keys |= KEY_DRIGHT;
    frame();
    EXPECT(still_until_again(KEY_DRIGHT, r7_ring), "G1-R7 %s: the D-pad down as the question opens and held: no "
           "cursor until let go and pressed again (ring %d)", what, pop_focus.ring);
    key(KEY_B);
    frames_for(400);
}

static int r7_deletes;
static void to_theme_ask(void)
{
    design_screen(2);
    key(KEY_A);
    frames_for(200);
    r7_deletes = fake_deletes;
}
static int theme_removed(void) { return fake_deletes - r7_deletes; }

static void to_file_ask(void)
{
    confirm_installed("r7.zip");
    key(KEY_A); frames_for(200);
    key(KEY_DRIGHT); frames_for(200);
}
static int file_removed(void) { return !victim_exists(); }

/* ---------------------------------------------------------------- FB-E: problems, refusals and empty states
 * Each draws its explanation in the information box on the bottom screen and no wrapped text beside it
 * (ui_info_box, text only since C135; main_stubs.c counts both); a refusal's note has a sign, a success's none.
 * FB-E2: on the pair of screens with a refusing note at least one sign and none on both, the top screen's (the
 * card's head and the footer's glyph, main_stubs.c keeps their sprites) against the note's as drawn. The meanings are
 * this test's own, not ui.h's rule: the head's and the footer's warning is the note's, whatever its size. */
extern int stub_info_boxes, stub_bottom_blocks, stub_popup_sign;
extern int stub_top_glyph[8], stub_top_glyphs;
extern char stub_info_text[600];
static int sign_meaning(int sprite)
{
    if (sprite == ART_WARN || sprite == ART_WARN_FOOT || sprite == ART_INFO_WARN)
        return 1;
    if (sprite == ART_FOLDER_BIG || sprite == ART_FOLDER_FOOT)
        return 2;
    if (sprite == ART_HOME_FOOT)
        return 4;
    if (sprite == ART_INFO_SD)
        return 8;
    return sprite >= 0 ? 64 : 0;
}
static int top_meanings(void)
{
    int m = 0;
    for (int k = 0; k < stub_top_glyphs; k++)
        m |= sign_meaning(stub_top_glyph[k]);
    return m;
}
static int sign_once(int bottom)
{
    int top = top_meanings(), low = sign_meaning(bottom);
    return (top || low) && !(top & low);
}
static void fb_e_drawn(const char *what, int blocks)
{
    stub_info_boxes = stub_bottom_blocks = 0;
    draw();
    EXPECT(stub_info_boxes == 1 && stub_bottom_blocks == blocks && stub_info_text[0],
           "FB-E %s: the explanation in one box, no text beside it (boxes %d, blocks %d)", what, stub_info_boxes,
           stub_bottom_blocks);
}
/* a refusal's note (refuses): its sign once on the two screens as a box's; a success's note without one */
static void fb_e_note(const char *what, int sign, int refuses)
{
    frames_for(POPUP_IN_MS + 100);
    stub_popup_sign = -2;
    draw();
    EXPECT(popup == POP_NOTE && stub_popup_sign == sign, "FB-E %s: a note with sign %d (drawn with %d)", what, sign,
           stub_popup_sign);
    if (refuses)
        EXPECT(sign_once(stub_popup_sign), "FB-E2 %s: a sign once on the two screens (top's motifs %d, note %d)",
               what, top_meanings(), stub_popup_sign);
    popup = POP_NONE;
    frames_for(POPUP_OUT_MS + 100);
}
static void fb_e_states(void)
{
    printf("-- FB-E: problems, refusals and empty states in one form\n");
    reset_app();
    show_problem(T_SCAN_QR, text(lang, T_CAMERA_FAILED), "", 0);
    fb_e_drawn("the camera failed", 0);
    EXPECT(!strcmp(stub_info_text, text(lang, T_TRY_AGAIN)), "FB-E a problem without a text of its own: try again");
    show_problem(T_QR_FOUND, text(lang, T_NO_WIFI), text(lang, T_NO_WIFI_HINT), 0);
    fb_e_drawn("no WLAN", 0);
    show_problem(T_SD_CARD, text_result(lang, TP_E_NOMEM), "", 0);
    fb_e_drawn("no memory for a folder", 0);
    int was = pv.state;
    pv.state = PACK_NOT_INSTALLED;
    pack_ready();
    fb_e_drawn("no theme pack (the head's warning, the box without it)", 0);
    show_note(ART_INFO_WARN, "Refused", "Why.");
    fb_e_note("a refusal over a problem's head (none today: the rule all the same)", -1, 1);
    pv.state = was;
    memset(&theme, 0, sizeof theme);
    theme_open = 1; theme_result = TP_OK;
    memset(&plan, 0, sizeof plan);
    plan.state = PLAN_FULL_THEMES;
    screen = SCR_CONFIRM;
    fb_e_drawn("the check refused by the pack", 0);
    theme_open = 0; theme_result = TP_E_ARG;
    fb_e_drawn("the check of a file that is no theme (the head's warning)", 0);
    scan_free(&list);
    screen = SCR_LIST;
    fb_e_drawn("an empty folder (the head's folder)", 0);
    size_t had = designs.count;
    designs.count = 0;
    screen = SCR_INSTALLED;
    fb_e_drawn("no design installed (the footer's house)", 0);
    designs.count = had;
    snprintf(scan_note, sizeof scan_note, "%s", "Hallo");
    screen = SCR_SCAN;
    fb_e_drawn("a QR code without a web address (the hint above it)", 1);
    scan_note[0] = 0;
    screen = SCR_DESIGNS;
    show_note(ART_INFO_WARN, "Refused", "Why.");
    fb_e_note("a refusal", ART_INFO_WARN, 1);
    show_note(-1, "Done", NULL);
    fb_e_note("a success", -1, 0);
    reset_app();
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: test_main ZIP_A ZIP_B [-v]\n");
        return 3;
    }
    stub_verbose = argc > 3;
    stub_each_frame = held_tick;
    const char *base = getenv("TMPDIR");
    snprintf(dir, sizeof dir, "%s/test-main-XXXXXX", base && *base ? base : "/tmp");
    if (!mkdtemp(dir)) {
        perror("mkdtemp");
        return 3;
    }
    lang = 0;

    /* 1. 0.8.0's double tap on "Datei löschen" at the tile's bottom rows (y 190: in the tile, and in the popup's
     *    "Löschen"), second touch-down g ms after the first; the first tap held 48 ms. */
    printf("-- double tap on \"Datei löschen\" (x 236, y 190)\n");
    int first_delete_gap = -1;
    for (int g = 64; g <= 640; g += 16) {
        confirm_installed("double.zip");
        tap(236, 190, 48);                      /* frames: down, 3 held, up */
        u64 since_first = 48 + 16;
        if ((u64)g > since_first)
            frames_for((u64)g - since_first);
        tap(236, 190, 48);
        frames_for(400);
        if (!victim_exists() && first_delete_gap < 0)
            first_delete_gap = g;
        if (g <= 450)
            EXPECT(victim_exists(), "gap %3d ms: file kept (popup %d)", g, popup);
        unlink(victim);
    }
    printf("     first gap that deletes: %d ms (PRESS_MS %d + first frame + POPUP_GUARD_MS %d)\n", first_delete_gap,
           PRESS_MS, POPUP_GUARD_MS);
    EXPECT(first_delete_gap >= PRESS_MS + POPUP_GUARD_MS, "no delete below PRESS_MS + POPUP_GUARD_MS");

    /* 2. a second touch that came down during the tile's push and is held through the opening, then lifted on
     *    "Löschen" long after the guard */
    printf("-- touch held through the opening\n");
    confirm_installed("held.zip");
    tap(236, 190, 32);
    stylus(236, 200);                           /* during the push (110 ms) */
    frames_for(1500);
    EXPECT(popup == POP_ASK_FILE, "question open while the stylus rests (popup %d)", popup);
    lift(); frame(); frames_for(400);
    EXPECT(victim_exists() && popup == POP_ASK_FILE, "lifted on Löschen after 1.5 s: nothing pressed");
    tap(236, 200, 48); frames_for(400);
    EXPECT(!victim_exists(), "the next deliberate tap on Löschen deletes (expected)");
    unlink(victim);

    /* 3. a touch that comes down inside the guard and is held past it, lifted on "Löschen" */
    printf("-- touch down inside the guard, held past it\n");
    confirm_installed("inside.zip");
    tap(236, 120, 48);                          /* the tile's middle, not under the popup's button */
    frames_for(200);
    EXPECT(popup == POP_ASK_FILE, "question open");
    stylus(236, 200); frames_for(800); lift(); frame(); frames_for(400);
    EXPECT(victim_exists(), "down inside the guard, held 800 ms, lifted on Löschen: nothing");
    stylus(60, 200); frames_for(48); stylus(236, 200); frames_for(48); lift(); frame(); frames_for(400);
    EXPECT(victim_exists(), "down on Abbrechen after the guard, slid to Löschen, lifted: nothing");
    unlink(victim);

    /* 4. keys only: A pressed over and over from the tile, also B, L, R, START, X, Y, held A */
    printf("-- keys without the D-pad\n");
    confirm_installed("keys.zip");
    focus = 1;
    for (int i = 0; i < 40; i++) { key(KEY_A); frames_for(150); }
    EXPECT(victim_exists(), "A pressed 40 times, 150 ms apart: file kept");
    const u32 keys[] = {KEY_A, KEY_B, KEY_L, KEY_R, KEY_START, KEY_X, KEY_Y, KEY_SELECT, KEY_ZL, KEY_ZR};
    unsigned seed = 7;
    for (int i = 0; i < 4000; i++) {
        seed = seed * 1103515245u + 12345u;
        key(keys[(seed >> 16) % (sizeof keys / sizeof keys[0])]);
        if ((seed >> 8) & 1) frames_for(120);
        if (screen != SCR_CONFIRM) { screen = SCR_CONFIRM; theme_open = 1; theme_result = TP_OK; plan.state = PLAN_DUPLICATE; }
    }
    EXPECT(victim_exists(), "4000 random presses of A B L R START X Y SELECT ZL ZR: file kept");
    confirm_installed("keys.zip");
    focus = 1;
    key(KEY_A); frames_for(200); key(KEY_A); frames_for(POPUP_GUARD_MS + 200);
    EXPECT(popup == POP_ASK_FILE, "A, A from the tile: the question open");
    sim_keys |= KEY_A; frames_for(3000); sim_keys &= ~KEY_A; frame(); frames_for(300);
    EXPECT(victim_exists() && popup == POP_ASK_FILE, "A held 3 s in the question: only the cursor shown, file kept");
    key(KEY_A); frames_for(300);
    EXPECT(victim_exists() && popup == POP_NONE, "the next A: Abbrechen");
    key(KEY_A); frames_for(300); key(KEY_DRIGHT); frames_for(50); key(KEY_DRIGHT); frames_for(50);
    sim_keys |= KEY_A | KEY_B; frame(); sim_keys = 0; frame(); frames_for(300);
    EXPECT(victim_exists(), "A and B in the same frame with the cursor on Löschen: B wins, file kept");
    key(KEY_A); frames_for(300); key(KEY_CPAD_RIGHT); key(KEY_CPAD_RIGHT); key(KEY_A); frames_for(300);
    EXPECT(!victim_exists(), "circle pad right twice, then A: deletes (deliberate)");
    unlink(victim);

    /* 5. after the help popup on "Installiert": a double tap on its Okay, right half, over the cleanup "Löschen" */
    printf("-- help popup on \"Installiert\": double tap on Okay\n");
    int first_reach = -1;
    for (int g = 112; g <= 640; g += 16) {
        done_with_cleanup("cleanup.zip");
        tap(294, 26, 48); frames_for(600);      /* the help button; the popup opens, the guard passes */
        int was = popup;
        tap(236, 200, 48);                       /* Okay */
        if ((u64)g > 64) frames_for((u64)g - 64);
        tap(236, 200, 48);                       /* the second tap of a double tap, on the cleanup "Löschen" */
        frames_for(400);
        if (!victim_exists() && first_reach < 0)
            first_reach = g;
        if (g <= 450)
            EXPECT(was == POP_HELP && victim_exists(), "gap %3d ms: help closed, file kept (screen %d)", g, screen);
        unlink(victim);
    }
    printf("     first gap that reaches the screen under it: %d ms\n", first_reach);

    /* 6. a note over the check screen of a theme with music that can be installed (as after "Design löschen"):
     *    double tap on Okay at y 192 (over "Ohne Musik installieren") */
    printf("-- note after \"Design löschen\": double tap on Okay\n");
    for (int g = 112; g <= 450; g += 16) {
        reset_app();
        memset(&theme, 0, sizeof theme);
        theme_open = 1; theme_result = TP_OK; theme.has_bgm = 1; theme.use_bgm = 1;
        memset(&plan, 0, sizeof plan); plan.state = PLAN_OK;
        screen = SCR_CONFIRM; confirm_from = SCR_DESIGNS;
        frames_for(200);
        show_note(-1, "Design gelöscht", "x");
        frames_for(600);
        tap(160, 192, 48); if ((u64)g > 64) frames_for((u64)g - 64); tap(160, 192, 48); frames_for(300);
        EXPECT(threads_started == 0 && pending == HIT_NONE, "gap %3d ms: no install started (job %d)", g,
               last_thread_job);
        if (threads_started) { job = JOB_NONE; worker = NULL; }
    }

    /* 7. keys: the note after "Design löschen" (as run_delete_theme leaves it: same screen, focus 0, the ring as
     *    it was, shown because the tile was reached with keys), then A twice */
    printf("-- note after \"Design löschen\" reached with keys: A, A\n");
    reset_app();
    memset(&theme, 0, sizeof theme);
    theme_open = 1; theme_result = TP_OK;
    memset(&plan, 0, sizeof plan); plan.state = PLAN_OK;   /* the theme gone: it can be installed again */
    screen = SCR_CONFIRM; confirm_from = SCR_DESIGNS;
    frames_for(200);
    ring = 1; focus = 0;
    show_note(-1, "Design gelöscht", "x");
    frames_for(300);
    key(KEY_A); frames_for(200);
    int closed = popup == POP_NONE;
    sim_keys |= KEY_A; frame(); sim_keys &= ~KEY_A;
    printf("     note closed by the first A: %d; after the second A: pending %d (HIT_GO %d)\n", closed, pending, HIT_GO);
    EXPECT(pending != HIT_GO, "A, A on the outcome note does not start an install");
    pending = pressed = HIT_NONE; frame();

    /* 8. "Design löschen" refused (HOME's data unread here): the note; a double tap on the tile's bottom rows,
     *    where the note's Okay lies, does not close it inside the guard */
    printf("-- double tap on \"Design löschen\" (x 80, y 190): the refusal note\n");
    confirm_installed("refused.zip");
    tap(80, 190, 48); frames_for(176 - 64); tap(80, 190, 48); frames_for(100);
    EXPECT(popup == POP_NOTE, "the note stays open after the second tap (popup %d)", popup);
    unlink(victim);

    /* 9. the help closed with B while the stylus rests on the cleanup "Löschen" (under the panel's text): lifting
     *    it deletes nothing */
    printf("-- help closed by B with the stylus on \"Löschen\"\n");
    done_with_cleanup("resting.zip");
    tap(294, 26, 48); frames_for(600);
    stylus(236, 150); frames_for(100);
    key(KEY_B); frames_for(800);
    EXPECT(popup == POP_NONE, "the help closed by B");
    lift(); frame(); frames_for(400);
    EXPECT(victim_exists() && screen == SCR_DONE, "lifted on \"Löschen\" 800 ms later: nothing pressed");
    tap(236, 150, 48); frames_for(400);
    EXPECT(!victim_exists(), "the next deliberate tap on \"Löschen\" deletes (expected)");
    unlink(victim);

    /* 10. keys after the help closed: the cursor is hidden, so A, A keeps the file (A on "Behalten") */
    printf("-- keys after the help closed\n");
    done_with_cleanup("keys-after.zip");
    key(KEY_X); frames_for(300);
    EXPECT(popup == POP_HELP, "X opens the help");
    key(KEY_B); frames_for(300);
    EXPECT(popup == POP_NONE && ring == 0, "B closes it, the cursor hidden");
    key(KEY_A); frames_for(200);
    EXPECT(screen == SCR_DONE && victim_exists(), "the first A only shows the cursor");
    key(KEY_A); frames_for(300);
    EXPECT(screen == SCR_DESIGNS && victim_exists(), "the second A keeps the file");
    unlink(victim);

    /* 11. 0.8.4: "Design löschen" from a theme chosen in the folder view, then back: the folder is read again, the
     *     rows of the deleted theme no longer show it installed, the scroll and the selection are kept */
    printf("-- back to the folder view after \"Design löschen\"\n");
    if (chdir(dir))
        return 3;
    make_pack(argv[1], argv[2], 0);
    mkdir("sdmc:/Themes", 0755);
    copy_to(argv[1], "sdmc:/Themes/a.zip");
    for (int i = 1; i <= 12; i++) {
        char name[64];
        snprintf(name, sizeof name, "sdmc:/Themes/m%02d.zip", i);
        copy_to(argv[2], name);
    }
    copy_to(argv[1], "sdmc:/Themes/z.zip");
    open_folder_view();
    EXPECT(screen == SCR_LIST && list.count == 14, "the folder view with 14 themes (screen %d, %u rows)", screen,
           (unsigned)list.count);
    EXPECT(row("z.zip") && row("z.zip")->in_pack == 2 && row("z.zip")->in_pack_usable, "z.zip shows installed");
    EXPECT(row_at(60, 225) >= HIT_ROW0 && row_at(BOTTOM_W - 60, 225) >= HIT_ROW0 && target_at(60, 225) == HIT_LEFT &&
           target_at(BOTTOM_W - 60, 225) == HIT_RIGHT, "rows lie under both tabs: a touch there is the tab's");
    char count_line[160];
    text_installed(lang, (unsigned)pv.usable, count_line, sizeof count_line);
    EXPECT(!lv.selected && !strcmp(stub_foot, count_line), "on entry the footer is the status line (\"%s\")",
           stub_foot);
    EXPECT(select_row("z.zip"), "z.zip selected with the D-pad");
    frames_for(600);
    float scrolled = lv.scroll;
    EXPECT(scrolled > 0 && !strcmp(stub_foot, count_line), "scrolled to it, the footer still the status line, not "
           "the installed index (\"%s\")", stub_foot);
    key(KEY_A);
    frames_for(500);
    EXPECT(screen == SCR_CONFIRM && installed_already(), "its check screen: installed, the delete buttons");
    key(KEY_A); frames_for(200);    /* the cursor on "Design löschen" */
    key(KEY_A); frames_for(500);
    EXPECT(popup == POP_ASK_THEME, "the question");
    key(KEY_DRIGHT); frames_for(100); key(KEY_DRIGHT); frames_for(100); key(KEY_A); frames_for(500);
    EXPECT(fake_deletes == 1 && popup == POP_NOTE, "deleted through the deleter, the note (deletes %d)", fake_deletes);
    key(KEY_B); frames_for(400);     /* closes the note */
    key(KEY_B); frames_for(600);     /* back */
    EXPECT(screen == SCR_LIST, "back in the folder view (screen %d)", screen);
    EXPECT(row("z.zip") && !row("z.zip")->in_pack_usable && row("a.zip") && !row("a.zip")->in_pack_usable,
           "a.zip and z.zip read again: deleted in HOME, no longer installed");
    EXPECT(row("m01.zip") && row("m01.zip")->in_pack_usable, "the other theme's rows still installed");
    EXPECT(lv.selected && !strcmp(list.items[lv.cursor].file, "z.zip") && lv.scroll == scrolled,
           "z.zip still selected, the scroll as it was (%.0f, was %.0f)", lv.scroll, scrolled);

    /* 12. "Datei löschen" from the folder view: the row is gone, nothing selected, the scroll kept as far as it can */
    printf("-- back to the folder view after \"Datei löschen\"\n");
    EXPECT(select_row("m12.zip"), "m12.zip selected");
    frames_for(600);
    scrolled = lv.scroll;
    key(KEY_A); frames_for(500);
    EXPECT(screen == SCR_CONFIRM && installed_already(), "m12.zip's check screen");
    key(KEY_A); frames_for(200); key(KEY_DRIGHT); frames_for(200); key(KEY_A); frames_for(500);
    EXPECT(popup == POP_ASK_FILE, "the question for the file");
    key(KEY_DRIGHT); frames_for(100); key(KEY_DRIGHT); frames_for(100); key(KEY_A); frames_for(800);
    struct stat st;
    EXPECT(stat("sdmc:/Themes/m12.zip", &st) != 0, "the file is gone from the card");
    EXPECT(screen == SCR_LIST && !row("m12.zip") && list.count == 13 && popup == POP_NOTE,
           "the folder view read again without it, the note over it");
    EXPECT(!lv.selected && lv.scroll == clamp_scroll(scrolled), "nothing selected, the scroll kept (%.0f, was %.0f)",
           lv.scroll, scrolled);
    key(KEY_A); frames_for(400);

    /* 13. an install from the folder view, then "Löschen" on "Installiert": back in that folder, read again, the file
     *     gone, the theme installed (its other file now shows it), nothing selected, the note over it */
    printf("-- install from the folder view, then \"Löschen\"\n");
    EXPECT(row("z.zip") && !row("z.zip")->in_pack_usable, "z.zip not installed (deleted in 11)");
    EXPECT(select_row("z.zip"), "z.zip selected");
    frames_for(600);
    key(KEY_A); frames_for(500);
    EXPECT(screen == SCR_CONFIRM && can_install(), "its check screen offers the install");
    key(KEY_A); frames_for(200); key(KEY_A); frames_for(3000);
    EXPECT(screen == SCR_DONE && may_clean, "Installiert, with the cleanup question (screen %d)", screen);
    key(KEY_A); frames_for(200); key(KEY_DRIGHT); frames_for(200); key(KEY_A); frames_for(800);
    EXPECT(screen == SCR_LIST && !row("z.zip") && popup == POP_NOTE, "back in the folder view without z.zip, the note");
    EXPECT(row("a.zip") && row("a.zip")->in_pack_usable, "a.zip, the same theme, shows installed");
    EXPECT(!lv.selected, "nothing selected");
    key(KEY_A); frames_for(400);

    /* 14. the same theme deleted again, installed from a.zip, "Behalten": back at a.zip, selected, installed */
    printf("-- install from the folder view, then \"Behalten\"\n");
    EXPECT(select_row("a.zip"), "a.zip selected");
    frames_for(600);
    scrolled = lv.scroll;
    key(KEY_A); frames_for(500);
    key(KEY_A); frames_for(200); key(KEY_A); frames_for(500);              /* "Design löschen", the question */
    key(KEY_DRIGHT); frames_for(100); key(KEY_DRIGHT); frames_for(100); key(KEY_A); frames_for(500);
    key(KEY_B); frames_for(400);                                            /* the note */
    EXPECT(screen == SCR_CONFIRM && can_install(), "deleted, the check screen offers the install again");
    key(KEY_A); frames_for(200); key(KEY_A); frames_for(3000);
    EXPECT(screen == SCR_DONE, "Installiert");
    key(KEY_A); frames_for(200); key(KEY_A); frames_for(800);               /* "Behalten" */
    EXPECT(screen == SCR_LIST && lv.selected && !strcmp(list.items[lv.cursor].file, "a.zip") && lv.scroll == scrolled,
           "back at a.zip, selected, the scroll as it was (%.0f, was %.0f)", lv.scroll, scrolled);
    EXPECT(row("a.zip")->in_pack_usable && stat("sdmc:/Themes/a.zip", &st) == 0, "a.zip installed, its file kept");

    /* 15. an install that started from a download: after "Installiert" the Designs screen on "QR-Code scannen" */
    printf("-- install from a download, then \"Behalten\"\n");
    key(KEY_A); frames_for(500);
    key(KEY_A); frames_for(200); key(KEY_A); frames_for(500);
    key(KEY_DRIGHT); frames_for(100); key(KEY_DRIGHT); frames_for(100); key(KEY_A); frames_for(500);
    key(KEY_B); frames_for(400);
    close_theme();
    go_designs(1);
    frames_for(200);
    open_confirm("a.zip", "sdmc:/Themes", SCR_DESIGNS);     /* as download_done opens the file it saved */
    frames_for(500);
    EXPECT(screen == SCR_CONFIRM && can_install(), "the downloaded theme's check screen");
    key(KEY_A); frames_for(200); key(KEY_A); frames_for(3000);
    key(KEY_A); frames_for(200); key(KEY_A); frames_for(500);
    EXPECT(screen == SCR_DESIGNS && focus == 1, "the Designs screen, on QR-Code scannen (screen %d, focus %d)", screen,
           focus);

    /* 16. 0.9.0, "Installierte Designs": the third tile, with keys; six themes (indices 2 to 7), a.zip in /Themes is
     *     theme 2; themes 2 and 4 have music, 3, 5 and 7 none, 6 cannot be read */
    printf("-- the installed designs\n");
    if (system("rm -rf sdmc:/Themes"))
        return 3;
    mkdir("sdmc:/Themes", 0755);
    copy_to(argv[1], "sdmc:/Themes/a.zip");
    make_pack(argv[1], argv[2], 4);
    fake_music[2] = fake_music[4] = MUSIC_YES;
    fake_music[3] = fake_music[5] = fake_music[7] = MUSIC_NONE;
    fake_music[6] = MUSIC_UNKNOWN;
    fake_music_reads = 0;
    open_installed_view();
    EXPECT(screen == SCR_INSTALLED && designs.count == 6, "the third tile opens them: 6 designs (screen %d, %u rows)",
           screen, (unsigned)designs.count);
    EXPECT(designs.count == 6 && designs.rows[0].index == 2 && designs.rows[5].index == 7, "in index order");
    EXPECT(designs.count == 6 && designs.rows[0].music == MUSIC_YES && designs.rows[1].music == MUSIC_NONE &&
           designs.rows[2].music == MUSIC_YES && designs.rows[4].music == MUSIC_UNKNOWN,
           "music as each content holds it, unknown where it could not be read");
    EXPECT(designs.count == 6 && !strcmp(designs.rows[0].file, "a.zip") && !designs.rows[1].file[0],
           "theme 2's file found in /Themes, theme 3 has none");
    EXPECT(fake_music_reads == 6, "each content read once (%d)", fake_music_reads);
    text_installed(lang, (unsigned)pv.usable, count_line, sizeof count_line);
    EXPECT(!lv.selected && !strcmp(stub_foot, count_line), "nothing selected, the footer the status line (\"%s\")",
           stub_foot);
    key(KEY_B); frames_for(500);
    EXPECT(screen == SCR_DESIGNS && focus == 2, "B: the Designs screen, on the wide tile (screen %d, focus %d)",
           screen, focus);
    key(KEY_A); frames_for(200);
    key(KEY_DUP); frames_for(150);
    EXPECT(focus == 0, "up from the wide tile: the left tile on first entry (focus %d)", focus);
    key(KEY_DRIGHT); frames_for(150); key(KEY_DDOWN); frames_for(150); key(KEY_DUP); frames_for(150);
    EXPECT(focus == 1, "right, down, up: back on the right tile (focus %d)", focus);
    key(KEY_DDOWN); frames_for(150); key(KEY_DLEFT); frames_for(150); key(KEY_DRIGHT); frames_for(150);
    EXPECT(focus == 2, "left and right on the wide tile move nothing (focus %d)", focus);
    tap(160, WIDE_Y + WIDE_H / 2, 48); frames_for(500);
    EXPECT(screen == SCR_INSTALLED && fake_music_reads == 7, "a tap on the wide tile opens them again; only the "
           "unreadable content read again (%d reads)", fake_music_reads);
    key(KEY_B); frames_for(500);
    tap(TILE_X2 + TILE_W / 2, DTILE_Y + DTILE_H - 4, 48); frames_for(300);
    EXPECT(screen == SCR_SCAN, "a tap on the right tile's lowest rows: the scan, not the wide tile (screen %d)", screen);
    key(KEY_B); frames_for(500);
    tap(TILE_X1 + 4, WIDE_Y + 4, 48); frames_for(500);
    EXPECT(screen == SCR_INSTALLED, "a tap on the wide tile's top left corner opens the designs (screen %d)", screen);
    /* the footer's two tabs as drawn, each its own target; between them and in their round inner corners the rows
     * drawn there (0.9.1: a tap at (110, 212) went back to Designs) */
    const int gaps[3][2] = {{BOTTOM_W / 2, BAR_Y + BAR_H / 2}, {110, 212}, {BOTTOM_W - 1 - 110, 212}};
    for (int k = 0; k < 3; k++) {
        long under = (long)row_at(gaps[k][0], gaps[k][1]) - HIT_ROW0;
        tap(gaps[k][0], gaps[k][1], 48); frames_for(500);
        EXPECT(under >= 0 && screen == SCR_DESIGN && design_index == designs.rows[under].index,
               "a tap at (%d, %d): the row drawn there opens its design (row %ld, screen %d)", gaps[k][0], gaps[k][1],
               under, screen);
        key(KEY_B); frames_for(500);
    }
    EXPECT(screen == SCR_INSTALLED && !lv.selected, "back in the list, nothing selected (screen %d)", screen);
    tap(BOTTOM_W - TAB_W / 2, BAR_Y + BAR_H / 2, 48); frames_for(400);
    EXPECT(screen == SCR_INSTALLED && lv.selected, "the right tab: selects, as A (screen %d)", screen);
    tap(TAB_W / 2, BAR_Y + BAR_H / 2, 48); frames_for(500);
    EXPECT(screen == SCR_DESIGNS, "the left tab: back (screen %d)", screen);
    key(KEY_A); frames_for(200); key(KEY_A); frames_for(500);
    EXPECT(screen == SCR_INSTALLED, "and in again (screen %d)", screen);

    /* 17. the D-pad to the last design (the list scrolls), A opens it, B back to it, still selected */
    printf("-- one design and back\n");
    for (int i = 0; i < 8; i++) { key(KEY_DDOWN); frames_for(150); }
    frames_for(600);
    scrolled = lv.scroll;
    EXPECT(lv.selected && lv.cursor == 5 && scrolled > 0, "the last design selected, the list scrolled (%.0f)",
           scrolled);
    /* G1-R6: the console's key repeat (the stub repeats as libctru's hid.c, with the application's delay and
     *     interval): up held from the last row moves the selection at once, then after the delay once every
     *     interval; down back to the last row */
    hidSetRepeatParameters(REPEAT_DELAY, REPEAT_INTERVAL);
    int moved_at[3] = {0, 0, 0}, moves = 0;
    size_t row_was = lv.cursor;
    sim_keys |= KEY_DUP;
    for (int i = 1; i <= REPEAT_DELAY + 2 * REPEAT_INTERVAL - 1; i++) {
        frame();
        if (lv.cursor != row_was && moves < 3)
            moved_at[moves++] = i;
        row_was = lv.cursor;
    }
    sim_keys &= ~KEY_DUP;
    frames_for(300);
    EXPECT(lv.selected && lv.cursor == 2 && moves == 3 && moved_at[0] == 1 && moved_at[1] == REPEAT_DELAY &&
               moved_at[2] == REPEAT_DELAY + REPEAT_INTERVAL,
           "up held in the list: a row at once, the next after %d frames, then every %d (moved in frames %d, %d, %d; "
           "row %ld)", REPEAT_DELAY, REPEAT_INTERVAL, moved_at[0], moved_at[1], moved_at[2], (long)lv.cursor);
    for (int i = 0; i < 3; i++) { key(KEY_DDOWN); frames_for(150); }
    frames_for(600);
    scrolled = lv.scroll;
    key(KEY_A); frames_for(500);
    EXPECT(screen == SCR_DESIGN && design_index == 7, "A: its screen (screen %d, design %u)", screen, design_index);
    key(KEY_B); frames_for(500);
    EXPECT(screen == SCR_INSTALLED && lv.selected && lv.cursor == 5 && lv.scroll == scrolled,
           "B: the list as it was, the design selected");

    /* 18. keys only on one design: A over and over never deletes (A twice in the question cancels) */
    printf("-- one design, keys without the D-pad\n");
    int deletes_were = fake_deletes;
    key(KEY_A); frames_for(500);
    for (int i = 0; i < 40; i++) { key(KEY_A); frames_for(150); }
    EXPECT(fake_deletes == deletes_were && design_row_of(7) >= 0, "A pressed 40 times, 150 ms apart: nothing deleted");
    key(KEY_A); frames_for(400);
    if (popup)
        key(KEY_B);
    frames_for(400);
    key(KEY_B); frames_for(500);
    EXPECT(screen == SCR_INSTALLED, "B back to the list (screen %d)", screen);

    /* 19. a double tap on "Design löschen" at its lowest rows (y 172; the question's buttons start at 187), second
     *     touch-down g ms after the first */
    printf("-- double tap on \"Design löschen\" (x 200, y 172: its lowest rows)\n");
    int first_design_gap = -1;
    for (int g = 64; g <= 640; g += 16) {
        design_screen(3);
        deletes_were = fake_deletes;
        tap(200, 172, 48);
        u64 since_first = 48 + 16;
        if ((u64)g > since_first)
            frames_for((u64)g - since_first);
        tap(200, 172, 48);
        frames_for(400);
        if (fake_deletes != deletes_were && first_design_gap < 0)
            first_design_gap = g;
        if (g <= 450)
            EXPECT(fake_deletes == deletes_were, "gap %3d ms: nothing deleted (popup %d)", g, popup);
    }
    printf("     first gap that deletes: %d ms (-1: none; the tile no longer lies under the question's buttons)\n",
           first_design_gap);
    EXPECT(first_design_gap < 0 || first_design_gap >= PRESS_MS + POPUP_GUARD_MS,
           "no delete below PRESS_MS + POPUP_GUARD_MS");

    /* 20. HOME uses theme 3: refused with a note, nothing deleted; a double tap on its Okay starts nothing */
    printf("-- the design in use\n");
    design_screen(3);
    fake_home.current = (home_entry){3, HOME_PACK_VARIATION, HOME_TYPE_DLC};
    deletes_were = fake_deletes;
    tap(160, 120, 48); frames_for(400);
    EXPECT(popup == POP_NOTE && fake_deletes == deletes_were, "refused: the note, nothing deleted (popup %d)", popup);
    tap(200, 200, 48); frames_for(100); tap(200, 200, 48); frames_for(500);
    EXPECT(popup == POP_NONE && screen == SCR_DESIGN && fake_deletes == deletes_were,
           "a double tap on Okay closes it, nothing else (popup %d, screen %d)", popup, screen);
    fake_home.current = (home_entry){0, 0, 0};

    /* 21. "Design löschen" from the list with keys: afterwards the list read again, the design gone, the scroll
     *     kept as far as it can, nothing selected, the note over it */
    printf("-- delete from the list\n");
    for (size_t j = 0; j < fake_am.count; j++)
        fake_am.usable[j] = 1;
    open_installed_view();
    for (int i = 0; i < 8; i++) { key(KEY_DDOWN); frames_for(150); }
    frames_for(600);
    scrolled = lv.scroll;
    deletes_were = fake_deletes;
    int reads_were = fake_music_reads;
    key(KEY_A); frames_for(500);
    key(KEY_A); frames_for(200); key(KEY_A); frames_for(500);
    EXPECT(popup == POP_ASK_THEME, "the question (popup %d)", popup);
    key(KEY_DRIGHT); frames_for(100); key(KEY_DRIGHT); frames_for(100); key(KEY_A); frames_for(800);
    EXPECT(fake_deletes == deletes_were + 1 && popup == POP_NOTE, "deleted through the deleter, the note");
    EXPECT(screen == SCR_INSTALLED && designs.count == 5 && design_row_of(7) < 0,
           "the list read again without it (%u rows)", (unsigned)designs.count);
    EXPECT(!lv.selected && lv.scroll == clamp_scroll(scrolled), "nothing selected, the scroll kept (%.0f, was %.0f)",
           lv.scroll, scrolled);
    EXPECT(fake_music_reads == reads_were + 1, "only the unreadable content read again (%d, was %d)",
           fake_music_reads, reads_were);
    text_installed(lang, (unsigned)pv.usable, count_line, sizeof count_line);
    EXPECT(pv.usable == 5 && !strcmp(stub_foot, count_line), "the status line one fewer (\"%s\")", stub_foot);
    key(KEY_B); frames_for(400);
    EXPECT(popup == POP_NONE && screen == SCR_INSTALLED, "B closes the note, the list stays");

    /* 22. a full pack (the mark at 349): the list opens, a design can be deleted; the folder view says why not */
    printf("-- a full pack\n");
    FILE *mf = fopen("sdmc:/theme-pack/installed.mark", "w");
    fputs("349\n", mf);
    fclose(mf);
    open_installed_view();
    EXPECT(pv.state == PACK_FULL_INDEX && screen == SCR_INSTALLED && designs.count == 5,
           "full (state %d): the list opens with 5 designs (screen %d)", pv.state, screen);
    key(KEY_A); frames_for(200); key(KEY_A); frames_for(500);
    key(KEY_A); frames_for(200); key(KEY_A); frames_for(500);
    EXPECT(screen == SCR_DESIGN && popup == POP_ASK_THEME, "a design of the full pack: the question (popup %d)", popup);
    key(KEY_B); frames_for(400); key(KEY_B); frames_for(400); key(KEY_B); frames_for(400);
    key(KEY_DUP); frames_for(150); key(KEY_DUP); frames_for(150); key(KEY_A); frames_for(500);
    EXPECT(screen == SCR_PROBLEM, "the folder view: the reason instead (screen %d)", screen);
    unlink("sdmc:/theme-pack/installed.mark");

    /* 23. a pack not read whole (an import pending): the reason instead of a list */
    printf("-- a pack not read whole\n");
    fake_am.pending = PENDING_YES;
    open_installed_view();
    EXPECT(pv.state == PACK_PENDING && screen == SCR_PROBLEM && problem_head == T_INSTALLED_DESIGNS,
           "pending: the reason (screen %d)", screen);
    fake_am.pending = PENDING_NO;

    /* 24. 0.8.4's bug on the console: after step 3 the start screen showed before "Installiert", step 4 never
     *     green. Through a whole install the top screen shows only installing, then "Installiert"; its last
     *     installing frame has all four steps green. G2-L2b: "Installiert" goes on with the installing screen's
     *     picture on top (no Done card, 'D'): the picture's frames ('p') are the last, on into "Installiert" */
    printf("-- the top screens through an install\n");
    make_pack(argv[1], argv[2], 0);
    open_folder_view();
    EXPECT(select_row("a.zip"), "a.zip selected");
    frames_for(600);
    key(KEY_A); frames_for(500);
    key(KEY_A); frames_for(200); key(KEY_A); frames_for(500);              /* "Design löschen", the question */
    key(KEY_DRIGHT); frames_for(100); key(KEY_DRIGHT); frames_for(100); key(KEY_A); frames_for(500);
    key(KEY_B); frames_for(400);                                            /* the note */
    EXPECT(screen == SCR_CONFIRM && can_install(), "deleted: the check screen offers the install");
    key(KEY_A); frames_for(200);
    stub_tops[0] = 0;
    key(KEY_A); frames_for(1000);
    printf("     top screens: %s\n", stub_tops);
    const char *p_at = strchr(stub_tops, 'p');
    EXPECT(screen == SCR_DONE && top_has('p') && !strchr(stub_tops, 'D'), "Installiert, the installing screen's "
           "picture on its top (screen %d)", screen);
    EXPECT(!strchr(stub_tops, 'S'), "never the start screen");
    EXPECT(p_at && !p_at[1] && pour_full(&install_pour), "the last installing frame: the picture full");
    int rising = 1;
    for (const char *c = stub_tops; p_at && c < p_at; c++)
        rising &= c[0] >= '0' && c[0] <= '4' && c[1] >= c[0];
    EXPECT(rising && p_at && !strpbrk(p_at, "01234SD"), "installing frames only, steps only rising, then only the "
           "picture, on into \"Installiert\"");

    /* 25. 0.9.1: the tabs take touches where they are drawn. Every pixel of the bar's rows against the held tabs'
     *     images (the shape alone, no shadow): on a tab exactly where at least half of the pixel is drawn (an
     *     edge pixel within 3 of half may go either way) */
    printf("-- the tabs as drawn\n");
    static uint8_t atlas[ART_ATLAS_W * ART_ATLAS_H * 4];
    uLongf atlas_size = sizeof atlas;
    EXPECT(uncompress(atlas, &atlas_size, ART_STOCK.atlas_z[ART_SCHEME_WEISS], ART_STOCK.atlas_z_size[ART_SCHEME_WEISS]) == Z_OK &&
           atlas_size == sizeof atlas, "the atlas unpacks");
    int wrong = 0, edge = 0, on_tabs = 0;
    for (int right = 0; right < 2; right++) {
        const art_sprite *sp = art_of(&ART_STOCK, right ? ART_TAB_R_DOWN : ART_TAB_L_DOWN);
        int x0 = right ? BOTTOM_W - sp->w : 0, y0 = BAR_Y - sp->pad[1];
        for (int y = BAR_Y; y < SCREEN_H; y++)
            for (int x = 0; x < BOTTOM_W; x++) {
                int ix = x - x0, iy = y - y0;
                int a = ix >= 0 && ix < sp->w && iy >= 0 && iy < sp->h ? atlas_alpha(atlas, sp->x + ix, sp->y + iy) : 0;
                int hit = ui_tab_hit(right, x, y);
                on_tabs += hit;
                if ((a >= 128) != hit) {
                    if (abs(a - 128) <= 3)
                        edge++;
                    else
                        wrong++;
                }
            }
    }
    EXPECT(!wrong, "the hit test follows the drawn tabs: %d pixels differ, %d edge pixels at half (%d px on the tabs)",
           wrong, edge, on_tabs);
    EXPECT(ui_tab_hit(0, 40, BAR_Y) && !ui_tab_hit(0, 110, 212) && !ui_tab_hit(1, BOTTOM_W - 1 - 110, 212) &&
           ui_tab_hit(0, 118, SCREEN_H - 1) && !ui_tab_hit(0, 119, SCREEN_H - 1) &&
           ui_tab_hit(1, BOTTOM_W - 1 - 118, SCREEN_H - 1) && !ui_tab_hit(0, 40, BAR_Y - 1),
           "the tabs' top row, the reviewer's (110, 212) and its mirror off, the inner edge at 118.5");
    /* a short list: in the gap and the round corners nothing is drawn, so a tap there does nothing */
    if (system("rm -rf sdmc:/Themes"))
        return 3;
    mkdir("sdmc:/Themes", 0755);
    copy_to(argv[1], "sdmc:/Themes/a.zip");
    make_pack(argv[1], argv[2], 0);
    open_folder_view();
    EXPECT(screen == SCR_LIST && list.count == 1 && bar_halves(), "one row, the two tabs");
    for (int k = 0; k < 3; k++) {
        tap(gaps[k][0], gaps[k][1], 48); frames_for(500);
        EXPECT(screen == SCR_LIST && !job && !lv.selected && pending == HIT_NONE,
               "a tap at (%d, %d) where no row is drawn: nothing (screen %d)", gaps[k][0], gaps[k][1], screen);
    }
    tap(BOTTOM_W - 20, SCREEN_H - 4, 48); frames_for(400);
    EXPECT(screen == SCR_LIST && lv.selected, "the right tab's outer end: selects, as A");
    tap(20, SCREEN_H - 4, 48); frames_for(500);
    EXPECT(screen == SCR_DESIGNS, "the left tab's outer end: back (screen %d)", screen);

    /* 26. 0.9.1: the selection is drawn after all rows and buttons, so the next row never covers its lower corners
     *     (and before the corner button, the scroll bar and the bar, which cover it as they cover the rows) */
    printf("-- the selection after all rows\n");
    for (int i = 0; i < 9; i++) {
        char name[64];
        snprintf(name, sizeof name, "sdmc:/Themes/m%d.zip", i);
        copy_to(argv[2], name);
    }
    mkdir("sdmc:/Themes/sub", 0755);
    copy_to(argv[2], "sdmc:/Themes/sub/b.zip");
    open_folder_view();
    EXPECT(select_row("sub"), "the folder row selected");
    frames_for(300);
    EXPECT(selection_last(row_rect(lv.cursor)), "the folder row's selection: after all rows, before the corner button");
    EXPECT(select_row("m2.zip"), "a theme row in the middle selected");
    frames_for(300);
    EXPECT(selection_last(row_rect(lv.cursor)) && row_drawn(lv.cursor + 1),
           "its selection: after all rows (the next one drawn too), before what covers them");
    fake_music[2] = MUSIC_YES;
    fake_music[3] = MUSIC_NONE;
    open_installed_view();
    key(KEY_DDOWN); frames_for(300);
    EXPECT(screen == SCR_INSTALLED && lv.selected && selection_last(row_rect(lv.cursor)) && row_drawn(lv.cursor + 1),
           "the installed designs: the same");
    key(KEY_B); frames_for(500);
    key(KEY_DUP); frames_for(150); key(KEY_DUP); frames_for(300);  /* the first shows the ring on the wide tile */
    EXPECT(screen == SCR_DESIGNS && focus == 0 && selection_last(DESIGNS_TILES[0]),
           "the Designs screen: the top left tile's selection after the wide tile below it");
    key(KEY_DDOWN); frames_for(300);
    EXPECT(focus == 2 && selection_last(DESIGNS_TILES[2]), "and the wide tile's");
    reset_app();
    memset(&theme, 0, sizeof theme);
    theme_open = 1; theme_result = TP_OK; theme.has_bgm = 1; theme.use_bgm = 1;
    memset(&plan, 0, sizeof plan); plan.state = PLAN_OK;
    screen = SCR_CONFIRM; confirm_from = SCR_DESIGNS;
    frames_for(300);
    key(KEY_DDOWN); frames_for(300);
    EXPECT(music_choice() && ring && selection_last(focus ? GO_LOWER : GO_UPPER), "the check screen's two buttons");

    /* 27. 0.9.1, changed in 0.9.2 (item 18): a job that is over within LOADER_WAIT_MS (8 frames here, 128 ms)
     *     shows no loader and no bar: both screens go on showing the screen it started from, the transition starts
     *     only where the screen changes, never for the job, and no bottom frame is empty. Case 31 is the other side
     *     of the wait. */
    printf("-- the bottom screen while a job runs that is over within the loader's wait\n");
    worker_frames = 8;
    EXPECT(worker_frames * 16 < LOADER_WAIT_MS, "8 frames are less than the wait (%d ms)", LOADER_WAIT_MS);
    close_theme();
    reset_app();
    load_pack();
    go_designs(0);
    frames_for(400);
    key(KEY_A); frames_for(200);                    /* the ring on "Von SD installieren" */
    trace_start();
    key(KEY_A); frames_for(600);
    trace_sum t = trace_check();
    int with_cursor = 0;
    for (int k = 0; k < traced; k++)
        with_cursor += trace[k].job != JOB_NONE && trace[k].selections == 1;
    EXPECT(screen == SCR_LIST && t.job_frames >= 6 && job_frames_show(SCR_DESIGNS, 3) && with_cursor == t.job_frames,
           "Designs to the folder view: %d job frames, all over the Designs tiles with the cursor on the one pressed "
           "(%d)", t.job_frames, with_cursor);
    EXPECT(!t.empty && t.restarts == 1 && !t.restarts_same, "no empty bottom, one transition: at the folder view "
           "(%d empty, %d starts, %d on the same screen)", t.empty, t.restarts, t.restarts_same);
    loader_sum lo = loader_check();
    EXPECT(!lo.loader && !lo.stray && !lo.back && !lo.jumps, "no loader, no bar on the top screen (%d, %d)",
           lo.loader, lo.stray);
    EXPECT(select_row("sub"), "the folder selected");
    frames_for(300);
    size_t rows_were = list.count;
    trace_start();
    key(KEY_A); frames_for(600);
    t = trace_check();
    EXPECT(screen == SCR_LIST && !strcmp(list_dir, "sdmc:/Themes/sub") && list.count == 1 && !lv.selected,
           "A: the folder opens, nothing selected (%u rows)", (unsigned)list.count);
    EXPECT(t.job_frames >= 6 && job_frames_show(SCR_LIST, 4) && rows_were == 11,
           "%d job frames, all over the rows it came from", t.job_frames);
    EXPECT(!t.empty && !t.restarts, "no empty bottom, no transition (%d empty, %d starts)", t.empty, t.restarts);
    trace_start();
    key(KEY_Y); frames_for(600);
    t = trace_check();
    EXPECT(screen == SCR_LIST && !strcmp(list_dir, "sdmc:/Themes") && !strcmp(list.items[lv.cursor].file, "sub"),
           "Y: one folder up, the cursor on the folder came from");
    EXPECT(t.job_frames >= 6 && job_frames_show(SCR_LIST, 1) && !t.empty && !t.restarts,
           "over the inner folder's row: %d job frames, %d empty, %d starts", t.job_frames, t.empty, t.restarts);
    EXPECT(select_row("a.zip"), "a.zip selected");
    frames_for(300);
    trace_start();
    key(KEY_A); frames_for(600);
    t = trace_check();
    EXPECT(screen == SCR_CONFIRM && installed_already() && t.job_frames >= 6 && job_frames_show(SCR_LIST, 4),
           "a theme: its check over the folder view (%d job frames), then the check screen", t.job_frames);
    EXPECT(!t.empty && t.restarts == 1 && !t.restarts_same && trace[traced - 1].since != trace_since,
           "no empty bottom, one transition: at the check screen (%d empty, %d starts)", t.empty, t.restarts);
    lo = loader_check();
    EXPECT(!lo.loader && !lo.stray && !lo.back && !lo.jumps, "no loader, no bar on the top screen (%d, %d)",
           lo.loader, lo.stray);
    /* "Datei löschen" (0.9.2): its frames over the check screen; the folder view shows without the file in its
     * first frame, and nothing is read after it */
    key(KEY_A); frames_for(200); key(KEY_DRIGHT); frames_for(200); key(KEY_A); frames_for(500);
    EXPECT(popup == POP_ASK_FILE, "the question for the file");
    key(KEY_DRIGHT); frames_for(100); key(KEY_DRIGHT); frames_for(100);
    trace_start();
    key(KEY_A); frames_for(800);
    t = trace_check();
    lo = loader_check();
    EXPECT(screen == SCR_LIST && !row("a.zip") && popup == POP_NOTE, "a.zip deleted, the folder view without it");
    EXPECT(t.job_frames >= 6 && job_frames_show(SCR_CONFIRM, 2) && !lo.loader && !lo.stray,
           "the delete over the check screen's two tiles (%d job frames, %d with the loader, %d stray)",
           t.job_frames, lo.loader, lo.stray);
    EXPECT(lo.runs == 1 && lo.after > 0 && trace[lo.after].screen == SCR_LIST && trace[lo.after].rows == list.count,
           "one load; the first frame after it is the folder view as it stays (%d loads, %u rows)", lo.runs,
           lo.after > 0 ? (unsigned)trace[lo.after].rows : 0);
    EXPECT(!t.empty && t.restarts == 1 && !t.restarts_same, "no empty bottom, one transition: at the folder view "
           "(%d empty, %d starts)", t.empty, t.restarts);
    key(KEY_A); frames_for(400);
    /* "Design löschen" on one design (0.9.2): its frames over it; the list shows without the design in its first
     * frame, never with it, and nothing is read after it */
    for (size_t j = 0; j < fake_am.count; j++)
        fake_am.usable[j] = 1;
    open_installed_view();
    size_t designs_were = designs.count;
    key(KEY_DDOWN); frames_for(300);
    key(KEY_A); frames_for(600);
    EXPECT(screen == SCR_DESIGN, "one design (screen %d)", screen);
    key(KEY_A); frames_for(200); key(KEY_A); frames_for(500);
    key(KEY_DRIGHT); frames_for(100); key(KEY_DRIGHT); frames_for(100);
    trace_start();
    key(KEY_A); frames_for(800);
    t = trace_check();
    lo = loader_check();
    EXPECT(screen == SCR_INSTALLED && designs.count == designs_were - 1 && popup == POP_NOTE,
           "deleted, the list without it (%u rows, were %u)", (unsigned)designs.count, (unsigned)designs_were);
    EXPECT(t.job_frames >= 6 && job_frames_show(SCR_DESIGN, 1) && !lo.loader && !lo.stray,
           "the delete over the design's tile (%d job frames, %d with the loader, %d stray)", t.job_frames,
           lo.loader, lo.stray);
    int with_it = 0;
    for (int k = 0; k < traced; k++)
        with_it += trace[k].screen == SCR_INSTALLED && trace[k].rows != designs_were - 1;
    EXPECT(lo.runs == 1 && lo.after > 0 && trace[lo.after].screen == SCR_INSTALLED && !with_it,
           "one load; the list's first frame is already without the design (%d loads, %d frames with it)", lo.runs,
           with_it);
    EXPECT(!t.empty && t.restarts == 1 && !t.restarts_same, "no empty bottom, one transition: at the list (%d empty, "
           "%d starts)", t.empty, t.restarts);
    key(KEY_A); frames_for(400);
    /* "Ohne Musik installieren": the job's own bottom, the two buttons, without reading the theme it changes */
    reset_app();
    memset(&theme, 0, sizeof theme);
    theme_open = 1; theme_result = TP_OK; theme.has_bgm = 1; theme.use_bgm = 1;
    memset(&plan, 0, sizeof plan); plan.state = PLAN_OK;
    screen = SCR_CONFIRM; confirm_from = SCR_DESIGNS;
    frames_for(400);
    trace_start();
    tap(GO_X + GO_W / 2, GO_TWO_Y2 + GO_H / 2, 48); frames_for(600);
    t = trace_check();
    EXPECT(t.job_frames >= 6 && job_frames_show(SCR_CONFIRM, 2) && !t.empty && !t.restarts,
           "without music: %d job frames over the two buttons, %d empty, %d starts", t.job_frames, t.empty,
           t.restarts);
    if (popup)
        key(KEY_B);
    frames_for(400);
    worker_frames = 0;

    /* ---- 0.9.2, the flow lead's cases (30 to 39) */

    /* 30. F2: the check books its time to its steps. Every look at the tick clock here is 1 ms after the last, so
     *     every stretch is a whole number of ms; planning reads no file and tells no phase: exactly one stretch */
    printf("-- 30. the check's times by step\n");
    make_pack(argv[1], argv[2], 4);
    copy_to(argv[1], "sdmc:/Themes/a.zip");
    close_theme();
    reset_app();
    load_pack();
    go_designs(0);
    frames_for(300);
    open_confirm("a.zip", "sdmc:/Themes", SCR_DESIGNS);
    frames_for(500);
    int whole = 1;
    for (int k = 0; k < CHECK_STEPS; k++)
        whole &= check_ticks[k] % 268112 == 0;
    stat("sdmc:/Themes/a.zip", &st);
    EXPECT(screen == SCR_CONFIRM && theme_open && check_bytes == (u64)st.st_size, "a.zip checked, %llu bytes",
           (unsigned long long)check_bytes);
    EXPECT(whole && check_ticks[CHECK_READ] && check_ticks[CHECK_UNZIP] && check_ticks[CHECK_LZ11] &&
           check_ticks[CHECK_HASH] && check_ticks[CHECK_PLAN] == 268112,
           "time booked to read, unzip, LZ11 and the RomFS hash; the plan one stretch (ms: read %u, CIA %u, unzip %u, "
           "LZ11 %u, hash %u, plan %u, other %u)", (unsigned)(check_ticks[CHECK_READ] / 268112),
           (unsigned)(check_ticks[CHECK_CIA] / 268112), (unsigned)(check_ticks[CHECK_UNZIP] / 268112),
           (unsigned)(check_ticks[CHECK_LZ11] / 268112), (unsigned)(check_ticks[CHECK_HASH] / 268112),
           (unsigned)(check_ticks[CHECK_PLAN] / 268112), (unsigned)(check_ticks[CHECK_OTHER] / 268112));
    close_theme();

    /* 31. item 18, the other side of the wait: a job whose work takes longer than LOADER_WAIT_MS (30 frames here,
     *     480 ms). Until the wait is over both screens show what they showed; then what the bottom screen shows goes
     *     away within LOADER_FADE_MS and the loader takes its place (the top screen shows no bar). When the work is
     *     done the ring fades, the job going on until it has; then a new screen comes in by its own transition, on
     *     the bare background, and on the same screen what shows now comes out of the background as the old went,
     *     the top screen not started anew. In no frame does the cover or the ring move further than the clock
     *     allows: nothing jumps. */
    printf("-- 31. the loader in the bottom screen's place while a job runs longer than the wait\n");
    worker_frames = 30;
    EXPECT(worker_frames * 16 > LOADER_WAIT_MS + LOADER_FADE_MS, "30 frames are more than the wait and the fade");
    reset_app();
    load_pack();
    go_designs(0);
    frames_for(400);
    key(KEY_A); frames_for(200);
    trace_start();
    key(KEY_A); frames_for(1200);
    t = trace_check();
    lo = loader_check();
    EXPECT(screen == SCR_LIST && t.job_frames > 30 && lo.runs == 1 && job_frames_show(SCR_DESIGNS, 3),
           "Designs to the folder view: %d job frames (30 of work, then the ring fades), the tiles drawn under the "
           "loader", t.job_frames);
    EXPECT(lo.before == WAIT_FRAMES && lo.loader == t.job_frames - WAIT_FRAMES && !lo.stray,
           "no loader for the first %d frames (%d ms), then in every frame of the job, no bar (%d before, %d with "
           "it, %d stray)", lo.before, LOADER_WAIT_MS, lo.before, lo.loader, lo.stray);
    EXPECT(lo.bare && !lo.jumps && !lo.back && t.restarts == 1 && !t.restarts_same && !t.empty,
           "handed over on the bare background to the folder view's own transition, nothing jumps (bare %d, %d "
           "jumps, %d starts)", lo.bare, lo.jumps, t.restarts);
    EXPECT(select_row("sub"), "the folder selected");
    frames_for(300);
    trace_start();
    key(KEY_A); frames_for(1200);
    t = trace_check();
    lo = loader_check();
    EXPECT(screen == SCR_LIST && !strcmp(list_dir, "sdmc:/Themes/sub") && list.count == 1 && !lv.selected,
           "A: the folder opens, nothing selected (%u rows)", (unsigned)list.count);
    EXPECT(lo.before == WAIT_FRAMES && lo.bare && !lo.stray && !lo.jumps,
           "the loader after the wait, over the rows it came from; left on the bare background (%d before, %d with "
           "it, %d jumps)", lo.before, lo.loader, lo.jumps);
    EXPECT(!t.restarts && lo.after > 0 && trace[lo.after].rows == 1 && trace[lo.after].cover == 1.0f &&
           lo.back >= LOADER_FADE_MS / 16 - 1 && trace[traced - 1].cover == 0,
           "the inner folder's rows come out of the background within %d frames; no screen is started anew (%d "
           "starts)", lo.back, t.restarts);
    /* work done between the wait and the fade's end (14 frames, 224 ms): the content goes all the way first */
    worker_frames = 14;
    trace_start();
    key(KEY_Y); frames_for(1200);
    t = trace_check();
    lo = loader_check();
    EXPECT(screen == SCR_LIST && !strcmp(list_dir, "sdmc:/Themes") && t.job_frames > (LOADER_WAIT_MS + LOADER_FADE_MS) / 16,
           "Y, the work done after 14 frames: the job goes on until the content is gone (%d job frames)",
           t.job_frames);
    EXPECT(lo.before == WAIT_FRAMES && lo.bare && !lo.jumps && !lo.stray && !t.restarts && lo.back > 0,
           "then the rows come out again; nothing jumps (%d jumps, bare %d, %d frames back)", lo.jumps, lo.bare,
           lo.back);
    worker_frames = 30;
    EXPECT(select_row("a.zip"), "a.zip selected");
    frames_for(300);
    /* nothing can be pressed while the loader shows: keys and the stylus during the check do nothing */
    trace_start();
    key(KEY_A);
    frames_for(200);
    int job_was = job;
    key(KEY_B); key(KEY_A); tap(160, 100, 48); key(KEY_Y); key(KEY_START);
    EXPECT(job_was == JOB_CHECK && job == JOB_CHECK && screen == SCR_LIST && pending == HIT_NONE,
           "B, A, a tap, Y and START during the check: it goes on (job %d, screen %d)", job, screen);
    while (!job_over)
        frame();
    key(KEY_B); key(KEY_A);     /* its work is done, the ring fades: still nothing can be pressed */
    EXPECT(job == JOB_CHECK && screen == SCR_LIST && pending == HIT_NONE, "nor while its ring fades");
    frames_for(900);
    t = trace_check();
    lo = loader_check();
    EXPECT(screen == SCR_CONFIRM && installed_already() && lo.runs == 1 && popup == POP_NONE,
           "then its check screen, nothing else started (screen %d)", screen);
    EXPECT(lo.before == WAIT_FRAMES && lo.loader == t.job_frames - WAIT_FRAMES && lo.bare && !lo.stray && !lo.jumps &&
           t.restarts == 1, "the check: the loader after the wait, no bar, handed over on the bare background (%d "
           "before, %d with it, %d stray, %d jumps)", lo.before, lo.loader, lo.stray, lo.jumps);

    /* the review's cases. No thread can be made and the work takes 400 ms where it was started: no frame of that
     * job was drawn, so no loader comes afterwards either; the folder view at once */
    reset_app();
    go_designs(0);
    frames_for(400);
    key(KEY_A); frames_for(200);
    threads_fail = 1;
    threads_fail_ms = 400;
    trace_start();
    key(KEY_A); frames_for(600);
    threads_fail = 0;
    threads_fail_ms = 0;
    t = trace_check();
    lo = loader_check();
    EXPECT(screen == SCR_LIST && !lo.loader && !lo.back && !lo.jumps && t.job_frames <= 1 && t.restarts == 1,
           "no worker, 400 ms of work on this thread: no loader afterwards, the folder view at once (%d job frames, "
           "%d with the loader)", t.job_frames, lo.loader);
    /* a download that cannot start right after a job that showed its loader: its end at once, not a job that is
     * never left */
    worker_frames = 30;
    EXPECT(select_row("sub"), "the folder selected");
    frames_for(300);
    key(KEY_A); frames_for(1200);
    worker_frames = 0;
    EXPECT(screen == SCR_LIST && job_over_dots > 0, "a job that showed its loader before");
    snprintf(url, sizeof url, "https://example.org/a.zip");
    text_shown(url_shown, sizeof url_shown, url);
    screen = SCR_URL;
    frames_for(300);
    int net_begins = stub_work_begins, net_ends = stub_work_ends;
    fake_net_fail = 1;
    key(KEY_A); frames_for(200); key(KEY_A); frames_for(500);
    fake_net_fail = 0;
    EXPECT(screen == SCR_PROBLEM && job == JOB_NONE && dl.status == DL_NETWORK,
           "the download cannot start: the problem's screen at once, no job left over (screen %d, job %d)", screen,
           job);
    EXPECT(stub_work_begins == net_begins && stub_work_ends == net_ends, "... and the working loop is told neither a "
           "begin nor an end (%d, %d)", stub_work_begins - net_begins, stub_work_ends - net_ends);
    key(KEY_B); frames_for(400);
    /* the folder view could not be read again after a delete on the check screen (as that leaves it): going back
     * says so once; and a folder read anew makes the view true again */
    open_folder_view();
    EXPECT(select_row("a.zip"), "a.zip selected");
    frames_for(300);
    key(KEY_A); frames_for(500);
    list_unread = 1;
    key(KEY_B); frames_for(500);
    EXPECT(screen == SCR_PROBLEM && !list_unread, "back from the check screen: the reason, once (screen %d)", screen);
    key(KEY_B); frames_for(500);
    open_folder_view();
    list_unread = 1;
    EXPECT(select_row("sub"), "the folder selected");
    frames_for(300);
    key(KEY_A); frames_for(500);
    EXPECT(screen == SCR_LIST && !list_unread, "a folder read anew: the view is true again");
    key(KEY_Y); frames_for(500);
    EXPECT(select_row("a.zip"), "a.zip selected");
    frames_for(300);
    worker_frames = 30;
    key(KEY_A); frames_for(1200);
    EXPECT(screen == SCR_CONFIRM && installed_already(), "its check screen again");

    /* 32. F5, "Design löschen" on the check screen of a file from the folder view: one load with the loader, HOME
     *     and the lid held off; the folder view is read again in it, so going back shows it true at once */
    printf("-- 32. \"Design löschen\" from the check screen: one load, the folder view true when it returns\n");
    EXPECT(row("a.zip") && row("a.zip")->in_pack_usable, "a.zip shows installed");
    float scrolled_was = lv.scroll;
    key(KEY_A); frames_for(200); key(KEY_A); frames_for(500);
    EXPECT(popup == POP_ASK_THEME, "the question");
    key(KEY_DRIGHT); frames_for(100); key(KEY_DRIGHT); frames_for(100);
    deletes_were = fake_deletes;
    threads_started = 0;
    int del_begins = stub_work_begins, del_ends = stub_work_ends;
    trace_start();
    key(KEY_A); frames_for(1200);
    t = trace_check();
    lo = loader_check();
    EXPECT(fake_deletes == deletes_were + 1 && popup == POP_NOTE && screen == SCR_CONFIRM && can_install(),
           "deleted, the note over the check screen, which offers the install");
    EXPECT(stub_work_begins == del_begins + 1 && stub_work_ends == del_ends + 1, "the working loop is told the "
           "delete's begin and its end, once each (%d, %d)", stub_work_begins - del_begins, stub_work_ends - del_ends);
    EXPECT(threads_started == 1 && lo.runs == 1 && lo.before == WAIT_FRAMES && lo.bare && !lo.stray &&
           job_frames_show(SCR_CONFIRM, 2), "one job, the loader after the wait over the check screen (%d threads, "
           "%d before, %d with it)", threads_started, lo.before, lo.loader);
    EXPECT(!lo.jumps && !t.restarts && lo.back > 0, "the check screen comes out of the background again under the "
           "note; nothing jumps, no screen is started anew (%d jumps, %d starts)", lo.jumps, t.restarts);
    EXPECT(row("a.zip") && !row("a.zip")->in_pack_usable && row("m2.zip") && row("m2.zip")->in_pack_usable,
           "the folder view under it is read again already: a.zip no longer installed, the other theme's file still");
    key(KEY_B); frames_for(400);
    threads_started = 0;
    trace_start();
    key(KEY_B); frames_for(600);
    t = trace_check();
    EXPECT(screen == SCR_LIST && !threads_started && !t.job_frames && lv.selected &&
           !strcmp(list.items[lv.cursor].file, "a.zip") && lv.scroll == scrolled_was,
           "back: the folder view at once, nothing read (%d threads), a.zip selected, the scroll as it was",
           threads_started);

    /* 33. F5, the plan refuses right before the call (HOME took the design into use after the question opened):
     *     the note, nothing deleted, the pack not read again, the screen as it was */
    printf("-- 33. a delete refused right before the call\n");
    design_screen(3);
    key(KEY_A); frames_for(200); key(KEY_A); frames_for(500);
    EXPECT(popup == POP_ASK_THEME, "the question");
    fake_home.current = (home_entry){3, HOME_PACK_VARIATION, HOME_TYPE_DLC};
    deletes_were = fake_deletes;
    const pack_theme *themes_were = pv.themes;
    key(KEY_DRIGHT); frames_for(100); key(KEY_DRIGHT); frames_for(100); key(KEY_A); frames_for(900);
    EXPECT(popup == POP_NOTE && fake_deletes == deletes_were && screen == SCR_DESIGN && pv.themes == themes_were &&
           !del.ran, "refused: the note, nothing deleted, the pack as it was (popup %d, screen %d)", popup, screen);
    fake_home.current = (home_entry){0, 0, 0};
    key(KEY_B); frames_for(400);
    worker_frames = 0;

    /* 34. the log says how each change of screen and each popup's opening and closing went; its lines wait for a
     *     moment when nothing moves, and go to the card at once only while the pack or the card is written */
    printf("-- 34. the motion lines and when the log is written to the card\n");
    reset_app();
    go_designs(0);
    frames_for(600);
    stub_motion_lines = 0;
    int flushes_were = stub_flushes, loose_were = stub_loose_lines;
    key(KEY_A); frames_for(200); key(KEY_A);    /* "Von SD installieren": the folder is read, its line logged */
    while (screen != SCR_LIST)
        frame();
    frames_for(200);
    EXPECT(stub_loose_lines > loose_were && stub_flushes == flushes_were && moving.active,
           "the folder view: its line is in the log's buffer, nothing went to the card while the screen comes in "
           "(%d lines, %d flushes)", stub_loose_lines - loose_were, stub_flushes - flushes_were);
    frames_for(400);
    EXPECT(stub_flushes == flushes_were + 1 && !moving.active, "then to the card, once (%d)",
           stub_flushes - flushes_were);
    key(KEY_B); frames_for(600);
    stub_motion_lines = 0;
    key(KEY_B); frames_for(700);
    EXPECT(screen == SCR_MAIN, "B, B: the start screen");
    unsigned first = 0, n_frames = 0, ms = 0, slowest = 0;
    int got = sscanf(stub_motion_line, "motion: screen designs to start: first frame after %u ms, %u frames in %u ms, "
                     "slowest %u ms", &first, &n_frames, &ms, &slowest);
    EXPECT(stub_motion_lines == 1 && got == 4 && first == 16 && slowest == 16 && ms >= 300 && ms < 320 &&
           n_frames == ms / 16 + 1 && stub_flushes > flushes_were,
           "then one line, and the log goes to the card: \"%s\"", stub_motion_line);
    /* work on this thread before the screen's first frame, and a lost frame in the movement, show in the line */
    key(KEY_A); frames_for(100);
    sim_keys |= KEY_A; frame(); sim_keys &= ~KEY_A;     /* the push on "Designs" */
    while (pending != HIT_NONE && sim_now < pending_at)
        frame();
    sim_now += 100;                                     /* as if the action took 100 ms */
    frame();
    frames_for(64);
    sim_now += 16;                                      /* a frame lost */
    frames_for(400);
    got = sscanf(stub_motion_line, "motion: screen start to designs: first frame after %u ms, %u frames in %u ms, "
                 "slowest %u ms", &first, &n_frames, &ms, &slowest);
    EXPECT(got == 4 && first == 116 && slowest == 32, "work before the first frame and a lost frame: \"%s\"",
           stub_motion_line);
    design_screen(3);
    stub_motion_lines = 0;
    key(KEY_A); frames_for(200); key(KEY_A); frames_for(700);
    EXPECT(popup == POP_ASK_THEME && stub_motion_lines == 1 && strstr(stub_motion_line, "popup delete theme opened"),
           "a popup's opening: \"%s\"", stub_motion_line);
    key(KEY_B); frames_for(700);
    EXPECT(popup == POP_NONE && stub_motion_lines == 2 && strstr(stub_motion_line, "popup delete theme closed"),
           "and its closing: \"%s\"", stub_motion_line);
    /* a delete: every line of it at once to the card, written by its job; none from the frames drawn meanwhile
     * (the delete starts when its question has faded out, POPUP_OUT_MS after the answer) */
    worker_frames = 30;
    key(KEY_A); frames_for(200); key(KEY_A); frames_for(700);
    key(KEY_DRIGHT); frames_for(100); key(KEY_DRIGHT); frames_for(100);
    EXPECT(popup == POP_ASK_THEME && !moving.active, "the question again, nothing moving");
    int strict_were = stub_strict_lines;
    stub_motion_lines = loose_in_delete = 0;
    stub_log_hook = watch_log;
    sim_keys |= KEY_A; frame(); sim_keys &= ~KEY_A;
    while (popup == POP_ASK_THEME || popup_then != POP_NONE)
        frame();
    stub_log_hook = NULL;
    EXPECT(popup == POP_NOTE && stub_strict_lines > strict_were + 2 && !loose_in_delete && !stub_strict,
           "the delete's %d lines went to the card at once; no other line was written while it ran (%d)",
           stub_strict_lines - strict_were, loose_in_delete);
    frames_for(700);
    EXPECT(stub_motion_lines >= 1 && strstr(stub_motion_line, "screen design to installed") &&
           strstr(stub_motion_line, "popup note opened"), "afterwards the lines of what moved: \"%s\"",
           stub_motion_line);
    key(KEY_B); frames_for(400);
    /* a question answered as soon as its keys act (POPUP_GUARD_MS after its first frame, G1-R7; before G1-R7 keys
     * answered it while its opening was still watched): the opening's line, and the closing's, which ends inside
     * the delete, are not lost */
    design_screen(4);
    key(KEY_A); frames_for(200);
    motion_lines[0] = 0;
    stub_log_hook = watch_log;
    sim_keys |= KEY_A; frame(); sim_keys &= ~KEY_A;
    while (popup != POP_ASK_THEME)
        frame();
    frames_for(POPUP_GUARD_MS);
    key(KEY_DRIGHT); key(KEY_DRIGHT);
    sim_keys |= KEY_A; frame(); sim_keys &= ~KEY_A;
    while (popup == POP_ASK_THEME || popup_then != POP_NONE)
        frame();
    frames_for(700);
    stub_log_hook = NULL;
    const char *cut_at = strstr(motion_lines, "popup delete theme opened");
    const char *closed_at = cut_at ? strstr(cut_at, "popup delete theme closed") : NULL;
    EXPECT(popup == POP_NOTE && cut_at && closed_at && strstr(closed_at, "screen design to installed, popup note opened"),
           "opened, closed and what came after: a line each, in their order:\n%s", motion_lines);
    key(KEY_B); frames_for(400);
    worker_frames = 0;

    /* 35. F8, item 20: the folder /Themes is missing at the start. Asked over the start screen before any input;
     *     yes creates that folder and nothing else, without another word; the next start does not ask */
    printf("-- 35. /Themes missing at the start: asked, created\n");
    close_theme();
    if (rename("sdmc:/Themes", "sdmc:/Themes.away"))
        return 3;
    unlink("sdmc:/theme-pack/themes-folder.declined");
    int in_root = entries("sdmc:"), in_data = entries("sdmc:/theme-pack");
    reset_app();
    load_pack();
    go_main(0);
    themes_start();
    EXPECT(themes_missing && !themes_declined && popup == POP_ASK_FOLDER && screen == SCR_MAIN,
           "asked over the start screen (popup %d)", popup);
    frames_for(500);
    key(KEY_START); frames_for(200);
    EXPECT(popup == POP_ASK_FOLDER && screen == SCR_MAIN, "START under the question does nothing");
    threads_started = 0;
    key(KEY_DRIGHT); frames_for(100); key(KEY_DRIGHT); frames_for(100); key(KEY_A); frames_for(500);
    EXPECT(stat("sdmc:/Themes", &st) == 0 && S_ISDIR(st.st_mode) && popup == POP_NONE && screen == SCR_MAIN &&
           threads_started == 1 && last_thread_job == JOB_THEMES, "\"Erstellen\": the folder is there, made by a "
           "job, no note (popup %d)", popup);
    EXPECT(stat("sdmc:/theme-pack/themes-folder.declined", &st) != 0 && entries("sdmc:") == in_root + 1 &&
           entries("sdmc:/theme-pack") == in_data && !entries("sdmc:/Themes"),
           "no mark; nothing else new on the card (%d entries in the root, were %d)", entries("sdmc:"), in_root);
    reset_app();
    load_pack();
    go_main(0);
    themes_start();
    frames_for(300);
    EXPECT(!themes_missing && popup == POP_NONE, "the next start does not ask");

    /* 36. F8: declined at the start. Nothing is created, the next start does not ask again; "Von SD installieren"
     *     asks again; declined there: the card's root, as before; said yes there: the folder, and its view */
    printf("-- 36. /Themes missing: declined at the start, asked again when it is needed\n");
    rmdir("sdmc:/Themes");
    reset_app();
    load_pack();
    go_main(0);
    themes_start();
    frames_for(500);
    EXPECT(popup == POP_ASK_FOLDER, "asked again once the folder is gone (the mark went with the create)");
    key(KEY_B); frames_for(500);
    EXPECT(popup == POP_NONE && screen == SCR_MAIN && stat("sdmc:/Themes", &st) != 0 &&
           stat("sdmc:/theme-pack/themes-folder.declined", &st) == 0 && st.st_size == 0,
           "B: nothing created, the mark on the card");
    reset_app();
    load_pack();
    go_main(0);
    themes_start();
    frames_for(300);
    EXPECT(themes_missing && themes_declined && popup == POP_NONE, "the next start does not ask again");
    go_designs(0);
    frames_for(300);
    key(KEY_A); frames_for(200); key(KEY_A); frames_for(500);
    EXPECT(popup == POP_ASK_FOLDER && screen == SCR_DESIGNS, "\"Von SD installieren\": asked again (popup %d, "
           "screen %d)", popup, screen);
    mkdir("sdmc:/Nintendo 3DS", 0755);     /* the root hides the system's folder and names with a dot (R16) */
    mkdir("sdmc:/.Trashes", 0755);
    key(KEY_DRIGHT); frames_for(100); key(KEY_DLEFT); frames_for(100); key(KEY_A); frames_for(700);
    EXPECT(popup == POP_NONE && screen == SCR_LIST && scan_is_root(list_dir) && stat("sdmc:/Themes", &st) != 0,
           "\"Abbrechen\" there: the card's root, nothing created (screen %d, %s)", screen, list_dir);
    int hidden_shown = 0;
    for (size_t k = 0; k < list.count; k++)
        hidden_shown += !strcmp(list.items[k].file, "Nintendo 3DS") || list.items[k].file[0] == '.';
    EXPECT(list.folders > 0 && !hidden_shown, "the root lists no Nintendo 3DS and no name with a dot (%u entries)",
           (unsigned)list.count);
    rmdir("sdmc:/Nintendo 3DS");
    rmdir("sdmc:/.Trashes");
    key(KEY_B); frames_for(500);
    key(KEY_A); frames_for(200); key(KEY_A); frames_for(500);
    EXPECT(popup == POP_ASK_FOLDER, "and asked again the next time");
    key(KEY_DRIGHT); frames_for(100); key(KEY_DRIGHT); frames_for(100); key(KEY_A); frames_for(700);
    EXPECT(popup == POP_NONE && screen == SCR_LIST && !strcmp(list_dir, "sdmc:/Themes") && !list.count &&
           stat("sdmc:/Themes", &st) == 0 && stat("sdmc:/theme-pack/themes-folder.declined", &st) != 0,
           "\"Erstellen\" there: the folder, its empty view, the mark gone (screen %d, %s)", screen, list_dir);

    /* 37. F8: the folder cannot be created (the card takes no new folder here): said plainly in a note, nothing
     *     else changes; the same when it was asked for "Von SD installieren" */
    printf("-- 37. /Themes cannot be created\n");
    rmdir("sdmc:/Themes");
    chmod("sdmc:", 0555);
    reset_app();
    load_pack();
    go_main(0);
    themes_start();
    frames_for(500);
    EXPECT(popup == POP_ASK_FOLDER, "asked");
    key(KEY_DRIGHT); frames_for(100); key(KEY_DRIGHT); frames_for(100); key(KEY_A); frames_for(500);
    EXPECT(popup == POP_NOTE && screen == SCR_MAIN && themes_make && !themes_made && stat("sdmc:/Themes", &st) != 0 &&
           stat("sdmc:/theme-pack/themes-folder.declined", &st) != 0,
           "\"Erstellen\": the note that it could not be created, no mark (popup %d)", popup);
    key(KEY_A); frames_for(400);
    EXPECT(popup == POP_NONE && screen == SCR_MAIN, "its Okay closes it");
    go_designs(0);
    frames_for(300);
    key(KEY_A); frames_for(200); key(KEY_A); frames_for(500);
    key(KEY_DRIGHT); frames_for(100); key(KEY_DRIGHT); frames_for(100); key(KEY_A); frames_for(700);
    EXPECT(popup == POP_NOTE && screen == SCR_DESIGNS && job == JOB_NONE, "for \"Von SD installieren\": the same "
           "note, the Designs screen stays (popup %d, screen %d)", popup, screen);
    key(KEY_A); frames_for(400);
    chmod("sdmc:", 0755);
    rmdir("sdmc:/Themes");
    if (rename("sdmc:/Themes.away", "sdmc:/Themes"))
        return 3;
    unlink("sdmc:/theme-pack/themes-folder.declined");
    go_main(0);
    frames_for(300);
    /* 38. F7, the install on the worker: the same AM calls in the same order with the same bytes as install_run
     *     called on this thread with the console's importer (what 0.9.1 did), the same record files and the same
     *     result, for a whole install and for a failure at every single call; after a failure the same screen and
     *     the same words. A third of the runs with a worker that takes 30 frames, a third with one that is done at
     *     once, a third with no thread to be had (the install then runs on this thread). Every AM call is made
     *     while HOME and sleep are held off and before the main loop comes round again. */
    printf("-- 38. the install on the worker against install_run on this thread\n");
    make_pack(argv[1], argv[2], 0);
    for (size_t j = 0; j < fake_am.count; j++)      /* theme 2 (a.zip's) deleted in HOME: a.zip can be installed */
        fake_am.usable[j] = fake_am.list[j].index != 2;
    copy_to(argv[1], "sdmc:/Themes/a.zip");
    store_copy("sdmc:/theme-pack", "store-snapshot");
    am_view am_was = fake_am;
    int calls = 0, differ = 0, unguarded = 0, drawn = 0, ran = 0;
    char first_diff[400] = "";
    for (int k = -1; k <= calls; k++) {             /* -1: count the calls; 1 to calls: that call fails; then 0 */
        int fail_at = k < 0 ? 0 : k == calls ? 0 : k + 1, all = k == calls;
        store_copy("store-snapshot", "sdmc:/theme-pack");
        fake_am = am_was;
        close_theme();
        reset_app();
        load_pack();
        go_designs(0);
        frames_for(200);
        open_confirm("a.zip", "sdmc:/Themes", SCR_DESIGNS);
        frames_for(300);
        if (!can_install()) {
            differ++;
            snprintf(first_diff, sizeof first_diff, "run %d: the check screen does not offer the install", k);
            break;
        }
        /* here, as 0.9.1: install_run with the console's importer on this thread */
        static install_result here;
        static char trace_here[sizeof fake_imp_trace], store_here[400], store_app[400], title_here[160],
                    body_here[1600];
        importer imp;
        install_progress quiet = {NULL, quiet_progress};
        fake_imp_trace[0] = 0;
        fake_imp_calls = 0;
        fake_imp_fail = fail_at;
        fake_imp_guard = NULL;
        fake_installed = NULL;
        console_importer(&imp);
        install_run(&plan, &theme, &imp, CARD_DIR, &quiet, &here);
        snprintf(trace_here, sizeof trace_here, "%s", fake_imp_trace);
        store_digest(store_here, sizeof store_here);
        judgement j_here;
        install_judge(&am_was, &am_was, &plan, &here, &j_here);
        text_outcome(lang, &j_here, &here, theme.name, title_here, sizeof title_here, body_here, sizeof body_here);
        if (k < 0) {
            calls = fake_imp_calls;
            continue;
        }
        /* through the application */
        store_copy("store-snapshot", "sdmc:/theme-pack");
        fake_imp_trace[0] = 0;
        fake_imp_calls = fake_imp_unguarded = 0;
        fake_imp_guard = install_guarded;
        fake_installed = installed;
        key(KEY_A); frames_for(200);
        worker_frames = k % 3 ? 0 : 30;
        threads_fail = k % 3 == 2;
        trace_start();
        key(KEY_A); frames_for(1500);
        t = trace_check();
        worker_frames = threads_fail = 0;
        fake_imp_guard = NULL;
        store_digest(store_app, sizeof store_app);
        if (stub_verbose && all)
            fprintf(stderr, "  the whole install's AM calls:\n%s  the record files after it: %s\n", fake_imp_trace,
                    store_app);
        ran++;
        unguarded += fake_imp_unguarded;
        drawn += k % 3 ? 0 : t.job_frames;
        const char *what = NULL;
        if (strcmp(trace_here, fake_imp_trace))
            what = "the AM calls";
        else if (strcmp(store_here, store_app))
            what = "the record files";
        else if (here.done != install_res.done || here.failed != install_res.failed ||
                 here.detail != install_res.detail || here.wrote != install_res.wrote ||
                 here.tmd_accepted != install_res.tmd_accepted || here.committed1 != install_res.committed1 ||
                 here.record_ok != install_res.record_ok ||
                 memcmp(here.content0.sha256, install_res.content0.sha256, 32) ||
                 memcmp(here.theme.sha256, install_res.theme.sha256, 32) || here.theme.size != install_res.theme.size)
            what = "the install's result";
        else if (all ? screen != SCR_DONE || !may_clean
                       : screen != SCR_PROBLEM || may_clean || strcmp(title, title_here) || strcmp(body, body_here))
            what = "the screen or its words afterwards";
        else if (apt_home != 1 || apt_sleep != 1 || job != JOB_NONE || theme_open)
            what = "what is left afterwards (HOME, sleep, the job, the theme)";
        if (what && !differ++)
            snprintf(first_diff, sizeof first_diff, "failing call %d of %d: %s differ", fail_at, calls, what);
        if (popup)
            key(KEY_B);
        frames_for(200);
    }
    EXPECT(calls >= 12 && ran == calls + 1 && !differ, "%d AM calls; a whole install and a failure at each of them, "
           "%d runs: all the same as on this thread%s%s", calls, ran, differ ? ": " : "", first_diff);
    EXPECT(!unguarded, "every AM call with HOME and sleep held off, before the main loop came round (%d were not)",
           unguarded);
    EXPECT(drawn >= 30 * (calls / 3), "the frames went on while the worker ran: %d installing frames in the runs "
           "with a held worker", drawn);

    /* 39. F7: the install's log lines all go to the card at once and keep 0.9.1's order; the top screen shows only
     *     installing frames while the worker runs */
    printf("-- 39. the install's log and screen while the worker runs\n");
    store_copy("store-snapshot", "sdmc:/theme-pack");
    fake_am = am_was;
    make_archive(3);
    close_theme();
    reset_app();
    load_pack();
    go_designs(0);
    frames_for(200);
    open_confirm("a.zip", "sdmc:/Themes", SCR_DESIGNS);
    frames_for(300);
    worker_frames = 30;
    install_log[0] = 0;
    loose_in_install = 0;
    stub_log_hook = watch_install_log;
    stub_tops[0] = 0;
    key(KEY_A); frames_for(200); key(KEY_A); frames_for(1500);
    stub_log_hook = NULL;
    worker_frames = 0;
    const char *at = install_log;
    static const char *const order[] = {"install step 1 ", "install step 3 ", "install step 12 ", "install: done 1",
                                        "outcome 0 ", "pack: state "};
    int in_order = 1;
    for (size_t k = 0; k < sizeof order / sizeof *order && in_order; k++) {
        at = strstr(at, order[k]);
        in_order = at != NULL;
    }
    EXPECT(screen == SCR_DONE && in_order && !loose_in_install, "the steps, the result, the outcome, then the pack "
           "read anew, in that order, every line at once to the card (%d were not)", loose_in_install);
    EXPECT(!strcmp(stub_tops, "p") && top_has('p') && !top_has('D'), "the top screen: installing frames only, on "
           "into \"Installiert\" (G2-L2b: its top is the same picture) (%s)", stub_tops);
    key(KEY_A); frames_for(200); key(KEY_A); frames_for(500);

    /* 65. the check screen's Installieren (GO_BUTTON, y 62 to 146) opens after its read job where the stylus just
     *     was, over the folder view's rows: a touch that came down before it had been there POPUP_GUARD_MS starts
     *     no install (the reviewer's finding on the badge check screen, d9bfed9), a later one does */
    printf("-- 65. no install from a touch that came down as the check screen came in\n");
    make_pack(argv[1], argv[2], 0);
    for (size_t j = 0; j < fake_am.count; j++)      /* theme 2 (a.zip's) deleted in HOME: a.zip can be installed */
        fake_am.usable[j] = fake_am.list[j].index != 2;
    mkdir("sdmc:/Themes", 0755);
    copy_to(argv[1], "sdmc:/Themes/a.zip");
    copy_to(argv[1], "sdmc:/Themes/b.zip");
    copy_to(argv[1], "sdmc:/Themes/c.zip");
    close_theme();
    reset_app();
    load_pack();
    go_designs(0);
    frames_for(200);
    last_thread_job = JOB_NONE;
    open_confirm("a.zip", "sdmc:/Themes", SCR_DESIGNS);
    for (int i = 0; i < 60 && screen != SCR_CONFIRM; i++)
        frame();
    frame();
    tap((int)(GO_X + GO_W / 2), (int)(GO_Y + GO_H / 2), 48);
    frames_for(600);
    EXPECT(screen == SCR_CONFIRM && can_install() && last_thread_job != JOB_INSTALL,
           "a tap on Installieren in the check screen's transition, right after its read: no install (screen %d)",
           screen);
    open_folder_view();
    int gx = 0, gy = 0;
    size_t gi = 0;
    for (; gi < list.count; gi++) {
        rect rr = row_rect(gi);
        float y0 = fmaxf(rr.y, GO_Y) + 2, y1 = fminf(rr.y + rr.h, GO_Y + GO_H) - 2;
        if (!list.items[gi].folder && y1 > y0 && rr.y >= 0) {
            gx = BOTTOM_W / 2, gy = (int)((y0 + y1) / 2);
            break;
        }
    }
    EXPECT(screen == SCR_LIST && gi < list.count, "the folder view: a theme's row under where Installieren will be "
           "(%d, %d)", gx, gy);
    last_thread_job = JOB_NONE;
    tap(gx, gy, 48);
    frames_for(80);
    tap(gx, gy, 48);
    frames_for(600);
    EXPECT(screen == SCR_CONFIRM && can_install() && last_thread_job != JOB_INSTALL,
           "a double tap on the row, 80 ms apart: its check screen, no install (screen %d)", screen);
    frames_for(POPUP_GUARD_MS);
    tap((int)(GO_X + GO_W / 2), (int)(GO_Y + GO_H / 2), 48);
    frames_for(600);
    EXPECT(last_thread_job == JOB_INSTALL && screen == SCR_DONE, "a tap on Installieren after the guard installs "
           "(screen %d)", screen);
    key(KEY_A); frames_for(200); key(KEY_A); frames_for(500);

    /* 66. a press begun while no input was read (the install's job, its end hold) presses nothing on "Installiert":
     *     lifted on "Löschen" it does not delete the file (the second reviewer's finding on 9c1a90d); a press after
     *     the guard does */
    printf("-- 66. \"Installiert\" takes no press begun before it showed\n");
    struct stat kept_st;
    const int del_x = (int)(DELETE_TILE.x + DELETE_TILE.w / 2), del_y = (int)(DELETE_TILE.y + DELETE_TILE.h / 2);
    static const char *const WHEN[3] = {"", "during the install", "during its end hold"};
    for (int when = 1; when <= 2; when++) {
        int shown = install_held(argv[1], argv[2], when, del_x, del_y, 0);
        int held = (sim_keys & KEY_TOUCH) != 0;
        frames_for(150);
        lift();
        frames_for(800);
        EXPECT(shown && held && stat("sdmc:/Themes/a.zip", &kept_st) == 0, "the stylus down on Löschen %s, lifted "
               "there once Installiert showed: the file stays (shown %d, held %d)", WHEN[when], shown, held);
    }
    int shown_a = install_held(argv[1], argv[2], 1, 0, 0, KEY_A);
    frames_for(150);
    sim_keys &= ~KEY_A;
    frames_for(800);
    EXPECT(shown_a && stat("sdmc:/Themes/a.zip", &kept_st) == 0, "A held through the install: the file stays");
    /* G1-R5b: a key (A, B) down from the install's job on and held as "Installiert" or the problem screen opens
     *     presses nothing there, as a touch (66); let go and pressed again it acts as there before (A on
     *     "Installiert" shows the cursor, A on the problem and B on both lead away) */
    printf("-- G1-R5b: a key held through the install\n");
    for (int fail = 0; fail < 2; fail++)
        for (int k = 0; k < 2; k++) {
            u32 held_key = k ? KEY_B : KEY_A;
            r5b_target = fail ? SCR_PROBLEM : SCR_DONE;
            fake_imp_calls = 0;
            fake_imp_fail = fail;
            install_held(argv[1], argv[2], 1, 0, 0, held_key);
            fake_imp_fail = 0;
            int shown = screen == r5b_target && !ring;
            int again = still_until_again(held_key, left_target);
            EXPECT(shown && again, "%s down during the install, held as %s opens: it stays, no cursor, until %s is "
                   "let go and pressed again (screen %d, ring %d)", k ? "B" : "A",
                   fail ? "the problem screen" : "\"Installiert\"", k ? "B" : "A", screen, ring);
        }
    install_held(argv[1], argv[2], 0, 0, 0, 0);
    frames_for(POPUP_GUARD_MS);
    tap(del_x, del_y, 48);
    frames_for(800);
    EXPECT(stat("sdmc:/Themes/a.zip", &kept_st) != 0, "a tap on Löschen after the guard deletes the file");

    /* 63. F9: a whole install through the application (here its work is done at once). "Installiert" comes when the
     *     picture is full and its glow has run (R9: POUR_CATCH_MS + POUR_END_MS at most), by the clock; the last
     *     installing frame shows the picture full. 64: the log says how the installing screen's frames went */
    printf("-- 63. \"Installiert\" comes on time, the picture full; 64. the installing screen's line in the log\n");
    make_pack(argv[1], argv[2], 0);
    for (size_t j = 0; j < fake_am.count; j++)      /* theme 2 (a.zip's) deleted in HOME: a.zip can be installed */
        fake_am.usable[j] = fake_am.list[j].index != 2;
    copy_to(argv[1], "sdmc:/Themes/a.zip");
    close_theme();
    reset_app();
    load_pack();
    go_designs(0);
    frames_for(200);
    open_confirm("a.zip", "sdmc:/Themes", SCR_DESIGNS);
    frames_for(300);
    EXPECT(screen == SCR_CONFIRM && can_install(), "a.zip's check screen offers the install");
    key(KEY_A); frames_for(200);
    stub_motion_line[0] = 0;
    motion_lines[0] = 0;
    stub_log_hook = watch_log;
    int pour_were = stub_pour_frames;
    watch_end_begin(SCR_DONE);
    key(KEY_A); frames_for(600);
    stub_log_hook = NULL;
    stub_frame_done = NULL;
    int pour_seen = stub_pour_frames - pour_were - end_pours;   /* G2-L2b: "Installiert" goes on with the picture */
    u64 shown_for = install_last - install_since, glowed = install_full_at ? install_last - install_full_at : 0;
    EXPECT(screen == SCR_DONE && pour_seen > 20 && stub_pour == &install_pour && pour_over(&install_pour),
           "Installiert; the last installing frame: the picture full, its glow to its end (%d frames)", pour_seen);
    EXPECT(shown_for <= POUR_CATCH_MS + POUR_END_MS + 32 && glowed >= POUR_END_MS && glowed <= POUR_END_MS + 32,
           "the installing screen showed for %llu ms (at most %d + %d), %llu of them full (%d)",
           (unsigned long long)shown_for, POUR_CATCH_MS, POUR_END_MS, (unsigned long long)glowed, POUR_END_MS);
    unsigned logged_frames = 0, logged_ms = 0, logged_slowest = 0;
    const char *line = strstr(motion_lines, "motion: installing screen: ");
    int got_line = line ? sscanf(line, "motion: installing screen: %u frames in %u ms, slowest %u ms", &logged_frames,
                                 &logged_ms, &logged_slowest) : 0;
    EXPECT(got_line == 3 && (int)logged_frames == pour_seen && logged_ms == 16 * logged_frames && logged_slowest == 16,
           "the log: %u frames in %u ms, slowest %u ms (drawn: %d frames)", logged_frames, logged_ms, logged_slowest,
           pour_seen);
    key(KEY_A); frames_for(200); key(KEY_A); frames_for(500);
    /* SND-1 (C163: "Z6 für jedes oktaeder das fällt"): the drop plays once for every piece of the installing picture
     *     as it lands by the model's clock, in the frame that shows it; pieces that land in the same frame sound
     *     once; none for a piece that never lands, none for the time no frame is drawn, none with the sounds off.
     *     The work is held 150 frames and its level rises meanwhile, so pieces land during it and after it */
    printf("-- SND-1: a drop for every piece that lands\n");
    install_hold = 150;
    drops_job = JOB_INSTALL;
    drops_ramp = 0.004f;
    drops_begin(&install_pour);
    int d_ok = install_held(argv[1], argv[2], 0, 0, 0, 0);
    frames_for(1000);
    drops_end();
    EXPECT(d_ok && drops_pieces == install_pour.total && install_pour.landed == install_pour.total &&
           drops_heard == drops_pieces - drops_shared && drops_frames == drops_heard && !drops_wrong &&
           !drops_before_first && drops_late < frame_ms,
           "an install: %d pieces landed, %d drops (%d pieces shared a frame), one in each frame with a landing and in "
           "no other (%d wrong), the latest %llu ms after its landing, none before the first", drops_pieces,
           drops_heard, drops_shared, drops_wrong, (unsigned long long)drops_late);
    frame_ms = 700;             /* a console that loses frames: several pieces land between two */
    install_hold = 12;
    drops_ramp = 0.05f;
    drops_begin(&install_pour);
    d_ok = install_held(argv[1], argv[2], 0, 0, 0, 0);
    frames_for(4000);
    drops_end();
    frame_ms = 16;
    EXPECT(d_ok && drops_pieces == install_pour.total && drops_shared > 0 && drops_heard == drops_pieces - drops_shared &&
           !drops_wrong && drops_late < 700, "a frame every 700 ms: %d pieces landed in %d frames, %d drops (%d pieces "
           "shared a frame), the latest %llu ms after its landing", drops_pieces, drops_frames, drops_heard,
           drops_shared, (unsigned long long)drops_late);
    install_hold = 150;
    drops_ramp = 0.004f;
    drops_mute = 1;
    drops_begin(&install_pour);
    d_ok = install_held(argv[1], argv[2], 0, 0, 0, 0);
    frames_for(1000);
    EXPECT(d_ok && !prefs.sounds && drops_pieces == install_pour.total && !drops_heard && !drops_wrong,
           "the sounds off: %d pieces landed, %d drops", drops_pieces, drops_heard);
    drops_end();
    fake_imp_calls = 0;
    fake_imp_fail = 1;
    drops_ramp = 0.004f;
    drops_fail_screen = SCR_PROBLEM;
    drops_begin(&install_pour);
    d_ok = install_held(argv[1], argv[2], 0, 0, 0, 0);
    frames_for(2000);
    int d_failed = drops_failed, d_in_air = drops_in_air, d_after = drops_after_fail;
    drops_end();
    fake_imp_fail = 0;
    EXPECT(!d_ok && d_failed && d_in_air > 0 && !d_after && drops_heard == drops_frames && !drops_wrong,
           "an install that fails: %d drops for the %d pieces that landed before; %d pieces in the air never land, "
           "and no drop sounds after it (%d)", drops_heard, drops_pieces, d_in_air, d_after);
    threads_fail = 1;           /* no thread: the work runs here for 400 ms without a frame, the picture after it */
    threads_fail_ms = 400;
    drops_begin(&install_pour);
    d_ok = install_held(argv[1], argv[2], 0, 0, 0, 0);
    frames_for(1000);
    drops_end();
    threads_fail = 0;
    threads_fail_ms = 0;
    install_hold = 20;
    EXPECT(d_ok && drops_pieces == install_pour.total && drops_heard == drops_pieces - drops_shared && !drops_wrong &&
           drops_late < frame_ms, "no worker, 400 ms of work without a frame: no landing and no drop for that time "
           "(the latest %llu ms after its landing); %d pieces landed after it, %d drops",
           (unsigned long long)drops_late, drops_pieces, drops_heard);
    key(KEY_A); frames_for(200); key(KEY_A); frames_for(500);
    /* 50. 0.9.2: every accepted input has its sound, and only an accepted one: the cursor shown or moved, a push
     *     (the bar's B and a question's "Abbrechen" go back, the rest confirms), a popup, a problem, an install's
     *     end */
    printf("-- sounds\n");
    close_theme();
    if (system("rm -rf sdmc:/Themes"))
        return 3;
    mkdir("sdmc:/Themes", 0755);
    static uint8_t s_body[SYNTH_BODY_MAX], s_lz[SYNTH_LZ_MAX], s_music[SYNTH_BCSTM_MAX(3000)];
    static uint8_t s_zip[SYNTH_ZIP_MAX(SYNTH_LZ_MAX, sizeof s_music)];
    size_t s_music_size = synth_bcstm(s_music, 3000, 1000);
    size_t s_lz_size = synth_lz11(s_body, synth_body(s_body, 1, 1, 4), s_lz);       /* still on top, a flip book below */
    write_file("sdmc:/Themes/synth.zip", s_zip, synth_zip(s_zip, s_lz, s_lz_size, s_music, s_music_size, "Synth"));
    s_lz_size = synth_lz11(s_body, synth_body_colors(s_body, 0), s_lz);
    write_file("sdmc:/Themes/plain.zip", s_zip, synth_zip(s_zip, s_lz, s_lz_size, NULL, 0, "Plain"));
    write_file("sdmc:/Themes/broken.zip", (const uint8_t *)"PK\3\4 not a zip", 14);
    make_pack(argv[1], argv[2], 0);
    reset_app();
    load_pack();
    go_designs(0);
    frames_for(300);
    forget_sounds();
    key(KEY_DRIGHT); frames_for(100);
    EXPECT(ring && played(SFX_MOVE) == 1, "the first D-pad press shows the cursor: the move sound");
    key(KEY_DRIGHT); frames_for(100);
    EXPECT(focus == 1 && played(SFX_MOVE) == 1, "right: the cursor moves, the move sound");
    key(KEY_DRIGHT); frames_for(100);
    EXPECT(focus == 1 && played(SFX_MOVE) == 0, "right again: it stays, no sound");
    key(KEY_DLEFT); frames_for(100);
    key(KEY_A); frames_for(500);
    EXPECT(screen == SCR_LIST && played(SFX_CONFIRM) == 1 && played(SFX_MOVE) == 1 && played(SFX_BACK) == 0,
           "A on the tile: the confirm sound (screen %d)", screen);
    key(KEY_DDOWN); frames_for(150);
    EXPECT(lv.selected && lv.cursor == 0 && played(SFX_MOVE) == 1,
           "down in the folder view: a row is selected, the move sound");
    for (int i = 0; i < 6; i++) { key(KEY_DDOWN); frames_for(150); }
    EXPECT(lv.cursor == list.count - 1 && played(SFX_MOVE) == (int)list.count - 1,
           "down to the last of %u rows: one move sound a row, none at the end", (unsigned)list.count);
    key(KEY_B); frames_for(500);
    EXPECT(screen == SCR_DESIGNS && played(SFX_BACK) == 1 && played(SFX_CONFIRM) == 0 && played(SFX_DROP) == 0,
           "B: the back sound");
    tap(TILE_X1 + 20, DTILE_Y + 20, 48); frames_for(500);
    EXPECT(screen == SCR_LIST && played(SFX_CONFIRM) == 1 && played(SFX_MOVE) == 0, "a tap on a tile: the confirm sound");
    tap(BOTTOM_W / 2, 100, 16); frames_for(300);
    confirm_installed("sound.zip");
    forget_sounds();
    tap(236, 120, 48); frames_for(600);
    EXPECT(popup == POP_ASK_FILE && played(SFX_CONFIRM) == 1 && played(SFX_POPUP) == 1,
           "a tap on \"Datei löschen\": the confirm sound, then the popup's");
    key(KEY_DRIGHT); frames_for(100);
    EXPECT(pop_focus.ring && played(SFX_MOVE) == 1, "the question's cursor shows: the move sound");
    key(KEY_B); frames_for(400);
    EXPECT(popup == POP_NONE && victim_exists() && played(SFX_BACK) == 1 && played(SFX_CONFIRM) == 0,
           "B in the question: the back sound");
    unlink(victim);
    show_problem(T_SCAN_QR, "x", "", 0);
    EXPECT(played(SFX_ERROR) == 1, "a problem: the error sound");
    frames_for(300);
    key(KEY_B); frames_for(400);
    EXPECT(played(SFX_BACK) == 1, "B on a problem with \"Zurück\": the back sound");
    show_problem(T_INSTALL_THEME, "x", "", 1);
    frames_for(300);
    forget_sounds();
    key(KEY_A); frames_for(400);
    EXPECT(played(SFX_CONFIRM) == 1 && played(SFX_BACK) == 0, "A on a problem with \"OK\": the confirm sound");
    finish(1);
    EXPECT(screen == SCR_DONE && played(SFX_DONE) == 1 && played(SFX_ERROR) == 0, "an install ends well: the done sound");
    finish(0);
    EXPECT(screen == SCR_PROBLEM && played(SFX_ERROR) == 1 && played(SFX_DONE) == 0, "it ends badly: the error sound");

    /* 51. 0.9.2: Y on the check screen of a file. The theme's body and music are read on the preview's own thread
     *     (40 frames here) while the top shows the wait and nothing listens; then both screens show its wallpapers
     *     from the body's textures, its music plays and the menu music pauses; B ends it and the check screen is
     *     as it was */
    printf("-- the preview of a file\n");
    worker_frames = 6;
    open_folder_view();
    EXPECT(select_row("synth.zip"), "synth.zip selected");
    key(KEY_A); frames_for(600);
    EXPECT(screen == SCR_CONFIRM && music_choice() && can_preview() && stub_bar_y && bar_drawn_last(),
           "the check screen of a theme with music: two buttons, the footer with the Y tab (screen %d)", screen);
    EXPECT(target_at(20, SCREEN_H - 4) == HIT_LEFT && target_at(BOTTOM_W - 20, SCREEN_H - 4) == HIT_PREVIEW &&
           target_at(GO_X + 20, GO_TWO_Y1 + 20) == HIT_FIRST, "the left tab backs, the right tab previews, the buttons as before");
    focus = 1;
    int sets_were = stub_wall_sets, starts_were = stub_theme_starts;
    forget_sounds();
    worker_frames = 40;
    key(KEY_Y); frames_for(PRESS_MS + 32);
    EXPECT(screen == SCR_PREVIEW && preview_seen == PREVIEW_LOADING && !stub_busy_top && stub_loader &&
           stub_loader_shown <= 0 && !stub_menu_paused && played(SFX_CONFIRM) == 1,
           "Y: the preview's wait: no bar on the top screen, the loader's ring not yet there (%d ms are not over), "
           "the menu music still on", LOADER_WAIT_MS);
    EXPECT(stub_wall_drawn[0] == 0 && stub_wall_drawn[1] == 0 && !bar_drawn_last(), "no wallpaper and no bar yet");
    key(KEY_B); frames_for(150); tap(20, SCREEN_H - 4, 48); frames_for(150);
    EXPECT(screen == SCR_PREVIEW && preview_seen == PREVIEW_LOADING && pending == HIT_NONE && played(SFX_BACK) == 0,
           "B and a tap on the bar's place while it reads: nothing");
    EXPECT(stub_loader && stub_loader_cover >= 1 && stub_loader_shown > 0.99f && !stub_busy_top,
           "after the wait and the fade the loader's ring stands on the bottom screen, as a job's (%.2f)",
           stub_loader_shown);
    frames_for(400);
    worker_frames = 6;
    EXPECT(screen == SCR_PREVIEW && preview_seen == PREVIEW_READY && stub_wall_sets == sets_were + 2,
           "read: both wallpapers handed to the drawing (state %d)", preview_seen);
    EXPECT(strstr(stub_motion_line, "to preview: ") != NULL, "the log's motion line names the preview: \"%s\"",
           stub_motion_line);
    EXPECT(stub_wall_width[0] == 512 && stub_wall_width[1] == 1024 && stub_wall_first[0] == synth_texel(0, 0, 0) &&
           stub_wall_first[1] == synth_texel(0, 0, 0x5A5A), "the body's two textures: 512 wide on top, 1024 below");
    EXPECT(stub_wall_drawn[0] == 'w' && stub_wall_drawn[1] == 'w' && stub_wall_x[0] == 6 && bar_drawn_last() &&
           !stub_bar_y && !stub_busy_top, "both drawn, the top one's middle 400 of 412, the bar over the bottom one");
    EXPECT(stub_theme_music && stub_menu_paused && stub_theme_starts == starts_were + 1 && stub_theme_file,
           "its music plays, the menu music is paused");
    int flips[4] = {0, 0, 0, 0}, other = 0;
    for (int i = 0; i < 4 * TP_WALL_FLIP_MS / 16; i++) {
        frame();
        int k = (int)(stub_wall_x[1] / 320);
        if (stub_wall_x[1] == 320.0f * k && k >= 0 && k < 3)
            flips[k]++;
        else
            other++;
    }
    EXPECT(!other && flips[0] > 40 && flips[2] > 40 && flips[1] > flips[0] + 40 && stub_wall_x[0] == 6,
           "the flip book below shows its three pictures, the middle one twice a round (%d %d %d frames)", flips[0],
           flips[1], flips[2]);
    key(KEY_A); key(KEY_Y); key(KEY_X); frames_for(200);
    EXPECT(screen == SCR_PREVIEW && pending == HIT_NONE, "A, Y and X in the preview: nothing");
    forget_sounds();
    key(KEY_B); frames_for(400);
    EXPECT(screen == SCR_CONFIRM && theme_open && music_choice() && focus == 1 && played(SFX_BACK) == 1,
           "B: the check screen again, the theme still open, the cursor where it was (screen %d)", screen);
    EXPECT(!stub_theme_music && !stub_menu_paused && preview_poll() == PREVIEW_OFF, "its music ended, the menu music on");

    /* 52. 0.9.2: the same by touch, on a theme without music whose screens are plain colours. The right tab of
     *     the check screen with one green button opens the preview (0.9.1: it installed); the green button and A
     *     install as before */
    printf("-- the preview by touch, plain colours, no music\n");
    key(KEY_B); frames_for(600);
    EXPECT(screen == SCR_LIST, "back in the folder view (screen %d)", screen);
    EXPECT(select_row("plain.zip"), "plain.zip selected");
    key(KEY_A); frames_for(600);
    EXPECT(screen == SCR_CONFIRM && can_install() && !music_choice() && can_preview() && stub_bar_y,
           "the check screen with one green button, the footer with the Y tab");
    EXPECT(target_at(BOTTOM_W - 20, SCREEN_H - 4) == HIT_PREVIEW && target_at(GO_X + 20, GO_Y + 20) == HIT_GO,
           "the right tab previews, the green button installs");
    threads_started = 0;
    starts_were = stub_theme_starts;
    tap(BOTTOM_W - 20, SCREEN_H - 4, 48); frames_for(400);
    EXPECT(screen == SCR_PREVIEW && preview_seen == PREVIEW_READY && threads_started == 1,
           "a tap on the right tab: the preview, one thread (its reader), no install (screen %d)", screen);
    static const uint8_t top_rgb[6] = {132, 177, 252, 10, 100, 250}, bottom_rgb[6] = {40, 50, 60, 40, 50, 60};
    EXPECT(stub_wall_drawn[0] == 'c' && stub_wall_drawn[1] == 'c' && !memcmp(stub_wall_rgb[0], top_rgb, 6) &&
           !memcmp(stub_wall_rgb[1], bottom_rgb, 6), "its colours over both screens, the top fading to white");
    EXPECT(!stub_theme_music && stub_theme_starts == starts_were && stub_menu_paused,
           "no music of its own: silence, the menu music paused");
    tap(BOTTOM_W - 20, SCREEN_H - 4, 48); frames_for(400);
    EXPECT(screen == SCR_PREVIEW, "a tap on the footer's right end in the preview: nothing, its one action is the left "
           "tab (screen %d)", screen);
    tap(20, SCREEN_H - 4, 48); frames_for(400);
    EXPECT(screen == SCR_CONFIRM && can_install() && !stub_menu_paused,
           "a tap on the left tab in the preview: back; the menu music on (screen %d)", screen);

    /* 53. 0.9.2: no preview where there is nothing to show: a file that is no theme has no Y tab and Y does nothing;
     *     in the folder view Y is still one folder up */
    printf("-- no preview without a theme\n");
    key(KEY_B); frames_for(600);
    EXPECT(select_row("broken.zip"), "broken.zip selected");
    key(KEY_A); frames_for(600);
    EXPECT(screen == SCR_CONFIRM && theme_result != TP_OK && !can_preview() && !stub_bar_y && bar_drawn_last(),
           "a file that is no theme: the plain bar (result %d)", theme_result);
    threads_started = 0;
    key(KEY_Y); frames_for(400);
    tap(BOTTOM_W - 20, SCREEN_H - 4, 48); frames_for(400);
    EXPECT(threads_started == 0 && preview_poll() == PREVIEW_OFF && (screen == SCR_CONFIRM || screen == SCR_LIST),
           "Y opens nothing; the bar is one button (screen %d)", screen);
    close_theme();

    /* 54. 0.9.2: an installed design. Its body and music come from the pack's content (content.h, here the test's
     *     bytes), the top wallpaper is a wide one that glides; B goes back to the design's screen */
    printf("-- the preview of an installed design\n");
    static uint8_t c_lz[SYNTH_LZ_MAX];
    size_t c_lz_size = synth_lz11(s_body, synth_body(s_body, 1, 0, 1), c_lz);      /* gliding on top, still below */
    fake_content_body = c_lz;
    fake_content_body_size = c_lz_size;
    fake_content_music = s_music;
    fake_content_music_size = s_music_size;
    design_screen(3);
    EXPECT(screen == SCR_DESIGN && can_preview() && stub_bar_y && target_at(BOTTOM_W - 20, SCREEN_H - 4) == HIT_PREVIEW,
           "one design: the footer with the Y tab");
    int content_reads = fake_content_reads;
    starts_were = stub_theme_starts;
    key(KEY_Y); frames_for(PRESS_MS + 32);
    EXPECT(screen == SCR_PREVIEW && preview_seen == PREVIEW_LOADING && !stub_busy_top && stub_loader,
           "Y: the wait, with the loader");
    frames_for(200);
    EXPECT(preview_seen == PREVIEW_READY && fake_content_reads == content_reads + 1 && fake_content_index == 3,
           "read from content 3 of the pack, once");
    EXPECT(stub_wall_width[0] == 1024 && stub_wall_width[1] == 512 && stub_wall_drawn[0] == 'w' &&
           stub_wall_drawn[1] == 'w' && stub_wall_x[1] == 0, "a wide wallpaper on top, a still one below");
    EXPECT(stub_theme_music && stub_theme_starts == starts_were + 1 && stub_menu_paused, "its music plays");
    float x_first = stub_wall_x[0], x_most = 0;
    int back_at_left = 0;
    for (int i = 0; i < 60 * 1000 / 16; i++) {      /* a minute: 608 texels there and back at 24 a second is 50.7 s */
        frame();
        x_most = stub_wall_x[0] > x_most ? stub_wall_x[0] : x_most;
        back_at_left |= x_most > 600 && stub_wall_x[0] < 8;
    }
    EXPECT(x_first < 12 && x_most > 600 && x_most <= 608 && back_at_left,
           "the top wallpaper glides to its right end (%.1f of 608) and back", (double)x_most);
    key(KEY_B); frames_for(400);
    EXPECT(screen == SCR_DESIGN && design_index == 3 && shown_design() && !stub_theme_music && !stub_menu_paused,
           "B: the design's screen again (screen %d)", screen);

    /* 55. 0.9.2: what the HOME Menu would not play is not played, and what cannot be read says so: the music byte
     *     off, a music file that is no stream, a body that is none, a content that cannot be read */
    printf("-- the preview's refusals\n");
    worker_frames = 0;
    c_lz_size = synth_lz11(s_body, synth_body(s_body, 0, 1, 1), c_lz);
    fake_content_body_size = c_lz_size;
    starts_were = stub_theme_starts;
    key(KEY_Y); frames_for(400);
    EXPECT(preview_seen == PREVIEW_READY && !stub_theme_music && stub_theme_starts == starts_were && stub_menu_paused &&
           stub_wall_drawn[0] == 'w', "a music file but the body's music byte off: wallpapers, silence");
    key(KEY_B); frames_for(400);
    c_lz_size = synth_lz11(s_body, synth_body(s_body, 1, 1, 1), c_lz);
    fake_content_body_size = c_lz_size;
    fake_content_music = (const uint8_t *)"CSTM, but no stream";
    fake_content_music_size = 19;
    key(KEY_Y); frames_for(400);
    EXPECT(preview_seen == PREVIEW_READY && !stub_theme_music && stub_menu_paused && stub_wall_drawn[1] == 'w',
           "a music file that is no stream: wallpapers, silence");
    key(KEY_B); frames_for(400);
    fake_content_music = NULL;
    fake_content_music_size = 0;
    s_body[0] = 2;                                  /* a body version this does not read */
    c_lz_size = synth_lz11(s_body, 0xD0 + 2 * 512 * 256 * 2, c_lz);
    fake_content_body_size = c_lz_size;
    forget_sounds();
    key(KEY_Y); frames_for(400);
    EXPECT(screen == SCR_PREVIEW && preview_seen == PREVIEW_FAILED && !stub_menu_paused && played(SFX_ERROR) == 1 &&
           stub_wall_drawn[0] == 0 && stub_wall_drawn[1] == 0 && bar_drawn_last(),
           "a body that is none: the preview says so, nothing drawn of it, the menu music on, the bar to go back");
    key(KEY_B); frames_for(400);
    EXPECT(screen == SCR_DESIGN, "B: back");
    fake_content_body = (const uint8_t *)"no LZ11";
    fake_content_body_size = 7;
    key(KEY_Y); frames_for(400);
    EXPECT(preview_seen == PREVIEW_FAILED, "a body that is no LZ11 stream: failed");
    key(KEY_B); frames_for(400);
    fake_content_body = NULL;
    key(KEY_Y); frames_for(400);
    EXPECT(preview_seen == PREVIEW_FAILED && !stub_menu_paused, "a content that cannot be read: failed");
    key(KEY_B); frames_for(400);
    EXPECT(screen == SCR_DESIGN && can_preview(), "back on the design's screen, Y still on offer");

    /* 56. 0.9.2: the application closes while a preview reads or plays: preview_close waits for the reader, stops
     *     the music and can be called again */
    printf("-- closing during a preview\n");
    worker_frames = 1000;
    c_lz_size = synth_lz11(s_body, synth_body(s_body, 1, 1, 1), c_lz);
    fake_content_body = c_lz;
    fake_content_body_size = c_lz_size;
    fake_content_music = s_music;
    fake_content_music_size = s_music_size;
    content_reads = fake_content_reads;
    key(KEY_Y); frames_for(300);
    EXPECT(screen == SCR_PREVIEW && preview_seen == PREVIEW_LOADING && fake_content_reads == content_reads,
           "a reader that has not run yet");
    preview_close();
    EXPECT(fake_content_reads == content_reads + 1 && preview_poll() == PREVIEW_OFF && !stub_theme_music,
           "closed while it reads: the reader ran to its end first, no music started");
    preview_close();
    worker_frames = 0;
    screen = SCR_DESIGN;
    frames_for(300);
    key(KEY_Y); frames_for(400);
    EXPECT(preview_seen == PREVIEW_READY && stub_theme_music, "a preview that plays");
    preview_close();
    EXPECT(!stub_theme_music && !stub_menu_paused && preview_poll() == PREVIEW_OFF, "closed while it plays: silence, the menu music on");
    preview_seen = PREVIEW_OFF;
    screen = SCR_DESIGN;
    fake_content_body = fake_content_music = NULL;

    /* 57. 0.9.2: on the check screen with one green button, whose right tab now opens the preview, a tap on the
     *     green button still installs (A does in case 24): through the whole install to "Installiert", with the
     *     confirm sound at the push and the done sound at the end */
    printf("-- the green button still installs\n");
    remove("sdmc:/theme-pack/installed.mark");  /* the installs of the cases before left the mark high: a pack as at
                                                   its start, so the new theme's number is one the fake console has */
    make_pack(argv[1], argv[2], 0);
    load_pack();
    open_folder_view();
    EXPECT(select_row("plain.zip"), "plain.zip selected");
    key(KEY_A); frames_for(600);
    EXPECT(screen == SCR_CONFIRM && can_install() && !music_choice() && can_preview(), "its check screen (screen %d)",
           screen);
    forget_sounds();
    stub_tops[0] = 0;
    tap(GO_X + GO_W / 2, GO_Y + GO_H / 2, 48); frames_for(1000);
    EXPECT(screen == SCR_DONE && !strcmp(stub_tops, "p") && top_has('p') && preview_poll() == PREVIEW_OFF,
           "a tap on the green button: installed (screen %d, top screens %s: G2-L2b, the picture on into "
           "\"Installiert\")", screen, stub_tops);
    EXPECT(played(SFX_CONFIRM) == 1 && played(SFX_DONE) == 1 && played(SFX_ERROR) == 0,
           "the confirm sound at the push, the done sound at \"Installiert\"");

    /* FIX-1 X3: the version carries the pre-release mark of app/identity.mk ("beta.1"): the settings' foot reads
     *     "v1.0.0-beta.1"; the log's first line and the user agent take the same text_version (console.c); without a
     *     mark the numbers stand alone */
    printf("-- the version's pre-release mark\n");
    {
        char v[24], want[40];
        text_version(v, sizeof v, 0, 9, 4, "beta");
        int marked = !strcmp(v, "0.9.4-beta");
        text_version(v, sizeof v, 0, 9, 4, "");
        int plain = !strcmp(v, "0.9.4");
        text_version(v, sizeof v, 1, 0, 0, "beta.1");
        EXPECT(marked && plain && !strcmp(v, "1.0.0-beta.1"), "text_version: \"0.9.4-beta\" with the mark, \"0.9.4\" "
               "without, \"1.0.0-beta.1\"");
        version_set("");
        snprintf(want, sizeof want, "v%d.%d.%d", APP_VERSION_MAJOR, APP_VERSION_MINOR, APP_VERSION_MICRO);
        int foot_plain = !strcmp(version, want);
        version_set(APP_VERSION_PRE);
        snprintf(want, sizeof want, "v%d.%d.%d-beta.1", APP_VERSION_MAJOR, APP_VERSION_MINOR, APP_VERSION_MICRO);
        EXPECT(foot_plain && !strcmp(APP_VERSION_PRE, "beta.1") && !strcmp(version, want) &&
               sizeof version > strlen("v99.99.99-beta.12 probe"), "the foot: \"%s\" in this build (identity.mk's "
               "mark), the numbers alone without it; the buffer holds a probe build's longest form", version);
        text_version(v, sizeof v, APP_VERSION_MAJOR, APP_VERSION_MINOR, APP_VERSION_MICRO, APP_VERSION_PRE);
        EXPECT(!strcmp(v, want + 1), "the log's first line and the user agent: \"%s %s\", \"%s/%s (Nintendo 3DS)\"",
               APP_NAME, v, APP_NAME, v);
        version[0] = 0;                 /* as main() leaves it in these tests, which never run main() */
    }

    /* 58. 0.9.2: the settings. At start the file on the card says which menu music plays, whether the interface
     *     sounds are on, and which logo style and background pattern the start screen has (settings_logo,
     *     settings_pattern); without a file, with a damaged one or with a track this build does not have, the
     *     defaults hold and no screen says anything. The start screen's corner button (Y, or a tap) opens the
     *     settings screen: a row a setting, from one table; A or a tap goes on to the row's next choice, left and
     *     right change it too; a track plays at once and every change is on the card at once */
    printf("-- the settings\n");
    remove(SETTINGS_PATH);
    popup = POP_NONE;
    go_main(0);
    frames_for(300);
    int sets_before = stub_menu_sets;
    start_again();
    EXPECT(music_pick == TRACK_DEFAULT && prefs.sounds == 1 && stub_menu_track == TRACK_DEFAULT && stub_sounds_on == 1 &&
           stub_menu_sets == sets_before + 1 && !strcmp(TRACK_LIST[TRACK_DEFAULT].file, "somewhere-in-the-elevator.bcstm"),
           "no file on the card: the default track plays, the sounds are on");
    EXPECT(settings_scheme() == ART_SCHEME_WEISS && settings_logo() == GLASS_SHARD && settings_pattern() == 1,
           "and the colour is white, the glass Shard, the pattern the small houses");
    frames_for(100);
    EXPECT(screen == SCR_MAIN && popup == POP_NONE && stub_settings_button, "the start screen, with its settings button");
    EXPECT(target_at(10, 10) == HIT_CORNER && target_at(CORNER_W - 1, CORNER_H - 1) == HIT_CORNER &&
           target_at(CORNER_W + 2, CORNER_H - 4) == HIT_NONE && target_at(TILE_X1 + 2, TILE_Y + 2) == HIT_FIRST &&
           target_at(TILE_X1 + 60, TILE_Y + 60) == HIT_FIRST && target_at(TILE_X2 + 60, TILE_Y + 60) == HIT_SECOND,
           "the button takes its corner, the tiles theirs, nothing beside the button");
    int ring_reach = SELECT_OUT + SELECT_AMP + SELECT_W;     /* the cursor's band at its outermost, beyond the tile */
    EXPECT(CORNER_H <= TILE_Y - ring_reach && TILE_Y + TILE_H + ring_reach <= BAR_Y - BAR_SHADOW,
           "the tiles lie under the button and above the bar's shadow, the cursor's corners round them too, %d px "
           "beyond (the button ends at %d, the tiles run from %d to %d, the bar's shadow begins at %d)", ring_reach,
           CORNER_H, TILE_Y, TILE_Y + TILE_H, BAR_Y - BAR_SHADOW);
    key(KEY_DRIGHT); frames_for(100);
    int cursor_at = -1, button_at = -1;
    for (int k = 0; k < stub_marks; k++) {
        if (stub_mark_kind[k] == 's')
            cursor_at = k;
        if (stub_mark_kind[k] == 'o' && stub_mark_at[k].w == CORNER_W && stub_mark_at[k].y == 0)
            button_at = k;
    }
    EXPECT(ring && button_at >= 0 && cursor_at > button_at, "the cursor is drawn after the button, so its corner shows over it");
    forget_sounds();
    key(KEY_Y); frames_for(300);
    EXPECT(screen == SCR_SETTINGS && played(SFX_CONFIRM) == 1 && stub_choices == rows_shown() && rows_shown() == 4 &&
           bar_drawn_last(), "Y: the settings screen, a row a setting (%d rows)", stub_choices);
    frames_for(100);
    EXPECT(strstr(stub_motion_line, "motion: screen start to settings: ") != NULL,
           "the log's motion line names the settings screen: \"%s\"", stub_motion_line);
    char default_track[160];
    snprintf(default_track, sizeof default_track, "%s, %s", TRACK_LIST[TRACK_DEFAULT].title, TRACK_LIST[TRACK_DEFAULT].author);
    char first_note[24];
    snprintf(first_note, sizeof first_note, "1 / %d", TRACK_COUNT + 1);
    /* the rows by their place: the colour scheme first (0.9.3) */
    enum { SR_COLOUR, SR_MUSIC, SR_SOUNDS, SR_PATTERN };
    EXPECT(!strcmp(stub_row_label[SR_COLOUR], "Colour") && !strcmp(stub_row_value[SR_COLOUR], "White") &&
           !strcmp(stub_row_note[SR_COLOUR], "1 / 8"), "the first row is the colour: white, the first of eight (%s, %s)",
           stub_row_value[SR_COLOUR], stub_row_note[SR_COLOUR]);
    static const char *const scheme_words[2][SETTINGS_SCHEMES] = {
        {"White", "Yellow", "Sand", "Mint", "Sky", "Lilac", "Rose", "Night"},
        {"Weiss", "Gelb", "Sand", "Minze", "Himmel", "Flieder", "Rose", "Nacht"}};
    int schemes_listed = stub_list_count == SETTINGS_SCHEMES && stub_list_chosen == 0;
    for (int i = 0; i < SETTINGS_SCHEMES; i++)
        schemes_listed &= !strcmp(stub_list_name[i], scheme_words[0][i]) && !strcmp(text(LANG_DE, T_SCHEME_WHITE + i),
                                                                                     scheme_words[1][i]);
    EXPECT(schemes_listed && !strcmp(text(LANG_DE, T_SCHEME), "Farbe"), "the top screen lists the eight schemes, white "
           "chosen; in German Farbe: Weiss, Gelb, Sand, Minze, Himmel, Flieder, Rose, Nacht");
    EXPECT(!strcmp(stub_row_label[SR_MUSIC], "Background music") && !strcmp(stub_row_value[SR_MUSIC], default_track) &&
           !strcmp(stub_row_note[SR_MUSIC], first_note), "music: the default track with title and author (%s, %s)",
           stub_row_value[SR_MUSIC], stub_row_note[SR_MUSIC]);
    EXPECT(!strcmp(stub_row_label[SR_SOUNDS], "Sounds") && !strcmp(stub_row_value[SR_SOUNDS], "On") &&
           !strcmp(stub_row_note[SR_SOUNDS], "2 / 2") &&
           !strcmp(stub_row_label[SR_PATTERN], "Background") && !strcmp(stub_row_value[SR_PATTERN], "Little houses") &&
           !strcmp(stub_row_note[SR_PATTERN], "2 / 3"), "sounds on, the small houses");
    rect last_row = settings_rect(rows_shown() - 1);
    EXPECT(last_row.y + last_row.h <= BAR_Y - BAR_SHADOW &&
           target_at((int)last_row.x + 5, (int)last_row.y + 5) == HIT_ROW0 + SR_PATTERN && target_at(4, 20) == HIT_NONE,
           "the rows end above the bar's shadow; beside them nothing");
    key(KEY_DDOWN); frames_for(100);
    EXPECT(ring && focus == 0 && played(SFX_MOVE) == 1, "the first D-pad press shows the cursor on the first row");
    /* the colour's row: A and right go on, left back, round both ends; each step is told to the drawing layer
     * before the next frame is drawn (it shows on this screen) and is on the card at once */
    forget_sounds();
    key(KEY_A); frames_for(300);
    settings card = on_card();
    EXPECT(prefs.scheme == ART_SCHEME_GELB && settings_scheme() == ART_SCHEME_GELB && stub_scheme == ART_SCHEME_GELB &&
           card.scheme == ART_SCHEME_GELB && screen == SCR_SETTINGS && !strcmp(stub_row_value[SR_COLOUR], "Yellow") &&
           !strcmp(stub_row_note[SR_COLOUR], "2 / 8") && stub_list_chosen == 1 && played(SFX_CONFIRM) == 1,
           "A on the colour's row: yellow, drawn at once on the settings screen, and on the card");
    EXPECT(ui_colours == ART_STOCK.colour[ART_SCHEME_GELB] && SC(COL_BG) == 0xF1E9A4 && SC(COL_GREEN) == COL_GREEN,
           "the colours drawn by code are the scheme's: yellow's ground is layout.h's own, the install green is "
           "every scheme's");
    int steps_right = 0, told_each = 0, on_card_each = 0;
    for (int k = ART_SCHEME_SAND; k < ART_SCHEMES; k++) {
        key(KEY_DRIGHT);
        told_each += stub_scheme == k;          /* told for the frame drawn right after the key */
        frames_for(60);
        steps_right += settings_scheme() == k && !strcmp(stub_row_value[SR_COLOUR], scheme_words[0][k]);
        on_card_each += on_card().scheme == k;
    }
    EXPECT(steps_right == ART_SCHEMES - 2 && told_each == steps_right && on_card_each == steps_right &&
           settings_scheme() == ART_SCHEME_NACHT && !strcmp(stub_row_note[SR_COLOUR], "8 / 8"),
           "right through the schemes to the last, night: %d steps, %d told in the frame after the key, %d on the card",
           steps_right, told_each, on_card_each);
    key(KEY_DRIGHT); frames_for(60);
    EXPECT(settings_scheme() == ART_SCHEME_WEISS && stub_scheme == ART_SCHEME_WEISS && on_card().scheme == 0 &&
           ui_colours == ART_STOCK.colour[ART_SCHEME_WEISS] && SC(COL_BG) == 0xFFFFFF, "right again: round to white");
    key(KEY_DLEFT); frames_for(60);
    EXPECT(settings_scheme() == ART_SCHEME_NACHT && on_card().scheme == ART_SCHEME_NACHT, "left: back round to night");
    key(KEY_DLEFT); frames_for(60);
    EXPECT(settings_scheme() == ART_SCHEME_ROSE && music_pick == TRACK_DEFAULT && prefs.sounds == 1 &&
           settings_logo() == GLASS_SHARD && settings_pattern() == 1, "left again: rose; no other setting moved");
    key(KEY_DRIGHT); frames_for(60); key(KEY_DRIGHT); frames_for(60);
    key(KEY_DDOWN); frames_for(100);
    int list_right = stub_list_count == TRACK_COUNT + 1 && stub_list_chosen == TRACK_DEFAULT &&
                     !strcmp(stub_list_name[TRACK_COUNT], "No music");
    for (int i = 0; i < TRACK_COUNT; i++)
        list_right &= strstr(stub_list_name[i], TRACK_LIST[i].title) && strstr(stub_list_name[i], TRACK_LIST[i].author);
    EXPECT(focus == SR_MUSIC && settings_scheme() == ART_SCHEME_WEISS && list_right, "white again, and down: the top "
           "screen lists the music's choices: every track with title and author, then no music");
    forget_sounds();
    sets_before = stub_menu_sets;
    key(KEY_A); frames_for(300);
    card = on_card();
    EXPECT(TRACK_COUNT >= 2 && music_pick == 1 && stub_menu_track == 1 && stub_menu_sets == sets_before + 1 &&
           played(SFX_CONFIRM) == 1, "A: the next track, it plays at once");
    EXPECT(!strcmp(card.music, TRACK_LIST[1].file) && card.sounds == 1 && card.logo == GLASS_SHARD && card.pattern == 1 &&
           card.scheme == 0 && strstr(stub_row_value[SR_MUSIC], TRACK_LIST[1].title) && stub_list_chosen == 1,
           "and is on the card at once (%s)", card.music);
    for (int i = 2; i <= TRACK_COUNT; i++) { key(KEY_DRIGHT); frames_for(60); }
    card = on_card();
    EXPECT(music_pick == TRACK_COUNT && stub_menu_track == -1 && !card.music[0] && !strcmp(stub_row_value[SR_MUSIC], "No music") &&
           played(SFX_MOVE) == TRACK_COUNT - 1, "right to the last choice: no music, on the card too");
    key(KEY_DRIGHT); frames_for(60);
    EXPECT(music_pick == 0 && stub_menu_track == 0 && !strcmp(on_card().music, TRACK_LIST[0].file), "right again: round to the first track");
    key(KEY_DLEFT); frames_for(60);
    EXPECT(music_pick == TRACK_COUNT && stub_menu_track == -1, "left: back round to no music");
    key(KEY_DLEFT); frames_for(60);
    EXPECT(music_pick == TRACK_COUNT - 1 && stub_menu_track == TRACK_COUNT - 1, "left again: the last track");
    key(KEY_DDOWN); frames_for(60);
    EXPECT(focus == SR_SOUNDS && stub_list_count == 2 && !strcmp(stub_list_name[0], "Off") && !strcmp(stub_list_name[1], "On") &&
           stub_list_chosen == 1, "down: the sounds' row, the top screen lists off and on");
    forget_sounds();
    key(KEY_A); frames_for(300);
    card = on_card();
    EXPECT(prefs.sounds == 0 && stub_sounds_on == 0 && card.sounds == 0 && !strcmp(stub_row_value[SR_SOUNDS], "Off") &&
           !strcmp(stub_row_note[SR_SOUNDS], "1 / 2"), "A: the sounds off, on the card too");
    key(KEY_DUP); frames_for(100); key(KEY_DDOWN); frames_for(100);
    EXPECT(focus == SR_SOUNDS && played(SFX_MOVE) == 0 && played(SFX_BACK) == 0, "with the sounds off the cursor moves in silence");
    key(KEY_DRIGHT); frames_for(60);
    EXPECT(prefs.sounds == 1 && stub_sounds_on == 1 && on_card().sounds == 1 && played(SFX_MOVE) == 1,
           "right: the sounds on again, and that is heard");
    key(KEY_DDOWN); frames_for(60); key(KEY_DDOWN); frames_for(60);
    EXPECT(focus == SR_PATTERN, "down to the last row, and no further");
    key(KEY_A); frames_for(300);
    card = on_card();
    EXPECT(prefs.pattern == 2 && settings_pattern() == 2 && card.pattern == 2 && card.logo == GLASS_SHARD &&
           !strcmp(stub_row_value[SR_PATTERN], "Bubbles") && !strcmp(stub_list_name[0], "Plus signs and rings"),
           "the background's row, A: bubbles, for the start screen and on the card");
    {   /* FIX-1 X1: the rows take the stylus as a list's, and four do not scroll: a drag that comes back to the row
         * it began on moves nothing and taps nothing; the taps below work as before */
        rect r = settings_rect(SR_MUSIC);
        int x = (int)r.x + 20, y = (int)r.y + 10, music = music_pick, sets = stub_menu_sets, still = 1;
        stylus(x, y); frame();
        for (int d = 1; d <= 30; d++) {
            stylus(x, d <= 15 ? y + d : y + 30 - d); frame();
            still &= sv.scroll == 0 && !sv.dragging;
        }
        lift(); frames_for(300);
        EXPECT(still && sv.scroll == 0 && focus == SR_PATTERN && music_pick == music && stub_menu_sets == sets &&
               prefs.pattern == 2 && pending == HIT_NONE, "four rows: a drag of 15 px on the music's row and back "
               "scrolls nothing and taps nothing");
    }
    {   /* FIX-1 X2: the Sounds row's top screen credits Kenney and every track of the build (tracks.h), in both
         * languages; the other rows' top screens draw no credits */
        extern int stub_credits;
        extern char stub_credit[][160];
        int others = 0;
        for (int i = 0; i < rows_shown(); i++) {
            if (i == SR_SOUNDS)
                continue;
            focus = i;
            stub_credits = 0;
            frame();
            others += stub_credits;
        }
        int named[2] = {1, 1};
        for (int de = 0; de < 2; de++) {
            lang = de ? LANG_DE : LANG_EN;
            focus = SR_SOUNDS;
            stub_credits = 0;
            frame();
            named[de] = stub_credits == 1 + TRACK_COUNT && strstr(stub_credit[0], "Kenney") &&
                        strstr(stub_credit[0], de ? "Bedient\xC3\xB6ne von" : "Interface sounds by");
            for (int k = 0; k < TRACK_COUNT; k++)
                named[de] &= strstr(stub_credit[1 + k], TRACK_LIST[k].title) &&
                             strstr(stub_credit[1 + k], TRACK_LIST[k].author) &&
                             !strncmp(stub_credit[1 + k], de ? "Musik: \"" : "Music: \"", 8);
        }
        lang = LANG_EN;
        focus = SR_PATTERN;
        frame();
        EXPECT(named[0] && named[1] && !others, "the sounds' top screen credits Kenney and each track, "
               "in English and in German (\"%s\", \"%s\"); the other rows' none", stub_credit[0], stub_credit[1]);
        /* FIX-2: on the sounds' row the choices stand at the card's top and the credits are told where they end;
         * every other row's choices stand centred */
        extern int stub_choices_top;
        extern float stub_choices_end, stub_credits_from;
        int tops_elsewhere = 0;
        for (int i = 0; i < rows_shown(); i++) {
            if (i == SR_SOUNDS)
                continue;
            focus = i;
            stub_choices_top = 0;
            frame();
            tops_elsewhere += stub_choices_top;
        }
        focus = SR_SOUNDS;
        stub_choices_top = 0;
        stub_credits_from = 0;
        frame();
        int top_here = stub_choices_top > 0 && stub_credits_from == stub_choices_end;
        focus = SR_PATTERN;
        frame();
        EXPECT(top_here && !tops_elsewhere, "the sounds' choices stand at the card's top (ui_choices_top) and the "
               "credits begin from where they end; the other rows' choices stand centred (ui_choices)");
    }
    rect first_row = settings_rect(SR_MUSIC);
    sets_before = stub_menu_sets;
    tap((int)first_row.x + 20, (int)first_row.y + 10, 48); frames_for(300);
    EXPECT(focus == SR_MUSIC && music_pick == TRACK_COUNT && stub_menu_track == -1 && stub_menu_sets == sets_before + 1 &&
           stub_list_count == TRACK_COUNT + 1, "a tap on the music's row: its next choice, and the top screen lists its choices");
    tap((int)first_row.x + 20, (int)first_row.y + 10, 48); frames_for(300);
    EXPECT(music_pick == 0 && stub_menu_track == 0 && !strcmp(on_card().music, TRACK_LIST[0].file), "another tap: the first track plays");
    rect colour_row = settings_rect(SR_COLOUR);
    tap((int)colour_row.x + 20, (int)colour_row.y + 10, 48); frames_for(300);
    EXPECT(focus == SR_COLOUR && settings_scheme() == ART_SCHEME_GELB && stub_scheme == ART_SCHEME_GELB &&
           stub_list_count == SETTINGS_SCHEMES && stub_list_chosen == ART_SCHEME_GELB && on_card().scheme == ART_SCHEME_GELB,
           "a tap on the colour's row: yellow, and the top screen lists the schemes");
    lang = LANG_DE;
    frames_for(50);
    EXPECT(!strcmp(stub_row_label[SR_COLOUR], "Farbe") && !strcmp(stub_row_value[SR_COLOUR], "Gelb") &&
           !strcmp(stub_row_label[SR_MUSIC], "Hintergrundmusik") && !strcmp(stub_row_label[SR_SOUNDS], "T\xC3\xB6ne") &&
           !strcmp(stub_row_value[SR_SOUNDS], "An") && !strcmp(stub_row_label[SR_PATTERN], "Hintergrund") &&
           !strcmp(stub_row_value[SR_PATTERN], "Blasen"), "the same rows in German");
    lang = LANG_EN;
    forget_sounds();
    key(KEY_B); frames_for(300);
    EXPECT(screen == SCR_MAIN && played(SFX_BACK) == 1, "B: the start screen");
    tap(10, 10, 48); frames_for(300);
    EXPECT(screen == SCR_SETTINGS && focus == 0, "a tap on the settings button opens it too");
    tap(20, SCREEN_H - 4, 48); frames_for(300);
    EXPECT(screen == SCR_MAIN, "a tap on the bar: back");
    /* the next start reads what was chosen: mint, the second track, sounds off, bubbles; the card's glass line as it was */
    key(KEY_Y); frames_for(300);
    key(KEY_DDOWN); frames_for(60);                                                     /* the cursor, on the colour */
    key(KEY_DRIGHT); frames_for(60); key(KEY_DRIGHT); frames_for(60);                   /* yellow to sand to mint */
    key(KEY_DDOWN); frames_for(60); key(KEY_DRIGHT); frames_for(60);                    /* the second track */
    key(KEY_DDOWN); frames_for(60); key(KEY_DLEFT); frames_for(60);                     /* the sounds off */
    key(KEY_B); frames_for(300);
    EXPECT(screen == SCR_MAIN && stub_scheme == ART_SCHEME_MINZE, "back on the start screen the scheme chosen is drawn");
    music_pick = -7; prefs.sounds = 1; prefs.scheme = prefs.logo = prefs.pattern = 0; stub_sounds_on = 1; stub_menu_track = -2;
    settings_apply(&prefs);
    start_again();
    EXPECT(music_pick == 1 && stub_menu_track == 1 && prefs.sounds == 0 && stub_sounds_on == 0 &&
           settings_logo() == GLASS_SHARD && settings_pattern() == 2 && settings_scheme() == ART_SCHEME_MINZE,
           "the next start: mint, the second track, the sounds off, bubbles; the glass as it was");
    stub_scheme = -1;
    shown_key = -1;
    draw();                             /* the first frame of all at a start (ST-B: none while the pack is read) */
    EXPECT(stub_scheme == ART_SCHEME_MINZE, "and the first frame is already told the scheme (%d)", stub_scheme);
    /* a settings file as 0.9.2 wrote it has no colour line: every value it holds stays, the colour is white */
    static const char *const old_files[] = {"3DStockTheme settings 1\nmusic=urban-shop.bcstm\nsounds=off\nglass=clear\npattern=bubbles\n",
                                            "3DStockTheme settings 1\nmusic=off\nsounds=on\nglass=platin\npattern=plus\n"};
    static const int old_want[][4] = {{1, 0, GLASS_CLEAR, PATTERN_BUBBLES}, {TRACK_COUNT, 1, GLASS_PLATIN, PATTERN_PLUS}};
    for (int k = 0; k < 2; k++) {
        write_file(SETTINGS_PATH, (const uint8_t *)old_files[k], strlen(old_files[k]));
        start_again();
        frames_for(100);
        EXPECT(settings_scheme() == ART_SCHEME_WEISS && stub_scheme == ART_SCHEME_WEISS && music_pick == old_want[k][0] &&
               prefs.sounds == old_want[k][1] && settings_logo() == old_want[k][2] && settings_pattern() == old_want[k][3],
               "a 0.9.2 settings file (%d): white, and its music, sounds, glass and pattern as they were", k);
    }
    key(KEY_Y); frames_for(300);
    key(KEY_DDOWN); frames_for(60); key(KEY_DDOWN); frames_for(60); key(KEY_DDOWN); frames_for(60);
    key(KEY_DRIGHT); frames_for(60);                                                    /* the sounds off: the file is written anew */
    card = on_card();
    EXPECT(focus == SR_SOUNDS && card.scheme == ART_SCHEME_WEISS && card.sounds == 0 && card.logo == GLASS_PLATIN &&
           card.pattern == PATTERN_PLUS && !card.music[0], "the first change writes it with the colour's line; the "
           "rest is kept");
    key(KEY_B); frames_for(300);
    /* a track this build does not have, a damaged file, a file of another version: the defaults, and no word */
    static const char *const bad_files[] = {"3DStockTheme settings 1\ncolour=purple\nmusic=no-such-track.bcstm\nsounds=off\nglass=glassy\npattern=9\n",
                                            "3DStockTheme settings 1\nmusic=", "\x01\x02garbage\n\n=", "",
                                            "3DStockTheme settings 2\ncolour=night\nmusic=off\nsounds=off\nglass=clear\npattern=plus\n"};
    static const int want_sounds[] = {0, 1, 1, 1, 1};
    for (int k = 0; k < 5; k++) {
        write_file(SETTINGS_PATH, (const uint8_t *)bad_files[k], strlen(bad_files[k]));
        stub_menu_track = -2;
        start_again();
        frames_for(100);
        EXPECT(music_pick == TRACK_DEFAULT && stub_menu_track == TRACK_DEFAULT && prefs.sounds == want_sounds[k] &&
               stub_sounds_on == want_sounds[k] && settings_scheme() == ART_SCHEME_WEISS && stub_scheme == ART_SCHEME_WEISS &&
               settings_logo() == GLASS_SHARD && settings_pattern() == 1 && screen == SCR_MAIN && popup == POP_NONE,
               "a file the application cannot use (%d): the defaults, no error screen", k);
    }
    mkdir("sdmc:/theme-pack/settings.txt.d", 0755);
    remove(SETTINGS_PATH);
    if (!rename("sdmc:/theme-pack/settings.txt.d", SETTINGS_PATH)) {        /* a folder in the file's place: nothing to write to */
        start_again();
        key(KEY_Y); frames_for(300);
        key(KEY_DDOWN); frames_for(60); key(KEY_DRIGHT); frames_for(60);
        key(KEY_DDOWN); frames_for(60); key(KEY_DRIGHT); frames_for(60);
        key(KEY_DDOWN); frames_for(60); key(KEY_DDOWN); frames_for(60); key(KEY_DRIGHT); frames_for(60);
        EXPECT(screen == SCR_SETTINGS && settings_scheme() == ART_SCHEME_GELB && stub_scheme == ART_SCHEME_GELB &&
               music_pick == 1 && stub_menu_track == 1 && settings_pattern() == PATTERN_BUBBLES && popup == POP_NONE,
               "the file cannot be written: the choices hold while the application runs, no error screen");
        key(KEY_B); frames_for(300);
        rmdir(SETTINGS_PATH);
    }
    remove(SETTINGS_PATH);
    start_again();
    forget_sounds();

    /* 59. 0.9.2: the working loop is told when work begins and ends (audio.c and work.c do the rest): every job
     *     (job_start to job_ended; the installed designs' read is one too, the pack's at the start is none since
     *     ST-B), the install from its start to its end whether it ends well or badly, the preview's reading. Every
     *     begin has its end, and the done or error sound comes after the end */
    printf("-- the working loop's hooks\n");
    make_pack(argv[1], argv[2], 0);
    worker_frames = 8;
    reset_app();
    stub_work_depth = 0;            /* the cases above drop jobs and a preview half way, as no user can */
    stub_work_ends = stub_work_begins;
    load_pack();
    go_designs(0);
    frames_for(300);
    key(KEY_A); frames_for(200);
    EXPECT(!job && stub_work_depth == 0 && stub_work_ends == stub_work_begins, "the pack and the designs are read: "
           "every begin had its end (%d, %d)", stub_work_begins, stub_work_ends);
    int begins = stub_work_begins;
    key(KEY_A); frames_for(48);
    frames_for(PRESS_MS);
    EXPECT(job == JOB_FOLDER && stub_work_depth == 1 && stub_work_begins == begins + 1, "reading a folder: work began (job %d)", job);
    frames_for(500);
    EXPECT(screen == SCR_LIST && !job && stub_work_depth == 0 && stub_work_ends == stub_work_begins, "the folder is read: it ended");
    EXPECT(select_row("synth.zip"), "synth.zip selected");
    begins = stub_work_begins;
    key(KEY_A); frames_for(PRESS_MS + 48);
    EXPECT(job == JOB_CHECK && stub_work_depth == 1, "checking a file: work began");
    frames_for(500);
    EXPECT(screen == SCR_CONFIRM && stub_work_depth == 0 && stub_work_begins == begins + 1, "checked: it ended");
    /* the preview's reading */
    begins = stub_work_begins;
    key(KEY_Y); frames_for(PRESS_MS + 48);
    EXPECT(screen == SCR_PREVIEW && preview_seen == PREVIEW_LOADING && stub_work_depth == 1 && stub_work_begins == begins + 1,
           "the preview reads: work began");
    frames_for(400);
    EXPECT(preview_seen == PREVIEW_READY && stub_work_depth == 0 && stub_theme_music, "read: it ended, the theme's music plays");
    key(KEY_B); frames_for(400);
    EXPECT(screen == SCR_CONFIRM && stub_work_depth == 0 && stub_work_begins == begins + 1, "back: nothing more began");
    /* an install that ends badly: the console reports no new theme afterwards (the install is a job on the worker:
     * 5 frames of it here) */
    worker_frames = 5;
    void (*installed_was)(const uint8_t *, size_t) = fake_installed;
    fake_installed = NULL;
    begins = stub_work_begins;
    work_deepest = 0;
    stub_frame_done = watch_work;
    forget_sounds();
    tap(GO_X + GO_W / 2, GO_TWO_Y1 + GO_H / 2, 48); frames_for(1000);
    EXPECT(screen == SCR_PROBLEM && stub_work_begins == begins + 1 && work_deepest == 1 && stub_work_depth == 0,
           "an install that ends badly: work from its start to its end (screen %d, deepest %d)", screen, work_deepest);
    EXPECT(played(SFX_ERROR) == 1 && played(SFX_DONE) == 0 && stub_close_depth == 0 && stub_close_ends == stub_work_ends,
           "then the error sound, after the work's end (not before its begin)");
    key(KEY_A); frames_for(400);
    /* and one that ends well */
    fake_installed = installed_was;
    make_pack(argv[1], argv[2], 0);
    open_folder_view();
    EXPECT(select_row("synth.zip"), "synth.zip selected again");
    key(KEY_A); frames_for(600);
    EXPECT(screen == SCR_CONFIRM && music_choice(), "its check screen");
    begins = stub_work_begins;
    work_deepest = steps_unworked = 0;
    steps_seen_work = stub_pour_frames;
    forget_sounds();
    tap(GO_X + GO_W / 2, GO_TWO_Y1 + GO_H / 2, 48); frames_for(1000);
    stub_frame_done = NULL;
    worker_frames = 0;
    EXPECT(screen == SCR_DONE && stub_work_begins == begins + 1 && work_deepest == 1 && stub_work_depth == 0,
           "an install that ends well: work from its start to its end (screen %d)", screen);
    EXPECT(steps_seen_work > 0 && !steps_unworked, "the working loop goes on while the installing screen shows the "
           "picture's end (%d of its frames without work)", steps_unworked);
    EXPECT(played(SFX_DONE) == 1 && played(SFX_ERROR) == 0 && stub_close_depth == 0 && stub_close_ends == stub_work_ends &&
           stub_work_ends == stub_work_begins, "then the done sound, after the work's end; every begin had its end");
    /* 40. 0.9.2: one action is the tab in the lower left corner, as drawn (the left one of the two); the bar's old
     *     place beside it takes nothing. The start screen keeps the bar across the screen. */
    printf("-- 40: one action, the left tab\n");
    reset_app();
    make_pack(argv[1], argv[2], 0);
    load_pack();
    go_designs(0);
    frames_for(400);
    EXPECT(screen == SCR_DESIGNS && !bar_halves(), "Designs has one action");
    const int beside[3][2] = {{BOTTOM_W / 2, BAR_Y + BAR_H / 2}, {BOTTOM_W - 20, SCREEN_H - 4}, {110, 212}};
    for (int k = 0; k < 3; k++) {
        tap(beside[k][0], beside[k][1], 48); frames_for(500);
        EXPECT(screen == SCR_DESIGNS && pending == HIT_NONE && !job, "a tap at (%d, %d), beside the tab: nothing "
               "(screen %d)", beside[k][0], beside[k][1], screen);
    }
    rect tab_box = ui_bar_rect();
    int outside = 0, inside = 0;
    for (int y = 0; y < SCREEN_H; y++)
        for (int x = 0; x < BOTTOM_W; x++)
            if (ui_tab_hit(0, x, y)) {
                inside++;
                outside += !ui_hit(tab_box, x, y);
            }
    EXPECT(inside > 3000 && !outside && tab_box.w == TAB_W, "the tab's box holds the tab (%d px, %d outside it)",
           inside, outside);
    tap(TAB_W / 2, BAR_Y + BAR_H / 2, 48); frames_for(500);
    EXPECT(screen == SCR_MAIN, "the left tab: back to the start screen (screen %d)", screen);
    int left_app = 0;
    stylus(BOTTOM_W - 20, SCREEN_H - 4); frames_for(48); lift();
    for (int i = 0; i < 20 && !left_app; i++)
        left_app = !frame();
    EXPECT(left_app, "the start screen: a tap on the bar's right end leaves, the bar is across the screen");
    reset_app();
    go_designs(0);
    frames_for(400);
    key(KEY_B); frames_for(500);
    EXPECT(screen == SCR_MAIN, "B goes back as before (screen %d)", screen);
    /* a download's Cancel is that tab too: a touch beside it, in the box around it, cancels nothing */
    reset_app();
    go_designs(1);
    frames_for(400);
    job = JOB_DOWNLOAD; job_over = 0; job_cancel = 0;
    stylus(110, 212); frame(); frame(); frame(); lift(); frame();
    int beside_cancels = job_cancel;
    stylus(TAB_W / 2, BAR_Y + BAR_H / 2); frame(); frame(); frame(); lift(); frame();
    EXPECT(ui_hit(ui_bar_rect(), 110, 212) && !beside_cancels && job_cancel, "a download: a touch at (110, 212) "
           "cancels nothing (%d), one on the tab cancels (%d)", beside_cancels, job_cancel);
    job = JOB_NONE; job_cancel = 0; pressed = HIT_NONE;

    /* 41. 0.9.2: a popup grows and fades in by the clock, a new value every frame for POPUP_IN_MS; closed, it is
     *     drawn on for POPUP_OUT_MS, falling, while the screen under it has the keys again */
    printf("-- 41: a popup opens and closes in steps\n");
    reset_app();
    go_designs(0);
    frames_for(400);
    show_note(-1, "A note", "Its text.");
    int opening = 1, in_frames = 0;
    float shown_was = -1;
    for (u64 t0 = sim_now; sim_now - t0 < POPUP_IN_MS + 64;) {
        frame();
        opening &= stub_popup_shown > shown_was || (shown_was == 1.0f && stub_popup_shown == 1.0f);
        in_frames += stub_popup_shown < 1.0f;
        shown_was = stub_popup_shown;
    }
    EXPECT(opening && shown_was == 1.0f && in_frames >= POPUP_IN_MS / 16 && in_frames <= POPUP_IN_MS / 16 + 1,
           "opening: %d frames, each further open than the one before, then all of it", in_frames);
    sim_keys |= KEY_B; frame(); sim_keys &= ~KEY_B;
    for (int i = 0; i < 40 && popup != POP_NONE; i++)
        frame();
    EXPECT(popup == POP_NONE && stub_popup_shown == 1.0f, "B closes it; the frame it closed in still shows all of it");
    sim_keys |= KEY_B; frame(); sim_keys &= ~KEY_B;     /* at once: the screen under it has the keys */
    int falling = 1, out_frames = 0;
    for (int i = 0; i < 40; i++) {
        float was = stub_popup_shown;
        frame();
        if (stub_popup_shown < 0)
            break;
        out_frames++;
        falling &= stub_popup_shown < was && popup == POP_NONE;
    }
    EXPECT(falling && out_frames >= POPUP_OUT_MS / 16 - 1 && out_frames <= POPUP_OUT_MS / 16 + 1 &&
           stub_popup_shown < 0, "closing: %d more frames, each less of it, then none", out_frames);
    frames_for(400);
    EXPECT(screen == SCR_MAIN && popup == POP_NONE, "the B pressed while it faded went back from Designs (screen "
           "%d)", screen);
    go_designs(0);
    frames_for(400);
    show_note(-1, "A note", "Its text.");
    frames_for(400);
    key(KEY_B);
    for (int i = 0; i < 40 && popup != POP_NONE; i++)
        frame();
    show_note(-1, "Another", "At once.");
    frame();
    EXPECT(popup == POP_NOTE && popup_gone == POP_NONE && stub_popup_shown < 0.2f, "a popup opened while one "
           "fades: only the new one, from its start (%.2f)", (double)stub_popup_shown);
    frames_for(400);
    key(KEY_B); frames_for(600);
    show_note(-1, "A note", "Closed early.");
    frame(); frame();
    sim_keys |= KEY_A; frame(); sim_keys &= ~KEY_A;
    float most = stub_popup_shown, first_back = -1;
    int back = 1, early_frames = 0;
    for (int i = 0; i < 40; i++) {
        float was = stub_popup_shown;
        frame();
        if (stub_popup_shown < 0)
            break;
        if (popup != POP_NONE) {
            most = stub_popup_shown;
            continue;
        }
        if (!early_frames++)
            first_back = stub_popup_shown;      /* the frame it closed in: where it would have been by now */
        else
            back &= stub_popup_shown < was;
    }
    float one_step = (float)FRAME_MS / POPUP_IN_MS;
    EXPECT(popup == POP_NONE && most > 0.3f && most < 0.9f && first_back >= most &&
           first_back <= most + 1.5f * one_step && back && early_frames <= POPUP_OUT_MS / 16,
           "closed when %.2f open: it turns at %.2f and goes back in %d frames, each less of it (0.9.2's first "
           "build jumped to all of it first)", (double)most, (double)first_back, early_frames);
    frames_for(400);

    /* 46. 0.9.2: a question's "Löschen" lets the question fade out before its delete starts (the delete's frames
     *     draw no popup: started in the closing frame it cut the fade off) */
    printf("-- 46: a question fades out before its delete starts\n");
    for (size_t j = 0; j < fake_am.count; j++)
        fake_am.usable[j] = 1;
    design_screen(2);
    int deleted_before = fake_deletes;
    key(KEY_A); frames_for(200); key(KEY_A); frames_for(500);
    EXPECT(popup == POP_ASK_THEME, "the question (popup %d)", popup);
    key(KEY_DRIGHT); frames_for(100); key(KEY_DRIGHT); frames_for(100);
    sim_keys |= KEY_A; frame(); sim_keys &= ~KEY_A;
    for (int i = 0; i < 40 && popup != POP_NONE; i++)
        frame();
    EXPECT(popup == POP_NONE && popup_then == POP_ASK_THEME && fake_deletes == deleted_before &&
           stub_popup_shown == 1.0f, "\"Löschen\": closed, still all of it on screen, nothing deleted yet");
    int fade_frames = 0, fading = 1;
    for (int i = 0; i < 40; i++) {
        float was = stub_popup_shown;
        stylus(BOTTOM_W / 2, DESIGN_TILE_Y + 20);       /* a touch on the tile under it meanwhile: not taken */
        frame();
        lift();
        if (fake_deletes != deleted_before)
            break;
        fade_frames++;
        fading &= stub_popup_shown >= 0 && stub_popup_shown < was;
    }
    frames_for(600);
    EXPECT(fake_deletes == deleted_before + 1 && fading && fade_frames >= POPUP_OUT_MS / 16 - 1 &&
           fade_frames <= POPUP_OUT_MS / 16 + 1, "the delete started after %d frames of the question fading, each "
           "less of it; one delete (%d)", fade_frames, fake_deletes - deleted_before);
    if (popup)
        key(KEY_B);
    frames_for(600);

    /* 47. 0.9.2: a screen's transition runs by the clock from its second frame: a first frame that takes long
     *     (it converts the screen's icons) costs it no step, and it always lasts TRANSITION_MS */
    printf("-- 47: a slow first frame costs the transition nothing\n");
    reset_app();
    go_main(0);
    frames_for(400);
    go_designs(0);
    frame();
    float first_frame = stub_entered;
    sim_now += 80;                  /* that frame took 96 ms */
    frame();
    float second_frame = stub_entered, step = (float)FRAME_MS / TRANSITION_MS, entered_was = second_frame;
    int steps = 1, onward = 1;
    for (int i = 0; i < 40 && stub_entered < 1.0f; i++) {
        frame();
        onward &= stub_entered > entered_was;
        entered_was = stub_entered;
        steps++;
    }
    EXPECT(first_frame == 0 && second_frame > 0.5f * step && second_frame < 1.5f * step,
           "the first frame at 0, the one after a 96 ms frame at %.3f (one step: %.3f)", (double)second_frame,
           (double)step);
    EXPECT(onward && steps >= TRANSITION_MS / 16 && steps <= TRANSITION_MS / 16 + 1, "then %d steps, each further, "
           "to the end", steps);

    /* 42. 0.9.2: the installed designs say how many more the pack takes only when fewer than DESIGNS_ROOM_LOW are
     *     left, as a warning at the footer's right end; the status line stays */
    printf("-- 42: the room that is left\n");
    for (size_t j = 0; j < fake_am.count; j++)
        fake_am.usable[j] = 1;
    open_installed_view();
    char want_status[160], want_warn[80];
    text_installed(lang, (unsigned)pv.usable, want_status, sizeof want_status);
    EXPECT(screen == SCR_INSTALLED && designs_room(&pv) >= DESIGNS_ROOM_LOW && !stub_foot_warn[0] &&
           !strcmp(stub_foot, want_status), "room for %u more: nothing said, the footer \"%s\"", designs_room(&pv),
           stub_foot);
    FILE *mark = fopen("sdmc:/theme-pack/installed.mark", "wb");
    if (!mark || fprintf(mark, "%d\n", TP_INDEX_MAX - 3) < 0 || fclose(mark))
        return 3;
    open_installed_view();
    snprintf(want_warn, sizeof want_warn, text(lang, T_ROOM), 3u);
    EXPECT(screen == SCR_INSTALLED && pv.state == PACK_OK && designs_room(&pv) == 3 &&
           !strcmp(stub_foot_warn, want_warn) && !strcmp(stub_foot, want_status),
           "indices up to %d handed out: \"%s\" beside \"%s\"", TP_INDEX_MAX - 3, stub_foot_warn, stub_foot);
    key(KEY_DDOWN); frames_for(300);
    key(KEY_A); frames_for(600);
    EXPECT(screen == SCR_DESIGN && !strcmp(stub_foot_warn, want_warn), "on one design too (screen %d)", screen);
    remove("sdmc:/theme-pack/installed.mark");
    key(KEY_B); frames_for(500);
    key(KEY_B); frames_for(500);

    /* 43. 0.9.2: while a theme that was opened is checked, the top screen's footer is the status line, not the
     *     folder the file lies in */
    printf("-- 43: the footer while a theme is checked\n");
    mkdir("sdmc:/Themes", 0755);
    copy_to(argv[1], "sdmc:/Themes/a.zip");
    worker_frames = 8;
    open_folder_view();
    EXPECT(screen == SCR_LIST && select_row("a.zip"), "a.zip selected in the folder view");
    frames_for(300);
    text_installed(lang, (unsigned)pv.usable, foot_want, sizeof foot_want);
    foot_frames = foot_other = 0;
    stub_frame_done = foot_frame;
    key(KEY_A); frames_for(600);
    stub_frame_done = NULL;
    worker_frames = 0;
    EXPECT(screen == SCR_CONFIRM && foot_frames >= 6 && !foot_other, "%d frames of the check, %d with another "
           "footer than \"%s\"", foot_frames, foot_other, foot_want);

    /* 45. 0.9.2: what is known about a design is a list in one box: installed already, the check with the line
     *     and the file with its folder; one installed design, the house with where HOME lists it; after an
     *     install, the file the cleanup question is about */
    printf("-- 45: the facts about a design\n");
    char want_fact[160];
    snprintf(want_fact, sizeof want_fact, text(lang, T_INSTALLED_AS), (unsigned)plan.duplicate);
    EXPECT(installed_already() && stub_fact_rows == 2 && stub_fact_symbol[0] == ART_FACT_CHECK &&
           !strcmp(stub_fact_text[0], want_fact) && stub_fact_symbol[1] == ART_FACT_FILE && stub_fact_end[1] &&
           !strcmp(stub_fact_text[1], "/Themes/a.zip"), "installed already: \"%s\", then \"%s\"",
           stub_fact_text[0], stub_fact_text[1]);
    key(KEY_B); frames_for(500);
    design_screen(2);
    snprintf(want_fact, sizeof want_fact, text(lang, T_DESIGN_IN_HOME), 2u);
    EXPECT(screen == SCR_DESIGN && stub_fact_rows == 1 && stub_fact_symbol[0] == ART_FACT_HOME &&
           !strcmp(stub_fact_text[0], want_fact), "one installed design: \"%s\"", stub_fact_text[0]);
    done_with_cleanup("fact.zip");
    snprintf(clean_dir, sizeof clean_dir, "sdmc:/");
    frame();
    EXPECT(screen == SCR_DONE && stub_fact_rows == 1 && stub_fact_symbol[0] == ART_FACT_FILE && stub_fact_end[0] &&
           !strcmp(stub_fact_text[0], "/fact.zip"), "after an install from the card's root: \"%s\"",
           stub_fact_text[0]);
    remove(victim);
    reset_app();
    may_clean = 0;
    go_designs(0);
    frames_for(400);

    /* 44. 0.9.2, the images: the selection's haze ends inside every corner image (it ended at a visible edge);
     *     the progress bar's fill has no hard light line; a held key carries the white copy above it; the name's
     *     placeholder is EMPTY_NAME_MORE higher than the capitals */
    printf("-- 44: the images\n");
    static uint8_t corner_tex[ART_CORNERS_W * ART_CORNERS_H * 4];
    uLongf corner_size = sizeof corner_tex;
    EXPECT(uncompress(corner_tex, &corner_size, ART_CORNERS_Z, ART_CORNERS_Z_SIZE) == Z_OK &&
           corner_size == sizeof corner_tex, "the corners' texture unpacks");
    int edge_most = 0;
    for (int id = 0; id < ART_CORNER_COUNT; id++) {
        const art_sprite *c = &ART_CORNER_SPRITES[id];
        for (int k = 0; k < c->w; k++) {
            const int edge_at[4][2] = {{k, 0}, {k, c->h - 1}, {0, k}, {c->w - 1, k}};
            for (int e = 0; e < 4; e++) {
                int a = texel(corner_tex, ART_CORNERS_W, c->x + edge_at[e][0], c->y + edge_at[e][1], 0);
                edge_most = a > edge_most ? a : edge_most;
            }
        }
    }
    EXPECT(edge_most <= 2, "%d corner images: along their edges the haze is at most %d of 255", ART_CORNER_COUNT,
           edge_most);
    /* 0.9.3: the download's bar in each glass style (round 12): the fill teal through its middle row, solid, and
     * stretching across only, as the track; Platin's collar the bar's height */
    int teal_fills = 0;
    for (int g = 0; g < GLASS_COUNT; g++) {
        const art_sprite *fill = art_of(&ART_STOCK, ART_PROGRESS_FILL + g), *track_sp = art_of(&ART_STOCK, ART_PROGRESS + g);
        int x = fill->x + fill->w / 2, y = fill->y + fill->pad[1] + PROGRESS_H / 2;
        teal_fills += texel(atlas, ART_ATLAS_W, x, y, 0) > 220 &&
                      texel(atlas, ART_ATLAS_W, x, y, 2) > texel(atlas, ART_ATLAS_W, x, y, 3) + 40 &&
                      !fill->cap[1] && !fill->cap[3] && !track_sp->cap[1] && !track_sp->cap[3] &&
                      track_sp->h - track_sp->pad[1] - track_sp->pad[3] == PROGRESS_H;
    }
    const art_sprite *collar = art_of(&ART_STOCK, ART_PROGRESS_COLLAR);
    EXPECT(teal_fills == GLASS_COUNT && collar->h - collar->pad[1] - collar->pad[3] == PROGRESS_H &&
           collar->w - collar->pad[0] - collar->pad[2] == PROGRESS_COLLAR_W, "the bar in %d of %d glass styles: "
           "the fill teal and nearly solid through its middle, track and fill stretching across; Platin's collar its size",
           teal_fills, GLASS_COUNT);
    const art_sprite *held_key = art_of(&ART_STOCK, ART_KEY_B_DOWN);
    int white = 0;
    for (int y = 0; y < LABEL_SHADOW_DY; y++)
        for (int x = 0; x < held_key->w; x++)
            white += texel(atlas, ART_ATLAS_W, held_key->x + x, held_key->y + y, 0) > 40 &&
                     texel(atlas, ART_ATLAS_W, held_key->x + x, held_key->y + y, 1) > 240;
    EXPECT(held_key->pad[1] == LABEL_SHADOW_DY && white >= 4, "the held B key: %d px of the white copy above its "
           "ring", white);
    EXPECT(art_of(&ART_STOCK, ART_EMPTY_NAME)->h == (FONT_CAP * GAL_NAME_PX + FONT_PX / 2) / FONT_PX + EMPTY_NAME_MORE &&
           EMPTY_NAME_MORE == 2 + 2 && EMPTY_NAME_BELOW == 1,
           "the name's placeholder is %d px high: 0.9.2's and 1 px more above and below (R8)",
           art_of(&ART_STOCK, ART_EMPTY_NAME)->h);

    /* 48. 0.9.2: the start screen's turning house and drifting pattern, by the clock (turn.h): the pictures in a
     *     row from face-on, each LOGO_TICKS sixtieths of a second, half a turn in 4 s and the same again; the eyes
     *     a picture apart from half the 3D slider's way up; the pattern back where it was after one whole turn */
    printf("-- 48: the turning house and the drifting pattern\n");
    int in_row = 1, each = 0, seen[LOGO_FRAMES] = {0};
    for (uint64_t turn_ms = 0; turn_ms < 8000; turn_ms++) {
        int pic_at = turn_picture(turn_ms, 0, 0), next = turn_picture(turn_ms + 1, 0, 0);
        seen[pic_at]++;
        in_row &= next == pic_at || next == (pic_at + 1) % LOGO_FRAMES;
    }
    for (int k = 0; k < LOGO_FRAMES; k++)
        each += seen[k] >= 2 * 1000 * LOGO_TICKS / 60 - 2 && seen[k] <= 2 * 1000 * LOGO_TICKS / 60 + 2;
    EXPECT(turn_picture(0, 0, 0) == LOGO_FRAMES / 2 && turn_picture(4000, 0, 0) == LOGO_FRAMES / 2 &&
           turn_picture(2000, 0, 0) == 0 && in_row && each == LOGO_FRAMES,
           "face-on at 0 and 4 s, edge-on at 2 s; in 8 s every picture twice %d ms long (%d of %d), in a row",
           1000 * LOGO_TICKS / 60, each, LOGO_FRAMES);
    EXPECT(turn_picture(1000, 1, 0) == turn_picture(1000, -1, 0) &&
           turn_picture(1000, 1, 0.49f) == turn_picture(1000, 0, 0) &&
           turn_picture(1000, 1, 0.5f) == (turn_picture(1000, 0, 0) + LOGO_EYE) % LOGO_FRAMES &&
           turn_picture(1000, -1, 1.0f) == (turn_picture(1000, 0, 0) + LOGO_FRAMES - LOGO_EYE) % LOGO_FRAMES &&
           turn_picture(2000, -1, 1.0f) == LOGO_FRAMES - LOGO_EYE && turn_picture(1999, 1, 1.0f) == 0,
           "the 3D slider down or under half: one picture for both eyes; from half up the left eye %d on, the right "
           "eye %d back, also across the sheet's end", LOGO_EYE, LOGO_EYE);
    EXPECT(turn_drift(0) == 0 && turn_drift(1000) == PATTERN_DRIFT && turn_drift(8000) == 0 &&
           turn_drift(7999) > PATTERN_TILE - 0.01f && turn_drift(7999) < PATTERN_TILE &&
           (uint64_t)PATTERN_TILE * 1000 / PATTERN_DRIFT == 2ull * LOGO_FRAMES * LOGO_TICKS * 1000 / 60,
           "the pattern moves %d px a second and is where it was after %d s, one whole turn", PATTERN_DRIFT,
           PATTERN_TILE / PATTERN_DRIFT);
    EXPECT(turn_opens(1, 0) && !turn_opens(2, 1) && !turn_opens(2, 2) && turn_opens(4, 2) && turn_opens(900, 2),
           "the turn starts anew when the start screen was not drawn in the frame before, also in the first frame "
           "of all, and not for the second eye");
    reset_app();
    go_main(0);
    stub_glass = stub_pattern = -1;
    frames_for(300);
    EXPECT(screen == SCR_MAIN && stub_scheme == ART_SCHEME_WEISS && stub_glass == GLASS_SHARD &&
           stub_pattern == PATTERN_HOUSES, "the start screen is told the settings' defaults: scheme %d, glass %d, "
           "pattern %d", stub_scheme, stub_glass, stub_pattern);
    stub_start_drawn = stub_glass_untold = 0;
    load_pack();                        /* ST-B: no frame while the pack is read */
    int while_loading = stub_start_drawn;
    frames_for(100);
    EXPECT(while_loading == 0 && stub_start_drawn > 0 && !stub_glass_untold,
           "the start screen's top is drawn only in frames that were told the choices first, and none while the pack "
           "is read (%d of %d draws untold, %d while loading)", stub_glass_untold, stub_start_drawn, while_loading);

    /* 49. 0.9.2, the start screen's textures: per glass 1 MiB of pictures, the cells after the last picture
     *     empty, the pictures in the turn's order (the cut edge on the right before face-on, on the left after:
     *     the eyes' pictures rely on it); per pattern a tile of the two tones only, its alpha at most
     *     PATTERN_ALPHA %, no second tone beside a shape; the tiles' two symbols */
    printf("-- 49: the start screen's textures\n");
    static uint8_t sheet[LOGO_SHEET * LOGO_SHEET * 4], tile[PATTERN_TILE * PATTERN_TILE * 4];
    int sheets_ok = 0, tails_empty = 0, has_glass = 0, per_row = LOGO_SHEET / HERO_ICON;
    uint32_t sheet_sum[ART_GLASSES];
    for (int g = 0; g < ART_GLASSES; g++) {
        uLongf size = sizeof sheet;
        const art_tex house = art_pictures_of(&ART_STOCK, g);     /* the part's pictures */
        sheets_ok += house.z && house.w == LOGO_SHEET && house.h == LOGO_SHEET &&
                     uncompress(sheet, &size, house.z, house.z_size) == Z_OK && size == sizeof sheet;
        int stray = 0, solid = 0;
        for (int y = 0; y < HERO_ICON; y++)
            for (int x = 0; x < HERO_ICON; x++) {
                for (int cell = LOGO_FRAMES; cell < per_row * per_row; cell++)
                    stray += texel(sheet, LOGO_SHEET, cell % per_row * HERO_ICON + x, cell / per_row * HERO_ICON + y,
                                   0) != 0;
                int face = LOGO_FRAMES / 2;
                solid += texel(sheet, LOGO_SHEET, face % per_row * HERO_ICON + x, face / per_row * HERO_ICON + y,
                               0) > 200;
            }
        tails_empty += !stray;
        has_glass += solid > 300;           /* the rims and the edge alone are over 300 px of the face-on picture */
        sheet_sum[g] = 0;
        for (size_t i = 0; i < sizeof sheet; i++)
            sheet_sum[g] = sheet_sum[g] * 31 + sheet[i];
    }
    EXPECT(sheets_ok == ART_GLASSES && tails_empty == ART_GLASSES && has_glass == ART_GLASSES &&
           per_row * per_row >= LOGO_FRAMES, "%d glasses unpack to %d x %d, %d pictures each, the cells after them "
           "empty", sheets_ok, LOGO_SHEET, LOGO_SHEET, LOGO_FRAMES);
    EXPECT(sheet_sum[GLASS_CLEAR] != sheet_sum[GLASS_SHARD] && sheet_sum[GLASS_SHARD] != sheet_sum[GLASS_PLATIN] &&
           sheet_sum[GLASS_CLEAR] != sheet_sum[GLASS_PLATIN], "the three styles' sheets are three different glasses");
    int tiles_ok = 0, tones_ok = 0;
    for (int k = 0; k < ART_SCHEMES * ART_PATTERNS; k++) {      /* every scheme's tiles, in its own two tones */
        const int in = k / ART_PATTERNS, light = ART_STOCK.colour[in][ART_COL_PATTERN_LIGHT],
                  dark = ART_STOCK.colour[in][ART_COL_PATTERN_DARK];
        uLongf size = sizeof tile;
        tiles_ok += uncompress(tile, &size, ART_STOCK.pattern_z[in][k % ART_PATTERNS],
                               ART_STOCK.pattern_z_size[in][k % ART_PATTERNS]) == Z_OK && size == sizeof tile;
        int other_tone = 0, peak = 0, marked = 0, mixed = 0;
        for (int y = 0; y < PATTERN_TILE; y++)
            for (int x = 0; x < PATTERN_TILE; x++) {
                int a = texel(tile, PATTERN_TILE, x, y, 0);
                int colour = texel(tile, PATTERN_TILE, x, y, 3) << 16 | texel(tile, PATTERN_TILE, x, y, 2) << 8 |
                             texel(tile, PATTERN_TILE, x, y, 1);
                other_tone += colour != light && colour != dark;
                peak = a > peak ? a : peak;
                marked += a > 0;
                /* the four texels the linear filter mixes (the tile repeats): where one shows, all carry its tone */
                int shows = 0, tones = 0;
                for (int q = 0; q < 4; q++) {
                    int qx = (x + (q & 1)) % PATTERN_TILE, qy = (y + (q >> 1)) % PATTERN_TILE;
                    shows |= texel(tile, PATTERN_TILE, qx, qy, 0) > 0;
                    tones |= texel(tile, PATTERN_TILE, qx, qy, 1) == (light & 255) ? 1 : 2;
                }
                mixed += shows && tones == 3;
            }
        tones_ok += !other_tone && !mixed && peak == (PATTERN_ALPHA * 255 + 50) / 100 && marked > 100 && marked < 1500;
    }
    EXPECT(tiles_ok == ART_SCHEMES * ART_PATTERNS && tones_ok == ART_SCHEMES * ART_PATTERNS,
           "%d pattern tiles unpack (%d schemes), in %d every pixel one of its scheme's two tones, at most %d %% solid, "
           "most of the tile left open, and no see-through pixel beside a shape carries the other tone (the linear "
           "filter would mix it in)", tiles_ok, ART_SCHEMES, tones_ok, PATTERN_ALPHA);
    /* 73. 0.9.2: one installed design whose file is still in /Themes shows it as a second row of the box, the file
     *     symbol and the path cut at its beginning; box and button then stand lower, the button still takes its
     *     touches and stays above the tab */
    printf("-- 73: an installed design's file\n");
    design_screen(2);
    rect lone = design_tile();
    design_row *shown = NULL;
    for (size_t i = 0; i < designs.count; i++)
        if (designs.rows[i].index == 2)
            shown = &designs.rows[i];
    EXPECT(shown && !shown->file[0] && stub_fact_rows == 1 && lone.y == DESIGN_TILE_Y, "no file: one row, the "
           "button at %d", (int)lone.y);
    if (shown) {
        snprintf(shown->file, sizeof shown->file, "kept.zip");
        frame();
        rect with = design_tile();
        EXPECT(stub_fact_rows == 2 && stub_fact_symbol[0] == ART_FACT_HOME && stub_fact_symbol[1] == ART_FACT_FILE &&
               stub_fact_end[1] && !strcmp(stub_fact_text[1], "/Themes/kept.zip"), "with its file: \"%s\", then "
               "\"%s\"", stub_fact_text[0], stub_fact_text[1]);
        EXPECT(with.y == ACT_TILE_Y && stub_fact_y == ACT_FACTS_Y &&
               stub_fact_y + 2 * FACT_PAD_Y + 2 * FACT_ROW_H <= with.y && with.y + with.h <= BAR_Y, "the box stands "
               "from %d to %d, the button from %d to %d, the tab starts at %d", (int)stub_fact_y,
               (int)stub_fact_y + 2 * FACT_PAD_Y + 2 * FACT_ROW_H, (int)with.y, (int)(with.y + with.h), BAR_Y);
        key(KEY_DDOWN);
        frames_for(100);
        EXPECT(selection_last(with), "the D-pad's selection is drawn around the button where it stands now");
        tap((int)(with.x + with.w / 2), (int)(with.y + with.h - 6), 48);
        frames_for(400);
        EXPECT(popup != POP_NONE && delete_index == 2 && delete_file_kept, "a tap on the button's lower end, where "
               "it was not before: the delete question, which knows of the file (popup %d)", popup);
        key(KEY_B);
        frames_for(400);
        shown->file[0] = 0;
    }

    /* 74. 0.9.2: a lone "A OK" stands on the right tab and takes its touches there; a lone Back stays left */
    printf("-- 74: a lone OK on the right tab\n");
    const int left_tab[2] = {TAB_W / 2, BAR_Y + BAR_H / 2}, right_tab[2] = {BOTTOM_W - TAB_W / 2, BAR_Y + BAR_H / 2};
    reset_app();
    may_clean = 0;
    install_from = SCR_DESIGNS;
    screen = SCR_DONE;
    frames_for(500);
    int drawn_right = stub_bar_right;
    stylus(right_tab[0], right_tab[1]);
    frames_for(64);
    int held_right = stub_bar_right && stub_bar_down;
    stylus(BOTTOM_W / 2, 100);          /* slid off: nothing happens */
    frames_for(32);
    lift();
    frames_for(100);
    EXPECT(held_right && !stub_bar_down && screen == SCR_DONE, "the stylus on the right tab: OK is drawn held (%d); "
           "slid off and lifted: not held, nothing done (screen %d)", held_right, screen);
    tap(left_tab[0], left_tab[1], 48);
    frames_for(400);
    int tab_stayed = screen == SCR_DONE;
    tap(right_tab[0], right_tab[1], 48);
    frames_for(500);
    EXPECT(drawn_right && tab_stayed && screen == SCR_DESIGNS, "\"Installiert\" without the question: OK is drawn on "
           "the right tab (%d), a tap on the left corner does nothing (%d), one on the right tab goes on (screen "
           "%d)", drawn_right, tab_stayed, screen);
    show_problem(T_SD_CARD, "a problem", "", 1);
    frames_for(500);
    drawn_right = stub_bar_right;
    tap(left_tab[0], left_tab[1], 48);
    frames_for(400);
    tab_stayed = screen == SCR_PROBLEM;
    key(KEY_A);
    frames_for(500);
    EXPECT(drawn_right && tab_stayed && screen == SCR_DESIGNS, "a problem with OK: on the right tab (%d), the left "
           "corner does nothing (%d), A goes on (screen %d)", drawn_right, tab_stayed, screen);
    show_problem(T_SD_CARD, "a problem", "", 0);
    frames_for(500);
    drawn_right = stub_bar_right;
    tap(right_tab[0], right_tab[1], 48);
    frames_for(400);
    tab_stayed = screen == SCR_PROBLEM;
    tap(left_tab[0], left_tab[1], 48);
    frames_for(500);
    EXPECT(!drawn_right && tab_stayed && screen == SCR_DESIGNS, "a problem with Back: on the left tab (%d), the right "
           "corner does nothing (%d), the left tab goes back (screen %d)", !drawn_right, tab_stayed, screen);

    /* 75. 0.9.3, the colour schemes' images and colours (schemes.py through art.py): every scheme has an atlas of
     *     its own with the same sprites at the same places; all glass is pixel for pixel the same in every scheme
     *     (the glass's own shadow colour), and so are the held green button, the progress fill and the grey bar
     *     (the green button at rest casts the scheme's shadow); the material of the interface is the scheme's; the
     *     colours drawn by code come from the same palette as the images */
    printf("-- 75: the colour schemes\n");
    static uint8_t scheme_atlas[ART_ATLAS_W * ART_ATLAS_H * 4];
    static const int scheme_own[] = {ART_TILE, ART_TILE_DOWN, ART_FOLDER_ROW, ART_SET_ROW, ART_SET_ROW_DOWN, ART_CARD,
                                     ART_ROW_OFF, ART_CARD_GLASS};
    int unpacked = 0, own = 0, cards = 0, faces = 0;
    for (int k = 0; k < ART_SCHEMES; k++) {
        uLongf atlas_bytes = sizeof scheme_atlas;
        unpacked += uncompress(scheme_atlas, &atlas_bytes, ART_STOCK.atlas_z[k], ART_STOCK.atlas_z_size[k]) == Z_OK &&
                    atlas_bytes == sizeof scheme_atlas;
        int own_n = 0;
        for (size_t i = 0; i < sizeof scheme_own / sizeof *scheme_own; i++)
            own_n += sprite_differs(atlas, scheme_atlas, scheme_own[i]) > 20;
        own += k == ART_SCHEME_WEISS ? !own_n : own_n == (int)(sizeof scheme_own / sizeof *scheme_own);
        /* the card's middle is the scheme's card colour; a tile's face 2 px below its top edge is the scheme's
         * upper face colour (the shading from the sides has not reached the middle) */
        const art_sprite *card_sp = art_of(&ART_STOCK, ART_CARD), *tile_sp = art_of(&ART_STOCK, ART_TILE);
        int cx = card_sp->x + card_sp->w / 2, cy = card_sp->y + card_sp->h / 2;
        uint32_t card_px = (uint32_t)texel(scheme_atlas, ART_ATLAS_W, cx, cy, 3) << 16 |
                           (uint32_t)texel(scheme_atlas, ART_ATLAS_W, cx, cy, 2) << 8 | texel(scheme_atlas, ART_ATLAS_W, cx, cy, 1);
        cards += card_px == ART_STOCK.colour[k][ART_COL_CARD] && texel(scheme_atlas, ART_ATLAS_W, cx, cy, 0) == 255;
        int tx = tile_sp->x + tile_sp->w / 2, ty = tile_sp->y + tile_sp->pad[1] + 2, near = 1;
        for (int c = 1; c < 4; c++)
            near &= abs(texel(scheme_atlas, ART_ATLAS_W, tx, ty, c) - (int)((ART_STOCK.colour[k][ART_COL_FACE_TOP] >> (8 * (c - 1))) & 255)) <= 3;
        faces += near;
    }
    EXPECT(unpacked == ART_SCHEMES && ART_SCHEMES == 8, "%d of %d schemes' atlases unpack to the atlas's size", unpacked,
           ART_SCHEMES);
    EXPECT(own == ART_SCHEMES, "tiles, rows, the settings' row and the card are each scheme's own (%d of %d)", own,
           ART_SCHEMES);
    EXPECT(cards == ART_SCHEMES && faces == ART_SCHEMES, "images and code draw from one palette: the card's image has "
           "the table's card colour in %d schemes, a tile's upper face the table's face colour in %d", cards, faces);
    EXPECT(ART_STOCK.colour[ART_SCHEME_WEISS][ART_COL_BG] == 0xFFFFFF && ART_STOCK.colour[ART_SCHEME_WEISS][ART_COL_CARD] == 0xD8DDE2 &&
           ART_STOCK.colour[ART_SCHEME_WEISS][ART_COL_INK] == 0x595B5C && ART_STOCK.colour[ART_SCHEME_WEISS][ART_COL_PRESS_TOP] == 0xCFD8E5,
           "white is the first scheme: ground FFFFFF, cards D8DDE2, text 595B5C, the held look its own cool grey");
    EXPECT(ART_STOCK.colour[ART_SCHEME_GELB][ART_COL_BG] == 0xF1E9A4 && ART_STOCK.colour[ART_SCHEME_GELB][ART_COL_INK] == 0x615A4B &&
           ART_STOCK.colour[ART_SCHEME_GELB][ART_COL_CARD] == 0xDCD49A && ART_STOCK.colour[ART_SCHEME_NACHT][ART_COL_BG] == 0x1E2631 &&
           ART_STOCK.colour[ART_SCHEME_NACHT][ART_COL_INK] == 0x9CA0A3, "yellow is layout.h's own palette; night is dark "
           "with light text");
    int fixed_same = 0, readable = 0;
    for (int k = 0; k < ART_SCHEMES; k++) {
        const uint32_t *c = ART_STOCK.colour[k];
        fixed_same += c[ART_COL_GREEN] == COL_GREEN && c[ART_COL_GO_TOP] == COL_GO_TOP && c[ART_COL_SELECT_IN] == COL_SELECT_IN &&
                      c[ART_COL_BAR_MID] == COL_BAR_MID && c[ART_COL_LABEL] == COL_LABEL && c[ART_COL_WHITE] == COL_WHITE &&
                      c[ART_COL_GLASS_SHADOW] == COL_GLASS_SHADOW && c[ART_COL_SHARD_A] == COL_SHARD_A;
        /* body text keeps WCAG's 4.5 on the ground it is mostly read on, in every scheme */
        readable += wcag_contrast(c[ART_COL_INK], c[ART_COL_TOP_BG_A]) >= 4.5 &&
                    wcag_contrast(c[ART_COL_INK], c[ART_COL_CARD]) >= 4.5 &&
                    wcag_contrast(c[ART_COL_MUTED], c[ART_COL_TOP_BG_A]) >= 4.5 &&
                    wcag_contrast(c[ART_COL_MUTED], c[ART_COL_FACE_TOP]) >= 4.5 &&
                    wcag_contrast(c[ART_COL_TILE_INK], c[ART_COL_FACE_TOP]) >= 4.5 &&
                    wcag_contrast(c[ART_COL_DLG_INK], c[ART_COL_FACE_TOP]) >= 4.5;
    }
    EXPECT(fixed_same == ART_SCHEMES && readable == ART_SCHEMES, "the install green, the selection's blue, the bar and "
           "the glass colours are the same in %d of %d schemes; text has a contrast of 4.5 or more on the top "
           "screen's ground, on a card and on a button's upper face in %d", fixed_same, ART_SCHEMES, readable);

    /* 76. 0.9.3, the start screen's word and the glass sheet (turn.h, art.py glass_sheet): the word breathes
     *     WORD_TRAVEL px down and back, at rest when the house is face-on, the lower line WORD_LAG_MS behind, smoothly;
     *     per glass one sheet with every glass sprite inside it, nothing between them (the word is read with linear
     *     filtering, so a texel beside it would bleed in), the word's lines as wide as the word, the icons
     *     DESIGNS_ICON px square where they stand and solid glass, three different glasses */
    printf("-- 76: the start screen's word and the glass sheet\n");
    float deepest = 0, lowest = 1, steepest = 0;
    for (int at_ms = 0; at_ms < 8000; at_ms++) {
        float o = turn_breath((uint64_t)at_ms, 0), rise = fabsf(turn_breath((uint64_t)at_ms + 1, 0) - o);
        deepest = o > deepest ? o : deepest;
        lowest = o < lowest ? o : lowest;
        steepest = rise > steepest ? rise : steepest;
    }
    EXPECT(turn_breath(0, 0) == 0 && turn_breath(4000, 0) < 1e-4f && fabsf(turn_breath(2000, 0) - WORD_TRAVEL) < 1e-4f &&
           fabsf(turn_breath(1000, 0) - WORD_TRAVEL / 2.0f) < 1e-3f && turn_breath(WORD_LAG_MS, WORD_LAG_MS) == 0 &&
           fabsf(turn_breath(1234, WORD_LAG_MS) - turn_breath(1234 - WORD_LAG_MS, 0)) < 1e-5f &&
           turn_breath(100, WORD_LAG_MS) > 0 && lowest == 0 && fabsf(deepest - WORD_TRAVEL) < 1e-4f &&
           steepest < 0.01f && turn_picture(0, 0, 0) == turn_picture(4000, 0, 0),
           "the word breathes %d px (deepest %.4f) once in 4 s with the house's loop, at rest at 0, the lower line %d ms "
           "behind, at most %.4f px a ms", WORD_TRAVEL, (double)deepest, WORD_LAG_MS, (double)steepest);
    /* the kit's sheet (0) and the application's part's (1): each one's size, its texels, which a sprite's place holds */
    const int gw[2] = {ART_GLASS_SHEET_W, ART_APP_PART.sheet_w}, gh[2] = {ART_GLASS_SHEET_H, ART_APP_PART.sheet_h};
    uint8_t *gsheet[2], *in_sprite[2];
    for (int tx = 0; tx < 2; tx++)
        gsheet[tx] = malloc((size_t)gw[tx] * gh[tx] * 4), in_sprite[tx] = calloc((size_t)gw[tx] * gh[tx], 1);
    int gsheets_ok = 0, in_sheet = 0, bleed = 0, icons_glass = 0, word_shown = 0, icons_square = 0;
    uint32_t gsum[ART_GLASSES];
    for (int k = 0; k < ART_SHEET_SPRITES; k++) {
        const art_sprite *s = art_of(&ART_STOCK, sheet_id(k));
        const int tx = sheet_id(k) >= ART_ALL;
        in_sheet += s->x + s->w <= gw[tx] && s->y + s->h <= gh[tx];
        for (int y = s->y; y < s->y + s->h && y < gh[tx]; y++)
            for (int x = s->x; x < s->x + s->w && x < gw[tx]; x++)
                in_sprite[tx][y * gw[tx] + x] = 1;
    }
    static const int glass_icons[] = {ART_SD, ART_QR, ART_INSTALLED};
    for (int k = 0; k < 3; k++) {
        const art_sprite *s = art_of(&ART_STOCK, glass_icons[k]);
        icons_square += s->w - s->pad[0] - s->pad[2] == DESIGNS_ICON && s->h - s->pad[1] - s->pad[3] == DESIGNS_ICON;
    }
    for (int g = 0; g < ART_GLASSES; g++) {
        gsum[g] = 0;
        for (int tx = 0; tx < 2; tx++) {
            const art_tex z = art_tex_of(&ART_STOCK, tx ? ART_ALL + ART_APP_COUNT : ART_COUNT, g);
            uLongf want = (uLongf)gw[tx] * gh[tx] * 4, size = want;
            gsheets_ok += z.z && uncompress(gsheet[tx], &size, z.z, z.z_size) == Z_OK && size == want;
            for (int y = 0; y < gh[tx]; y++)
                for (int x = 0; x < gw[tx]; x++) {
                    int a = texel(gsheet[tx], gw[tx], x, y, 0);
                    bleed += a && !in_sprite[tx][y * gw[tx] + x];
                    gsum[g] = gsum[g] * 31 + (uint32_t)a * 7 + texel(gsheet[tx], gw[tx], x, y, 3);
                }
        }
        for (int k = 0; k < 3; k++) {
            const art_sprite *s = art_of(&ART_STOCK, glass_icons[k]);
            const int tx = glass_icons[k] >= ART_ALL;
            int opaque = 0;
            for (int y = 0; y < s->h; y++)
                for (int x = 0; x < s->w; x++)
                    opaque += texel(gsheet[tx], gw[tx], s->x + x, s->y + y, 0) > 128;
            icons_glass += opaque > 200;
        }
        for (int id = ART_WORD_TOP; id <= ART_WORD_LOW; id++) {
            const art_sprite *s = art_of(&ART_STOCK, id);
            const int tx = id >= ART_ALL;
            int rims = 0, opaque = 0;    /* the image's first and last rows hold only the shadow's last trace */
            for (int x = 0; x < s->w; x++) {
                rims += texel(gsheet[tx], gw[tx], s->x + x, s->y, 0) > 8 ||
                        texel(gsheet[tx], gw[tx], s->x + x, s->y + s->h - 1, 0) > 8;
                for (int y = 0; y < s->h; y++)
                    opaque += texel(gsheet[tx], gw[tx], s->x + x, s->y + y, 0) > 128;
            }
            word_shown += !rims && opaque > 1500;
        }
    }
    for (int tx = 0; tx < 2; tx++)
        free(gsheet[tx]), free(in_sprite[tx]);
    EXPECT(gsheets_ok == 2 * ART_GLASSES && in_sheet == ART_SHEET_SPRITES && !bleed && icons_square == 3 &&
           icons_glass == 3 * ART_GLASSES && word_shown == 2 * ART_GLASSES &&
           art_of(&ART_STOCK, ART_WORD_TOP)->w == WORD_W &&
           art_of(&ART_STOCK, ART_WORD_TOP)->h == WORD_TOP_H &&
           art_of(&ART_STOCK, ART_WORD_LOW)->h == WORD_LOW_H &&
           WORD_LOW_ROW + WORD_LOW_H <= WORD_H && WORD_TOP_ROW < WORD_LOW_ROW,
           "%d of %d glass sheets unpack (the kit's to %d x %d, the part's to %d x %d), %d of %d sprites inside "
           "their own, %d texels outside every sprite, the icons %d px square and solid in %d of %d, the word's lines "
           "whole in %d of %d", gsheets_ok, 2 * ART_GLASSES, ART_GLASS_SHEET_W, ART_GLASS_SHEET_H,
           ART_APP_PART.sheet_w, ART_APP_PART.sheet_h, in_sheet, ART_SHEET_SPRITES, bleed, DESIGNS_ICON, icons_glass,
           3 * ART_GLASSES, word_shown, 2 * ART_GLASSES);
    EXPECT(gsum[GLASS_CLEAR] != gsum[GLASS_SHARD] && gsum[GLASS_SHARD] != gsum[GLASS_PLATIN] &&
           gsum[GLASS_CLEAR] != gsum[GLASS_PLATIN], "the three glass sheets are three different glasses");

    /* -- 77: the carousel (0.9.3, round 14 F3; swarm.h): the panes' slots as drawn; which items stand in which pane
     *     for every selection of lists of 1 to 12, the ends included (nothing wraps, the shorter side has fewer
     *     panes); a move glides by the clock and draws at most SWARM_KEYED items, none in the gone slot */
    printf("-- 77: the carousel's panes\n");
    {
        static const float sw_want[5][7] = {    /* middle x left, right; middle y left, right; w; h; opacity */
            {200, 200, 88 + 3, 88 + 3, 80, 80, 1}, {141.38f, 258.62f, 93 + 3, 93 + 3, 45.24f, 58, 0.80f},  /* FB-L: */
            {110.20f, 289.80f, 96 + 3, 98 + 3, 33.12f, 46, 0.52f},     /* every middle 3 px under F3's */
            {92.76f, 307.24f, 101 + 3, 99 + 3, 23.76f, 36, 0.28f}, {86.20f, 313.80f, 103 + 3, 101 + 3, 17.36f, 28, 0}};
        int sw_slots_ok = 0;
        for (int d = 0; d <= SWARM_GONE; d++) {
            swarm_pane l = swarm_at((float)-d, 0), sw_r = swarm_at((float)d, 0);
            sw_slots_ok += fabsf(l.x + l.w / 2 - sw_want[d][0]) < 0.01f && fabsf(sw_r.x + sw_r.w / 2 - sw_want[d][1]) < 0.01f &&
                           fabsf(l.y + l.h / 2 - sw_want[d][2]) < 0.01f && fabsf(sw_r.y + sw_r.h / 2 - sw_want[d][3]) < 0.01f &&
                           fabsf(sw_r.w - sw_want[d][4]) < 0.01f && fabsf(l.h - sw_want[d][5]) < 0.01f &&
                           fabsf(sw_r.opacity - sw_want[d][6]) < 0.001f && l.side == (d ? -1 : 0) && sw_r.side == (d ? 1 : 0) &&
                           (d < SWARM_GONE ? sw_r.alpha == 1 : sw_r.alpha == 0) && (d == 3 ? sw_r.edge == 0.6f : d < 3 ? sw_r.edge == 0 : 1);
        }
        swarm_pane sw_mid = swarm_at(0, 0), sw_half = swarm_at(0.5f, 0);
        EXPECT(sw_slots_ok == SWARM_GONE + 1 && sw_mid.x == GAL_PICTURE_X && sw_mid.y == GAL_PICTURE_Y &&
               sw_mid.w == GAL_PICTURE && sw_mid.depth == DEPTH_ICON && swarm_at(1, 0).depth == 2 && swarm_at(3, 0).depth == 0 &&
               fabsf(sw_half.h - 69) < 0.001f && fabsf(sw_half.opacity - 0.9f) < 0.001f && fabsf(sw_half.depth - 3) < 0.001f,
               "the slots as F3 drew them: middle %g px at %g, %g; d1 to d3 and gone at their places, sizes, opacities; "
               "half way between two slots half of each", (double)sw_mid.w, (double)sw_mid.x, (double)sw_mid.y);
        int sw_lists = 0, sw_one_side = 0;
        for (size_t sw_n = 1; sw_n <= 12; sw_n++)
            for (size_t sw_c = 0; sw_c < sw_n; sw_c++) {
                long sw_first, sw_last;
                swarm_span((float)sw_c, sw_n, &sw_first, &sw_last);
                long sw_left = (long)sw_c - sw_first, sw_right = sw_last - (long)sw_c;
                sw_lists += sw_left == (long)(sw_c < SWARM_SIDE ? sw_c : SWARM_SIDE) &&
                            sw_right == (long)(sw_n - 1 - sw_c < SWARM_SIDE ? sw_n - 1 - sw_c : SWARM_SIDE);
                sw_one_side += sw_c == 0 && sw_left == 0;
            }
        long f1, l1, f2, l2, f3, l3;
        swarm_span(0, 10, &f1, &l1);
        swarm_span(1, 10, &f2, &l2);
        swarm_span(1, 2, &f3, &l3);
        EXPECT(sw_lists == 78 && sw_one_side == 12 && f1 == 0 && l1 == 3 && f2 == 0 && l2 == 4 && f3 == 0 && l3 == 1,
               "%d of 78 selections in lists of 1 to 12 have min(items, %d) panes on each side, the first item the swarm "
               "on its right only (items 0 to %ld), the second one pane on its left, a list of two two panes", sw_lists,
               SWARM_SIDE, l1);
        /* a move from item 5 to 6 in a list of 20, a frame every 16 ms */
        swarm sw = {0};
        float sw_pos = swarm_follow(&sw, &sw, 5, 1000, 1), sw_was = sw_pos;
        int sw_frames = 0, sw_steady = 1, sw_most = 0, sw_gone = 0;
        uint32_t frame_n = 2;
        for (uint64_t sw_t = 1016; sw_t <= 1300; sw_t += 16, frame_n++) {
            sw_pos = swarm_follow(&sw, &sw, 6, sw_t, frame_n);
            sw_frames += eased_moving(&sw.pos, sw_t);
            sw_steady &= sw_pos >= sw_was && sw_pos - sw_was <= 1.5f * 16 / SWARM_MOVE_MS + 0.001f;
            long sw_first, sw_last;
            swarm_span(sw_pos, 20, &sw_first, &sw_last);
            int sw_drawn = (int)(sw_last - sw_first + 1), sw_keys = 0;
            for (long i = sw_first; i <= sw_last; i++) {
                sw_gone += fabsf((float)i - sw_pos) >= SWARM_GONE;
                sw_keys |= 1 << icon_slot_keyed((size_t)i);
            }
            sw_most = sw_drawn > sw_most ? sw_drawn : sw_most;
            sw_steady &= __builtin_popcount((unsigned)sw_keys) == sw_drawn;
            sw_was = sw_pos;
        }
        EXPECT(sw_pos == 6 && sw_frames >= 11 && sw_frames <= 13 && sw_steady && sw_most == SWARM_KEYED && !sw_gone,
               "a move glides in %d frames (%d ms), never more than 1.5 times the even step, %d items drawn at most, "
               "each in its own slot, none at the gone slot", sw_frames, SWARM_MOVE_MS, sw_most);
        /* sent on before it arrives: on from where it is; a jump of more than SWARM_SIDE, another list or a frame
         * without the carousel: at rest at once */
        swarm_follow(&sw, &sw, 7, 2000, frame_n++);
        float mid_way = swarm_follow(&sw, &sw, 7, 2100, frame_n++);
        float sent_on = swarm_follow(&sw, &sw, 8, 2100, frame_n++);
        int jumps = 0;
        jumps += swarm_follow(&sw, &sw, 15, 2116, frame_n++) == 15;
        jumps += swarm_follow(&sw, &sw_was, 14, 2132, frame_n++) == 14;
        frame_n += 5;
        jumps += swarm_follow(&sw, &sw_was, 13, 2148, frame_n++) == 13;
        EXPECT(mid_way > 6 && mid_way < 7 && sent_on == mid_way && swarm_follow(&sw, &sw_was, 12, 2164, frame_n++) == 13 && eased_moving(&sw.pos, 2180) &&
               jumps == 3, "sent on half way (at %.2f) it goes on from there; a jump of 8 items, another list, a pause: "
               "there at once (%d of 3); one step on from there glides again", (double)mid_way, jumps);
    }

    /* -- 78: the carousel's icon slots: seven panes and the folder view's rows, in 3D, over a run of moves; each item
     *     keeps its slot while it glides, a move converts only the picture coming in, the right eye converts nothing
     *     and takes no slots of its own, and no picture is ever left without a slot */
    printf("-- 78: the carousel's icon slots\n");
    {
        const int sw_rows = (SCREEN_H + ROW_H + FACE_PAD_LOW) / ROW_H + 1;     /* the folder view's rows drawn at most */
        int sw_held[ICON_SLOTS], sw_converted = 0, sw_right_eye = 0, sw_missing = 0, sw_worst_move = 0, sw_frames = 0;
        for (int sw_k = 0; sw_k < ICON_SLOTS; sw_k++)
            sw_held[sw_k] = -1;
        icon_slots sw_is = {0};
        swarm sw = {0};
        uint64_t sw_t = 5000;
        uint32_t frame_n = 1;
        const size_t sw_count = 30;
        for (size_t sw_c = 0; sw_c < sw_count; sw_c++) {
            int sw_before = sw_converted;
            for (int sw_f = 0; sw_f < 16; sw_f++, sw_t += 16, frame_n++) {
                icon_slots_frame(&sw_is);
                float sw_pos = swarm_follow(&sw, &sw, sw_c, sw_t, frame_n);
                long sw_first, sw_last;
                swarm_span(sw_pos, sw_count, &sw_first, &sw_last);
                for (int sw_eye = 0; sw_eye < 2; sw_eye++) {
                    icon_slots_top(&sw_is, sw_eye);
                    for (long i = sw_first; i <= sw_last; i++) {
                        int sw_k = icon_slot_keyed((size_t)i);
                        if (sw_held[sw_k] != (int)i) {
                            sw_held[sw_k] = (int)i;
                            sw_converted++;
                            sw_right_eye += sw_eye;
                        }
                    }
                    if (sw_eye)
                        break;
                    for (int sw_r = 0; sw_r < sw_rows; sw_r++) {    /* the bottom screen's rows, the same ones in every frame */
                        int sw_k = icon_slot_next(&sw_is);
                        sw_missing += sw_k < 0;
                        if (sw_k >= 0 && sw_held[sw_k] != 1000 + sw_r) {
                            sw_held[sw_k] = 1000 + sw_r;
                            sw_converted++;
                        }
                    }
                }
                sw_frames++;
            }
            if (sw_c)
                sw_worst_move = sw_converted - sw_before > sw_worst_move ? sw_converted - sw_before : sw_worst_move;
        }
        EXPECT(sw_rows + 1 <= ICON_SLOTS - SWARM_KEYED && !sw_missing && !sw_right_eye && sw_worst_move == 1 &&
               sw_converted == SWARM_SIDE + 1 + sw_rows + (int)sw_count - SWARM_SIDE - 1,
               "%d moves in 3D, %d frames, %d rows below and one more picture fit the %d slots after the carousel's %d; "
               "%d pictures converted in all (at most %d a move, the right eye %d), none without a slot", (int)sw_count - 1,
               sw_frames, sw_rows, ICON_SLOTS - SWARM_KEYED, SWARM_KEYED, sw_converted, sw_worst_move, sw_right_eye);
    }


    /* -- 79: the carousel's idle light: once the panes have rested SWARM_LIGHT_AFTER_MS it crosses the left
     *     neighbour, the middle pane and the right one in turn, SWARM_LIGHT_MS each, then again every
     *     SWARM_LIGHT_EVERY_MS; never during a move, never on the panes further out */
    printf("-- 79: the carousel's idle light\n");
    {
        swarm lw = {0};
        swarm_follow(&lw, &lw, 4, 10000, 1);
        swarm_follow(&lw, &lw, 5, 10016, 2);       /* a move: at rest at 10216 */
        int during = 0, outer = 0, order_ok = 1, passes = 0, lit_was = -2;
        for (uint64_t lt = 10016; lt < 10216 + SWARM_LIGHT_AFTER_MS + 2 * SWARM_LIGHT_EVERY_MS; lt += 4) {
            int lit = -2;
            for (int d = -SWARM_SIDE; d <= SWARM_SIDE; d++) {
                float l = swarm_light(&lw, lt, d);
                if (l < 0)
                    continue;
                during += lt < 10216 + SWARM_LIGHT_AFTER_MS;
                outer += d < -1 || d > 1;
                order_ok &= lit == -2 && l < 1;
                lit = d;
            }
            if (lit != lit_was && lit != -2) {
                order_ok &= lit == (lit_was == -2 ? -1 : lit_was + 1);
                passes += lit == 1;
            }
            if (lit != -2 || lit_was == 1)
                lit_was = lit;
        }
        EXPECT(!during && !outer && order_ok && passes == 2 && swarm_light(&lw, 10216 + SWARM_LIGHT_AFTER_MS, -1) == 0 &&
               fabsf(swarm_light(&lw, 10216 + SWARM_LIGHT_AFTER_MS + SWARM_LIGHT_MS + SWARM_LIGHT_MS / 2, 0) - 0.5f) < 1e-4f,
               "the light crosses left, middle, right in turn, %d ms each, %d times in %d ms after a move, never "
               "before %d ms of rest, never further out", SWARM_LIGHT_MS, passes, SWARM_LIGHT_AFTER_MS + 2 * SWARM_LIGHT_EVERY_MS,
               SWARM_LIGHT_AFTER_MS);
    }

    /* -- 82: the carousel under a held key (moves every 96 ms, each before the last glide ends): in every frame the
     *     items drawn have slots of their own, and each item is converted once while it stays in the swarm; in 3D
     *     the panes' depth is the same on both sides, falls with the distance in small steps and stays in front of
     *     the card */
    printf("-- 82: the carousel under a held key, and its depth\n");
    {
        int kh_held[SWARM_KEYED], kh_clash = 0, kh_converted = 0, kh_most = 0;
        for (int k = 0; k < SWARM_KEYED; k++)
            kh_held[k] = -1;
        swarm kh = {0};
        uint32_t kh_frame = 1;
        const size_t kh_count = 40;
        size_t kh_item = 0;
        for (uint64_t kt = 20000; kt < 20000 + 96 * (kh_count - 1) + 400; kt += 16, kh_frame++) {
            if (kt >= 20000 + 96 * (kh_item + 1) && kh_item + 1 < kh_count)
                kh_item++;
            float kh_pos = swarm_follow(&kh, &kh, kh_item, kt, kh_frame);
            long kh_first, kh_last;
            swarm_span(kh_pos, kh_count, &kh_first, &kh_last);
            int kh_used = 0;
            kh_most = kh_last - kh_first + 1 > kh_most ? (int)(kh_last - kh_first + 1) : kh_most;
            for (long i = kh_first; i <= kh_last; i++) {
                int k = icon_slot_keyed((size_t)i);
                kh_clash += (kh_used >> k) & 1;
                kh_used |= 1 << k;
                if (kh_held[k] != (int)i) {
                    kh_held[k] = (int)i;
                    kh_converted++;
                }
            }
        }
        int kh_depth_ok = 1;
        for (float d = 0; d <= SWARM_GONE; d += 0.05f) {
            swarm_pane l = swarm_at(-d, 0), r = swarm_at(d, 0), on = swarm_at(d + 0.05f, 0);
            kh_depth_ok &= l.depth == r.depth && on.depth <= r.depth && r.depth <= DEPTH_ICON && r.depth >= DEPTH_CARD;
        }
        for (int d = 0; d < SWARM_GONE; d++)
            kh_depth_ok &= swarm_at((float)d, 0).depth - swarm_at((float)d + 1, 0).depth <= 2;
        EXPECT(!kh_clash && kh_most <= SWARM_KEYED && kh_converted == (int)kh_count && kh_depth_ok,
               "%d moves 96 ms apart: at most %d items drawn at once, no two in one slot, %d pictures converted for %d "
               "items; depth the same left and right, from %d in the middle down to the card's %d in steps of 2 or less",
               (int)kh_count - 1, kh_most, kh_converted, (int)kh_count, DEPTH_ICON, DEPTH_CARD);
    }

    /* 81. 0.9.3, O5: on the settings screen a change of the glass or the pattern shows there at once, as the colour
     *     does: the glass's row shows the start screen's word in the chosen glass on the top screen, the pattern's
     *     row the chosen pattern behind it; each step (right) told to the drawing layer for the frame right after the
     *     key */
    printf("-- 81: glass and pattern shown on the settings screen\n");
    {
        extern int stub_word_shown, stub_backdrop_shown;
        reset_app();
        load_pack();
        go_main(0);
        frames_for(300);
        key(KEY_Y); frames_for(300);
        key(KEY_DDOWN); frames_for(100);
        for (int k = 0; k < 3; k++) {
            key(KEY_DDOWN); frames_for(100);
        }
        int on_pattern = screen == SCR_SETTINGS && focus == 3;
        int pattern_was = prefs.pattern, behind_now = stub_backdrop_shown, word_now = stub_word_shown;
        key(KEY_DRIGHT);
        int pattern_told =
            settings_pattern() == (pattern_was + 1) % SETTINGS_STYLES && stub_pattern == settings_pattern();
        frames_for(100);
        EXPECT(on_pattern && pattern_told && stub_backdrop_shown > behind_now && stub_word_shown == word_now,
               "the pattern's row: the pattern behind (%d frames), a change told at once (%d), no word",
               stub_backdrop_shown - behind_now, pattern_told);
    }

    printf("-- 83: a colour step is drawn in the frame of its key\n");
    go_main(0);
    frames_for(300);
    key(KEY_Y); frames_for(300);
    key(KEY_DDOWN); frames_for(60);
    int from_scheme = settings_scheme(), to_scheme = (from_scheme + 1) % ART_SCHEMES;
    EXPECT(screen == SCR_SETTINGS && ring && focus == 0, "the settings screen, the cursor on the colour's row");
    look_frames = look_mismatch = 0;
    stub_frame_done = look_frame;
    sim_keys |= KEY_DRIGHT;
    frame();                    /* the one frame in which the key goes down */
    int key_frames = look_frames, key_scheme = look_scheme;
    const uint32_t *key_colours = look_colours;
    sim_keys &= ~KEY_DRIGHT;
    frames_for(200);
    key(KEY_DLEFT); frames_for(200);
    stub_frame_done = NULL;
    EXPECT(key_frames == 1 && key_scheme == to_scheme && key_colours == ART_STOCK.colour[to_scheme] && !look_mismatch &&
           settings_scheme() == from_scheme && ui_colours == ART_STOCK.colour[from_scheme],
           "right on the colour's row: the frame of the key is drawn in the next scheme (%d, told %d) with its colours; "
           "no frame is drawn with colours other than the setting's (%d); left: back to %d", to_scheme, key_scheme,
           look_mismatch, from_scheme);

    /* 85. 0.9.3, the QR screen when a download ends and its check begins (main.c job_bottom): the download's bar
     *     stays, never held, until the loader's cover has taken its place, so the bottom screen leaves as any does
     *     when a job begins (0.9.2: the bar gone in one frame, the bare background until the loader came) */
    printf("-- 85: the QR screen's bar leaves with the loader coming in\n");
    {
        snprintf(url, sizeof url, "https://example.org/a.zip");
        text_shown(url_shown, sizeof url_shown, url);
        screen = SCR_URL;
        frames_for(300);
        worker_frames = 40;
        open_confirm("a.zip", "sdmc:/Themes", SCR_DESIGNS);     /* as download_done opens the file it saved */
        int checking = job == JOB_CHECK, check_frames = 0, with_bar = 0, bare = 0, held_bar = 0, covered = 0;
        float cover_most = 0;
        while (job == JOB_CHECK && check_frames < 200) {
            frame();
            check_frames++;
            rect bar_at = ui_bar_rect();
            int bar_seen = 0;
            for (int k = 0; k < stub_marks; k++)
                bar_seen |= stub_mark_kind[k] == 'o' && stub_mark_at[k].y == bar_at.y && stub_mark_at[k].w == bar_at.w;
            float cover_now = stub_loader ? stub_loader_cover : 0;
            with_bar += bar_seen;
            held_bar += bar_seen && stub_bar_down;
            bare += !bar_seen && cover_now <= 0;
            covered += cover_now >= 1;
            cover_most = cover_now > cover_most ? cover_now : cover_most;
        }
        worker_frames = 0;
        frames_for(500);
        EXPECT(checking && !bare && with_bar > LOADER_WAIT_MS / 16 && !held_bar && covered && cover_most >= 1 &&
                   screen == SCR_CONFIRM,
               "the check after a download: %d of %d frames with the bar (never held: %d), %d with neither bar nor "
               "cover, the loader's cover reaches %.2f; then the check screen (screen %d)", with_bar, check_frames,
               !held_bar, bare, cover_most, screen);
    }

    /* 86. 0.9.3, the "Installiert" card of both parts (ui_done_card): the name's cap line lies at least 4 px under
     *     the check's disc (before: 0.2 px, so a tall letter under the check touched it, seen in the emulator on the
     *     Badges part's done screen), and the way under the name still ends inside the card */
    printf("-- 86: the done card's name clears the check\n");
    {
        const int air = 4;
        float cap_top = DONE_TEXT_Y + NAME_LH / 2.0f - (float)FONT_CAP * NAME_PX / FONT_PX / 2;
        float disc_bottom = DONE_BADGE_Y + DONE_BADGE, way_bottom = DONE_TEXT_Y + NAME_LH + DONE_TEXT_GAP + WAY_LH;
        EXPECT(cap_top - disc_bottom >= air && way_bottom <= DONE_CARD_Y + DONE_CARD_H - air,
               "the name's cap line %.1f px under the check (at least %d), the way's line box ends %.1f px above the "
               "card's foot", cap_top - disc_bottom, air, DONE_CARD_Y + DONE_CARD_H - way_bottom);
    }

    /* 87. G1: what a popup's right button does goes to the look before the popup is drawn (ui_popup_tone): a
     *     question that deletes a theme or a file removes, the folder's question, a note and the help do not */
    printf("-- 87: the popup's tone\n");
    {
        static const int kinds[] = {POP_ASK_THEME, POP_ASK_FILE, POP_ASK_FOLDER, POP_NOTE, POP_HELP};
        static const char want[] = "11000";
        char told[8] = "";
        for (int n = 0; n < 5; n++) {
            popup = kinds[n], popup_fresh = 1;
            stub_popup_removes = -1;
            frames_for(100);
            told[n] = stub_popup_removes == 1 ? '1' : stub_popup_removes == 0 ? '0' : '?';
            popup = POP_NONE, popup_gone = POP_NONE;
            frames_for(100);
        }
        EXPECT(!strcmp(told, want), "the right button removes in the theme's and the file's question, not in the "
               "folder's, a note or the help (%s, want %s)", told, want);
        EXPECT(stub_look == LOOK_STOCK, "a build of Stock alone tells Stock");
    }

    /* G1-R5b: a key down during a delete and held as what follows it opens presses nothing there: the note after
     *     "Design löschen"; let go and pressed again it acts as there (B closes the note) */
    printf("-- G1-R5b: a key held through a delete\n");
    make_pack(argv[1], argv[2], 0);
    for (size_t j = 0; j < fake_am.count; j++)
        fake_am.usable[j] = 1;
    open_installed_view();
    key(KEY_DDOWN); frames_for(600);
    key(KEY_A); frames_for(500);
    key(KEY_A); frames_for(200); key(KEY_A); frames_for(500);
    int asked = popup == POP_ASK_THEME;
    key(KEY_DRIGHT); frames_for(100); key(KEY_DRIGHT); frames_for(100);
    deletes_were = fake_deletes;
    key_through(JOB_DELETE_THEME, KEY_B, KEY_A, 1200);
    int noted = fake_deletes == deletes_were + 1 && popup == POP_NOTE;
    EXPECT(asked && noted && still_until_again(KEY_B, note_closed), "B down during \"Design löschen\", held as its "
           "note opens: the note stays until B is let go and pressed again (asked %d, noted %d, popup %d)", asked,
           noted, popup);
    mkdir("sdmc:/Themes", 0755);

    /* G1-R5b: the questions: a key down from before one showed presses nothing in it until let go and pressed
     *     again (input is read in every frame up to them, so a held key never was new there; pinned here): B, down
     *     during the push of "Design löschen" and held as the question opens, then pressed again cancels; A, down during the check and held as the check screen opens, then pressed again shows the
     *     cursor */
    printf("-- G1-R5b: a key held into a question\n");
    copy_to(argv[1], "sdmc:/Themes/a.zip");
    close_theme();
    reset_app();
    load_pack();
    go_designs(0);
    frames_for(200);
    through_job = JOB_CHECK, through_key = KEY_A, through_frames = 0;
    worker_frames = 20;
    stub_each_frame = through_tick;
    open_confirm("a.zip", "sdmc:/Themes", SCR_DESIGNS);
    for (int i = 0; i < 200 && screen != SCR_CONFIRM; i++)
        frame();
    stub_each_frame = held_tick;
    worker_frames = 0;
    r5b_target = SCR_CONFIRM;
    int checked = screen == SCR_CONFIRM && !ring;
    EXPECT(checked && still_until_again(KEY_A, left_target), "A down during the check, held as the check screen "
           "opens: no cursor until A is let go and pressed again (checked %d, ring %d)", checked, ring);
    for (size_t j = 0; j < fake_am.count; j++)
        fake_am.usable[j] = 1;
    open_installed_view();
    key(KEY_DDOWN); frames_for(600);
    key(KEY_A); frames_for(500);
    key(KEY_A); frames_for(200);
    sim_keys |= KEY_A; frame();
    sim_keys |= KEY_B; frames_for(500);
    sim_keys &= ~KEY_A;
    asked = popup == POP_ASK_THEME;
    EXPECT(asked && still_until_again(KEY_B, ask_closed), "B down during the push of \"Design löschen\", held as "
           "its question opens: it stays until B is let go and pressed again (asked %d, popup %d)", asked, popup);

    /* G1-R6: the file delete's way to the Designs screen ("Datei löschen" on the check screen of a file chosen
     *     there): neither a popup nor guard_screen follows it, only job_here's read of the keys at its end. A, let go
     *     after the confirming press, down again during the delete and held as the Designs screen opens, shows no
     *     cursor there until let go and pressed again */
    printf("-- G1-R6: a key held through a file delete\n");
    confirm_installed("held.zip");
    key(KEY_A); frames_for(200);
    key(KEY_DRIGHT); frames_for(200);
    key(KEY_A); frames_for(500);
    asked = popup == POP_ASK_FILE;
    key(KEY_DRIGHT); frames_for(100); key(KEY_DRIGHT); frames_for(100);
    key_through(JOB_DELETE_FILE, KEY_A, KEY_A, 1200);
    int file_gone = !victim_exists(), held_on = screen == SCR_DESIGNS && !ring && (sim_keys & KEY_A);
    r5b_target = SCR_DESIGNS;
    EXPECT(asked && file_gone && held_on && still_until_again(KEY_A, left_target), "A down during \"Datei löschen\", "
           "held as the Designs screen opens: no cursor until A is let go and pressed again (asked %d, deleted %d, "
           "screen %d, ring %d)", asked, file_gone, screen, ring);

    /* G1-R6: a D-pad key down during a job and held as the screen after it opens does not repeat there until let
     *     go (handle_input's repeat less keys_stale): down during "Design löschen" and held through its note (B
     *     closes it) and the list under it; let go, the D-pad moves again */
    printf("-- G1-R6: no repeat of a key held through a job\n");
    for (size_t j = 0; j < fake_am.count; j++)
        fake_am.usable[j] = 1;
    open_installed_view();
    key(KEY_DDOWN); frames_for(600);
    key(KEY_A); frames_for(500);
    key(KEY_A); frames_for(200); key(KEY_A); frames_for(500);
    asked = popup == POP_ASK_THEME;
    key(KEY_DRIGHT); frames_for(100); key(KEY_DRIGHT); frames_for(100);
    deletes_were = fake_deletes;
    key_through(JOB_DELETE_THEME, KEY_DDOWN, KEY_A, 1200);
    noted = fake_deletes == deletes_were + 1 && popup == POP_NOTE;
    key(KEY_B); frames_for(100);
    int sel_was = lv.selected;
    size_t at_was = lv.cursor;
    frames_for(1000);
    int no_repeat = screen == SCR_INSTALLED && popup == POP_NONE && lv.selected == sel_was && lv.cursor == at_was &&
                    (sim_keys & KEY_DDOWN);
    sim_keys &= ~KEY_DDOWN;
    frames_for(300);
    key(KEY_DUP); frames_for(300);
    int acts = lv.selected != sel_was || lv.cursor != at_was;
    EXPECT(asked && noted && no_repeat && acts, "down during \"Design löschen\", held through its note and the list: "
           "no repeat there until let go; then the D-pad moves (asked %d, noted %d, still %d, acts %d, row %ld)",
           asked, noted, no_repeat, acts, (long)lv.cursor);

    /* G1-R7: the questions take no key while they open (main.c popup_input waits for the touch's guard time) */
    printf("-- G1-R7: no key while a question opens\n");
    keys_wait_for_question("the design's removal", POP_ASK_THEME, to_theme_ask, theme_removed);
    keys_wait_for_question("the file delete", POP_ASK_FILE, to_file_ask, file_removed);

    /* G2-L2b (C53: "man könnte den loesch screen unten anzeigen nachdem es fertig geladen hat"): the install's end
     *     moves on to "Installiert" by itself as before, its question below, but the top screen does not change: it
     *     goes on with the installing screen's picture, full and glowing, without a transition; only the bottom
     *     screen's content comes in. "Installiert" reached any other way shows its Done card as before */
    printf("-- G2-L2b: \"Installiert\" after an install goes on with the installing screen's top\n");
    struct stat est;
    watch_end_begin(SCR_DONE);
    int e_shown = install_held(argv[1], argv[2], 0, 0, 0, 0);
    int e_keep = 0, e_delete = 0;
    for (int k = 0; k < stub_marks; k++) {
        e_keep |= stub_mark_kind[k] == 'e' && !memcmp(&stub_mark_at[k], &KEEP_TILE, sizeof(rect));
        e_delete |= stub_mark_kind[k] == 'e' && !memcmp(&stub_mark_at[k], &DELETE_TILE, sizeof(rect));
    }
    EXPECT(e_shown && done_end && top_has('p') && !top_has('D') && stub_pour == &install_pour &&
           stub_pour_given.title == text(lang, T_INSTALLING) && stub_pour_given.done_title == text(lang, T_INSTALLED) &&
           stub_pour_given.icon == done_icon && done_name[0] && !strcmp(stub_pour_given.name, done_name) &&
           pour_full(&install_pour) && e_keep && e_delete && stat("sdmc:/Themes/a.zip", &est) == 0,
           "the install's end moves on to Installiert by itself: on top the installing screen's picture, full, with "
           "the theme's name \"%s\" and icon, no Done card (picture %d, card %d); below the question's Keep and "
           "Delete (%d, %d)", stub_pour_given.name, top_has('p'), top_has('D'), e_keep, e_delete);
    EXPECT(end_frames == 1 && top_same(&end_last, &end_first) && end_last.model == &install_pour &&
           end_last.entered == 1.0f && end_first.clock > end_last.clock &&
           end_first.clock - end_last.clock <= END_STEP_MS && fabsf(end_first.glow - end_last.glow) <= 0.03f,
           "the last installing frame's top and the first Installiert frame's: the same pieces at the same places, "
           "the same picture, name and texts, no transition (%.2f, %.2f); only the picture's clock went on, by %llu "
           "ms (the glow from %.3f to %.3f)", end_last.entered, end_first.entered,
           (unsigned long long)(end_first.clock - end_last.clock), end_last.glow, end_first.glow);
    frames_for(TRANSITION_MS + 100);
    EXPECT(fabsf(end_first.cover - TRANSITION_FADE_PCT / 100.0f) < 0.001f && !end_cover_rose &&
           end_now_shot.cover == 0 && end_entered_low == 1.0f && end_pours == end_frames && !end_cards,
           "only the bottom screen comes in: its content out of the background, %.0f %% covered in the first frame, "
           "never more after it, uncovered %d ms later; on top the picture and the transition's value 1 in all %d "
           "frames (lowest %.2f)", 100 * end_first.cover, TRANSITION_MS + 100, end_frames, end_entered_low);
    EXPECT(end_glow_low >= 0.69f && end_glow_step <= 0.03f, "the glow goes on from where the installing screen left "
           "it: its breath to the end, never under %.2f, at most %.3f from one frame to the next", end_glow_low,
           end_glow_step);
    frames_for(1000);
    end_glow_low = 1;
    end_glow_step = 0;
    int e_frames = end_frames;
    u64 e_clock = install_pour.now, e_from = sim_now;
    frames_for(3000);
    EXPECT(screen == SCR_DONE && may_clean && end_glow_low >= 0.999f && end_glow_step <= 0.0001f &&
           end_pours == end_frames && !end_cards && end_entered_low == 1.0f &&
           install_pour.now - e_clock == sim_now - e_from, "3 s of Installiert (%d frames): the glow steady at %.3f "
           "(step %.4f), the picture on top in every frame, its clock with the frames (%llu ms)",
           end_frames - e_frames, end_glow_low, end_glow_step, (unsigned long long)(install_pour.now - e_clock));
    e_clock = install_pour.now;
    sim_now += 3600000;         /* an hour without a frame: HOME, the lid */
    frame();
    EXPECT(install_pour.now - e_clock == END_STEP_MS && top_has('p') && end_now_shot.glow >= 0.999f,
           "an hour without a frame (HOME, the lid): the picture's clock goes on by %llu ms (END_STEP_MS %d), the "
           "model does not catch the hour up", (unsigned long long)(install_pour.now - e_clock), END_STEP_MS);
    may_clean = 0;              /* an install whose file may not be deleted: the lone OK below, the same top */
    frames_for(100);
    EXPECT(screen == SCR_DONE && lone_ok() && stub_bar_right && top_has('p') && !top_has('D') &&
           end_now_shot.entered == 1.0f, "with the lone OK instead of the question (no cleanup): OK on the right "
           "tab, the same top (picture %d, card %d)", top_has('p'), top_has('D'));
    may_clean = 1;
    frames_for(100);
    stub_frame_done = NULL;
    key(KEY_A); frames_for(200); key(KEY_A); frames_for(500);      /* Keep */
    int e_left = screen != SCR_DONE && !done_end && stat("sdmc:/Themes/a.zip", &est) == 0;
    watch_end_begin(SCR_DONE);
    done_with_cleanup("other.zip");
    stub_frame_done = NULL;
    EXPECT(e_left && screen == SCR_DONE && top_has('D') && !top_has('p') && end_cards == end_frames && !end_pours &&
           end_first.entered == 0 && end_first.cover == 0, "Keep leaves it (%d); Installiert reached another way "
           "shows its Done card and comes in by the screen's transition as before (first value %.2f, %d frames)",
           e_left, end_first.entered, end_frames);
    key(KEY_A); frames_for(200); key(KEY_A); frames_for(500);
    unlink(victim);
    /* an install that fails goes to its problem as before: nothing of the end stays */
    fake_imp_calls = 0;
    fake_imp_fail = 1;
    watch_end_begin(SCR_DONE);
    int e_failed = !install_held(argv[1], argv[2], 0, 0, 0, 0);
    stub_frame_done = NULL;
    fake_imp_fail = 0;
    EXPECT(e_failed && screen == SCR_PROBLEM && !done_end && !end_frames && !top_has('p') && !top_has('D'),
           "an install that fails: its problem screen as before, no Installiert, no picture on top (screen %d)",
           screen);
    key(KEY_A); frames_for(500);

    /* G4-A: under the loader's line the notice not to switch off while a job writes the theme pack or HOME's badge
     * files (a design's delete, a set's removal in each of its steps, writing the backup back included), in both
     * languages; none while a job reads, makes a folder or deletes a file on the card */
    {
        static const struct { int job, remove, step, notice; const char *name; } JOBS[] = {
            {JOB_DELETE_THEME, 0, 0, 1, "a design's delete"},
            {JOB_BADGE_WRITE, 1, BSTEP_READ, 1, "a set's removal, reading"},
            {JOB_BADGE_WRITE, 1, BSTEP_WRITE, 1, "a set's removal, writing"},
            {JOB_BADGE_WRITE, 1, BSTEP_RESTORE, 1, "a set's removal, writing the backup back"},
            {JOB_FOLDER, 0, 0, 0, "a folder read"},
            {JOB_DESIGNS, 0, 0, 0, "the pack read"},
            {JOB_CHECK, 0, 0, 0, "a theme's check"},
            {JOB_NO_MUSIC, 0, 0, 0, "the music left out"},
            {JOB_DELETE_FILE, 0, 0, 0, "a file's delete"},
            {JOB_THEMES, 0, 0, 0, "the themes folder made or read"},
            {JOB_BADGES, 0, 0, 0, "the badges read"},
        };
        int was_screen = screen, was_lang = lang;
        screen = SCR_DESIGNS;
        for (size_t i = 0; i < sizeof JOBS / sizeof JOBS[0]; i++)
            for (int l = 0; l < 2; l++) {
                lang = l;
                job = JOBS[i].job;
                bremove = bwriting = JOBS[i].remove;
                bstep = JOBS[i].step;
                job_over = 0;
                job_over_at = 0;
                job_since = osGetTime() - LOADER_WAIT_MS - LOADER_FADE_MS - 100;
                draw();
                const char *want = JOBS[i].notice ? (l ? "Nicht ausschalten" : "Do not switch off") : "";
                EXPECT(stub_loader && stub_loader_shown >= 1 && stub_loader_line[0] &&
                       !strcmp(stub_loader_notice, want), "G4-A %s, %s: the loader with its line \"%s\" and the "
                       "notice \"%s\" (wanted \"%s\")", JOBS[i].name, l ? "German" : "English", stub_loader_line,
                       stub_loader_notice, want);
            }
        job = JOB_NONE;
        bremove = bwriting = 0;
        screen = was_screen;
        lang = was_lang;
        frames_for(500);
    }

    /* DN: "Installiert" without the question shows the way to the design in the HOME Menu on a pill above the bar,
     *     in both languages; with the question it shows none; a tap on the pill does nothing */
    printf("-- DN: the Done screen's pill\n");
    {
        static const char *const HOME[2] = {"HOME Menu", "HOME-Menü"}, *const CHANGE[2] = {"Change Theme",
                                                                                             "Design ändern"};
        int was_screen = screen, was_lang = lang;
        for (int l = 0; l < 2; l++) {
            lang = l;
            reset_app();
            may_clean = 0;
            install_from = SCR_DESIGNS;
            screen = SCR_DONE;
            frames_for(500);
            EXPECT(stub_pill_bottom && !strcmp(stub_pill_home, HOME[l]) && !strcmp(stub_pill_change, CHANGE[l]),
                   "DN %s, Installiert without the question: the pill on the bottom screen, \"%s\" to \"%s\"",
                   l ? "German" : "English", stub_pill_home, stub_pill_change);
            forget_sounds();
            tap(BOTTOM_W / 2, PILL_Y + PILL_H / 2, 48);
            frames_for(400);
            int sounds = 0;
            for (int id = 0; id < SFX_COUNT; id++)
                sounds += played(id);
            EXPECT(screen == SCR_DONE && !may_clean && !sounds && !stub_bar_down && pressed == HIT_NONE &&
                   stub_pill_home[0], "DN %s: a tap on the pill does nothing (screen %d, %d sounds, OK held %d)",
                   l ? "German" : "English", screen, sounds, stub_bar_down);
            done_with_cleanup("dn.zip");
            EXPECT(screen == SCR_DONE && may_clean && !stub_pill_home[0], "DN %s, Installiert with the question: no "
                   "pill (\"%s\")", l ? "German" : "English", stub_pill_home);
            unlink(victim);
        }
        may_clean = 0;
        screen = was_screen;
        lang = was_lang;
        frames_for(500);
    }

    /* ST-B: the start as main() runs it after the settings (start_up), the pack's read taking frames' time as on the
     *     console: no frame before the pack is taken; the first frame is the start screen's first one coming in,
     *     under the black at full strength; the second, after a slow first frame, still nearly all black (its clock
     *     runs from there); the menu music is asked for on the second frame, once the first is shown; the black is
     *     gone after START_BLACK_MS and never comes back (a screen, a job under its loader, a popup, a return from
     *     the HOME Menu and from sleep); one "start:" line */
    printf("-- ST-B: the start out of black\n");
    {
        reset_app();
        frames_for(START_BLACK_MS + 100);
        int st_sets0 = stub_menu_sets, st_lines_before = st_lines;
        shown_key = -1;                 /* a new run: nothing drawn yet */
        st_frames = st_untaken = 0;
        st_slow_ms = 400;
        st_max = 0;
        stub_frame_done = st_frame;
        stub_log_hook = st_log;
        int st_set0 = record_set;
        st_reads = st_reads_on_thread = thread_runs = threads_started = 0;
        last_thread_stack = 0;
        join_record_set = -1;
        worker_frames = 3;              /* the thread's work waits for its join */
        start_up();
        worker_frames = 0;
        int st_before_frame = st_frames, st_sets_before_frame = stub_menu_sets;
        EXPECT(st_before_frame == 0 && !st_untaken && screen == SCR_MAIN && st_sets_before_frame == st_sets0,
               "ST-B no frame before the pack is taken (%d frames in the start's steps, %d with the pack read and not "
               "taken), the menu music not yet asked for (%d)", st_before_frame, st_untaken, st_sets_before_frame - st_sets0);
        EXPECT(threads_started == 1 && last_thread_stack == WORKER_STACK && thread_runs == 1 && st_reads == 1 &&
               st_reads_on_thread == 1 && !held_job && job == JOB_NONE,
               "ST-B2 the start's read runs on a thread of its own with the worker's stack (%d threads, stack 0x%zx, "
               "%d reads, %d of them on the thread), no job (%d)", threads_started, last_thread_stack, st_reads,
               st_reads_on_thread, job);
        EXPECT(join_record_set == st_set0 && record_set == (st_set0 ^ 1),
               "ST-B2 the thread is joined before the pack is taken (record set %d at the join, %d before, %d after)",
               join_record_set, st_set0, record_set);
        frame();
        frame();
        EXPECT(st_frames == 2 && st_screen[0] == SCR_MAIN && st_entered[0] == 0 && st_black[0] == 1 &&
               st_sets[0] == st_sets0, "ST-B the first frame shown: the start screen's first one coming in (entered %g) "
               "under the black at full strength (%g), the music not asked for in it", st_entered[0], st_black[0]);
        EXPECT(st_black[1] > 0.85f && st_black[1] < 1 && st_entered[1] > 0 && st_entered[1] < 0.2f &&
               st_sets[1] == st_sets0 + 1 && stub_menu_track == (music_pick < TRACK_COUNT ? music_pick : -1),
               "ST-B the second frame, after a first one %d ms slow: the black %g, the coming in %g, the menu music "
               "asked for now (%d)", st_slow_ms, st_black[1], st_entered[1], st_sets[1] - st_sets0);
        frames_for(START_BLACK_MS - 2 * FRAME_MS);
        float st_late = stub_black;
        frames_for(2 * FRAME_MS);
        EXPECT(st_late > 0 && stub_black == 0 && start_frames < 0, "ST-B the black clears over its time (%g one frame "
               "before its end) and is gone after START_BLACK_MS (%g)", st_late, stub_black);
        st_max = 0;
        int st_loader_seen = 0;
        go_designs(0);
        frames_for(300);
        key(KEY_A); frames_for(200);
        worker_frames = 30;             /* the folder's read under its loader */
        key(KEY_A);
        for (int k = 0; k < 40; k++) {
            frame();
            st_loader_seen |= stub_loader && stub_loader_shown > 0;
        }
        worker_frames = 0;
        frames_for(500);
        int st_listed = screen == SCR_LIST;
        show_note(ART_INFO_SD, text(lang, T_THEMES_NOT_CREATED), NULL);
        frames_for(400);
        int st_noted = popup != POP_NONE;
        key(KEY_A); frames_for(400);
        on_apt(APTHOOK_ONSUSPEND, NULL); sim_now += 5000; on_apt(APTHOOK_ONRESTORE, NULL);
        frames_for(300);
        on_apt(APTHOOK_ONSLEEP, NULL); sim_now += 5000; on_apt(APTHOOK_ONWAKEUP, NULL);
        frames_for(300);
        go_main(0);
        frames_for(300);
        EXPECT(st_loader_seen && st_listed && st_noted && st_max == 0, "ST-B the black never comes back: a screen change, a "
               "folder read under its loader (%d), a popup (%d), a return from HOME and from sleep, the start screen "
               "again (most black %g)", st_loader_seen, st_noted, st_max);
        EXPECT(st_lines == st_lines_before + 1 && stub_menu_sets == st_sets0 + 1, "ST-B one \"start:\" line (%d) and the "
               "menu music asked for once (%d)", st_lines - st_lines_before, stub_menu_sets - st_sets0);
        {
            /* no thread can be made: the read runs here, once */
            int st_set1 = record_set;
            st_reads = st_reads_on_thread = thread_runs = threads_started = 0;
            threads_fail = 1;
            load_pack();
            threads_fail = 0;
            EXPECT(threads_started == 1 && thread_runs == 0 && st_reads == 1 && st_reads_on_thread == 0 &&
                   record_set == (st_set1 ^ 1) && job == JOB_NONE,
                   "ST-B2 with no thread to be had the start's read runs here, once (%d reads, %d on a thread), and the "
                   "pack is taken", st_reads, st_reads_on_thread);
        }
        stub_frame_done = NULL;
        stub_log_hook = NULL;
        st_slow_ms = 0;

        /* ST-B3: the first loop frame's gap (the motion log's "first frame after") counts from the start's end, not
         * from 0; START and A held from before the start count only once let go and pressed again (keys_sync in
         * load_pack): START does not leave, A does not show the ring or act */
        reset_app();
        frames_for(START_BLACK_MS + 100);
        last_frame = 0;                 /* a new run: no frame drawn yet */
        sim_keys = KEY_START | KEY_A;
        start_up();
        u64 st_end = sim_now;
        EXPECT(last_frame == st_end && st_end != 0, "ST-B3 the first frame's gap counts from the start's end (%llu, the "
               "start ended at %llu)", (unsigned long long)last_frame, (unsigned long long)st_end);
        int st_go = 1;
        for (int k = 0; k < 40; k++)    /* held on, past the keys' repeat delay */
            st_go &= frame();
        EXPECT(st_go && screen == SCR_MAIN && popup == POP_NONE && !ring && pending == HIT_NONE && pressed == HIT_NONE,
               "ST-B3 START and A held from before the start count for nothing (go on %d, screen %d, popup %d, ring %d, "
               "pending %d)", st_go, screen, popup, ring, pending);
        sim_keys = 0; frame();
        key(KEY_A);
        int st_ring = ring;
        sim_keys = KEY_START;
        int st_left = !frame();
        sim_keys = 0; frame();
        EXPECT(st_ring && st_left, "ST-B3 let go and pressed again they count: A shows the ring (%d), START leaves (%d)",
               st_ring, st_left);
    }

    fb_e_states();

    char cmd[1100];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", dir);
    if (chdir("/") || system(cmd))
        fprintf(stderr, "could not remove %s\n", dir);
    EXPECT(pill_frames > 0 && !pill_elsewhere, "DN the pill on %d frames, all of them on \"Installiert\" without the "
           "question (%d elsewhere)", pill_frames, pill_elsewhere);
    printf("test_main: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
