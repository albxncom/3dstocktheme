/* The Badges part's screens in app/source/main.c on the Mac: host/test_main.c's simulated keys, touch screen and
 * clock (included, with its own main renamed), HOME's badge files as a folder (host/badge_stubs.c: badge-home in the
 * working folder), sets in sdmc:/Badges made here. What the screens do with a write is checked in the files.
 *
 *   test_main_badges      prints a line per check and "N failed" last; exit 1 if any failed */
#include "test_main.c"   /* its main becomes test_main_main (host/badges_shim/zlib.h) */
#undef main
#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>

#include "badge_internal.h"   /* the set's areas and HOME's view field (B2) */

extern int badge_jump, badge_close, badge_home_jumps, badge_jump_at_read, badge_jump_at_write, badge_fail_write,
    badge_reads, badge_writes, badge_fail_open;
extern int badge_open_code, badge_open_write_code, badge_opens_read, badge_opens_write;

/* ---- fixtures */

static void put32be(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24), p[1] = (uint8_t)(v >> 16), p[2] = (uint8_t)(v >> 8), p[3] = (uint8_t)v;
}

/* An RGBA PNG of w x h generated pixels. */
static void write_png(const char *path, uint32_t w, uint32_t h, uint32_t seed)
{
    size_t raw_size = (size_t)h * (1 + 4 * w);
    uint8_t *raw = malloc(raw_size);
    for (uint32_t y = 0; y < h; y++) {
        uint8_t *row = raw + (size_t)y * (1 + 4 * w);
        row[0] = 0;
        for (uint32_t x = 0; x < 4 * w; x++)
            row[1 + x] = (uint8_t)(x * 7 + y * 13 + seed * 31);
    }
    uLongf packed_size = compressBound((uLong)raw_size);
    uint8_t *packed = malloc(packed_size);
    compress(packed, &packed_size, raw, (uLong)raw_size);
    FILE *f = fopen(path, "wb");
    static const uint8_t SIG[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    fwrite(SIG, 1, 8, f);
    struct {
        const char *kind;
        const uint8_t *data;
        uint32_t size;
    } chunks[3];
    uint8_t ihdr[13];
    put32be(ihdr, w);
    put32be(ihdr + 4, h);
    memcpy(ihdr + 8, (const uint8_t[]){8, 6, 0, 0, 0}, 5);
    chunks[0].kind = "IHDR", chunks[0].data = ihdr, chunks[0].size = 13;
    chunks[1].kind = "IDAT", chunks[1].data = packed, chunks[1].size = (uint32_t)packed_size;
    chunks[2].kind = "IEND", chunks[2].data = NULL, chunks[2].size = 0;
    for (int i = 0; i < 3; i++) {
        uint8_t head[8], tail[4];
        put32be(head, chunks[i].size);
        memcpy(head + 4, chunks[i].kind, 4);
        uLong crc = crc32(0, head + 4, 4);
        if (chunks[i].size)
            crc = crc32(crc, chunks[i].data, chunks[i].size);
        put32be(tail, (uint32_t)crc);
        fwrite(head, 1, 8, f);
        fwrite(chunks[i].data ? chunks[i].data : head, 1, chunks[i].size, f);
        fwrite(tail, 1, 4, f);
    }
    fclose(f);
    free(raw);
    free(packed);
}

/* HOME's files as an empty collection (both zero-filled, as the folder store makes them). */
static void empty_collection(void)
{
    mkdir("badge-home", 0755);
    static const char *const NAMES[2] = {"badge-home/BadgeData.dat", "badge-home/BadgeMngFile.dat"};
    static const uint32_t SIZES[2] = {BADGE_DATA_SIZE, BADGE_MNG_SIZE};
    for (int i = 0; i < 2; i++) {
        FILE *f = fopen(NAMES[i], "wb");
        uint8_t *zero = calloc(1, SIZES[i]);
        fwrite(zero, 1, SIZES[i], f);
        fclose(f);
        free(zero);
    }
}

/* Both of HOME's files, to compare. */
static uint8_t *home_now(void)
{
    uint8_t *both = malloc(BADGE_DATA_SIZE + BADGE_MNG_SIZE);
    FILE *f = fopen("badge-home/BadgeData.dat", "rb");
    size_t n = f ? fread(both, 1, BADGE_DATA_SIZE, f) : 0;
    if (f)
        fclose(f);
    f = fopen("badge-home/BadgeMngFile.dat", "rb");
    n += f ? fread(both + BADGE_DATA_SIZE, 1, BADGE_MNG_SIZE, f) : 0;
    if (f)
        fclose(f);
    if (n != BADGE_DATA_SIZE + BADGE_MNG_SIZE)
        memset(both, 0xEE, BADGE_DATA_SIZE + BADGE_MNG_SIZE);
    return both;
}

static int same_home(const uint8_t *was)
{
    uint8_t *now = home_now();
    int same = !memcmp(now, was, BADGE_DATA_SIZE + BADGE_MNG_SIZE);
    free(now);
    return same;
}

/* ---- what the log shows of the writes */

static int appends_logged, removals_logged, backups_logged, home_in_write, sleep_in_write, write_lines;

static void badge_watch_log(const char *line)
{
    if (strstr(line, "== Append badge set"))
        appends_logged++;
    if (strstr(line, "== Remove the last badge set"))
        removals_logged++;
    if (strstr(line, "badges: backup "))
        backups_logged++;
    if (strstr(line, "badges: ") || strstr(line, "== ")) {
        write_lines++;
        home_in_write |= apt_home;      /* any line of a write with HOME allowed */
        sleep_in_write |= apt_sleep;
    }
}

static int busy_frames;
static void count_busy(void)
{
    busy_frames += stub_busy_top;
}

static void center_tap(rect r)
{
    tap((int)(r.x + r.w / 2), (int)(r.y + r.h / 2), 48);
    frames_for(300);
}

static void a_press(void)
{
    key(KEY_A);
    frames_for(300);
}

/* ---- a web server on 127.0.0.1 for the QR code's way, in a child process: /Sonne.png is a PNG (a set of one
 * badge), /Kaputt.png a PNG's signature and nothing after it, /page a web page, anything else 404 */

static pid_t server_pid = -1;
static int server_port;

static void serve(int listener, const uint8_t *png, size_t png_bytes)
{
    static const char PAGE[] = "<html><body>no badges here</body></html>";
    static const uint8_t BROKEN[16] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    for (;;) {
        int c = accept(listener, NULL, NULL);
        if (c < 0)
            continue;
        char req[2048];
        ssize_t n = recv(c, req, sizeof req - 1, 0);
        req[n > 0 ? n : 0] = 0;
        const uint8_t *reply = NULL;
        size_t size = 0;
        const char *kind = "text/html", *status = "200 OK";
        if (!strncmp(req, "GET /Sonne.png ", 15))
            reply = png, size = png_bytes, kind = "image/png";
        else if (!strncmp(req, "GET /Kaputt.png ", 16))
            reply = BROKEN, size = sizeof BROKEN, kind = "image/png";
        else if (!strncmp(req, "GET /page ", 10))
            reply = (const uint8_t *)PAGE, size = sizeof PAGE - 1;
        else
            status = "404 Not Found";
        char head[256];
        int h = snprintf(head, sizeof head, "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
                         "Connection: close\r\n\r\n", status, kind, size);
        send(c, head, (size_t)h, 0);
        if (size)
            send(c, reply, size, 0);
        close(c);
    }
}

static int server_start(void)
{
    write_png("sonne-served.png", 64, 64, 9);
    FILE *f = fopen("sonne-served.png", "rb");
    static uint8_t png[1 << 16];
    size_t png_size = f ? fread(png, 1, sizeof png, f) : 0;
    if (f)
        fclose(f);
    remove("sonne-served.png");
    int listener = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a = {0};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t len = sizeof a;
    if (listener < 0 || bind(listener, (struct sockaddr *)&a, sizeof a) || listen(listener, 8) ||
        getsockname(listener, (struct sockaddr *)&a, &len) || !png_size)
        return -1;
    server_port = ntohs(a.sin_port);
    fflush(stdout);
    server_pid = fork();
    if (server_pid == 0) {
        serve(listener, png, png_size);
        _exit(0);
    }
    close(listener);
    return server_pid > 0 ? 0 : -1;
}

static void server_stop(void)
{
    if (server_pid > 0) {
        kill(server_pid, SIGTERM);
        waitpid(server_pid, NULL, 0);
    }
    server_pid = -1;
}

/* the link screen with a link the camera found (as test_main.c does it: the camera is a stub) */
static void qr_found(const char *path)
{
    snprintf(url, sizeof url, "http://127.0.0.1:%d%s", server_port, path);
    text_shown(url_shown, sizeof url_shown, url);
    screen = SCR_URL;
    frames_for(300);
}

static int badges_files(void)
{
    int n = 0;
    DIR *d = opendir("sdmc:/Badges");
    for (struct dirent *e; d && (e = readdir(d));)
        n += e->d_name[0] != '.';
    if (d)
        closedir(d);
    return n;
}

/* ---- main.c's main loop: its turn with the system (console_main_loop), then test_main.c's frame (the loop's body);
 * 0 once the application ended */
static int turns(int n)
{
    for (int i = 0; i < n; i++) {
        if (!console_main_loop())
            return 0;
        frame();
    }
    return 1;
}

/* between cases: nothing of the system waits, no failure is set up */
static void next_case(void)
{
    badge_jump = badge_close = badge_jump_at_read = badge_jump_at_write = badge_fail_write = 0;
}

/* a tab of the bar was drawn on the bottom screen's last frame */
static int bar_drawn(void)
{
    for (int i = 0; i < stub_marks; i++)
        if (stub_mark_kind[i] == 'o' && stub_mark_at[i].y >= BAR_Y - 1)
            return 1;
    return 0;
}

/* R11: how often the Badges part's buttons come in after its tile on the start screen is tapped, while the look at
 * HOME's files takes late frames (0: at once) and fails or not. The part shows when its screen is drawn and no
 * loader covers it more than half. */
static int menu_seen, menu_entrances, menu_loader;
static void watch_menu(void)
{
    int seen = screen == SCR_BADGES && !(stub_loader && stub_loader_cover >= 0.5f);
    menu_entrances += seen && !menu_seen;
    menu_seen = seen;
    menu_loader |= stub_loader && stub_loader_cover >= 0.5f;
}

static void badges_entrance(int late, int fail)
{
    reset_app();
    screen = SCR_MAIN;
    focus = 0;
    frames_for(300);
    worker_frames = late;
    badge_fail_open = fail;
    menu_seen = menu_entrances = menu_loader = 0;
    void (*was)(void) = stub_frame_done;
    stub_frame_done = watch_menu;
    center_tap(TILE_TWO);
    frames_for(1500);
    stub_frame_done = was;
    worker_frames = 0;
    badge_fail_open = 0;
    EXPECT(screen == SCR_BADGES && menu_entrances == 1 && menu_loader == (late > 20) && blist_valid,
           "R11: Badges from the start screen, the look at HOME's files %s%s: the part comes in %d times (once), "
           "loader %d", late ? "late" : "at once", fail ? " and failing" : "", menu_entrances, menu_loader);
}

/* row i of the list shown, the list where a key would take it for that row; 1 when the row shows whole */
static int row_shown(size_t i, rect *r)
{
    lv.cursor = i;
    lv.scroll = scroll_for_cursor();
    *r = row_rect(i);
    scroll_shape s = shape();
    return i < s.count && scroll_whole(&s, &lv, i);
}

/* a point inside both a and b, 2 px from their edges; 0 if they hardly meet */
static int both_at(rect a, rect b, int *x, int *y)
{
    float x0 = fmaxf(a.x, b.x) + 2, y0 = fmaxf(a.y, b.y) + 2;
    float x1 = fminf(a.x + a.w, b.x + b.w) - 2, y1 = fminf(a.y + a.h, b.y + b.h) - 2;
    if (x1 <= x0 || y1 <= y0)
        return 0;
    *x = (int)((x0 + x1) / 2), *y = (int)((y0 + y1) / 2);
    return 1;
}

/* a tap on the list at x, y, then on until the check screen it opens has drawn its second frame */
static void check_from_row(int x, int y)
{
    tap(x, y, 48);
    for (int i = 0; i < 60 && screen != SCR_BADGE_SET; i++)
        frame();
    frame();
}

/* the stylus comes down at (wx, wy) in the 5th frame of the badge write, while no input is read */
static int write_x, write_y, write_frames;
static void write_tick(void)
{
    held_tick();
    if (job == JOB_BADGE_WRITE && ++write_frames == 5)
        stylus(write_x, write_y);
}

/* R15: on the installed set's screen (the listing as read, then changed to state one reason) the remove tile opens
 * an alert with why (reason; NULL: removing is allowed, the question opens) and nothing is written */
static void one_tile(const char *what, const char *reason)
{
    int started = threads_started;
    removals_logged = badge_reads = badge_writes = 0;
    stub_tile_symbol[0] = -1;
    frames_for(POPUP_GUARD_MS + 50);    /* a touch counts only this long after a popup closed */
    EXPECT(screen == SCR_BADGE_ONE && stub_fact_rows == 2 && stub_tile_symbol[0] == ART_DELETE_THEME,
           "R15 %s: the screen as for a design, the facts box and the tile (screen %d, %d facts)", what, screen,
           stub_fact_rows);
    center_tap(badges_remove_rect());
    if (reason)
        EXPECT(popup == POP_NOTE && pop.count == 1 && !strcmp(stub_popup_para[0], reason) &&
                   !stub_popup_left && stub_popup_right && !strcmp(stub_popup_right, text(lang, T_OKAY)),
               "R15 %s: the tile opens the alert \"%s\" with Okay (popup %d, \"%s\")", what, reason, popup,
               stub_popup_para[0]);
    if (reason) {
        draw();
        EXPECT(stub_popup_sign == ART_INFO_WARN && sign_once(stub_popup_sign),
               "FB-E2 R15 %s: the note's warning, once on the two screens (top's motifs %d, note %d)", what,
               top_meanings(), stub_popup_sign);
    }
    else
        EXPECT(popup == POP_ASK_SET, "R15 %s: the tile opens the question", what);
    EXPECT(screen == SCR_BADGE_ONE && !removals_logged && !badge_writes && !badge_reads &&
               threads_started == started && !bwriting,
           "R15 %s: the remove not reached, nothing read or written", what);
    if (reason) {
        frames_for(POPUP_GUARD_MS);     /* and after it opened */
        center_tap(ui_popup_button(0, 0));
        EXPECT(popup == POP_NONE && screen == SCR_BADGE_ONE && !removals_logged && !badge_writes,
               "R15 %s: Okay closes it, back on the set's screen", what);
    }
}

/* the part, Von SD installieren, the set's check screen */
static void to_set(const char *file)
{
    reset_app();
    badges_open(0);
    frames_for(300);
    center_tap(DESIGNS_TILES[0]);
    size_t i = 0;
    while (i < bents.count && strcmp(bents.items[i].file, file))
        i++;
    rect r;
    if (row_shown(i, &r))
        center_tap(r);
    frames_for(POPUP_GUARD_MS);     /* the check screen takes a touch only this long after it opened */
}

static void append_set(const char *file)
{
    to_set(file);
    badge_reads = badge_writes = 0;
    center_tap(GO_BUTTON);
}

/* Installierte Badges, the last set, Entfernen: the question open, past its guard */
static void remove_ask(void)
{
    reset_app();
    badges_open(2);
    frames_for(300);
    center_tap(DESIGNS_TILES[2]);
    rect r;
    size_t last = blist.col.sets ? blist.col.sets - 1 : 0;
    if (row_shown(last, &r))
        center_tap(r);
    center_tap(badges_remove_rect());
    frames_for(POPUP_GUARD_MS);
    badge_reads = badge_writes = 0;
}

/* G1-R6: how far the set's question had opened in the last frame drawn; -1: it was not drawn */
static float set_asked(void)
{
    return stub_popup_shown >= 0 && stub_popup_right && !strcmp(stub_popup_right, badge_text(lang, BT_REMOVE))
               ? stub_popup_shown
               : -1;
}

/* G1-R6: a tap of 48 ms at the middle of r, no frames after it */
static void quick_tap(rect r)
{
    stylus((int)(r.x + r.w / 2), (int)(r.y + r.h / 2));
    frames_for(48);
    lift();
}

/* G1-R6: on the last set's screen, the tile tapped: the frames up to the question's first; how far it had opened
 * there (-1: none came) */
static float ask_by_tap(void)
{
    quick_tap(badges_remove_rect());
    float first = -1;
    for (int i = 0; i < 40 && first < 0; i++) {
        frame();
        first = set_asked();
    }
    return first;
}

/* ... and the question's Entfernen */
static void remove_last(void)
{
    remove_ask();
    center_tap(ui_popup_button(1, 1));
}

/* G1-R7: a set appended, its question cancelled, the cursor on Entfernen, up to the A that opens the question */
static void to_set_ask(void)
{
    to_set("Stern.png");
    center_tap(GO_BUTTON);
    frames_for(300);
    next_case();
    remove_ask();
    key(KEY_B);
    frames_for(POPUP_GUARD_MS + 300);
    removals_logged = badge_writes = 0;
    key(KEY_A);
}
static int set_removed(void) { return removals_logged; }

/* FB-E: a removal ends as a design's delete does: back in "Installierte Badges", read again without the set, the note
 * "<set>" wurde entfernt. with its two sentences and no sign over it; SCR_REMOVED stands for that end */
#define SCR_REMOVED (-2)
extern int stub_popup_sign;
static int where(void) { return screen == SCR_BADGE_SETS && popup == POP_NOTE ? SCR_REMOVED : screen; }
static int removal_end(void)
{
    for (int i = 0; i < 300 && (job != JOB_NONE || bremoved); i++)
        frame();
    frame();
    frame();
    char head[BADGE_ENTRY_NAME + 40];
    snprintf(head, sizeof head, badge_text(lang, BT_REMOVED_HEAD), bname);
    return where() == SCR_REMOVED && !strcmp(stub_popup_para[0], head) &&
           !strcmp(stub_popup_para[1], badge_text(lang, BT_REMOVED_NOTE)) && stub_popup_sign == -1;
}

/* After a write that read back, with a POWER press or a HOME press waiting since it: the next turns of the main loop
 * go to HOME once, with HOME and sleep allowed, and the Done screen is there when HOME lets the application go on */
static void expect_jump(const char *what, int jumps_before)
{
    int go = turns(5);
    EXPECT(go && (bremove ? removal_end() : screen == SCR_BADGE_DONE) && apt_home && apt_sleep && badge_home_jumps == jumps_before + 1,
           "%s: carried out after the write, HOME once (%d jumps, screen %d)", what, badge_home_jumps - jumps_before,
           screen);
}

/* R13 (decided on the console on 2026-11-15: HOME takes an appended set without a restart and keeps it over one):
 * the Done screen after an install and after a removal leads back into the part, by B, by A and by its tab, as the
 * Designs part's; HOME and sleep are allowed again once the write is over, and the application goes on */
extern const char *stub_done_home;
static void done_leads_back(int remove, int how)
{
    static const char *const HOW[3] = {"B", "A", "its tab"};
    if (remove)
        remove_last();
    else
        append_set("Tiere");
    frames_for(300);
    /* FB-E: a removal ends in the list read again with its note (removal_end), no Done card; G2-L2b: after an
     * install the Done screen's top is the installing screen's end. G2-J: in layout C that end is given the success
     * sentence where the way stood (G2-L1b) */
    int done = remove ? removal_end() && bres.outcome == BFLOW_DONE && bremove && !top_has('D')
                      : screen == SCR_BADGE_DONE && bres.outcome == BFLOW_DONE && !bremove;
    const char *home_line = stub_pour_given.home;
    EXPECT(done && (remove || (top_has('p') && !top_has('D') && stub_pour == &bpour && home_line &&
                               !strcmp(home_line, text(lang, T_INSTALL_SUCCESS)))),
           "O3: after %s, %s (done %d, line %s)", remove ? "a removal" : "an install",
           remove ? "the list with its note, no Done card" : "the installing screen's end on top with the success "
           "sentence", done, home_line ? home_line : "none");
    int allowed = apt_home && apt_sleep, tab = remove || bar_drawn();
    rect okay = ui_popup_button(0, 0);
    frames_for(POPUP_GUARD_MS);
    if (how == 0)
        key(KEY_B);
    else if (how == 1)
        key(KEY_A);
    else if (remove)
        tap((int)(okay.x + okay.w / 2), (int)(okay.y + okay.h / 2), 48);
    else
        tap(40, SCREEN_H - 8, 48);
    frames_for(300);
    int go = turns(30);
    EXPECT(done && allowed && tab && go && where() == (remove ? SCR_BADGE_SETS : SCR_BADGES) && apt_home && apt_sleep,
           "R13: the Done screen after %s: HOME and sleep allowed, the B tab; %s leads back into the part and the "
           "application goes on (done %d, allowed %d, tab %d, screen %d)",
           remove ? "a removal" : "an install", HOW[how], done, allowed, tab, screen);
}

/* Both of HOME's files from what home_now gave. */
static void put_home(const uint8_t *both)
{
    FILE *f = fopen("badge-home/BadgeData.dat", "r+b");
    fwrite(both, 1, BADGE_DATA_SIZE, f);
    fclose(f);
    f = fopen("badge-home/BadgeMngFile.dat", "r+b");
    fwrite(both + BADGE_DATA_SIZE, 1, BADGE_MNG_SIZE, f);
    fclose(f);
}

/* B2 (the console's log of 2026-11-15): a byte the append wrote changed since (here one byte of its badges; HOME
 * moved the view too), so no undo record fits them (BADGE_E_SET_CHANGED): the removal is refused before anything is
 * written, the backup too, and the problem screen says what happened, in both languages */
static void set_changed_text(void)
{
    empty_collection();
    append_set("Tiere");
    next_case();
    reset_app();
    badges_open(0);
    frames_for(300);
    const badge_set_info *s = &blist.col.set[blist.col.sets ? blist.col.sets - 1 : 0];
    badge_region regs[REGIONS];
    badge_regions(s->slot, s->start, s->count, regs);
    int k = 0;
    while (k < REGIONS && !(regs[k].file == REG_DATA && regs[k].size))
        k++;
    uint8_t *appended = home_now(), *changed = home_now();
    if (k < REGIONS)
        changed[regs[k].offset + regs[k].size / 2] ^= 0x5A;
    changed[BADGE_DATA_SIZE + BM_VIEW] ^= 1;
    put_home(changed);
    removals_logged = backups_logged = 0;
    remove_last();
    frames_for(300);
    char en_title[160], en_body[1600];
    badge_text_outcome(0, &bres, en_title, sizeof en_title, en_body, sizeof en_body);
    EXPECT(k < REGIONS && removals_logged == 1 && screen == SCR_BADGE_PROBLEM && bres.outcome == BFLOW_REFUSED &&
               !bres.wrote && bres.stage == BSTAGE_UNDO && bres.badge == BADGE_E_SET_CHANGED && same_home(changed) &&
               !backups_logged && !bres.backup[0],
           "B2: the set changed: refused before a write and before the backup, the files as HOME left them (screen "
           "%d, outcome %d, stage %d, %s, %d backups)", screen, bres.outcome, bres.stage, badge_result_name(bres.badge),
           backups_logged);
    EXPECT(!strcmp(btitle, "Nichts wurde geändert") &&
               !strcmp(bbody, "Das HOME-Menü hat die Badge-Daten geändert, seit dieses Set installiert wurde, deshalb "
                              "kann die Anwendung das Set so nicht zurücknehmen. Die Badge-Daten des HOME-Menüs sind "
                              "wie vorher."),
           "B2: the problem screen: %s / %s", btitle, bbody);
    EXPECT(!strcmp(en_title, "Nothing was changed") &&
               !strcmp(en_body, "The HOME Menu has changed the badge data since this set was installed, so the "
                             "application cannot take the set back this way. The HOME Menu's badge data is as it was."),
           "B2: in English: %s / %s", en_title, en_body);
    put_home(appended);
    next_case();
    remove_last();
    EXPECT(removal_end() && bres.outcome == BFLOW_DONE && bremove,
           "with the files as the append left them the set is removed (outcome %d)", bres.outcome);
    next_case();
    free(appended);
    free(changed);
}

/* B2: what HOME did on the console once it had shown the new set (the view on it, the set record's + 0x28 at 3): the
 * removal goes, with a backup first, and leaves the files byte for byte as before the append (both fields go with the
 * set); the Done screen */
static void home_changed_goes(void)
{
    empty_collection();
    uint8_t *empty = home_now();
    append_set("Tiere");
    next_case();
    reset_app();
    uint8_t *files = home_now();
    put_le32(files + BADGE_DATA_SIZE + BM_VIEW, 0);
    put_le32(files + BADGE_DATA_SIZE + BM_SET_INFO + 0x28, 3);
    put_home(files);
    removals_logged = backups_logged = 0;
    remove_last();
    frames_for(300);
    EXPECT(removals_logged == 1 && backups_logged == 1 && removal_end() && bres.outcome == BFLOW_DONE &&
               bremove && bres.identical && same_home(empty),
           "B2: after HOME's changes the set is removed, the backup first, the files as before the append (screen %d, "
           "outcome %d, %s, %d backups)", screen, bres.outcome, badge_result_name(bres.badge), backups_logged);
    next_case();
    free(empty);
    free(files);
}

/* G1-R5 (C43): a set's removal runs as a design's delete. Per frame drawn while a set is written (bwriting), from the
 * question's Entfernen to the Done or problem screen: the vessel (ui_pour); a loader's ring on the top screen; the
 * loader's ring on the bottom screen, and with the line of the step under way; the top screen as the set's screen
 * draws it, with its picture and name; whether the job was still to come, under way or over */
extern int stub_pour_frames, stub_top_loader, stub_loader;
extern float stub_loader_cover, stub_loader_shown;
extern char stub_loader_line[120], stub_gallery_name[120];
extern ui_pic stub_gallery_pic;
extern char stub_piece_kind[64];        /* main_stubs.c's STUB_PIECES */
extern int stub_piece_bottom[64], stub_pieces;
extern rect stub_piece_at[64];
static int wf_frames, wf_pour, wf_top, wf_ring, wf_line, wf_one, wf_before, wf_job, wf_after, wf_threads,
    wf_pour_was;
/* the pieces of a frame (main_stubs.c): the first of a removal with its loader whole (cover and ring at 1), and the
 * cursor ring then */
typedef struct {
    int count, ring;
    char kind[64];
    int bottom[64];
    rect at[64];
} frame_pieces;
static frame_pieces wf_whole;

static void keep_pieces(frame_pieces *f)
{
    f->count = stub_pieces;
    f->ring = ring;
    memcpy(f->kind, stub_piece_kind, sizeof f->kind);
    memcpy(f->bottom, stub_piece_bottom, sizeof f->bottom);
    memcpy(f->at, stub_piece_at, sizeof f->at);
}

static void watch_write(void)
{
    if (bwriting) {
        int step = bstep < BSTEP_DONE ? bstep : BSTEP_VERIFY;
        int ring_shown = stub_loader && stub_loader_shown > 0;
        ui_pic pic = badges_set_pic(bone);
        wf_frames++;
        wf_pour += stub_pour_frames != wf_pour_was;
        wf_top += stub_top_loader;
        wf_ring += ring_shown;
        wf_line += ring_shown && !strcmp(stub_loader_line, badge_text(lang, BT_WORK_READ + step));
        wf_one += screen == SCR_BADGE_ONE && badges_one_shown() && !strcmp(stub_gallery_name, blist.col.set[bone].name) &&
                  stub_gallery_pic.icon == pic.icon && stub_gallery_pic.symbol == pic.symbol;
        wf_before += threads_started == wf_threads;
        wf_job += job == JOB_BADGE_WRITE;
        wf_after += threads_started > wf_threads && job != JOB_BADGE_WRITE;
        if (!wf_whole.count && stub_loader && stub_loader_cover >= 1 && stub_loader_shown >= 1)
            keep_pieces(&wf_whole);
    }
    wf_pour_was = stub_pour_frames;
}

static void watch_write_begin(void)
{
    wf_frames = wf_pour = wf_top = wf_ring = wf_line = wf_one = wf_before = wf_job = wf_after = 0;
    wf_whole.count = 0;
    wf_threads = threads_started;
    wf_pour_was = stub_pour_frames;
    stub_frame_done = watch_write;
    worker_frames = 40;         /* the write takes 40 frames, past LOADER_WAIT_MS and LOADER_FADE_MS */
}

static void watch_write_end(void)
{
    stub_frame_done = NULL;
    worker_frames = 0;
}

/* a removal's frames: none before the job, none after it; in each the set's screen on top, with its picture and
 * name, no loader there and no vessel; on the bottom screen the loader's ring once the job has run LOADER_WAIT_MS,
 * each time with its step's line */
static void expect_removal(const char *what, int done_screen)
{
    watch_write_end();
    EXPECT((done_screen ? removal_end() : screen == SCR_BADGE_PROBLEM) && !wf_before && !wf_after &&
               wf_job == wf_frames && wf_job >= 30 && wf_one == wf_frames && !wf_top && !wf_pour && wf_ring >= 20 &&
               wf_line == wf_ring && wf_whole.count > 0,
           "%s: %d frames (%d before the job, %d in it, %d after it), the set's screen on top in %d, a loader there in "
           "%d, the vessel in %d; the ring on the bottom screen in %d, its step's line in %d (screen %d)", what,
           wf_frames, wf_before, wf_job, wf_after, wf_one, wf_top, wf_pour, wf_ring, wf_line, screen);
}

/* The frame drawn while a design is removed, its loader whole: the one-design screen of a design whose file is in
 * /Themes (as a set's facts, two rows), as main.c's delete draws it (job_here's frame, draw); cursor ring as given */
static void design_removal_frame(frame_pieces *f, int with_ring)
{
    static pack_theme th;
    memset(&th, 0, sizeof th);
    th.index = 7;
    th.usable = 1;
    snprintf(th.name, sizeof th.name, "Herbst");
    memset(th.icon, 0x5A, sizeof th.icon);
    designs.count = 1;
    designs.rows[0] = (design_row){7, &th, MUSIC_NONE, "herbst.zip"};
    design_index = 7;
    focus = 0;
    ring = with_ring;
    back_since = 0;
    screen = SCR_DESIGN;
    job = JOB_DELETE_THEME;
    job_over = 0;
    job_over_at = 0;
    job_since = osGetTime() - LOADER_WAIT_MS - LOADER_FADE_MS - 100;
    draw();
    keep_pieces(f);
    job = JOB_NONE;
    designs.count = 0;
    screen = SCR_BADGES;
}

/* Where two frames' pieces differ: the index of the first piece that is not the same kind on the same screen in the
 * same place, the footer's warning (a text) left out; -1: none */
static int pieces_differ(const frame_pieces *a, const frame_pieces *b)
{
    int i = 0, j = 0;
    for (;;) {
        while (i < a->count && a->kind[i] == 'W')
            i++;
        while (j < b->count && b->kind[j] == 'W')
            j++;
        if (i == a->count || j == b->count)
            return i == a->count && j == b->count ? -1 : i;
        if (a->kind[i] != b->kind[j] || a->bottom[i] != b->bottom[j] || memcmp(&a->at[i], &b->at[j], sizeof(rect)))
            return i;
        i++, j++;
    }
}

static const char *pieces_text(const frame_pieces *f, char *out, size_t size)
{
    size_t n = 0;
    out[0] = 0;
    for (int i = 0; i < f->count && n + 40 < size; i++)
        n += (size_t)snprintf(out + n, size - n, "%s%c(%g %g %g %g)", f->bottom[i] && (!i || !f->bottom[i - 1]) ? "| " : "",
                              f->kind[i], (double)f->at[i].x, (double)f->at[i].y, (double)f->at[i].w, (double)f->at[i].h);
    return out;
}

/* The stylus (or a key) comes down in the 5th frame of the badge write, while no input is read, and stays down */
static u32 write_key;
static void write_down_tick(void)
{
    held_tick();
    if (job == JOB_BADGE_WRITE && ++write_frames == 5) {
        if (write_key)
            sim_keys |= write_key;
        else
            stylus(write_x, write_y);
    }
}

/* G1-R5: the removal's question, Entfernen; the write fails first when fail; a press begun during the write (the
 * stylus at (x, y), or the key k) is down as the Done or problem screen opens */
static int remove_pressed_through(int fail, int x, int y, u32 k)
{
    next_case();
    remove_ask();
    badge_fail_write = fail;
    write_key = k, write_x = x, write_y = y, write_frames = 0;
    worker_frames = 20;
    stub_each_frame = write_down_tick;
    rect b = ui_popup_button(1, 1);
    tap((int)(b.x + b.w / 2), (int)(b.y + b.h / 2), 48);
    int target = fail ? SCR_BADGE_PROBLEM : SCR_REMOVED;
    for (int i = 0; i < 400 && where() != target; i++)
        frame();
    stub_each_frame = held_tick;
    worker_frames = 0;
    return where() == target && (k ? (sim_keys & k) != 0 : (sim_keys & KEY_TOUCH) != 0);
}

/* G1-R5b: the set's check screen, Installieren; the write fails first when fail; the key k, down during the write,
 * is down as the Done or problem screen opens */
static int append_pressed_through(int fail, u32 k)
{
    next_case();
    to_set("Tiere");
    badge_reads = badge_writes = 0;
    badge_fail_write = fail;
    write_key = k, write_x = write_y = 0, write_frames = 0;
    worker_frames = 20;
    stub_each_frame = write_down_tick;
    center_tap(GO_BUTTON);
    int target = fail ? SCR_BADGE_PROBLEM : SCR_BADGE_DONE;
    for (int i = 0; i < 400 && screen != target; i++)
        frame();
    stub_each_frame = held_tick;
    worker_frames = 0;
    return screen == target && (sim_keys & k) != 0;
}

/* G1-R5b: the key k, held, stays without effect on the screen target until it is let go and pressed again; then it
 * leads away */
static int held_until_again(u32 k, int target)
{
    frames_for(POPUP_GUARD_MS + 300);
    int still = where() == target && pending == HIT_NONE;
    sim_keys &= ~k;
    frames_for(300);
    still = still && where() == target;
    key(k);
    frames_for(300);
    int go = turns(30);
    return still && go && where() != target;
}

int main(void)
{
    stub_each_frame = held_tick;
    const char *base = getenv("TMPDIR");
    snprintf(dir, sizeof dir, "%s/test-main-badges-XXXXXX", base && *base ? base : "/tmp");
    if (!mkdtemp(dir) || chdir(dir))
        return 3;
    lang = 1;
    mkdir("sdmc:", 0755);
    mkdir("sdmc:/theme-pack", 0755);
    mkdir("sdmc:/Badges", 0755);
    mkdir("sdmc:/Badges/Tiere", 0755);
    write_png("sdmc:/Badges/Tiere/Fuchs.png", 64, 64, 1);
    write_png("sdmc:/Badges/Tiere/Voegel.png", 128, 64, 2);
    write_png("sdmc:/Badges/Stern.png", 64, 64, 3);
    mkdir("sdmc:/Badges/Mehr", 0755);
    FILE *f = fopen("sdmc:/Badges/Mehr/liesmich.txt", "wb");
    fputs("x", f);
    fclose(f);
    write_png("sdmc:/Badges/Mehr/Mond.png", 64, 64, 4);
    /* elsewhere on the card (R16): a folder of 14 sets beside a note, an empty folder; what the root hides */
    mkdir("sdmc:/Sammlung", 0755);
    for (int k = 0; k < 14; k++) {
        char name[64];
        snprintf(name, sizeof name, "sdmc:/Sammlung/Set %02d.png", k);
        write_png(name, 64, 64, 10 + (uint32_t)k);
    }
    f = fopen("sdmc:/Sammlung/liesmich.txt", "wb");
    fputs("x", f);
    fclose(f);
    mkdir("sdmc:/Leer", 0755);
    mkdir("sdmc:/Nintendo 3DS", 0755);
    mkdir("sdmc:/Nintendo 3DS/Private", 0755);
    mkdir("sdmc:/.Spotlight-V100", 0755);
    f = fopen("sdmc:/._Sammlung", "wb");
    fputs("x", f);
    fclose(f);
    stub_log_hook = badge_watch_log;

    /* 1. without HOME's badge files: the part opens, its footer says so, the installed list says why */
    printf("-- 1: no badge data\n");
    reset_app();
    go_main(0);
    frames_for(300);
    center_tap(TILE_TWO);
    frames_for(300);
    EXPECT(screen == SCR_BADGES && blist_valid && blist.state == BLIST_NO_DATA, "the Badges tile opens the part, "
           "the collection is not there (screen %d, state %d)", screen, blist.state);
    EXPECT(stub_hero_symbol == in_glass(ART_BADGES), "the part's hero symbol");
    EXPECT(strstr(stub_foot, "Keine Badge-Daten") != NULL, "its footer: %s", stub_foot);

    /* 1b (0.9.4, console finding 1). HOME holds its badge files (C92044E6): the part opens them to read only and
     * says they are locked, in the footer and in the installed list; another code: they could not be opened. Locked
     * against writes only: the list reads, a write meets the lock at its open and writes nothing */
    printf("-- 1b: the badge files locked\n");
    badge_open_code = (int)0xC92044E6;
    badge_opens_read = badge_opens_write = 0;
    reset_app();
    badges_open(2);
    frames_for(300);
    EXPECT(screen == SCR_BADGES && blist_valid && blist.state == BLIST_LOCKED && blist.code == (int)0xC92044E6,
           "the collection is locked (state %d, code %08X)", blist.state, (unsigned)blist.code);
    EXPECT(strstr(stub_foot, "Die Badge-Daten sind gesperrt") != NULL, "its footer: %s", stub_foot);
    EXPECT(badge_opens_read >= 1 && badge_opens_write == 0, "opened to read only (%d read, %d read and write)",
           badge_opens_read, badge_opens_write);
    center_tap(DESIGNS_TILES[2]);
    frames_for(100);
    EXPECT(screen == SCR_BADGE_SETS && blist.state == BLIST_LOCKED && blist.col.sets == 0,
           "Installierte Badges: locked, no sets (screen %d, state %d)", screen, blist.state);
    badge_open_code = (int)0xC8A04555;
    reset_app();
    badges_open(0);
    frames_for(300);
    EXPECT(blist.state == BLIST_OPEN && strstr(stub_foot, "Die Badge-Daten sind nicht lesbar") != NULL,
           "another code: not opened (state %d), footer: %s", blist.state, stub_foot);
    badge_open_code = 0;
    empty_collection();
    badge_open_write_code = (int)0xC92044E6;
    badge_opens_read = badge_opens_write = 0;
    to_set("Stern.png");
    EXPECT(screen == SCR_BADGE_SET && bplan == BPLAN_OK && blist.state == BLIST_OK && badge_opens_write == 0,
           "locked against writes only: the list reads, Stern.png's check (screen %d, plan %d)", screen, bplan);
    uint8_t *locked_was = home_now();
    appends_logged = badge_reads = badge_writes = 0;
    center_tap(GO_BUTTON);
    frames_for(100);
    EXPECT(appends_logged == 1 && screen == SCR_BADGE_PROBLEM && bres.outcome == BFLOW_REFUSED && !bres.wrote &&
               bres.stage == BSTAGE_OPEN && bres.code == (int)0xC92044E6 && badge_opens_write == 1,
           "the write meets the lock at its open (screen %d, outcome %d, stage %d)", screen, bres.outcome,
           bres.stage);
    EXPECT(!strcmp(btitle, "Nichts wurde geändert") && strstr(bbody, "Die Badge-Daten sind gesperrt. ") == bbody &&
               strstr(bbody, "(Code C92044E6)") != NULL,
           "the problem screen: %s / %s", btitle, bbody);
    struct stat none;
    EXPECT(same_home(locked_was) && badge_writes == 0 && stat("sdmc:/theme-pack/badge-backup", &none) != 0 &&
               stat("sdmc:/theme-pack/badge-manifest.txt", &none) != 0,
           "nothing written: HOME's files as they were, no backup folder, no manifest");
    EXPECT(apt_home && apt_sleep, "HOME and sleep allowed again");
    free(locked_was);
    badge_open_write_code = 0;
    key(KEY_B);
    frames_for(300);

    /* 2. an empty collection: the SD list, a folder, back up, a set chosen, its check */
    printf("-- 2: the SD card's sets\n");
    empty_collection();
    reset_app();
    badges_open(0);
    frames_for(300);
    EXPECT(blist.state == BLIST_OK && blist.col.sets == 0, "an empty collection reads (state %d)", blist.state);
    center_tap(DESIGNS_TILES[0]);
    EXPECT(screen == SCR_BADGE_LIST && bents.count == 3 && bents.folders == 1, "Von SD installieren: /Badges with "
           "one folder and two sets (screen %d, %u entries)", screen, (unsigned)bents.count);
    EXPECT(bents.count == 3 && bents.items[0].folder && !strcmp(bents.items[0].file, "Mehr"), "the folder first");
    EXPECT(rows_of == ROWS_BADGES && !lv.selected && row_rect(0).y == LIST_INSET && row_rect(0).h == FOLDER_H &&
           row_rect(1).y == LIST_INSET + FOLDER_H + LIST_GAP && row_rect(1).h == ROW_H,
           "the rows as the Designs part's folder view's: the folder FOLDER_H high, then the sets, nothing selected");
    a_press();                  /* the first A selects the first row, as in every list */
    EXPECT(lv.selected && lv.cursor == 0 && screen == SCR_BADGE_LIST, "the first A selects the first row");
    a_press();
    EXPECT(screen == SCR_BADGE_LIST && !strcmp(bdir, "sdmc:/Badges/Mehr") && bents.count == 1 && bents.other == 1,
           "A opens the folder: one set, one other file (%s, %u entries)", bdir, (unsigned)bents.count);
    key(KEY_Y);
    frames_for(300);
    EXPECT(!strcmp(bdir, "sdmc:/Badges") && !strcmp(bents.items[lv.cursor].file, "Mehr") && !lv.selected,
           "Y goes up again, the cursor on the folder it came from (%s)", bdir);
    key(KEY_Y);
    frames_for(300);
    int hidden = 0;
    for (size_t k = 0; k < bents.count; k++)
        hidden += !strcmp(bents.items[k].file, "Nintendo 3DS") || bents.items[k].file[0] == '.';
    EXPECT(!strcmp(bdir, "sdmc:/") && bents.count == 4 && bents.folders == 4 && !hidden &&
           !strcmp(bents.items[lv.cursor].file, "Badges"),
           "Y goes on up to the card's root: Badges, Leer, Sammlung, theme-pack; no Nintendo 3DS, no name with a dot "
           "(%s, %u entries, %u folders)", bdir, (unsigned)bents.count, (unsigned)bents.folders);
    EXPECT(!strcmp(stub_foot, "/"), "the footer names the root: %s", stub_foot);
    int started = threads_started;
    key(KEY_Y);
    frames_for(300);
    EXPECT(!strcmp(bdir, "sdmc:/") && threads_started == started, "not above the root: nothing read");
    rect r;
    size_t at = 0;
    while (at < bents.count && strcmp(bents.items[at].file, "Leer"))
        at++;
    if (row_shown(at, &r))
        center_tap(r);
    EXPECT(!strcmp(bdir, "sdmc:/Leer") && bents.count == 0 && !bents.missing && !lv.selected,
           "a tap opens an empty folder (%s, %u entries)", bdir, (unsigned)bents.count);
    a_press();
    key(KEY_DDOWN);
    frames_for(300);
    EXPECT(screen == SCR_BADGE_LIST && !lv.selected && lv.scroll == 0, "in it the keys select and move nothing");
    key(KEY_Y);
    frames_for(300);
    at = 0;
    while (at < bents.count && strcmp(bents.items[at].file, "Badges"))
        at++;
    if (row_shown(at, &r))
        center_tap(r);
    EXPECT(!strcmp(bdir, "sdmc:/Badges"), "back into /Badges from the root (%s)", bdir);
    size_t tiere = 0;
    while (tiere < bents.count && strcmp(bents.items[tiere].file, "Tiere"))
        tiere++;
    EXPECT(row_shown(tiere, &r), "the set's row shows");
    center_tap(r);
    EXPECT(screen == SCR_BADGE_SET && bplan == BPLAN_OK && bpeek.count == 3, "the set's check: it can be installed, "
           "3 badges (screen %d, plan %d, %u)", screen, bplan, (unsigned)bpeek.count);
    key(KEY_B);
    frames_for(300);
    EXPECT(screen == SCR_BADGE_LIST && !bsrc_loaded, "B back to the list, the set let go");

    /* 3. the install: a double tap starts one write, HOME and sleep stay off while it runs and HOME after it */
    printf("-- 3: the write\n");
    center_tap(r);
    uint8_t *home_before = home_now();
    appends_logged = write_lines = home_in_write = sleep_in_write = 0;
    frames_for(POPUP_GUARD_MS);     /* and after the check screen opened */
    busy_frames = 0;
    stub_frame_done = count_busy;
    tap((int)(GO_X + GO_W / 2), (int)(GO_Y + GO_H / 2), 40);
    frame();
    tap((int)(GO_X + GO_W / 2), (int)(GO_Y + GO_H / 2), 40);
    frames_for(600);
    EXPECT(appends_logged == 1, "a double tap on Installieren starts one write (%d)", appends_logged);
    EXPECT(write_lines > 3 && !home_in_write && !sleep_in_write, "no line of the write with HOME or sleep allowed "
           "(%d lines)", write_lines);
    stub_frame_done = NULL;
    EXPECT(busy_frames > 0, "the work screen was drawn (%d frames)", busy_frames);
    EXPECT(screen == SCR_BADGE_DONE && bres.outcome == BFLOW_DONE && bres.count == 3, "Done (screen %d, outcome %d)",
           screen, bres.outcome);
    EXPECT(!same_home(home_before), "HOME's files hold the set");
    EXPECT(apt_home && apt_sleep && bar_drawn(), "R13: after it HOME and sleep are allowed again, the Done screen "
           "shows the B tab");
    free(home_before);

    next_case();

    /* 4. the removal: the question's Cancel writes nothing; Entfernen removes the set, the files are as before */
    printf("-- 4: the removal\n");
    reset_app();
    badges_open(2);
    frames_for(300);
    center_tap(DESIGNS_TILES[2]);
    EXPECT(screen == SCR_BADGE_SETS && blist.col.sets == 1 && blist.col.set[0].removable && blist.undo,
           "Installierte Badges: the set, removable, with its undo record (screen %d, %u sets)", screen,
           (unsigned)blist.col.sets);
    EXPECT(rows_of == ROWS_SETS && row_rect(0).y == DESIGNS_INSET && row_rect(0).h == ROW_H && !lv.selected,
           "the installed sets are the same list: rows by pixels from the top, nothing selected");
    EXPECT(row_shown(0, &r), "its row shows");
    center_tap(r);
    EXPECT(screen == SCR_BADGE_ONE && badges_can_remove(), "the set's screen offers the removal");
    uint8_t *with_set = home_now();
    removals_logged = 0;
    center_tap(badges_remove_rect());
    EXPECT(popup == POP_ASK_SET, "the question opens");
    frames_for(POPUP_GUARD_MS);     /* a touch counts only this long after the question opened or closed */
    EXPECT(stub_popup_removes == 1, "its right button removes (ui_popup_tone)");
    center_tap(ui_popup_button(1, 0));
    EXPECT(popup != POP_ASK_SET && screen == SCR_BADGE_ONE && same_home(with_set) && !removals_logged,
           "its Abbrechen writes nothing");
    frames_for(POPUP_GUARD_MS);
    center_tap(badges_remove_rect());
    frames_for(POPUP_GUARD_MS);     /* a key, as a touch, acts only this long after the question opened (G1-R7) */
    key(KEY_B);
    frames_for(300);
    EXPECT(popup != POP_ASK_SET && screen == SCR_BADGE_ONE && same_home(with_set), "B is Abbrechen too");
    frames_for(POPUP_GUARD_MS);
    center_tap(badges_remove_rect());
    home_in_write = sleep_in_write = 0;
    frames_for(POPUP_GUARD_MS);
    center_tap(ui_popup_button(1, 1));
    frames_for(300);
    EXPECT(removal_end() && bres.outcome == BFLOW_DONE && bres.identical && bremove,
           "Entfernen: removed, both files as before the install (screen %d, outcome %d, identical %d)", screen,
           bres.outcome, bres.identical);
    EXPECT(!home_in_write && !sleep_in_write && apt_home && apt_sleep, "HOME and sleep off during it, allowed after it");
    free(with_set);
    next_case();

    /* R15: the installed set's screen is the installed design's; when a set may be removed and when it is refused
     * is as before (badges_can_remove, badges_why_not), only the reason moved from the screen into the alert behind
     * the tile. Each reason is stated in the listing as badge_read and badge_flow would leave it. */
    printf("-- R15: the set's screen, its tile, the alert or the question\n");
    append_set("Tiere");
    next_case();
    reset_app();
    badges_open(2);
    frames_for(300);
    center_tap(DESIGNS_TILES[2]);
    EXPECT(row_shown(blist.col.sets - 1, &r), "the set's row shows");
    center_tap(r);
    static badge_listing listed;
    listed = blist;
    EXPECT(screen == SCR_BADGE_ONE && badges_can_remove(), "the set appended last, removable");
    blist.manifest_bad = 1;
    one_tile("a damaged manifest", badge_text_result(lang, BADGE_E_MANIFEST));
    blist = listed;
    blist.col.set[1] = blist.col.set[0];
    blist.col.set[0].removable = 0;
    snprintf(blist.col.set[1].name, sizeof blist.col.set[1].name, "Danach");
    blist.col.sets = 2;
    bone = 0;
    one_tile("not the set added last", badge_text(lang, BT_ONLY_LAST));
    blist = listed;
    bone = blist.col.sets - 1;
    blist.col.set[bone].mine = blist.col.set[bone].removable = 0;
    blist.col.remove = BADGE_E_NOT_OURS;
    one_tile("a set of another application", badge_text(lang, BT_NOT_OURS));
    blist = listed;
    blist.col.set[bone].removable = 0;
    blist.col.remove = BADGE_E_PLACED;
    one_tile("badges of it on the HOME Menu", badge_text_result(lang, BADGE_E_PLACED));
    blist = listed;
    blist.col.set[bone].removable = 0;
    blist.col.remove = BADGE_E_LAST;
    one_tile("the collection's end not this set's", badge_text_result(lang, BADGE_E_LAST));
    blist = listed;
    blist.undo = 0;
    one_tile("no undo record", badge_text(lang, BT_NO_UNDO));
    blist = listed;
    blist.undo = 0;
    key(KEY_A);                 /* after a popup the first key shows the cursor */
    frames_for(300);
    EXPECT(ring && popup == POP_NONE, "R15: the first A shows the cursor on the tile");
    key(KEY_A);
    frames_for(300);
    EXPECT(popup == POP_NOTE && !strcmp(stub_popup_para[0], badge_text(lang, BT_NO_UNDO)) && !removals_logged &&
               !badge_writes, "R15: A on the tile opens the same alert");
    key(KEY_B);
    frames_for(300);
    EXPECT(popup == POP_NONE && screen == SCR_BADGE_ONE, "R15: B closes the alert");
    blist = listed;
    one_tile("removable", NULL);
    frames_for(POPUP_GUARD_MS);     /* a touch counts only this long after the question opened or closed */
    center_tap(ui_popup_button(1, 0));
    EXPECT(popup != POP_ASK_SET && screen == SCR_BADGE_ONE && !removals_logged && !badge_writes,
           "R15 removable: Abbrechen, nothing written");
    frames_for(POPUP_GUARD_MS);
    center_tap(badges_remove_rect());
    EXPECT(popup == POP_ASK_SET && !removals_logged, "R15 removable: the question again");
    frames_for(POPUP_GUARD_MS);
    center_tap(ui_popup_button(1, 1));
    frames_for(300);
    EXPECT(removal_end() && bremove && removals_logged == 1 && bres.outcome == BFLOW_DONE,
           "R15 removable: only Entfernen removes it (screen %d, %d removals)", screen, removals_logged);
    next_case();

    /* 5. a write the flow refuses: the problem screen, nothing written, HOME allowed again */
    printf("-- 5: a refusal\n");
    reset_app();
    badges_open(0);
    frames_for(300);
    center_tap(DESIGNS_TILES[0]);
    tiere = 0;
    while (tiere < bents.count && strcmp(bents.items[tiere].file, "Stern.png"))
        tiere++;
    row_shown(tiere, &r);
    center_tap(r);
    EXPECT(screen == SCR_BADGE_SET && bplan == BPLAN_OK, "Stern.png's check");
    frames_for(POPUP_GUARD_MS);
    rename("badge-home", "badge-away");     /* HOME's files gone between the check and the write */
    appends_logged = 0;
    center_tap(GO_BUTTON);
    rename("badge-away", "badge-home");
    EXPECT(appends_logged == 1 && screen == SCR_BADGE_PROBLEM && bres.outcome == BFLOW_REFUSED && !bres.wrote,
           "the store does not open: the problem screen, nothing written (screen %d, outcome %d)", screen,
           bres.outcome);
    EXPECT(apt_home && apt_sleep, "HOME and sleep allowed again");
    key(KEY_B);
    frames_for(300);
    EXPECT(screen == SCR_BADGES, "B back to the part");

    /* 7. nothing interrupts a write: a POWER press (in libctru 2.7.0 it asks for HOME whether HOME is allowed or
     * not) during the write's first write, during its first read and during the read-back, each for an append or a
     * removal, a HOME press let through before the write and an order to close wait until the write is over (job_here
     * does not come back to the main loop's turn with the system); then they are carried out, as anywhere (R13: no
     * restart, nothing holds after the write) */
    printf("-- 7: POWER, HOME and the end during a write\n");
    int jumps = badge_home_jumps;
    badge_jump_at_write = 1;
    append_set("Tiere");
    EXPECT(bres.outcome == BFLOW_DONE && badge_jump && badge_home_jumps == jumps,
           "a POWER press during the append's first write waits; the write goes to its end");
    int reads_append = badge_reads;
    expect_jump("POWER during the append", jumps);
    next_case();
    jumps = badge_home_jumps;
    badge_jump_at_write = 1;
    remove_last();
    EXPECT(bres.outcome == BFLOW_DONE && bremove && badge_jump && badge_home_jumps == jumps,
           "a POWER press during the removal's first write waits");
    int reads_remove = badge_reads;
    expect_jump("POWER during the removal", jumps);
    int list_reads = badge_reads;   /* FB-E: the list read again after a removal reads last; the read-back before it */
    badges_read(BJ_SETS);
    for (int i = 0; i < 100 && job != JOB_NONE; i++)
        frame();
    list_reads = badge_reads - list_reads;
    next_case();

    /* during the read before the backup, and during the read-back */
    jumps = badge_home_jumps;
    badge_jump_at_read = 1;
    append_set("Tiere");
    EXPECT(bres.outcome == BFLOW_DONE && badge_jump && badge_home_jumps == jumps && reads_append > 2,
           "a POWER press during the first read of HOME's files waits; the write goes on (%d reads)", reads_append);
    expect_jump("POWER during the read before the backup", jumps);
    next_case();
    jumps = badge_home_jumps;
    badge_jump_at_read = reads_remove - list_reads;
    remove_last();
    EXPECT(bres.outcome == BFLOW_DONE && badge_jump && badge_home_jumps == jumps && badge_reads == reads_remove,
           "a POWER press during the removal's last read (the read-back) waits");
    expect_jump("POWER during the read-back", jumps);
    next_case();

    jumps = badge_home_jumps;
    to_set("Tiere");
    badge_jump = 1;             /* HOME pressed while it was allowed, just before Installieren */
    center_tap(GO_BUTTON);
    EXPECT(bres.outcome == BFLOW_DONE && badge_home_jumps == jumps, "a HOME press let through before the write waits");
    expect_jump("a HOME press let through before the write", jumps);
    next_case();
    jumps = badge_home_jumps;
    to_set("Stern.png");
    uint8_t *files_was = home_now();
    badge_close = 1;            /* the order comes before Installieren: the write still goes to its end */
    center_tap(GO_BUTTON);
    EXPECT(bres.outcome == BFLOW_DONE && !same_home(files_was) && screen == SCR_BADGE_DONE,
           "an order to close waits until the write is over (outcome %d)", bres.outcome);
    EXPECT(!turns(1) && badge_home_jumps == jumps, "then the application ends");
    free(files_was);
    next_case();
    remove_last();
    next_case();
    remove_last();
    next_case();
    EXPECT(blist_valid, "the collection is empty again");

    /* a write the flow refuses (nothing written): POWER goes to HOME as everywhere */
    jumps = badge_home_jumps;
    to_set("Stern.png");
    files_was = home_now();
    rename("badge-home", "badge-away");
    badge_jump = 1;
    center_tap(GO_BUTTON);
    rename("badge-away", "badge-home");
    EXPECT(bres.outcome == BFLOW_REFUSED && !bres.wrote && apt_home && apt_sleep, "a refusal holds nothing (outcome "
           "%d)", bres.outcome);
    EXPECT(turns(1) && badge_home_jumps == jumps + 1 && same_home(files_was),
           "the POWER press goes to HOME; HOME's files untouched");
    free(files_was);
    next_case();

    /* a write that fails and is put back: the problem screen with HOME and sleep allowed; B, A and its tab lead back */
    for (int how = 0; how < 3; how++) {
        to_set("Stern.png");
        files_was = home_now();
        badge_reads = badge_writes = 0;
        badge_fail_write = 1;
        center_tap(GO_BUTTON);
        int put_back = bres.outcome == BFLOW_RESTORED && bres.wrote && same_home(files_was);
        int allowed = screen == SCR_BADGE_PROBLEM && apt_home && apt_sleep && bar_drawn();
        frames_for(POPUP_GUARD_MS);
        if (how == 0)
            key(KEY_B);
        else if (how == 1)
            key(KEY_A);
        else
            tap(40, SCREEN_H - 8, 48);
        frames_for(300);
        EXPECT(put_back && allowed && turns(5) && screen == SCR_BADGES,
               "a failed write is put back; its problem screen allows HOME and sleep and shows the B tab, %s leads "
               "back into the part (outcome %d, screen %d)", how == 0 ? "B" : how == 1 ? "A" : "the tab",
               bres.outcome, screen);
        free(files_was);
        next_case();
    }

    /* 8. the QR code's way, as the Designs part's: the camera screen, the link screen, the download with its
     * progress and Cancel, then the check and install screens of a set from the SD card; the file goes to /Badges */
    printf("-- 8: the QR code's way\n");
    reset_app();
    badges_open(0);
    frames_for(300);
    center_tap(DESIGNS_TILES[1]);
    EXPECT(screen == SCR_SCAN && bqr, "QR-Code scannen opens the camera screen (screen %d)", screen);
    key(KEY_B);
    frames_for(300);
    EXPECT(screen == SCR_BADGES && !bqr && focus == 1, "B back to the part, on the QR tile (screen %d)", screen);
    center_tap(DESIGNS_TILES[1]);
    EXPECT(server_start() == 0, "the test's web server on 127.0.0.1:%d", server_port);
    qr_found("/Sonne.png");
    EXPECT(strstr(stub_foot, "/Badges") && !strstr(stub_foot, "Themes"), "the link screen names /Badges: %s",
           stub_foot);
    fake_net_fail = 1;
    key(KEY_A);
    frames_for(200);
    key(KEY_A);
    frames_for(500);
    fake_net_fail = 0;
    EXPECT(screen == SCR_PROBLEM && dl.status == DL_NETWORK && job == JOB_NONE && apt_home && apt_sleep,
           "no network: the problem screen (screen %d, status %d)", screen, dl.status);
    key(KEY_B);
    frames_for(300);
    EXPECT(screen == SCR_BADGES && !bqr, "its B leads back to the part (screen %d)", screen);

    center_tap(DESIGNS_TILES[1]);
    qr_found("/Sonne.png");
    int sets_was = badges_files();
    worker_frames = 20;
    key(KEY_A);
    frames_for(200);
    key(KEY_A);
    frames_for(100);
    EXPECT(job == JOB_DOWNLOAD && strstr(stub_foot, "/Badges") && !apt_home && !apt_sleep, "the download: its "
           "progress names /Badges, HOME and sleep off meanwhile (job %d, %s)", job, stub_foot);
    for (int i = 0; i < 100 && job != JOB_BADGES; i++)
        frame();
    frames_for(50);
    EXPECT(job == JOB_BADGES && screen == SCR_URL && strstr(stub_foot, "/Badges") && !strstr(stub_foot, "Themes"),
           "then the set's check on the link screen, which still names /Badges (job %d, %s)", job, stub_foot);
    frames_for(900);
    worker_frames = 0;
    frames_for(300);
    struct stat st;
    EXPECT(dl.status == DL_OK && !strcmp(dl.file, "Sonne.png") && stat("sdmc:/Badges/Sonne.png", &st) == 0 &&
               badges_files() == sets_was + 1 && apt_home && apt_sleep,
           "the set is in /Badges as Sonne.png (status %d, %s)", dl.status, dl.file);
    EXPECT(screen == SCR_BADGE_SET && bplan == BPLAN_OK && bpeek.count == 1 && !bqr, "then the set's check "
           "screen, as from the SD card (screen %d, plan %d, %u badges)", screen, bplan, (unsigned)bpeek.count);
    key(KEY_B);
    frames_for(300);
    EXPECT(screen == SCR_BADGES && focus == 1, "its B back to the part (screen %d)", screen);
    center_tap(DESIGNS_TILES[1]);
    qr_found("/page");
    sets_was = badges_files();
    key(KEY_A);
    frames_for(200);
    key(KEY_A);
    frames_for(600);
    EXPECT(screen == SCR_PROBLEM && dl.status == DL_NOT_THEME && strstr(body, "kein Badge-Set") &&
               badges_files() == sets_was && stat(PART_PATH, &st) != 0,
           "a web page: the problem screen says it is no badge set, nothing left on the card (status %d)", dl.status);
    key(KEY_B);
    frames_for(300);
    center_tap(DESIGNS_TILES[1]);
    qr_found("/Kaputt.png");
    key(KEY_A);
    frames_for(200);
    key(KEY_A);
    frames_for(600);
    EXPECT(screen == SCR_PROBLEM && dl.status == DL_BAD_THEME && dl.detail != BADGE_OK &&
               strstr(body, badge_text_result(1, dl.detail)) && badges_files() == sets_was && stat(PART_PATH, &st) != 0,
           "a damaged PNG: the problem screen says what the core found (%s), nothing left on the card",
           badge_result_name(dl.detail));
    key(KEY_B);
    frames_for(300);
    center_tap(DESIGNS_TILES[1]);
    qr_found("/Sonne.png");
    worker_frames = 30;
    key(KEY_A);
    frames_for(200);
    key(KEY_A);
    frames_for(100);
    key(KEY_B);                 /* Abbrechen while it runs */
    frames_for(900);
    worker_frames = 0;
    frames_for(300);
    EXPECT(dl.status == DL_CANCELLED && screen == SCR_PROBLEM && badges_files() == sets_was &&
               stat(PART_PATH, &st) != 0 && apt_home,
           "Abbrechen: nothing on the card, the problem screen (status %d, screen %d)", dl.status, screen);
    key(KEY_B);
    frames_for(300);
    EXPECT(screen == SCR_BADGES && !bqr, "back to the part");

    /* the downloaded set installs as one from the SD card */
    center_tap(DESIGNS_TILES[1]);
    qr_found("/Sonne.png");
    key(KEY_A);
    frames_for(200);
    key(KEY_A);
    frames_for(900);
    EXPECT(screen == SCR_BADGE_SET && bplan == BPLAN_OK && !strcmp(dl.file, "Sonne (2).png"), "a second download "
           "of it: Sonne (2).png, its check screen (screen %d, %s)", screen, dl.file);
    center_tap(GO_BUTTON);
    EXPECT(screen == SCR_BADGE_DONE && bres.outcome == BFLOW_DONE && !strcmp(bres.name, "Sonne (2)"), "installed "
           "(screen %d, %s)", screen, bres.name);
    key(KEY_B);
    frames_for(300);
    EXPECT(screen == SCR_BADGES, "B leads back into the part (screen %d)", screen);
    next_case();
    remove_last();
    EXPECT(bres.outcome == BFLOW_DONE && bremove, "and removed again");
    next_case();
    server_stop();

    /* the Designs part's own QR way is as it was: the link screen names /Themes */
    bqr = 0;
    snprintf(url, sizeof url, "https://example.org/a.zip");
    text_shown(url_shown, sizeof url_shown, url);
    screen = SCR_URL;
    frames_for(300);
    EXPECT(strstr(stub_foot, "/Themes") != NULL, "the Designs part's link screen names /Themes: %s", stub_foot);
    key(KEY_B);
    frames_for(300);

    /* R11: the part comes in once, whether the look at HOME's files returns at once or late, works or fails */
    printf("-- R11: one entrance of the part\n");
    badges_entrance(0, 0);
    badges_entrance(40, 0);
    badges_entrance(40, 1);
    badges_entrance(0, 1);

    /* 9. a long list elsewhere on the card (R14, R16): the D-pad glides it by pixels, the stylus drags it 1 to 1 and
     * it glides on; a drag opens nothing, a tap at rest opens its row; rows are cut at the edges, the bar shows */
    printf("-- 9: a long list\n");
    reset_app();
    badges_open(0);
    frames_for(300);
    center_tap(DESIGNS_TILES[0]);
    key(KEY_Y);
    frames_for(300);
    at = 0;
    while (at < bents.count && strcmp(bents.items[at].file, "Sammlung"))
        at++;
    if (row_shown(at, &r))
        center_tap(r);
    EXPECT(!strcmp(bdir, "sdmc:/Sammlung") && bents.count == 14 && !bents.folders && bents.other == 1 &&
           max_scroll() > 0, "Sammlung: 14 sets and a note (%s, %u entries)", bdir, (unsigned)bents.count);
    key(KEY_DDOWN);
    EXPECT(lv.selected && lv.cursor == 0 && lv.scroll == 0, "the first press selects the first row");
    for (int k = 0; k < 5; k++)
        key(KEY_DDOWN);
    int glide = 0, between = 0;
    while (lv.follow && glide < 200) {
        frame();
        glide++;
        between += fmodf(lv.scroll, ROW_H + LIST_GAP) != 0;
    }
    scroll_shape sh = shape();
    EXPECT(lv.cursor == 5 && glide > 2 && between > 0 && scroll_whole(&sh, &lv, 5) && lv.scroll > 0,
           "five down: the list glides after the cursor by pixels, no page of rows (%d frames, %.0f px)", glide,
           lv.scroll);
    int cut = 0, bars = 0;
    for (int k = 0; k < stub_marks; k++) {
        rect m = stub_mark_at[k];
        cut += stub_mark_kind[k] == 'e' &&
               ((m.y < 0 && m.y + m.h > 0) || (m.y < LIST_BOTTOM && m.y + m.h > LIST_BOTTOM));
        bars += stub_mark_kind[k] == 'o' && m.x == SCROLL_X;
    }
    EXPECT(cut > 0 && bars == 1, "rows cut at the edges (%d), the scroll bar drawn (%d)", cut, bars);
    float dragged_from = lv.scroll;
    started = threads_started;
    stylus(150, 160);
    frame();
    for (int y = 158; y >= 100; y -= 2) {
        stylus(150, y);
        frame();
    }
    EXPECT(lv.dragging && !lv.selected && fabsf(lv.scroll - (dragged_from + 60)) < 0.01f,
           "the stylus drags the rows 1 to 1 (%.0f, from %.0f)", lv.scroll, dragged_from);
    lift();
    frame();
    float lifted = lv.scroll;
    frames_for(1500);
    EXPECT(lv.scroll > lifted && lv.scroll <= max_scroll() && !scroll_moving(&lv) && screen == SCR_BADGE_LIST &&
           threads_started == started && pending == HIT_NONE,
           "lifted, it glides on and stops inside the list; the drag opened nothing (%.0f, lifted at %.0f)", lv.scroll,
           lifted);
    for (int stroke = 0; stroke < 3; stroke++) {
        stylus(150, 40);
        frame();
        for (int y = 40; y <= 220; y += 30) {
            stylus(150, y);
            frame();
        }
        lift();
        frame();
    }
    frames_for(1500);
    EXPECT(lv.scroll == 0 && !scroll_moving(&lv), "flung down past the top: it stops at the top (%.0f)", lv.scroll);
    size_t first = (size_t)first_shown();
    float rest = lv.scroll;
    center_tap(row_rect(first));
    EXPECT(screen == SCR_BADGE_SET && !strcmp(bsrc_path, "sdmc:/Sammlung/Set 00.png") && first == 0,
           "a tap at rest opens its row (screen %d, %s)", screen, bsrc_path);
    key(KEY_B);
    frames_for(300);
    EXPECT(screen == SCR_BADGE_LIST && lv.scroll == rest && lv.cursor == first, "B: back where the list was");

    /* 10. no write starts without a press that began after its question showed whole (the reviewer's finding of
     * d9bfed9: the remove tile, y 82 to 192, lies under the question's Entfernen, y 187 to 228, and the check
     * screen's Installieren, y 82 to 166, under the list's rows): a touch counts only POPUP_GUARD_MS after the
     * question or the check screen opened, as on the Designs part's popups, and none that was down then */
    printf("-- 10: a press only after the question showed\n");
    reset_app();
    badges_open(0);
    frames_for(300);
    center_tap(DESIGNS_TILES[0]);
    int on_x = 0, on_y = 0;
    size_t on = 0;
    while (on < bents.count && (bents.items[on].folder || !row_shown(on, &r) || !both_at(r, GO_FACTS, &on_x, &on_y)))
        on++;
    char on_file[BADGE_ENTRY_NAME];
    snprintf(on_file, sizeof on_file, "%s", on < bents.count ? bents.items[on].file : "");
    EXPECT(screen == SCR_BADGE_LIST && on < bents.count, "/Badges: a set's row under where Installieren will be "
           "(%d, %d)", on_x, on_y);
    appends_logged = badge_writes = 0;
    tap(on_x, on_y, 48);
    frames_for(80);
    tap(on_x, on_y, 48);
    frames_for(600);
    EXPECT(screen == SCR_BADGE_SET && bplan == BPLAN_OK && !appends_logged && !badge_writes,
           "a double tap on the row, 80 ms apart: its check screen, nothing written (screen %d, %d writes)", screen,
           appends_logged);
    key(KEY_B);
    frames_for(300);
    check_from_row(on_x, on_y);
    stylus((int)(GO_X + GO_W / 2), (int)(GO_FACTS_Y + GO_H / 2));
    frames_for(POPUP_GUARD_MS + 100);
    lift();
    frames_for(600);
    EXPECT(screen == SCR_BADGE_SET && !appends_logged && !badge_writes, "down on Installieren as the check screen "
           "came in, held past the guard: nothing written (screen %d)", screen);
    key(KEY_B);
    frames_for(300);
    check_from_row(on_x, on_y);
    for (int y = 30; y <= GO_FACTS_Y + GO_H / 2; y += 6) {
        stylus(BOTTOM_W / 2, y);
        frame();
    }
    frames_for(POPUP_GUARD_MS);
    lift();
    frames_for(600);
    EXPECT(screen == SCR_BADGE_SET && !appends_logged && !badge_writes, "down on the facts as it came in, slid onto "
           "Installieren and lifted there: nothing written (screen %d)", screen);
    key(KEY_B);
    frames_for(300);
    lv.selected = 1;
    lv.cursor = on;
    key(KEY_A);
    frames_for(150);
    EXPECT(screen == SCR_BADGE_SET, "A on the row: its check screen (screen %d)", screen);
    key(KEY_A);
    frames_for(300);
    EXPECT(screen == SCR_BADGE_SET && ring && !appends_logged && !badge_writes,
           "A again at once only shows the cursor on Installieren, as on the Designs part's check (ring %d)", ring);
    key(KEY_A);
    frames_for(600);
    EXPECT(screen == SCR_BADGE_DONE && appends_logged == 1 && bres.outcome == BFLOW_DONE,
           "and the next A installs (screen %d, %d writes)", screen, appends_logged);
    next_case();

    reset_app();
    badges_open(2);
    frames_for(300);
    center_tap(DESIGNS_TILES[2]);
    EXPECT(row_shown(blist.col.sets - 1, &r), "the set's row shows");
    center_tap(r);
    frames_for(POPUP_GUARD_MS);
    EXPECT(screen == SCR_BADGE_ONE && badges_can_remove(), "the set appended last, removable");
    rect remove_low = ui_popup_button(1, 1);
    EXPECT(ui_hit(badges_remove_rect(), 200, 189) && ui_hit(remove_low, 200, 189), "(200, 189) is on the tile and "
           "on the question's Entfernen");
    removals_logged = badge_writes = 0;
    tap(200, 189, 48);
    frames_for(80);
    tap(200, 189, 48);
    frames_for(600);
    EXPECT(popup == POP_ASK_SET && screen == SCR_BADGE_ONE && !removals_logged && !badge_writes,
           "a double tap low on the tile, 80 ms apart: the question stays open, nothing removed (screen %d, %d "
           "removals)", screen, removals_logged);
    center_tap(ui_popup_button(1, 0));
    EXPECT(popup != POP_ASK_SET && screen == SCR_BADGE_ONE && !removals_logged, "Abbrechen closes it");
    tap(200, 189, 48);
    frames_for(80);
    EXPECT(popup != POP_ASK_SET, "a tap on the tile right after Abbrechen opens nothing, as after a popup closed");
    frames_for(POPUP_GUARD_MS);
    tap(200, 189, 48);
    frames_for(80);
    EXPECT(popup == POP_ASK_SET, "the question");
    stylus(200, 200);
    frames_for(POPUP_GUARD_MS + 100);
    lift();
    frames_for(600);
    EXPECT(popup == POP_ASK_SET && !removals_logged && !badge_writes, "down on Entfernen as the question opened, held past the "
           "guard: nothing removed");
    center_tap(ui_popup_button(1, 0));
    frames_for(POPUP_GUARD_MS);
    tap(200, 120, 48);
    frames_for(80);
    for (int y = 120; y <= 205; y += 5) {
        stylus(200, y);
        frame();
    }
    frames_for(POPUP_GUARD_MS);
    lift();
    frames_for(600);
    EXPECT(popup == POP_ASK_SET && !removals_logged && !badge_writes, "down inside the guard, slid onto Entfernen and lifted "
           "there: nothing removed");
    center_tap(ui_popup_button(1, 0));
    frames_for(POPUP_GUARD_MS);
    stylus(40, 40);
    frame();
    key(KEY_A);                 /* the cursor on the tile */
    key(KEY_A);                 /* the question, the stylus still down */
    frames_for(200);
    EXPECT(popup == POP_ASK_SET, "A opens the question while the stylus is down");
    for (int y = 40; y <= 205; y += 5) {
        stylus(40 + (y - 40), y);
        frame();
    }
    frames_for(POPUP_GUARD_MS);
    lift();
    frames_for(600);
    EXPECT(popup == POP_ASK_SET && !removals_logged && !badge_writes, "the stylus held through its opening, slid onto Entfernen "
           "and lifted there: nothing removed");
    key(KEY_A);
    frames_for(300);
    EXPECT(popup == POP_ASK_SET && pop_focus.ring && pop_focus.at == 0 && !removals_logged,
           "A in the question shows its cursor on Abbrechen, as in the design's");
    key(KEY_A);
    frames_for(300);
    EXPECT(popup != POP_ASK_SET && screen == SCR_BADGE_ONE && !removals_logged && !badge_writes,
           "A again presses Abbrechen, where its cursor starts");
    frames_for(POPUP_GUARD_MS);
    center_tap(badges_remove_rect());
    frames_for(POPUP_GUARD_MS);
    center_tap(remove_low);
    frames_for(300);
    EXPECT(removal_end() && bremove && removals_logged == 1 && bres.outcome == BFLOW_DONE,
           "a press on Entfernen after the guard removes the set (screen %d, %d removals)", screen, removals_logged);
    next_case();
    to_set(on_file);
    appends_logged = 0;
    center_tap(GO_FACTS);
    frames_for(300);
    EXPECT(screen == SCR_BADGE_DONE && appends_logged == 1 && bres.outcome == BFLOW_DONE,
           "a tap on Installieren after the guard installs (screen %d, %d writes)", screen, appends_logged);
    next_case();
    remove_last();
    frames_for(300);
    EXPECT(removal_end() && bremove && bres.outcome == BFLOW_DONE, "and it is removed again");
    next_case();

    /* the Done screen's tab takes no press begun during the write (the second reviewer's finding on 9c1a90d, the
     * same on the Designs part's "Installiert"), nor the problem screen's after a write that was put back */
    for (int fail = 0; fail < 2; fail++) {
        to_set(on_file);
        int target = fail ? SCR_BADGE_PROBLEM : SCR_BADGE_DONE;
        badge_writes = 0;
        badge_fail_write = fail;    /* its first write fails: the backup is put back */
        write_x = 40, write_y = SCREEN_H - 8, write_frames = 0;    /* where the screen's tab will be */
        worker_frames = 20;
        stub_each_frame = write_tick;
        tap((int)(GO_X + GO_W / 2), (int)(GO_FACTS_Y + GO_H / 2), 48);
        for (int i = 0; i < 400 && screen != target; i++)
            frame();
        stub_each_frame = held_tick;
        worker_frames = 0;
        int down_then = (sim_keys & KEY_TOUCH) != 0;
        frames_for(150);
        lift();
        frames_for(600);
        EXPECT(screen == target && down_then, "the stylus down on the %s screen's tab during the write, lifted "
               "there: it stays (screen %d)", fail ? "problem" : "Done", screen);
        frames_for(POPUP_GUARD_MS);
        tap(40, SCREEN_H - 8, 48);
        frames_for(300);
        EXPECT(screen == SCR_BADGES, "a tap on it after the guard leads back into the part (screen %d)", screen);
        next_case();
        if (!fail) {            /* the set off again; a write put back left nothing */
            remove_last();
            frames_for(300);
            next_case();
        }
    }

    /* the tab before the row under it (the reviewer's finding of d9bfed9: in /Sammlung a tap at (60, 225) on
     * Zurück opened "Set 03.png"), on both lists */
    printf("-- 10: Zurück over the rows\n");
    reset_app();
    badges_open(0);
    frames_for(300);
    center_tap(DESIGNS_TILES[0]);
    key(KEY_Y);
    frames_for(300);
    at = 0;
    while (at < bents.count && strcmp(bents.items[at].file, "Sammlung"))
        at++;
    if (row_shown(at, &r))
        center_tap(r);
    lv.scroll = 0;
    frames_for(300);
    EXPECT(!strcmp(bdir, "sdmc:/Sammlung") && row_at(60, 225) >= HIT_ROW0 && ui_tab_hit(0, 60, 225),
           "/Sammlung: a row lies under Zurück at (60, 225) (%d)", row_at(60, 225));
    started = threads_started;
    tap(60, 225, 48);
    frames_for(600);
    EXPECT(screen == SCR_BADGES && threads_started == started, "a tap there is Zurück: the part, nothing read "
           "(screen %d)", screen);
    static badge_listing many;
    many = blist;
    for (uint32_t k = 1; k < 8; k++)
        many.col.set[k] = many.col.set[0];
    many.col.sets = 8;
    many.icons = NULL;
    static badge_listing kept;
    kept = blist;
    blist = many;
    blist_valid = 1;
    badges_rows_from(ROWS_SETS, 0);
    screen = SCR_BADGE_SETS;
    frames_for(300);
    EXPECT(row_at(60, 225) >= HIT_ROW0 && ui_tab_hit(0, 60, 225), "Installierte Badges with 8 sets: a row lies "
           "under Zurück at (60, 225) (%d)", row_at(60, 225));
    tap(60, 225, 48);
    frames_for(600);
    EXPECT(screen == SCR_BADGES, "a tap there is Zurück: the part (screen %d)", screen);
    blist = kept;
    bone = 0;

    /* G1-R5 (C43): removing a set runs as removing a design: after the question the set's screen stays on the top
     * screen, its bottom screen goes back into the background and the loader takes its place there with the step
     * under way as its line, then the Done or problem screen; installing keeps the vessel */
    printf("-- G1-R5: removing a set as a design\n");
    next_case();
    to_set("Stern.png");
    watch_write_begin();
    worker_frames = 20;
    center_tap(GO_BUTTON);
    for (int i = 0; i < 600 && screen != SCR_BADGE_DONE; i++)
        frame();
    watch_write_end();
    EXPECT(screen == SCR_BADGE_DONE && !bremove && wf_before == 1 && wf_job >= 10 && wf_after > 0 &&
               wf_pour == wf_frames && !wf_top && !wf_ring,
           "installing: the vessel in %d of %d frames (%d before the job, %d in it, %d of its end after it), a loader "
           "on top in %d, on the bottom in %d", wf_pour, wf_frames, wf_before, wf_job, wf_after, wf_top, wf_ring);
    next_case();
    remove_ask();
    watch_write_begin();
    center_tap(ui_popup_button(1, 1));
    expect_removal("removing", 1);
    EXPECT(bremove && bres.outcome == BFLOW_DONE && removals_logged > 0, "... and the set was removed (outcome %d)",
           bres.outcome);
    frame_pieces set_whole = wf_whole, design_whole;
    design_removal_frame(&design_whole, set_whole.ring);
    int differ = pieces_differ(&set_whole, &design_whole);
    char set_text[1600], design_text[1600];
    EXPECT(set_whole.count > 0 && differ < 0, "a set's removal and a design's, the loader whole: the same pieces in "
           "the same places on both screens, texts and pictures aside (first difference at %d)\n      set:    %s\n"
           "      design: %s", differ, pieces_text(&set_whole, set_text, sizeof set_text),
           pieces_text(&design_whole, design_text, sizeof design_text));
    next_case();
    to_set("Stern.png");
    center_tap(GO_BUTTON);
    next_case();
    badge_fail_write = 1;
    remove_ask();
    watch_write_begin();
    center_tap(ui_popup_button(1, 1));
    expect_removal("removing, the write fails and is put back", 0);
    EXPECT(bres.outcome == BFLOW_RESTORED, "... put back (outcome %d)", bres.outcome);
    next_case();
    remove_last();
    next_case();
    /* in each of the write's steps, also writing the backup back: the frame a removal draws (main.c, draw) with the
     * loader whole on the bottom screen and that step's line, none on the top, no vessel */
    int each = 0;
    for (int k = BSTEP_READ; k <= BSTEP_RESTORE; k++) {
        bremove = bwriting = 1;
        bstep = k;
        screen = SCR_BADGE_ONE;
        job = JOB_BADGE_WRITE;
        job_over = 0;
        job_over_at = 0;
        job_since = osGetTime() - LOADER_WAIT_MS - LOADER_FADE_MS - 100;
        int pour_was = stub_pour_frames;
        draw();
        each += stub_loader && stub_loader_shown >= 1 && !stub_top_loader && stub_pour_frames == pour_was &&
                !strcmp(stub_loader_line, badge_text(lang, BT_WORK_READ + k));
    }
    EXPECT(each == BSTEP_RESTORE - BSTEP_READ + 1, "removing, step by step: the loader on the bottom screen with the "
           "step's line, none on top, no vessel, in %d of %d steps", each, BSTEP_RESTORE - BSTEP_READ + 1);
    for (int l = 0; l < 2; l++) {
        lang = l;
        bstep = BSTEP_WRITE;
        draw();
        EXPECT(stub_loader && !strcmp(stub_loader_line, l ? "Wird geschrieben" : "Writing"),
               "the line in %s: \"%s\"", l ? "German" : "English", stub_loader_line);
    }
    lang = 1;
    job = JOB_NONE;
    bwriting = 0;
    screen = SCR_BADGES;

    /* G1-R5: the Done and the problem screen after a removal take no press begun before they showed: the stylus
     * down on their tab during the write and held through their opening, or down elsewhere and slid onto it, a
     * double tap whose second lands on the tab, A down during the write */
    printf("-- G1-R5: the screens after a removal take no press begun before them\n");
    next_case();
    to_set("Stern.png");
    center_tap(GO_BUTTON);
    for (int fail = 0; fail < 2; fail++) {
        const char *name = fail ? "problem screen's tab" : "note's Okay (FB-E)";
        int target = fail ? SCR_BADGE_PROBLEM : SCR_REMOVED, back = fail ? SCR_BADGES : SCR_BADGE_SETS;
        rect okay = ui_popup_button(0, 0);
        int tx = fail ? 40 : (int)(okay.x + okay.w / 2), ty = fail ? SCREEN_H - 8 : (int)(okay.y + okay.h / 2);
        int down = remove_pressed_through(fail, tx, ty, 0);
        frames_for(150);
        lift();
        frames_for(600);
        EXPECT(down && where() == target, "the stylus down on the %s during the removal, held through its "
               "opening, lifted there: it stays (screen %d)", name, where());
        frames_for(POPUP_GUARD_MS);
        tap(tx, ty, 48);
        frames_for(300);
        EXPECT(where() == back, "a tap on it after the guard leads on: into the part, or the list (screen %d)",
               where());
        if (!fail) {
            to_set("Stern.png");
            center_tap(GO_BUTTON);
        }
        down = remove_pressed_through(fail, 200, 120, 0);
        for (int y = 120; y <= SCREEN_H - 8; y += 5) {
            stylus(200 - (y - 120) * 160 / (SCREEN_H - 128), y);
            frame();
        }
        lift();
        frames_for(600);
        EXPECT(down && where() == target, "the stylus down during the removal, slid down past the %s and "
               "lifted: it stays (screen %d)", name, where());
        frames_for(POPUP_GUARD_MS);
        key(KEY_B);
        frames_for(300);
        if (!fail) {
            to_set("Stern.png");
            center_tap(GO_BUTTON);
        }
        next_case();
        remove_ask();
        badge_fail_write = fail;
        rect b = ui_popup_button(1, 1);
        tap((int)(b.x + b.w / 2), (int)(b.y + b.h / 2), 48);
        frames_for(80);
        tap(tx, ty, 48);
        for (int i = 0; i < 400 && where() != target; i++)
            frame();
        frames_for(600);
        EXPECT(where() == target, "a double tap, Entfernen and 80 ms later the %s: it stays (screen %d)", name,
               where());
        frames_for(POPUP_GUARD_MS);
        key(KEY_B);
        frames_for(300);
        if (!fail) {
            to_set("Stern.png");
            center_tap(GO_BUTTON);
        }
        /* A is no touch: the screens' guard (popup.h) holds touches only, so A down from the write on may lead back
         * as A does there (R13); it writes nothing */
        down = remove_pressed_through(fail, 0, 0, KEY_A);
        int writes_were = badge_writes, removals_were = removals_logged;
        frames_for(150);
        sim_keys &= ~KEY_A;
        frames_for(600);
        EXPECT(down && badge_writes == writes_were && removals_logged == removals_were &&
                   (where() == target || where() == back),
               "A down during the removal, held through the %s's opening and let go: nothing written (%d "
               "writes; screen %d)", name, badge_writes - writes_were, where());
        frames_for(POPUP_GUARD_MS);
        if (where() == target)
            key(KEY_B);
        frames_for(300);
        if (!fail) {
            to_set("Stern.png");
            center_tap(GO_BUTTON);
        }
        next_case();
    }
    /* G1-R5b: a key (A, B) down during a set's write, after a removal and after an install, and held as the Done or
     * problem screen opens presses nothing there, as a touch; let go and pressed again it leads back (R13) */
    printf("-- G1-R5b: a key held through a set's write\n");
    for (int fail = 0; fail < 2; fail++)
        for (int remove = 1; remove >= 0; remove--)
            for (int k = 0; k < 2; k++) {
                u32 held_key = k ? KEY_B : KEY_A;
                int target = fail ? SCR_BADGE_PROBLEM : remove ? SCR_REMOVED : SCR_BADGE_DONE;
                int down = remove ? remove_pressed_through(fail, 0, 0, held_key) : append_pressed_through(fail, held_key);
                int again = held_until_again(held_key, target);
                EXPECT(down && again && where() == (!fail && remove ? SCR_BADGE_SETS : SCR_BADGES), "%s down during "
                       "the %s, held as the %s opens: it stays until %s is let go and pressed again, then leads on "
                       "(screen %d)", k ? "B" : "A", remove ? "removal" : "install",
                       fail ? "problem screen" : remove ? "note (FB-E)" : "Done screen", k ? "B" : "A", where());
                frames_for(300);
                if (!fail && remove) {
                    to_set("Stern.png");
                    center_tap(GO_BUTTON);
                    frames_for(300);
                } else if (!fail) {
                    remove_last();
                    frames_for(300);
                }
                next_case();
            }
    remove_last();
    frames_for(300);
    next_case();

    /* SND-1 (C163): a set's install sounds the drop for every piece of its picture that lands, as a design's
     * (test_main): once in each frame with a landing and in no other. A write that fails is put back and freezes the
     * picture (BSTEP_RESTORE): the pieces in the air never land, and no drop sounds after it. The write is held 150
     * frames with its level rising meanwhile, so pieces land during it */
    printf("-- SND-1: a drop for every piece of a set's install that lands\n");
    for (int fail = 0; fail < 2; fail++) {
        to_set("Tiere");
        badge_reads = badge_writes = 0;
        badge_fail_write = fail;
        worker_frames = 150;
        drops_job = JOB_BADGE_WRITE;
        drops_ramp = 0.004f;
        drops_fail_screen = fail ? SCR_BADGE_PROBLEM : 0;
        drops_begin(&bpour);
        center_tap(GO_BUTTON);
        int target = fail ? SCR_BADGE_PROBLEM : SCR_BADGE_DONE;
        for (int i = 0; i < 400 && screen != target; i++)
            frame();
        worker_frames = 0;
        int reached = screen == target;
        frames_for(1500);
        if (fail) {     /* the putting back drawn for a second, as when it takes that long: the picture stays frozen */
            int was_step = bstep;
            bwriting = 1;
            bstep = BSTEP_RESTORE;
            for (int k = 0; k < 60; k++)
                badges_work_frame();
            bstep = was_step;
            bwriting = 0;
        }
        int d_failed = drops_failed, d_in_air = drops_in_air, d_after = drops_after_fail;
        drops_end();
        if (fail)
            EXPECT(reached && d_failed && d_in_air > 0 && bpour.how == POUR_FAILED && !d_after &&
                   drops_heard == drops_frames && !drops_wrong,
                   "a set's write that fails: %d drops for the %d pieces that landed before; %d pieces in the air never "
                   "land, also not in a second of the putting back drawn, and no drop sounds after it (%d)",
                   drops_heard, drops_pieces, d_in_air, d_after);
        else
            EXPECT(reached && drops_pieces == bpour.total && bpour.landed == bpour.total &&
                   drops_heard == drops_pieces - drops_shared && drops_frames == drops_heard && !drops_wrong &&
                   !drops_before_first && drops_late < frame_ms,
                   "a set's install: %d pieces landed, %d drops (%d pieces shared a frame), one in each frame with a "
                   "landing and in no other (%d wrong), the latest %llu ms after its landing, none before the first",
                   drops_pieces, drops_heard, drops_shared, drops_wrong, (unsigned long long)drops_late);
        if (!fail) {
            key(KEY_B);
            frames_for(300);
            remove_last();
            frames_for(300);
        }
        next_case();
    }

    /* G1-R6 (C43): the set's question opens and closes as the design's, through main.c's popup: its first frame is
     * not all of it and it grows by the clock; a press during its opening starts nothing; its Entfernen closes it
     * and the removal starts once it has faded out, as the design's delete (46 in test_main); a second tap and a
     * key during the fading start nothing; one removal */
    printf("-- G1-R6: the set's question as the design's\n");
    to_set("Stern.png");
    center_tap(GO_BUTTON);
    frames_for(300);
    EXPECT(screen == SCR_BADGE_DONE && bres.outcome == BFLOW_DONE, "G1-R6: a set appended to remove (screen %d)",
           screen);
    next_case();
    remove_ask();
    key(KEY_B);
    frames_for(POPUP_GUARD_MS + 300);
    removals_logged = badge_writes = 0;
    float at_first = ask_by_tap(), was = at_first;
    int in_frames = 1, growing = at_first > 0;
    for (int i = 0; i < 40 && was < 1.0f; i++) {
        frame();
        float now = set_asked();
        growing &= now > was;
        was = now;
        in_frames++;
    }
    EXPECT(at_first > 0 && at_first < 0.5f && growing && was == 1.0f && in_frames >= POPUP_IN_MS / 16 - 1 &&
               in_frames <= POPUP_IN_MS / 16 + 2,
           "the question's first frame is not all of it (%.2f); it grows in %d frames, each more of it, to all of it",
           (double)at_first, in_frames);
    frames_for(POPUP_GUARD_MS);     /* B closes it only from then on (G1-R7) */
    key(KEY_B);
    frames_for(POPUP_GUARD_MS + 300);
    rect yes = ui_popup_button(1, 1);
    ask_by_tap();
    stylus((int)(yes.x + yes.w / 2), (int)(yes.y + yes.h / 2));     /* down on Entfernen as it opens */
    frames_for(POPUP_IN_MS + 50);
    lift();
    frames_for(600);
    int tap_none = set_asked() == 1.0f && !removals_logged && !badge_writes;
    key(KEY_B);
    frames_for(POPUP_GUARD_MS + 300);
    key(KEY_A);                 /* the cursor on the tile */
    sim_keys |= KEY_A;          /* A on it: the question */
    frame();
    sim_keys &= ~KEY_A;
    for (int i = 0; i < 40 && set_asked() < 0; i++)
        frame();
    float keys_at = set_asked();
    key(KEY_DRIGHT);            /* the D-pad and A while it opens */
    key(KEY_A);
    frames_for(600);
    EXPECT(tap_none && keys_at >= 0 && keys_at < 1.0f && !removals_logged && !badge_writes && screen == SCR_BADGE_ONE,
           "a touch on Entfernen down as the question opens, and the D-pad and A pressed as it opens (at %.2f): "
           "nothing removed (%d removals, %d writes)", (double)keys_at, removals_logged, badge_writes);
    if (set_asked() >= 0)
        key(KEY_B);
    frames_for(POPUP_GUARD_MS + 300);
    ask_by_tap();
    frames_for(POPUP_GUARD_MS);
    quick_tap(yes);
    int fade_frames = 0, fading = 1;
    was = 1.0f;
    for (int i = 0; i < 60 && !removals_logged; i++) {
        frame();
        if (removals_logged)
            break;
        float now = set_asked();
        if (now < 1.0f) {
            fading &= now >= 0 && now < was;
            fade_frames++;
        }
        was = now;
    }
    frames_for(600);
    EXPECT(removals_logged == 1 && fading && fade_frames >= POPUP_OUT_MS / 16 - 1 &&
               fade_frames <= POPUP_OUT_MS / 16 + 1 && removal_end(),
           "Entfernen: the removal starts after %d frames of the question fading, each less of it; one removal, the "
           "list with its note (%d, screen %d)", fade_frames, removals_logged, screen);
    next_case();
    to_set("Stern.png");
    center_tap(GO_BUTTON);
    frames_for(300);
    next_case();
    remove_ask();
    removals_logged = badge_writes = 0;
    quick_tap(yes);
    frames_for(80);
    quick_tap(yes);             /* the second tap of a double tap, as the question fades */
    key(KEY_A);                 /* and A */
    frames_for(600);
    EXPECT(removals_logged == 1 && removal_end() && bremove && bres.outcome == BFLOW_DONE,
           "a double tap on Entfernen, 80 ms apart, and A as the question fades: one removal, the list with its note "
           "(%d removals, screen %d)", removals_logged, screen);
    next_case();

    /* G1-R7: the set's question takes no key while it opens, as the design's (test_main) */
    printf("-- G1-R7: no key while the set's question opens\n");
    keys_wait_for_question("the set's removal", POP_ASK_SET, to_set_ask, set_removed);
    next_case();

    /* G2-L2b: a set's Done screen after its install goes on with the installing screen's top, as a design's
     * "Installiert" (test_main, G2-L2b): the picture full and glowing, no transition on the top, only the bottom
     * screen's content (its B tab) comes in. After a removal the Done card, as before */
    printf("-- G2-L2b: a set's Done screen after its install goes on with the installing screen's top\n");
    to_set("Tiere");
    badge_reads = badge_writes = 0;
    worker_frames = 20;
    watch_end_begin(SCR_BADGE_DONE);
    center_tap(GO_BUTTON);
    for (int i = 0; i < 400 && screen != SCR_BADGE_DONE; i++)
        frame();
    worker_frames = 0;
    /* G2-J: in layout C the end is given the success sentence and no count (G2-L1b) */
    EXPECT(screen == SCR_BADGE_DONE && bres.outcome == BFLOW_DONE && !bremove && top_has('p') && !top_has('D') &&
           stub_pour == &bpour && stub_pour_given.title == badge_text(lang, BT_INSTALL_BADGES) &&
           stub_pour_given.done_title == text(lang, T_INSTALLED) && bname[0] && !strcmp(stub_pour_given.name, bname) &&
           !strcmp(bname, bres.name) && stub_pour_given.home == text(lang, T_INSTALL_SUCCESS) &&
           !stub_pour_given.what[0] && pour_full(&bpour) && bar_drawn() &&
           apt_home && apt_sleep, "a set's install moves on to its Done screen by itself: on top the installing "
           "screen's picture, full, with the set's name \"%s\" and \"%s\", no count (\"%s\"), no Done card (picture "
           "%d, card %d); below the B tab; HOME and sleep allowed", stub_pour_given.name, stub_pour_given.home,
           stub_pour_given.what, top_has('p'), top_has('D'));
    EXPECT(end_frames == 1 && top_same(&end_last, &end_first) && end_last.model == &bpour &&
           end_last.entered == 1.0f && end_first.clock > end_last.clock &&
           end_first.clock - end_last.clock <= END_STEP_MS && fabsf(end_first.glow - end_last.glow) <= 0.03f,
           "the last installing frame's top and the first Done frame's: the same pieces at the same places, the same "
           "picture, name and texts, no transition (%.2f, %.2f); only the picture's clock went on, by %llu ms (the "
           "glow from %.3f to %.3f)", end_last.entered, end_first.entered,
           (unsigned long long)(end_first.clock - end_last.clock), end_last.glow, end_first.glow);
    frames_for(TRANSITION_MS + 100);
    EXPECT(fabsf(end_first.cover - TRANSITION_FADE_PCT / 100.0f) < 0.001f && !end_cover_rose &&
           end_now_shot.cover == 0 && end_entered_low == 1.0f && end_pours == end_frames && !end_cards &&
           end_glow_low >= 0.69f && end_glow_step <= 0.03f, "only the bottom screen comes in: %.0f %% covered in "
           "the first frame, never more after it, uncovered %d ms later; on top the picture and the transition's "
           "value 1 in all %d frames, the glow never under %.2f", 100 * end_first.cover, TRANSITION_MS + 100,
           end_frames, end_glow_low);
    frames_for(1000);
    end_glow_low = 1;
    end_glow_step = 0;
    int set_frames = end_frames;
    u64 set_clock = bpour.now, set_from = sim_now;
    frames_for(3000);
    EXPECT(screen == SCR_BADGE_DONE && end_glow_low >= 0.999f && end_glow_step <= 0.0001f &&
           end_pours == end_frames && !end_cards && end_entered_low == 1.0f && bpour.now - set_clock == sim_now - set_from,
           "3 s of the Done screen (%d frames): the glow steady at %.3f (step %.4f), the picture on top in every "
           "frame, its clock with the frames (%llu ms)", end_frames - set_frames, end_glow_low, end_glow_step,
           (unsigned long long)(bpour.now - set_clock));
    set_clock = bpour.now;
    sim_now += 3600000;         /* an hour without a frame: HOME, the lid */
    frame();
    EXPECT(bpour.now - set_clock == END_STEP_MS && top_has('p') && end_now_shot.glow >= 0.999f, "an hour without a "
           "frame (HOME, the lid): the picture's clock goes on by %llu ms, the model does not catch the hour up",
           (unsigned long long)(bpour.now - set_clock));
    stub_frame_done = NULL;
    key(KEY_B);
    frames_for(300);
    int set_left = turns(30) && screen == SCR_BADGES;
    next_case();
    watch_end_begin(SCR_BADGE_SETS);
    remove_last();
    frames_for(300);
    int ended = removal_end();
    stub_frame_done = NULL;
    EXPECT(set_left && ended && bremove && bres.outcome == BFLOW_DONE && !end_cards && !end_pours,
           "B leads back (%d); FB-E: a removal ends in the list with its note, never a Done card or the vessel "
           "(%d cards, %d frames of the list, screen %d)", set_left, end_cards, end_frames, where());
    next_case();

    /* DN: a set's Done screen after its install shows the way to the badges in the HOME Menu on a pill above the
     *     bar, in both languages, and a tap on it does nothing; after a removal it shows none */
    printf("-- DN: a set's Done screen and the pill\n");
    to_set("Tiere");
    worker_frames = 20;
    center_tap(GO_BUTTON);
    for (int i = 0; i < 400 && screen != SCR_BADGE_DONE; i++)
        frame();
    worker_frames = 0;
    frames_for(TRANSITION_MS + 100);
    {
        static const char *const HOME[2] = {"HOME Menu", "HOME-Menü"}, *const PLACE[2] = {"Place Badges",
                                                                                           "Marken anbringen"};
        int was_lang = lang;
        for (int l = 0; l < 2; l++) {
            lang = l;
            frame();
            EXPECT(screen == SCR_BADGE_DONE && !bremove && stub_pill_bottom && !strcmp(stub_pill_home, HOME[l]) &&
                   !strcmp(stub_pill_change, PLACE[l]), "DN %s, a set installed: the pill on the bottom screen, \"%s\" "
                   "to \"%s\" (screen %d)", l ? "German" : "English", stub_pill_home, stub_pill_change, screen);
        }
        lang = was_lang;
    }
    forget_sounds();
    tap(BOTTOM_W / 2, PILL_Y + PILL_H / 2, 48);
    frames_for(400);
    int dn_sounds = 0;
    for (int id = 0; id < SFX_COUNT; id++)
        dn_sounds += played(id);
    EXPECT(screen == SCR_BADGE_DONE && !bremove && !dn_sounds && !stub_bar_down && pressed == HIT_NONE &&
           stub_pill_home[0], "DN a tap on the pill does nothing (screen %d, %d sounds, Back held %d)", screen,
           dn_sounds, stub_bar_down);
    key(KEY_B);
    frames_for(300);
    next_case();
    remove_last();
    frames_for(300);
    EXPECT(removal_end() && bremove && bres.outcome == BFLOW_DONE && !stub_pill_home[0],
           "DN a set removed: no Done screen, no pill (\"%s\", screen %d)", stub_pill_home, screen);
    next_case();

    /* BS: the write's six stages shown as four steps (the read; the backup written and checked; prepared and written;
     *     the read back): each stage, half done and done (badges_step, as the flow reports it), lights the step that
     *     holds it; a step is ticked only once its last stage is done, and then the next is under way; a write that
     *     fails leaves the step that holds the failing stage under way; after an install the Done top shows the four
     *     steps ticked */
    printf("-- BS: six stages, four steps\n");
    {
        static const int ROW[BSTEP_VERIFY + 1] = {0, 1, 1, 2, 2, 3};
        static const char *const NAMES[BSTEP_VERIFY + 1] = {"read", "backup written", "backup checked", "prepared",
                                                            "written", "read back"};
        u64 bs_t = 1000;
        int was_bstep = bstep;
        pour_start(&bpour, &POUR_BADGES, bs_t);
        for (int s = BSTEP_READ; s <= BSTEP_VERIFY; s++)
            for (int half = 1; half <= 2; half++) {
                badges_step(NULL, s, (uint64_t)half, 2);
                for (int k = 0; k < 5000 / 16; k++)
                    pour_work(&bpour, bs_t += 16, job_fraction, POUR_WORKING);
                int last = half == 2 && (s == BSTEP_VERIFY || ROW[s + 1] != ROW[s]), right = bpour.t->rows == 4;
                float m[4];
                for (int row = 0; row < 4; row++) {
                    m[row] = pour_row_mark(&bpour, row);
                    float want = row < ROW[s] || (row == ROW[s] && last) ? 2 : row == ROW[s] || (row == ROW[s] + 1 && last) ? 1 : 0;
                    right &= fabsf(m[row] - want) < 0.001f;
                }
                EXPECT(right, "BS the stage \"%s\" %s (level %.3f): step %d %s; the marks %.2f %.2f %.2f %.2f",
                       NAMES[s], half == 1 ? "half done" : "done", job_fraction, ROW[s] + 1,
                       last ? "ticked" : "under way", m[0], m[1], m[2], m[3]);
            }
        pour_start(&bpour, &POUR_BADGES, bs_t);
        badges_step(NULL, BSTEP_CHANGE, 1, 2);
        for (int k = 0; k < 8000 / 16; k++)
            pour_work(&bpour, bs_t += 16, job_fraction, POUR_WORKING);
        for (int k = 0; k < 2000 / 16; k++)
            pour_work(&bpour, bs_t += 16, job_fraction, POUR_FAILED);
        float bs_f[4];
        for (int row = 0; row < 4; row++)
            bs_f[row] = pour_row_mark(&bpour, row);
        EXPECT(fabsf(bs_f[0] - 2) < 0.001f && fabsf(bs_f[1] - 2) < 0.001f && fabsf(bs_f[2] - 1) < 0.001f && bs_f[3] == 0, "BS a write that fails while it prepares: step 3 "
               "stays under way, the two before ticked, the last to come (%.2f %.2f %.2f %.2f)", bs_f[0], bs_f[1], bs_f[2], bs_f[3]);
        bstep = was_bstep;
    }
    to_set("Tiere");
    worker_frames = 20;
    center_tap(GO_BUTTON);
    for (int i = 0; i < 400 && screen != SCR_BADGE_DONE; i++)
        frame();
    worker_frames = 0;
    frames_for(1000);
    int bs_ticked = stub_pour == &bpour && bpour.t->rows == 4;
    for (int row = 0; row < 4; row++)
        bs_ticked &= fabsf(pour_row_mark(&bpour, row) - 2) < 0.001f;
    EXPECT(screen == SCR_BADGE_DONE && !bremove && bs_ticked && !strcmp(stub_pour_given.step0, badge_text(lang, BT_STEP_READ)),
           "BS a set's Done top after its install: the four steps, all ticked (screen %d)", screen);
    key(KEY_B);
    frames_for(300);
    next_case();
    remove_last();
    frames_for(300);
    next_case();

    /* R13: the Done screen leads back into the part, after an install and after a removal */
    printf("-- R13: the Done screen leads back\n");
    for (int how = 0; how < 3; how++) {
        done_leads_back(0, how);
        next_case();
        done_leads_back(1, how);
        next_case();
    }

    /* B2: the sentence for a removal HOME's own changes refuse */
    printf("-- B2: a removal after HOME changed the set\n");
    set_changed_text();
    home_changed_goes();

    /* FB-E2: the part's problem and empty states, each forced, a sign once on their two screens (test_main.c) */
    printf("-- FB-E2: a sign once on the pair of screens\n");
    reset_app();
    typeof(blist) blist_was = blist;
    typeof(bents) bents_was = bents;
    int valid_was = blist_valid;
    screen = SCR_BADGE_SET;
    memset(&bpeek, 0, sizeof bpeek);
    bpeek.result = BADGE_E_SIZE;
    bplan = BPLAN_SET;
    fb_e_drawn("badges: a set that does not read (the head's warning)", 0);
    bpeek.result = BADGE_OK;
    bplan = BPLAN_PRESENT;
    fb_e_drawn("badges: a set refused by the collection (the set's picture on top)", 0);
    screen = SCR_BADGE_PROBLEM;
    snprintf(btitle, sizeof btitle, "%s", "Nicht geschrieben, Sicherung ist zurück");
    bbody[0] = 0;
    fb_e_drawn("badges: a write that failed (the head's warning)", 0);
    screen = SCR_BADGE_SETS;
    blist_valid = 1;
    blist.col.sets = 0;
    blist.state = BLIST_LOCKED;
    fb_e_drawn("badges: the collection locked (the empty placeholder on top)", 0);
    blist.state = BLIST_OK;
    fb_e_drawn("badges: no set installed", 0);
    screen = SCR_BADGE_LIST;
    bents.count = 0;
    bents.missing = 1;
    fb_e_drawn("badges: /Badges missing", 0);
    bents.missing = 0;
    fb_e_drawn("badges: an empty folder (the footer's folder before the path)", 0);
    blist = blist_was;
    bents = bents_was;
    blist_valid = valid_was;
    reset_app();

    /* 6. every screen draws in both languages without a write */
    printf("-- 6: every screen draws\n");
    static const int SCREENS_HERE[] = {SCR_BADGES, SCR_BADGE_LIST, SCR_BADGE_SET, SCR_BADGE_SETS, SCR_BADGE_ONE,
                                       SCR_BADGE_PROBLEM};
    int drawn = 0;
    for (int l = 0; l < 2; l++)
        for (size_t k = 0; k < sizeof SCREENS_HERE / sizeof *SCREENS_HERE; k++) {
            lang = l;
            screen = SCREENS_HERE[k];
            frames_for(100);
            drawn += screen == SCREENS_HERE[k];
        }
    EXPECT(drawn == 12, "%d of 12", drawn);
    EXPECT(pill_frames > 0 && !pill_elsewhere, "DN the pill on %d frames, all of them on a set's Done screen after its "
           "install or on \"Installiert\" without the question (%d elsewhere)", pill_frames, pill_elsewhere);

    printf("%d failed\n", fails);
    return fails ? 1 : 0;
}
