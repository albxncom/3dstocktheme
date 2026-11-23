"""gen_driver.py TREE OUT_DIR EPILOGUE: driver.c and stubs.c for the flow renderer. TREE is a tree of this
repository; driver.c is the application's own main.c on the Mac (built with the kit's uikit/tools/flows/fake/3ds.h and
fake/citro2d.h, drawing into its rec.c's recorder), then the pack fixture of TREE's host/test_main.c, then EPILOGUE (designs.c or badges.c: the walk
with its main()). stubs.c is TREE's host/main_stubs.c from the camera on, with the camera and the wireless switchable
by the walk (fl_cam_text, fl_cam_fail, fl_wifi). Harness, not application: the frame loop below also polls the camera
on the scan screen (as main()'s loop does after handle_input) and draws the application's frames while the
demonstration build's holds sleep (draw() for a delete and a set's removal, badges_work_frame for a set's install,
install_frame for a design's install)."""
import pathlib
import sys

tree, out, epi = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2]), pathlib.Path(sys.argv[3])
test = (tree / "host" / "test_main.c").read_text(encoding="utf-8")
a = test.index("/* ---- a pack as the console would report it")
b = test.index("/* the folder view of sdmc:/Themes, opened from the Designs screen with keys")
fixture = test[a:b]


def swap(text, old, new):
    if text.count(old) != 1:
        raise SystemExit("gen_driver: no single match for %r" % old[:60])
    return text.replace(old, new)


prologue = r'''#define _DARWIN_C_SOURCE
#define main app_main
#include "main.c"
#undef main
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>
#include "turn.h"

#define FAKE_ICONS 8
extern am_view fake_am;
extern const uint8_t *fake_archive;
extern size_t fake_archive_size;
extern uint8_t fake_icons[FAKE_ICONS][TP_ICON_SIZE];
extern home_state fake_home;
extern void (*fake_installed)(const uint8_t *tmd, size_t size);
extern int fake_music[PACK_CONTENTS];

static u64 sim_now = 1000000;
static u32 sim_keys, cur, old, k_down, k_up;
static int sim_x, sim_y;
void hidScanInput(void) { old = cur; cur = sim_keys; k_down = cur & ~old; k_up = old & ~cur; }
u32 hidKeysDown(void) { return k_down; }
u32 hidKeysHeld(void) { return cur; }
u32 hidKeysUp(void) { return k_up; }
u32 hidKeysDownRepeat(void) { return k_down; }
void hidTouchRead(touchPosition *t) { t->px = (u16)sim_x; t->py = (u16)sim_y; }
void hidSetRepeatParameters(u32 a, u32 b) { (void)a; (void)b; }
u64 osGetTime(void) { return sim_now; }
static u64 sim_ticks;
u64 svcGetSystemTick(void) { return sim_ticks += 268112; }
bool aptMainLoop(void) { return true; }
static int in_install;
void aptSetHomeAllowed(bool a)
{
    if (a && in_install) {      /* G2-M0b: the install's end is over, finish comes next */
        const char *e = getenv("REC_CLOCK_AFTER");
        fprintf(stderr, "clock after the install's end: %llu, ticks %llu\n", (unsigned long long)sim_now,
                (unsigned long long)sim_ticks);
        if (e && *e)
            sim_now = strtoull(e, 0, 10);
    }
}
void aptSetSleepAllowed(bool a) { (void)a; }
void aptHook(aptHookCookie *c, aptHookFn fn, void *p) { (void)c; (void)fn; (void)p; }
void aptUnhook(aptHookCookie *c) { (void)c; }
Thread threadCreate(ThreadFunc fn, void *arg, size_t stack, int prio, int core, bool detached)
{
    (void)stack; (void)prio; (void)core; (void)detached;
    fn(arg);
    return (Thread)1;
}
Result threadJoin(Thread t, u64 ns) { (void)t; (void)ns; return 0; }
void threadFree(Thread t) { (void)t; }
Result svcGetThreadPriority(s32 *prio, Handle h) { (void)h; *prio = 0x30; return 0; }
/* the worker's work runs where it is started; whenever the pretended install sleeps the clock goes on and the main
 * thread's installing frames are drawn, 16 ms apart */
static u64 next_install_frame;
void svcSleepThread(int64_t ns)
{
    for (int64_t ms = ns / 1000000; ms > 0; ms--) {
        sim_now++;
        if (job == JOB_INSTALL && sim_now >= next_install_frame) {
            next_install_frame = sim_now + 16;
            install_frame();
        }
        if (job == JOB_BADGE_WRITE && sim_now >= next_install_frame) {
            next_install_frame = sim_now + 16;
            if (bremove)
                draw();
            else
                badges_work_frame();
        }
        if ((job == JOB_DELETE_THEME || job == JOB_DELETE_FILE || job == JOB_CHECK) && sim_now >= next_install_frame) {
            next_install_frame = sim_now + 16;
            draw();
        }
    }
}

/* one pass of the application's main loop (main.c, main), 16 ms a frame */
static u64 frame_at;
static int frame(void)
{
    int go = 1;
    frame_at = sim_now;
    if (job) {
        if (job_left())
            job_finish();
        else
            job_input();
    } else {
        go = handle_input();
    }
    if (!job && screen == SCR_SCAN)
        poll_camera();
    if (!job && rows_screen())
        list_motion();
    loop_frame();
    sim_now += 16;
    return go;
}
static void frames_for(u64 ms) { u64 end = sim_now + ms; while (sim_now < end) frame(); }
static void stylus(int x, int y) { sim_x = x; sim_y = y; sim_keys |= KEY_TOUCH; }
static void lift(void) { sim_keys &= ~KEY_TOUCH; }
static void tap(int x, int y, u64 hold) { stylus(x, y); frames_for(hold); lift(); frame(); }
static void key(u32 k) { sim_keys |= k; frame(); sim_keys &= ~k; frame(); }

static char dir[512];
static const char *out_dir, *tag;
static void capture(const char *name)
{
    char prefix[1024];
    snprintf(prefix, sizeof prefix, "%s/%s-%s", out_dir, tag, name);
    rec_dump(prefix);
    fprintf(stderr, "captured %s-%s: screen %d, popup %d, focus %d, ring %d, scheme %d, glass %d, pattern %d\n", tag, name,
            screen, popup, focus, ring, settings_scheme(), settings_logo(), settings_pattern());
}
/* the cursor's corners at rest (in): the frame drawn at a whole SELECT_MS */
static void still(void)
{
    while (sim_now % SELECT_MS >= 16)
        frame();
    frame();
}
/* the start screen's house face-on: a frame drawn a whole half turn after t0, when its clock started (turn.h) */
static void face_on(u64 t0)
{
    const u64 half = (u64)LOGO_FRAMES * LOGO_TICKS * 1000 / 60;
    while ((sim_now - t0) % half >= 16)
        frame();
    frame();
}
/* the installing screen once two steps are done and the third has been under way for a while */
static int install_shot;
static int in_install;
/* N-C2: the installing screen 700 ms after each count of steps done (0 to 4) is reached, and every frame of the
 * 400 ms after the second step's end (its mark becoming done, the line passing under it) */
static u64 count_at[5];
static int count_shot[5], strip_n;
static int extra_now;
static void extra_frame(const char *nm, const pour *p, int badges, const char *name)
{
    static const char *const four[4] = {"Prüfen", "Katalog", "Design", "Abschluss"};
    static const char *const six[6] = {"Badges lesen", "Sicherung schreiben", "Sicherung prüfen", "Vorbereiten",
                                       "Schreiben", "Nachlesen"};
    static const char *const bs[4] = {"Badges lesen", "Sicherung", "Schreiben", "Prüfen"};   /* DN: BS */
    extra_now = 1;
    ui_begin(1.0f);
    do {
        ui_top();
        ui_pour(p, badges ? "Badges installieren" : "Wird installiert", "Installiert", name, badges ? NULL : theme.icon,
                badges ? in_glass(ART_BADGES) : -1, badges ? bs : four, "Erfolgreich installiert", NULL);
        ui_bottom();
    } while (ui_next_eye());
    ui_end();
    extra_now = 0;
    capture(nm);
}
static void extras(int k)
{
    static const char *const B[] = {"b3-1-start", "b3-2-mid", "b3-3-change", "b3-4-third", "b3-5-last", "b3-6-glow",
                                    "b3-7-end"};
    static const unsigned UNTIL[] = {450, 2000, 3600, 6000, 8700, 10500, 14000};
    static pour bp;
    pour_start(&bp, &POUR_BADGES, 1000);
    for (unsigned ms = 16; ms <= UNTIL[k]; ms += 16)
        pour_work(&bp, 1000 + ms, ms < 9000 ? ms / 9000.0f : 1, ms < 9500 ? POUR_WORKING : POUR_SUCCEEDED);
    extra_frame(B[k], &bp, 1, "Kiesel");
    if (k == 3)
        extra_frame("n3-4-third", &install_pour, 0, "Waldweg im ersten Morgenlicht nach dem langen Regen");
    if (k == 6)
        extra_frame("n3-7-end", &install_pour, 0, "Waldweg im ersten Morgenlicht nach dem langen Regen");
}
static void each_frame(void)
{
    if (extra_now)
        return;
    if (in_install && job != JOB_INSTALL)       /* install_shown_done's loop waits for the clock */
        sim_now += 16;
    if (getenv("REC_DONE") && in_install && job != JOB_INSTALL) {   /* G2-P: the end hold's last frame */
        char pre[1024];
        snprintf(pre, sizeof pre, "%s/%s-5h-last", out_dir, tag);
        rec_dump(pre);
    }
#if UI_DESIGN == DESIGN_STOCK
    /* R9 D3: the moments of the picture (r9-work) */
    static const char *const NAMES[] = {"d3-1-start", "d3-2-mid", "d3-3-change", "d3-4-third", "d3-5-last",
                                        "d3-6-glow", "d3-7-end"};
    static int at;
    const pour *p = &install_pour;
    float l = pour_level(p);
    int now = at == 0 ? sim_now - install_since >= 450 : at == 1 ? l >= 0.12f : at == 2 ? pour_mark(p, 0) >= 1.99f
            : at == 3 ? l >= 0.62f : at == 4 ? p->real >= 1 && l >= 0.80f && p->gone == p->total
            : at == 5 ? pour_glow(p) >= 0.98f : at == 6 ? pour_full(p) && p->now >= p->full_at + 1100 : 0;
    if (at < 7 && now) {
        install_shot = 1;
        capture(NAMES[at++]);
        if (getenv("REC_EXTRA"))
            extras(at - 1);
    }
#else
    if (job != JOB_INSTALL || progress_shown < 0)
        return;
    int d = progress_steps_done(progress_shown);
    if (!count_at[d])
        count_at[d] = sim_now;
    char name[64];
    if (d == 2 && sim_now - count_at[2] <= 400 && strip_n < 40) {
        snprintf(name, sizeof name, "4s-%02d", strip_n++);
        capture(name);
    }
    if (!count_shot[d] && sim_now - count_at[d] >= (d == 4 ? 250 : 700)) {
        count_shot[d] = 1;
        install_shot = 1;
        snprintf(name, sizeof name, "4-install-%d", d);
        capture(name);
    }
#endif
}
/* G2-P: the Done screen's first frames after the install */
static void done_frames(void)
{
    static int k;
    char name[64];
    if (k == 0 || k == 1 || k == 2 || k == 4 || k == 8 || k == 12 || k == 19 || k == 30) {
        snprintf(name, sizeof name, "5i-%02d", k);
        capture(name);
    }
    k++;
}
/* frames drawn by the main loop's draw alone (no input, no job's end), 16 ms apart, one captured every `every` ms */
static void drawn_for(u64 ms, u64 every, const char *stem)
{
    u64 end = sim_now + ms, from = sim_now;
    int n = 0;
    char name[64];
    while (sim_now < end) {
        loop_frame();
        if (every && (sim_now - from) % every < 16) {
            snprintf(name, sizeof name, "%s-%02d", stem, n++);
            capture(name);
        }
        sim_now += 16;
    }
}

'''
(out / "driver.c").write_text(prologue + fixture + epi.read_text(encoding="utf-8"), encoding="utf-8")

stubs = (tree / "host" / "main_stubs.c").read_text(encoding="utf-8")
part = stubs[stubs.index("int camera_start(void)"):stubs.index("/* The wallpapers: what was handed over per screen")]
part = swap(part, "int camera_start(void) { return 0; }",
            "int fl_cam_fail, fl_wifi = 1;\nconst char *fl_cam_text;\nstatic uint16_t fl_cam_px[400 * 240];\n"
            "int camera_start(void) { return fl_cam_fail ? -1 : 0; }")
part = swap(part, "int camera_frame(const uint16_t **frame) { (void)frame; return 0; }",
            "int camera_frame(const uint16_t **frame) { if (!fl_cam_text) return 0; *frame = fl_cam_px; return 1; }")
part = swap(part, "int camera_qr(const uint16_t *frame, char *text, size_t size) { (void)frame; (void)text; (void)size; return 0; }",
            "int camera_qr(const uint16_t *frame, char *text, size_t size) { (void)frame; if (!fl_cam_text) return 0; "
            "snprintf(text, size, \"%s\", fl_cam_text); return 1; }")
part = swap(part, "int console_wifi(void) { return 1; }", "int console_wifi(void) { return fl_wifi; }")
(out / "stubs.c").write_text(
    "#include <stdarg.h>\n#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n#include <zlib.h>\n"
    "#include <3ds.h>\n#include \"ui.h\"\n#include \"console.h\"\n#include \"camera.h\"\n#include \"audio.h\"\n"
    "#include \"content.h\"\n#define FAKE_ICONS 8\nint stub_verbose;\n" + part, encoding="utf-8")
