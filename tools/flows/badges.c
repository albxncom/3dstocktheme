
/* FL: the Badges part's install and removal screens, one capture per state, along the application's own paths
 * (keys pressed as a user presses them; forced only where the host has no console: the download, the set's check
 * loader, a placed or foreign set, a failed write). driver OUT_DIR FIXTURE_DIR SCHEME GLASS TAG */
extern int fl_cam_fail, fl_wifi;
extern const char *fl_cam_text;

static const char *arm_name;
static int arm_job, arm_hold;
static u64 arm_ms;
static void arm_hook(void)
{
    if (arm_hold && job == JOB_NONE)
        sim_now += 16;
    if (job_over && (job == JOB_DELETE_THEME || job == JOB_DELETE_FILE || job == JOB_BADGE_WRITE))
        sim_now += 16;
    if (arm_name && job == arm_job && sim_now - job_since >= arm_ms) {
        const char *n = arm_name;
        arm_name = NULL;
        capture(n);
    }
}
static void arm(const char *name, int kind, u64 ms)
{
    arm_name = name;
    arm_job = kind;
    arm_ms = ms;
}
static void frames_n(u64 ms)
{
    for (u64 n = ms / 16; n > 0; n--)
        frame();
}
static void shot(const char *name)
{
    frames_n(900);
    still();
    capture(name);
}
static void press(u32 k, u64 ms)
{
    key(k);
    frames_n(ms);
}
static void loader_shot(int kind, const char *name)
{
    job = kind;
    job_over = 0;
    job_over_at = 0;
    back_since = 0;
    job_since = sim_now - LOADER_WAIT_MS - LOADER_FADE_MS - 100;
    draw();
    capture(name);
    job = JOB_NONE;
    frames_n(300);
}
static void select_row(size_t i)
{
    lv.selected = 1;
    lv.cursor = i;
    lv.scroll = scroll_for_cursor();
    frames_n(200);
}
static size_t entry_named(const char *name)
{
    for (size_t i = 0; i < bents.count; i++)
        if (!strcmp(bents.items[i].file, name))
            return i;
    fprintf(stderr, "no entry %s\n", name);
    exit(1);
}
static void expect(int scr, const char *what)
{
    if (screen != scr) {
        fprintf(stderr, "%s: screen %d, popup %d, job %d\n", what, screen, popup, job);
        exit(1);
    }
    fprintf(stderr, "ok %s: screen %d, popup %d\n", what, screen, popup);
}
static void menu(int which)
{
    badges_open(which);
    frames_n(900);
    expect(SCR_BADGES, "Badges");
}
static void to_link(void)
{
    menu(1);
    press(KEY_A, 300);
    press(KEY_A, 900);
    expect(SCR_SCAN, "scan");
    fl_cam_text = "https://beispiel.example/badges/Blumen.zip";
    frame();
    fl_cam_text = NULL;
    frames_n(900);
    expect(SCR_URL, "link");
}
static void download_shown(double done, double total)
{
    progress_start(&bar);
    job = JOB_DOWNLOAD;
    job_over = 0;
    job_over_at = 0;
    job_since = sim_now;
    job_done = (uint64_t)(done * 1048576), job_total = (uint64_t)(total * 1048576);
    drawn_for(1200, 0, "");
}
static void download_ends(int status, long http)
{
    job = JOB_NONE;
    memset(&dl, 0, sizeof dl);
    dl.status = status;
    dl.http = http;
    dl.kind = BADGE_SRC_ZIP;
    snprintf(dl.file, sizeof dl.file, "%s", "Blumen.zip");
    download_done();
    frames_n(900);
}
static void install_set(const char *mid)
{
    if (mid)
        arm(mid, JOB_BADGE_WRITE, 5000);
    arm_hold = 1;
    press(KEY_A, 1500);
    arm_hold = 0;
    arm_name = NULL;
    expect(SCR_BADGE_DONE, "set written");
}
/* the set screen of row i of the installed sets, its tile pressed: the question or the note */
static void open_set(size_t i)
{
    menu(2);
    press(KEY_A, 300);
    press(KEY_A, 900);
    expect(SCR_BADGE_SETS, "Installierte Badges");
    select_row(i);
    press(KEY_A, 900);
    expect(SCR_BADGE_ONE, "one set");
    press(KEY_A, 300);
}
static void refused(const char *name)
{
    press(KEY_A, 700);
    if (popup != POP_NOTE) {
        fprintf(stderr, "%s: no note (popup %d)\n", name, popup);
        exit(1);
    }
    shot(name);
    press(KEY_B, 600);
}
static void problem(int outcome, int stage, int wrote, const char *name)
{
    badge_flow_result res;
    memset(&res, 0, sizeof res);
    res.outcome = outcome;
    res.stage = stage;
    res.code = -1;
    res.wrote = wrote;
    if (wrote)
        snprintf(res.backup, sizeof res.backup, "sdmc:/theme-pack/badge-backup/0003");
    badge_text_outcome(lang, &res, btitle, sizeof btitle, bbody, sizeof bbody);
    screen = SCR_BADGE_PROBLEM;
    shot(name);
}

int main(int argc, char **argv)
{
    if (argc < 6) {
        fprintf(stderr, "usage: driver OUT_DIR FIXTURE_DIR SCHEME GLASS TAG\n");
        return 3;
    }
    static char out_abs[1024], fix_abs[1024];
    if (!realpath(argv[1], out_abs) || !realpath(argv[2], fix_abs))
        return 3;
    out_dir = out_abs;
    tag = argv[5];
    const char *base = getenv("TMPDIR");
    snprintf(dir, sizeof dir, "%s/fl-badges-XXXXXX", base && *base ? base : "/tmp");
    if (!mkdtemp(dir) || chdir(dir))
        return 3;
    ui_part(&ART_APP_PART);
    if (ui_init())
        return 2;
    version_set(APP_VERSION_PRE);
    lang = getenv("REC_LANG") && getenv("REC_LANG")[0] == 'e' ? LANG_EN : LANG_DE;     /* REC_LANG=en: English */
    mkdir("sdmc:", 0755);
    mkdir("sdmc:/theme-pack", 0755);
    char cmd[2400];
    snprintf(cmd, sizeof cmd, "cp -R '%s/Badges' sdmc:/Badges && cp -R '%s/extdata' badge-home", fix_abs, fix_abs);
    if (system(cmd))
        return 3;
    settings_start();
    prefs.scheme = atoi(argv[3]);
    prefs.logo = atoi(argv[4]);
    if (getenv("REC_LOOK"))
        look_pick = atoi(getenv("REC_LOOK")), prefs.design = LOOKS[look_pick];
    settings_apply(&prefs);
    rec_frame_done = arm_hook;

    /* ---- A: a set from the SD card (badges_activate SCR_BADGES HIT_FIRST, badges_scan, BJ_LOAD, SCR_BADGE_SET,
     * badges_append, badges_write(0), SCR_BADGE_DONE, badges_open) */
    menu(0);
    press(KEY_A, 300);
    shot("bA-01-menu-sd");
    press(KEY_A, 900);
    expect(SCR_BADGE_LIST, "SD-Karte");
    select_row(entry_named("Tiere"));
    shot("bA-02-list");
    bjob = BJ_LOAD;                 /* forced: the set's read shows its loader only when it takes longer */
    loader_shot(JOB_BADGES, "bA-03-checking");
    press(KEY_A, 900);
    expect(SCR_BADGE_SET, "check Tiere");
    press(KEY_A, 300);
    shot("bA-04-check");
    install_set("bA-05-installing");
    shot("bA-06-done");
    press(KEY_B, 900);
    expect(SCR_BADGES, "after the set");
    shot("bA-07-after");

    /* ---- B: by QR code (HIT_SECOND with bqr, start_scan, poll_camera, SCR_URL, badges_download_work,
     * badges_download_done, BJ_LOAD, SCR_BADGE_SET from SCR_BADGES) */
    press(KEY_RIGHT, 300);
    if (focus != 1)
        press(KEY_RIGHT, 300);
    shot("bB-01-menu-qr");
    press(KEY_A, 900);
    expect(SCR_SCAN, "QR-Code scannen");
    shot("bB-02-scan");
    fl_cam_text = "https://beispiel.example/badges/Blumen.zip";
    frame();
    fl_cam_text = NULL;
    expect(SCR_URL, "QR code read");
    press(KEY_A, 300);
    shot("bB-03-found");
    download_shown(1.2, 3.0);
    capture("bB-04-downloading");
    job = JOB_NONE;
    bjob = BJ_LOAD;
    loader_shot(JOB_BADGES, "bB-05-checking");
    download_ends(DL_OK, 200);
    expect(SCR_BADGE_SET, "check of the download");
    press(KEY_A, 300);
    shot("bB-06-check");
    install_set("bB-07-installing");
    shot("bB-08-done");
    press(KEY_B, 900);

    /* ---- C: removing the set added last (HIT_THIRD, BJ_SETS, SCR_BADGE_SETS, SCR_BADGE_ONE, badges_ask,
     * POP_ASK_SET, badges_remove, badges_write(1), SCR_BADGE_DONE with bremove) */
    menu(2);
    press(KEY_DOWN, 300);
    press(KEY_DOWN, 300);
    shot("bC-01-menu-installed");
    press(KEY_A, 900);
    expect(SCR_BADGE_SETS, "Installierte Badges");
    select_row(blist.col.sets - 1);
    shot("bC-02-sets");
    press(KEY_A, 900);
    expect(SCR_BADGE_ONE, "the last set");
    press(KEY_A, 300);
    shot("bC-03-set");
    press(KEY_A, 700);
    if (popup != POP_ASK_SET) {
        fprintf(stderr, "no question (popup %d)\n", popup);
        return 1;
    }
    shot("bC-04-ask");
    press(KEY_RIGHT, 200);
    press(KEY_RIGHT, 200);
    arm("bC-05-removing", JOB_BADGE_WRITE, 2500);
    press(KEY_A, 1500);
    arm_name = NULL;
    fprintf(stderr, "the removal ended on screen %d, popup %d\n", screen, popup);
    shot("bC-06-removed");
    press(KEY_B, 900);

    /* ---- D: a removal refused (badges_activate SCR_BADGE_ONE HIT_GO without badges_can_remove: badges_why_not) */
    open_set(0);
    shot("bD-01-set-not-last");
    refused("bD-02-refused-not-last");
    open_set(blist.col.sets - 1);
    blist.col.set[bone].removable = 0;      /* forced: a badge of the set placed in HOME */
    blist.col.remove = BADGE_E_PLACED;
    refused("bD-03-refused-placed");
    open_set(blist.col.sets - 1);
    blist.col.set[bone].mine = blist.col.set[bone].removable = 0;  /* forced: the last set not added here */
    refused("bD-04-refused-not-ours");
    open_set(blist.col.sets - 1);
    blist.undo = 0;                         /* forced: no backup to undo the set with */
    refused("bD-05-refused-no-undo");
    open_set(blist.col.sets - 1);
    blist.manifest_bad = 1;                 /* FB-E: forced: the set list's manifest is damaged */
    refused("bD-06-refused-manifest");
    mkdir("sdmc:/Badges/Leer", 0755);       /* FB-E: an empty folder in /Badges */
    menu(0);
    badges_scan("sdmc:/Badges/Leer", NULL);
    frames_n(900);
    expect(SCR_BADGE_LIST, "empty folder");
    shot("bX-14-folder-empty");

    /* ---- X: off the straight path */
    menu(0);
    press(KEY_A, 300);
    press(KEY_A, 900);
    expect(SCR_BADGE_LIST, "SD-Karte again");
    select_row(entry_named("Kaputt.png"));
    press(KEY_A, 900);
    expect(SCR_BADGE_SET, "check Kaputt");
    shot("bX-01-check-unreadable");
    press(KEY_B, 900);
    select_row(entry_named("Tiere"));
    press(KEY_A, 900);
    expect(SCR_BADGE_SET, "check Tiere again");
    shot("bX-02-check-present");
    problem(BFLOW_REFUSED, BSTAGE_BACKUP, 0, "bX-03-write-refused");
    problem(BFLOW_RESTORED, BSTAGE_WRITE, 1, "bX-04-write-restored");
    problem(BFLOW_UNKNOWN, BSTAGE_WRITE, 1, "bX-13-write-unknown");     /* FB-E: the third outcome (also a removal's) */
    menu(1);
    fl_cam_fail = 1;
    press(KEY_A, 300);
    press(KEY_A, 900);
    fl_cam_fail = 0;
    expect(SCR_PROBLEM, "camera failed");
    shot("bX-05-camera-failed");
    menu(1);
    press(KEY_A, 300);
    press(KEY_A, 900);
    expect(SCR_SCAN, "scan again");
    fl_cam_text = "Hallo vom Kühlschrank";
    frame();
    fl_cam_text = NULL;
    camera_image = NULL;        /* the camera's picture is not drawn on the host: the screen as before its first frame */
    shot("bX-06-scan-no-link");
    fl_cam_text = "https://beispiel.example/badges/Blumen.zip";
    frame();
    fl_cam_text = NULL;
    expect(SCR_URL, "link again");
    fl_wifi = 0;
    press(KEY_A, 300);
    press(KEY_A, 900);
    fl_wifi = 1;
    expect(SCR_PROBLEM, "no wifi");
    shot("bX-07-no-wifi");
    to_link();
    download_shown(0.8, 3.0);
    download_ends(DL_CANCELLED, 0);
    expect(SCR_PROBLEM, "cancelled");
    shot("bX-08-download-cancelled");
    to_link();
    download_shown(0.0, 0.0);
    download_ends(DL_NOT_THEME, 200);
    expect(SCR_PROBLEM, "not a set");
    shot("bX-09-download-not-a-set");
    menu(2);
    press(KEY_A, 300);
    press(KEY_A, 900);
    expect(SCR_BADGE_SETS, "sets");
    blist.state = BLIST_LOCKED;             /* forced: the collection cannot be read */
    blist.col.sets = 0;
    shot("bX-10-sets-unreadable");
    blist.state = BLIST_OK;                 /* forced: no set in HOME */
    blist.col.badges = 0;
    shot("bX-11-sets-none");
    if (rename("sdmc:/Badges", "sdmc:/Badges-weg"))
        return 3;
    menu(0);
    press(KEY_A, 300);
    press(KEY_A, 900);
    shot("bX-12-sd-missing");
    (void)tap; (void)stylus; (void)lift; (void)each_frame; (void)face_on; (void)extras; (void)copy_to;
    return 0;
}
