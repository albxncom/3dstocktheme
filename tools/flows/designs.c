
/* FL: the Designs part's install and removal screens, one capture per state, along the application's own paths
 * (keys pressed as a user presses them; forced only where the host has no console: the download, the check's and
 * the music's loaders, a full pack, a failed delete). driver OUT_DIR A.zip .. E.zip SCHEME GLASS PATTERN TAG */
extern int fl_cam_fail, fl_wifi;
extern const char *fl_cam_text;

static const char *arm_name;
static int arm_job, arm_hold;
static u64 arm_ms;
/* after every frame drawn: the install's end hold waits for the clock (as each_frame); one capture once the job
 * armed for has run arm_ms */
static void arm_hook(void)
{
    if (arm_hold && job == JOB_NONE)
        sim_now += 16;
    /* job_here's frames after the work: the loader leaves by the clock */
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
/* frames by count: a demonstration hold inside one of them moves the clock seconds on (demo_hold) */
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
/* a job's loader whole over the screen it started from (as the walk's r5 frames) */
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
    frames_for(300);
}
static size_t row_named(const char *name)
{
    for (size_t i = 0; i < list.count; i++)
        if (!strcmp(list.items[i].file, name))
            return i;
    fprintf(stderr, "no row %s in %s (screen %d, popup %d):", name, folder, screen, popup);
    for (size_t i = 0; i < list.count; i++)
        fprintf(stderr, " [%s]", list.items[i].file);
    fprintf(stderr, "\n");
    exit(1);
}
static void select_row(size_t i)
{
    lv.selected = 1;
    lv.cursor = i;
    lv.scroll = scroll_for_cursor();
    frames_for(200);
}
static void expect(int scr, const char *what)
{
    if (screen != scr) {
        fprintf(stderr, "%s: screen %d, popup %d, job %d, pending %d, selected %d, cursor %zu, count %zu, over %d at %llu dots %f %f since %llu\n", what, screen, popup, job, pending, lv.selected, lv.cursor, list.count, job_over, (unsigned long long)job_over_at, job_dots, job_over_dots, (unsigned long long)(sim_now - job_since));
        exit(1);
    }
    fprintf(stderr, "ok %s: screen %d, popup %d\n", what, screen, popup);
}
static void to_link(void)
{
    go_designs(1);
    frames_for(800);
    press(KEY_A, 300);
    press(KEY_A, 900);
    expect(SCR_SCAN, "scan");
    fl_cam_text = "https://beispiel.example/designs/Papierflieger.zip";
    frame();
    fl_cam_text = NULL;
    frames_for(900);
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
    dl.kind = TP_KIND_ZIP;
    snprintf(dl.file, sizeof dl.file, "%s", "Papierflieger.zip");
    download_done();
    frames_n(900);
}

int main(int argc, char **argv)
{
    if (argc < 11) {
        fprintf(stderr, "usage: driver OUT_DIR A.zip B.zip C.zip D.zip E.zip SCHEME GLASS PATTERN TAG\n");
        return 3;
    }
    static char out_abs[1024];
    if (!realpath(argv[1], out_abs))
        return 3;
    out_dir = out_abs;
    tag = argv[10];
    const char *base = getenv("TMPDIR");
    snprintf(dir, sizeof dir, "%s/fl-designs-XXXXXX", base && *base ? base : "/tmp");
    if (!mkdtemp(dir) || chdir(dir))
        return 3;
    ui_part(&ART_APP_PART);
    if (ui_init())
        return 2;
    version_set(APP_VERSION_PRE);
    lang = getenv("REC_LANG") && getenv("REC_LANG")[0] == 'e' ? LANG_EN : LANG_DE;     /* REC_LANG=en: English */
    make_pack(argv[2], argv[3], 0);
    mkdir("sdmc:/Themes", 0755);
    mkdir("sdmc:/Themes/Sammlung", 0755);
    mkdir("sdmc:/Themes/Leer", 0755);
    copy_to(argv[2], "sdmc:/Themes/Morgenlicht.zip");
    copy_to(argv[4], "sdmc:/Themes/Waldweg.zip");
    copy_to(argv[3], "sdmc:/Themes/Sammlung/Kieselgrau.zip");
    copy_to(argv[5], "sdmc:/Themes/Tiefsee.zip");
    copy_to(argv[6], "sdmc:/Themes/Papierflieger.zip");
    copy_to(argv[6], "sdmc:/Themes/Kaputt.zip");     /* a theme zip cut short: listed, its check fails */
    if (truncate("sdmc:/Themes/Kaputt.zip", 300))
        return 3;
    settings_start();
    prefs.scheme = atoi(argv[7]);
    prefs.logo = atoi(argv[8]);
    prefs.pattern = atoi(argv[9]);
    if (getenv("REC_LOOK"))
        look_pick = atoi(getenv("REC_LOOK")), prefs.design = LOOKS[look_pick];
    settings_apply(&prefs);
    rec_frame_done = arm_hook;
    load_pack();
    sound_start();
    go_main(0);
    themes_start();
    frames_for(1500);

    /* ---- A: from the SD card (activate SCR_DESIGNS HIT_FIRST, open_list, open_item, check_done, run_install,
     * finish, SCR_DONE, after_install) */
    go_designs(0);
    frames_for(800);
    press(KEY_A, 300);
    shot("dA-01-menu-sd");
    press(KEY_A, 900);
    expect(SCR_LIST, "SD-Karte");
    select_row(row_named("Waldweg.zip"));
    shot("dA-02-list");
    arm("dA-03-checking", JOB_CHECK, 1500);
    press(KEY_A, 900);
    arm_name = NULL;
    expect(SCR_CONFIRM, "check Waldweg");
    if (!can_install())
        return 1;
    press(KEY_A, 300);
    shot("dA-04-check");
    arm("dA-05-installing", JOB_INSTALL, 4000);
    arm_hold = 1;
    press(KEY_A, 1500);
    arm_hold = 0;
    arm_name = NULL;
    expect(SCR_DONE, "installed Waldweg");
    if (!may_clean)
        return 1;
    shot("dA-06-done-ask");
    press(KEY_X, 700);
    if (popup != POP_HELP)
        return 1;
    shot("dA-07-help");
    press(KEY_B, 600);
    may_clean = 0;              /* the end without the question (install_may_clean 0: record or phase 1 not confirmed) */
    shot("dA-09-done-ok");
    may_clean = 1;
    frames_for(300);
    /* the fake console now lists Waldweg, as a real install leaves it (the demonstration build writes nothing) */
    make_pack(argv[4], argv[3], 0);
    read_pack();
    take_pack();
    press(KEY_A, 300);
    press(KEY_A, 1200);
    expect(SCR_LIST, "Behalten");
    shot("dA-08-after-keep");
    select_row(row_named("Tiefsee.zip"));
    press(KEY_A, 900);
    expect(SCR_CONFIRM, "check Tiefsee");
    if (!music_choice())
        return 1;
    press(KEY_A, 300);
    shot("dA-10-check-music");
    press(KEY_DOWN, 300);
    snprintf(done_name, sizeof done_name, "%s", theme.name);    /* as start_no_music, for its loader */
    memcpy(done_icon, theme.icon, sizeof done_icon);
    loader_shot(JOB_NO_MUSIC, "dA-11-no-music");
    arm_hold = 1;
    press(KEY_A, 1500);
    arm_hold = 0;
    arm_name = NULL;
    expect(SCR_DONE, "installed Tiefsee without music");
    read_pack();                /* the count as the fake console has it (the demo counts its pretended install) */
    take_pack();
    unlink("sdmc:/Themes/Tiefsee.zip");     /* what the delivered build's clean_up does, which the demo leaves out */
    press(KEY_RIGHT, 300);
    press(KEY_RIGHT, 300);
    press(KEY_A, 1200);
    expect(SCR_LIST, "Löschen");
    shot("dA-12-after-delete");
    if (popup)
        press(KEY_B, 600);

    /* ---- B: by QR code (HIT_SECOND, start_scan, poll_camera, SCR_URL, run_download, download_done,
     * open_confirm from SCR_DESIGNS, run_install, after_install to go_designs(1)) */
    go_designs(1);
    frames_for(800);
    press(KEY_A, 300);
    shot("dB-01-menu-qr");
    press(KEY_A, 900);
    expect(SCR_SCAN, "QR-Code scannen");
    shot("dB-02-scan");
    fl_cam_text = "https://beispiel.example/designs/Papierflieger.zip";
    frame();
    fl_cam_text = NULL;
    expect(SCR_URL, "QR code read");
    press(KEY_A, 300);
    shot("dB-03-found");
    download_shown(3.4, 8.1);
    capture("dB-04-downloading");
    job = JOB_NONE;
    arm("dB-05-checking", JOB_CHECK, 1500);
    download_ends(DL_OK, 200);
    arm_name = NULL;
    expect(SCR_CONFIRM, "check of the download");
    press(KEY_A, 300);
    shot("dB-06-check");
    arm("dB-07-installing", JOB_INSTALL, 4000);
    arm_hold = 1;
    press(KEY_A, 1500);
    arm_hold = 0;
    arm_name = NULL;
    expect(SCR_DONE, "installed the download");
    read_pack();                /* the count as the fake console has it (the demo counts its pretended install) */
    take_pack();
    shot("dB-08-done-ask");
    press(KEY_A, 300);
    press(KEY_A, 1200);
    expect(SCR_DESIGNS, "after the QR install");
    shot("dB-09-after");

    /* ---- C: removing a design (HIT_THIRD, open_designs, open_design, ask_delete_theme, POP_ASK_THEME,
     * run_delete_theme, show_note, designs_done) */
    press(KEY_DOWN, 300);
    press(KEY_DOWN, 300);
    shot("dC-01-menu-installed");
    press(KEY_A, 900);
    expect(SCR_INSTALLED, "Installierte Designs");
    press(KEY_DOWN, 300);
    shot("dC-02-installed");
    press(KEY_A, 900);
    expect(SCR_DESIGN, "one design");
    press(KEY_A, 300);
    shot("dC-03-design");
    press(KEY_A, 700);
    if (popup != POP_ASK_THEME) {
        fprintf(stderr, "no delete question (popup %d)\n", popup);
        return 1;
    }
    shot("dC-04-ask");
    press(KEY_RIGHT, 200);
    press(KEY_RIGHT, 200);
    arm("dC-05-deleting", JOB_DELETE_THEME, 1500);
    press(KEY_A, 1200);
    arm_name = NULL;
    expect(SCR_INSTALLED, "deleted");
    shot("dC-06-deleted");
    if (popup)
        press(KEY_B, 600);

    /* ---- D: the file of an installed design deleted from its check screen (installed_already, ask_delete_file,
     * POP_ASK_FILE, run_delete_file) */
    go_designs(0);
    frames_for(800);
    open_list();
    frames_for(900);
    select_row(row_named("Sammlung"));
    press(KEY_A, 900);
    select_row(row_named("Kieselgrau.zip"));
    press(KEY_A, 900);
    expect(SCR_CONFIRM, "check Kieselgrau");
    if (!installed_already())
        return 1;
    press(KEY_A, 300);
    shot("dD-01-check-installed");
    press(KEY_RIGHT, 300);
    press(KEY_A, 700);
    if (popup != POP_ASK_FILE)
        return 1;
    shot("dD-02-ask-file");
    press(KEY_RIGHT, 200);
    press(KEY_RIGHT, 200);
    unlink("sdmc:/Themes/Sammlung/Kieselgrau.zip");     /* the delivered build's remove, left out by the demo */
    arm("dD-03-deleting-file", JOB_DELETE_FILE, 1500);
    press(KEY_A, 1200);
    arm_name = NULL;
    expect(SCR_LIST, "file deleted");
    shot("dD-04-deleted");
    if (popup)
        press(KEY_B, 600);

    /* ---- X: off the straight path */
    folder_up();
    frames_for(900);
    select_row(row_named("Kaputt.zip"));
    press(KEY_A, 900);
    expect(SCR_CONFIRM, "check Kaputt");
    shot("dX-01-check-unreadable");
    press(KEY_B, 900);
    expect(SCR_LIST, "back");
    select_row(row_named("Papierflieger.zip"));
    press(KEY_A, 900);
    expect(SCR_CONFIRM, "check Papierflieger");
    plan.state = PLAN_FULL_THEMES;       /* forced: a pack with no room left */
    shot("dX-02-check-full");
    plan.state = PLAN_OK;
    frames_for(300);
    fake_am.pending = PENDING_YES;      /* AM reports an import pending right before the first write: still_same */
    press(KEY_A, 300);
    press(KEY_A, 1200);
    expect(SCR_PROBLEM, "refused before writing");
    shot("dX-03-install-refused");
    fake_am.pending = PENDING_NO;
    read_pack();
    take_pack();
    press(KEY_A, 900);
    open_list();
    frames_for(900);
    select_row(row_named("Leer"));
    press(KEY_A, 900);
    expect(SCR_LIST, "empty folder");
    shot("dX-04-folder-empty");
    go_designs(0);
    frames_for(600);
    {
        int was = pv.state;
        pv.state = PACK_NOT_INSTALLED;  /* forced: no theme pack on the console */
        press(KEY_A, 300);
        press(KEY_A, 900);
        expect(SCR_PROBLEM, "no pack");
        shot("dX-05-no-pack");
        pv.state = was;
    }
    go_designs(1);
    frames_for(600);
    fl_cam_fail = 1;
    press(KEY_A, 300);
    press(KEY_A, 900);
    fl_cam_fail = 0;
    expect(SCR_PROBLEM, "camera failed");
    shot("dX-06-camera-failed");
    go_designs(1);
    frames_for(600);
    press(KEY_A, 300);
    press(KEY_A, 900);
    expect(SCR_SCAN, "scan again");
    fl_cam_text = "Hallo vom Kühlschrank";
    frame();
    fl_cam_text = NULL;
    camera_image = NULL;        /* the camera's picture is not drawn on the host: the screen as before its first frame */
    shot("dX-07-scan-no-link");
    fl_cam_text = "https://beispiel.example/designs/Papierflieger.zip";
    frame();
    fl_cam_text = NULL;
    expect(SCR_URL, "link again");
    fl_wifi = 0;
    press(KEY_A, 300);
    press(KEY_A, 900);
    fl_wifi = 1;
    expect(SCR_PROBLEM, "no wifi");
    shot("dX-08-no-wifi");
    to_link();
    download_shown(1.2, 8.1);
    download_ends(DL_CANCELLED, 0);
    expect(SCR_PROBLEM, "cancelled");
    shot("dX-09-download-cancelled");
    to_link();
    download_shown(0.0, 0.0);
    download_ends(DL_HTTP, 404);
    expect(SCR_PROBLEM, "HTTP 404");
    shot("dX-10-download-failed");
    go_designs(2);
    frames_for(600);
    open_designs();
    frames_for(900);
    expect(SCR_INSTALLED, "installed again");
    press(KEY_DOWN, 300);
    press(KEY_A, 900);
    expect(SCR_DESIGN, "a design again");
    {
        const design_row *d = shown_design();
        fake_home.current.index = d->index;     /* the theme in use in HOME */
        fake_home.current.variation = HOME_PACK_VARIATION;
        fake_home.current.type = HOME_TYPE_DLC;
        press(KEY_A, 300);
        press(KEY_A, 700);
        if (popup != POP_NOTE) {
            fprintf(stderr, "no refusal (popup %d)\n", popup);
            return 1;
        }
        shot("dX-11-delete-refused");
        memset(&fake_home.current, 0, sizeof fake_home.current);
        press(KEY_B, 600);
        /* forced: AM refuses the delete (the demonstration build always succeeds): run_delete_theme's note */
        deletion_judgement j = {DEL_NOT_DONE, 0, 1};
        text_deleted(lang, &j, 1, 0xD8E0806AU, d->file[0] != 0, d->theme ? d->theme->name : "?", del.head,
                     sizeof del.head, del.why, sizeof del.why);
        show_note(ART_INFO_WARN, del.head, del.why);
        shot("dX-12-delete-failed");
        press(KEY_B, 600);
    }
    /* FB-E: the states FL could not draw, each reached as the application reaches it, its cause forced */
    no_music_result = TP_E_NOMEM;       /* forced: leaving the music out failed */
    no_music_done();
    shot("dX-13-no-music-failed");
    press(KEY_B, 600);
    {
        char done[SCAN_NAME + 80];       /* forced: the file could not be deleted (clean_up, remove_file) */
        snprintf(done, sizeof done, text(lang, T_DELETE_FAILED), "Kieselgrau.zip");
        show_note(ART_INFO_WARN, done, NULL);
        shot("dX-14-file-not-deleted");
        press(KEY_B, 600);
        judgement j = {OUT_UNKNOWN, 0, 1, 0, PENDING_YES};  /* review of FB-E2: AM's view unread, an import pending */
        install_res.detail = TP_E_ZIP;
        install_res.wrote = 0;
        install_res.failed = STEP_NONE;
        text_outcome(lang, &j, &install_res, "Waldweg", title, sizeof title, body, sizeof body);
        problem_head = T_INSTALL_THEME;
        problem_ok = 1;
        screen = SCR_PROBLEM;
        shot("dX-15-install-failed");
    }
    show_problem(T_SD_CARD, text_result(lang, TP_E_NOMEM), "", 0);    /* forced: no memory for the list */
    shot("dX-16-folder-no-memory");
    go_designs(0);
    frames_for(600);
    show_note(ART_INFO_SD, text(lang, T_THEMES_NOT_CREATED), NULL);     /* forced: /Themes could not be made */
    shot("dX-17-themes-not-created");
    press(KEY_B, 600);
    {
        size_t was = designs.count;     /* forced: no design installed */
        designs.count = 0;
        screen = SCR_INSTALLED;
        shot("dX-18-installed-none");
        designs.count = was;
    }
    (void)tap; (void)stylus; (void)lift; (void)each_frame; (void)face_on; (void)extras;
    return 0;
}
