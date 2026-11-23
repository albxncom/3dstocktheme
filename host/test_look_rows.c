/* The settings row "Stil" (app/source/main.c, G1) in a build of two looks: main.c compiled with a looks_built.h of
 * Stock and a second look under Prisma's number (build/host/two/; main.c reads only the looks' numbers, so no
 * second look's drawing is linked), with host/test_main.c's simulated console (included, its main renamed by
 * host/badges_shim/zlib.h). The row stands first and names the looks held; a change is drawn from the next frame
 * on and is on the card at once; Prisma's rows lack the background's and its first colour is "Perle"; a card
 * naming a look this build does not hold, or no look, or another word, starts in Stock, and the card keeps it.
 * Stock's five rows (G1 0.7) scroll at the four rows' height, by the row: the selected row always shows whole, up and
 * down scroll at the ends, a touch picks the row under it and only scrolls in a row cut at the edge; the stylus drags
 * them as a list's (FIX-1); Prisma's four do not scroll.
 *
 *   test_look_rows   prints one line per check and "N failed"; exit 1 if any failed */
#include "test_main.c"
#undef main

static int failed;

static void check(int ok, const char *what)
{
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    failed += !ok;
}

/* the settings screen's rows as drawn last: "label|label|...", the first two with their value ("Style: Stock") */
static const char *rows_drawn(void)
{
    static char all[600];
    all[0] = 0;
    for (int i = 0; i < stub_choices && i < 6; i++)
        snprintf(all + strlen(all), sizeof all - strlen(all), "%s%s%s%s", i ? "|" : "", stub_row_label[i],
                 i < 2 ? ": " : "", i < 2 ? stub_row_value[i] : "");
    return all;
}

/* the start with this card's file (NULL: none), then the settings screen opened */
static void start_with(const char *file)
{
    remove(SETTINGS_PATH);
    if (file)
        write_file(SETTINGS_PATH, (const uint8_t *)file, strlen(file));
    start_again();
    popup = POP_NONE;
    go_main(0);
    frames_for(300);
    key(KEY_Y);
    frames_for(300);
}

/* The rows' scroll as drawn: SET_SHOWN rows whole, the cursor's among them, between SET_Y and the lower edge of the
 * SET_SHOWN-th row at scroll 0. */
static int shows_cursor(void)
{
    rect r = settings_rect(focus), last = settings_row(SET_SHOWN - 1, rows_shown());
    int whole = 0;
    for (int i = 0; i < rows_shown(); i++)
        whole += settings_whole(i);
    return settings_whole(focus) && r.y >= SET_Y && r.y + r.h <= last.y + last.h &&
           whole == (rows_shown() < SET_SHOWN ? rows_shown() : SET_SHOWN);
}

/* A point on the part of row i that shows above the bar, on its right (clear of the tab): its middle. */
static int row_x(void) { return SET_X + SET_W - 30; }
static int row_y(int i)
{
    rect r = settings_rect(i);
    float top = r.y < 0 ? 0 : r.y, bottom = r.y + r.h > BAR_Y ? BAR_Y : r.y + r.h;
    return (int)((top + bottom) / 2);
}

/* every row shown takes a touch on what shows of it */
static int touch_picks(void)
{
    int ok = 1;
    for (int i = 0; i < rows_shown(); i++)
        ok &= target_at(row_x(), row_y(i)) == HIT_ROW0 + i;
    return ok;
}

static void scrolling(void)
{
    char what[300];
    const float pitch = SET_ROW_H(SET_SHOWN) + SET_GAP;
    start_with(NULL);
    rect fifth = settings_rect(4);
    snprintf(what, sizeof what, "Stock: five rows of %d px (the four's), four whole, the fifth from y %g under the "
             "bar's shadow; nothing scrolled", SET_ROW_H(SET_SHOWN), fifth.y);
    check(rows_shown() == 5 && stub_choices == 5 && settings_rect(0).h == SET_ROW_H(SET_SHOWN) &&
          settings_rect(0).y == SET_Y && fifth.h == SET_ROW_H(SET_SHOWN) && fifth.y == SET_Y + 4 * pitch &&
          fifth.y < BAR_Y - BAR_SHADOW && !settings_whole(4) && sv.scroll == 0 && shows_cursor(), what);
    check(touch_picks() && target_at(60, BAR_Y + 15) == HIT_LEFT,
          "at scroll 0 each row takes a touch on what shows of it, the tab over the fifth is the tab");

    key(KEY_DDOWN);                     /* the first press shows the cursor */
    int always = 1, scrolls_at = -1;
    for (int k = 1; k <= 5; k++) {
        float was = sv.scroll;
        key(KEY_DDOWN);
        frames_for(50);
        always &= shows_cursor() && focus == (k < 4 ? k : 4);
        if (sv.scroll != was && scrolls_at < 0)
            scrolls_at = focus;
    }
    snprintf(what, sizeof what, "down: the fifth row reached, and no further; the rows scrolled once, by a row (%g px, "
             "on reaching row %d), the cursor's row whole after every press; the top screen lists its choices",
             sv.scroll, scrolls_at + 1);
    check(always && focus == 4 && sv.scroll == pitch && scrolls_at == 4 && stub_list_count == SETTINGS_STYLES &&
          !strcmp(stub_row_label[4], "Background") && settings_rect(4).y == SET_Y + 3 * pitch, what);
    rect first = settings_rect(0);
    snprintf(what, sizeof what, "at the end the first row is cut at the screen's top (%g to %g), the other four whole",
             first.y, first.y + first.h);
    check(first.y < 0 && first.y + first.h > 0 && !settings_whole(0) && shows_cursor() && touch_picks(), what);

    always = 1;
    for (int k = 3; k >= 0; k--) {
        key(KEY_DUP);
        frames_for(50);
        always &= shows_cursor() && focus == k && sv.scroll == (k ? pitch : 0);
    }
    key(KEY_DUP);
    check(always && focus == 0 && sv.scroll == 0 && shows_cursor() && stub_list_count == LOOKS_HELD,
          "up: the rows stay until the first row is reached, then scroll back by a row; and no further");

    int pattern = prefs.pattern;
    tap(row_x(), row_y(4), 100);
    frames_for(300);
    check(focus == 4 && sv.scroll == pitch && prefs.pattern == pattern && shows_cursor(),
          "a touch on the fifth row, cut under the bar's shadow: it scrolls in and is selected, its choice kept");
    tap(row_x(), row_y(4), 100);
    frames_for(300);
    check(focus == 4 && sv.scroll == pitch && prefs.pattern == (pattern + 1) % SETTINGS_STYLES,
          "a touch on it now that it shows whole: its next choice");
    int look = prefs.design;
    tap(row_x(), row_y(0), 100);
    frames_for(300);
    check(focus == 0 && sv.scroll == 0 && prefs.design == look && stub_list_count == LOOKS_HELD,
          "a touch on the first row cut at the top: it scrolls in and is selected, the look kept");
    int sounds = prefs.sounds;
    tap(row_x(), row_y(3), 100);
    frames_for(300);
    check(focus == 3 && sv.scroll == 0 && prefs.sounds == !sounds && stub_list_count == 2,
          "a touch on the fourth row, whole: selected and switched, nothing scrolls");
    tap(row_x(), row_y(3), 100);
    frames_for(300);

    key(KEY_DUP);
    key(KEY_DUP);
    key(KEY_DUP);
    key(KEY_DUP);
    key(KEY_DDOWN);
    key(KEY_DDOWN);
    key(KEY_DDOWN);
    key(KEY_DDOWN);
    key(KEY_B);
    frames_for(300);
    key(KEY_Y);
    frames_for(300);
    check(screen == SCR_SETTINGS && focus == 0 && sv.scroll == 0 && rows_shown() == 5,
          "out from the end and back: the first row, nothing scrolled, as when the screen opens");

    key(KEY_DRIGHT);
    key(KEY_DRIGHT);
    frames_for(60);
    int all = 1;
    for (int i = 0; i < rows_shown(); i++)
        all &= settings_whole(i) && settings_rect(i).h == SET_ROW_H(4);
    key(KEY_DDOWN);
    key(KEY_DDOWN);
    key(KEY_DDOWN);
    key(KEY_DDOWN);
    check(stub_look == LOOK_PRISM && rows_shown() == 4 && all && focus == 3 && sv.scroll == 0 && shows_cursor() &&
          touch_picks(), "Prisma: four rows, all whole, nothing scrolls, down ends on the fourth");
    key(KEY_DUP);
    key(KEY_DUP);
    key(KEY_DUP);
    key(KEY_DRIGHT);
    frames_for(60);
    key(KEY_B);
    frames_for(300);
}

/* every setting's choice, to see that a drag changes none */
static int choices(void)
{
    return look_pick + 10 * prefs.scheme + 100 * music_pick + 1000 * prefs.sounds + 10000 * prefs.pattern;
}

/* FIX-1 X1: Stock's five rows scroll by the stylus as every list does (scroll.h): a drag past DRAG_START moves them
 * 1 to 1, clamped at both ends, and taps nothing; lifted, they go on and come to rest inside the range; a tap still
 * steps a whole row and only scrolls in a cut one; the held look comes after TOUCH_HOLD_MS; the D-pad brings the
 * cursor's row in whole again. */
static void stylus_scrolling(void)
{
    char what[300];
    start_with(NULL);
    scroll_shape s = settings_shape();
    scroll_area a = scroll_of(&s);
    const float max = scroll_max(&a), pitch = SET_ROW_H(SET_SHOWN) + SET_GAP;
    const int x = row_x(), y0 = row_y(2), was = choices();
    stylus(x, y0);
    frame();
    int still = 1;
    for (int d = 1; d < DRAG_START; d++) {
        stylus(x, y0 - d);
        frame();
        still &= sv.scroll == 0 && !sv.dragging;
    }
    int one_to_one = 1, d = DRAG_START;
    for (; d <= (int)max; d++) {
        stylus(x, y0 - d);
        frame();
        one_to_one &= sv.scroll == (float)d && sv.dragging && !is_pressed(HIT_ROW0 + 2);
    }
    for (; d < y0 - 2; d++) {
        stylus(x, y0 - d);
        frame();
    }
    float top_end = sv.scroll;
    for (d = y0 - 2; d > -40; d--) {
        stylus(x, y0 - d);
        frame();
    }
    float bottom_end = sv.scroll;
    lift();
    frames_for(300);
    snprintf(what, sizeof what, "a drag up on the third row: still for %d px, then 1 to 1, at most %g (one row, %g), "
             "back down at least 0 (%g); lifted, no row tapped, every choice kept, the cursor on the first row",
             DRAG_START - 1, top_end, pitch, bottom_end);
    check(still && one_to_one && max == pitch && top_end == max && bottom_end == 0 && sv.scroll == 0 &&
          choices() == was && focus == 0 && pending == HIT_NONE, what);

    stylus(x, y0);
    frame();
    for (d = 1; d <= 20; d++) {
        stylus(x, y0 - d);
        frame();
    }
    float lifted = sv.scroll;
    lift();
    frame();
    frame();
    float glided = sv.scroll;
    frames_for(1000);
    float rest = sv.scroll;
    snprintf(what, sizeof what, "a slow drag of 20 px, lifted at %g: the rows go on (%g two frames later) and come to "
             "rest at %g, inside 0 to %g; nothing tapped", lifted, glided, rest, max);
    check(lifted == 20 && glided > lifted && rest > glided && rest <= max && sv.fling == 0 && !scroll_moving(&sv) &&
          choices() == was && focus == 0 && pending == HIT_NONE, what);

    int sounds = prefs.sounds;
    stylus(row_x(), row_y(3));
    frame();
    int held_at_once = is_pressed(HIT_ROW0 + 3);
    frames_for(TOUCH_HOLD_MS + 20);
    int held_later = is_pressed(HIT_ROW0 + 3);
    lift();
    frames_for(300);
    snprintf(what, sizeof what, "at rest (%g) a tap on the fourth row, whole: held after %d ms, not at once; switched, "
             "nothing scrolls", rest, TOUCH_HOLD_MS);
    check(settings_whole(3) && !held_at_once && held_later && focus == 3 && prefs.sounds == !sounds &&
          sv.scroll == rest, what);
    int pattern = prefs.pattern;
    tap(row_x(), row_y(4), 100);
    frames_for(300);
    check(focus == 4 && sv.scroll == pitch && prefs.pattern == pattern && shows_cursor(),
          "then a tap on the fifth row, cut: it scrolls in whole and is selected, its choice kept");

    stylus(x, y0);
    frame();
    for (d = 1; d <= 60; d++) {
        stylus(x, y0 + d);
        frame();
    }
    lift();
    frames_for(1000);
    float dragged = sv.scroll;
    int cut = !settings_whole(4);
    key(KEY_DDOWN);
    frames_for(50);
    int in_again = focus == 4 && sv.scroll == pitch && shows_cursor();
    key(KEY_DUP);
    frames_for(50);
    snprintf(what, sizeof what, "dragged back to %g, the cursor's fifth row cut: down (the first press, the ring) "
             "brings it in whole; up then moves to the fourth row, whole", dragged);
    check(dragged == 0 && cut && in_again && focus == 3 && shows_cursor(), what);
    tap(row_x(), row_y(3), 100);
    frames_for(300);
    check(prefs.sounds == sounds, "a tap on the fourth row again: the sounds as at the start");
    key(KEY_B);
    frames_for(300);
}

/* the first and the last log line about the settings since settings_lines was emptied */
static char settings_first[300], settings_last[300];
static void settings_lines(const char *line)
{
    if (strncmp(line, "settings:", 9))
        return;
    if (!settings_first[0])
        snprintf(settings_first, sizeof settings_first, "%s", line);
    snprintf(settings_last, sizeof settings_last, "%s", line);
}

/* The log names the look drawn by its word on the card (the join's review, finding 3): at the start and after a
 * change of "Style"; a card naming a look this build does not hold logs the look drawn, Stock. */
static void log_names_look(void)
{
    static const struct {
        const char *line, *first, *after;       /* the card's line; the start's look; after "Style" goes right */
    } runs[] = {
        {"design=prism\n", "look prism", "look stock"},
        {"design=frost\n", "look stock", "look prism"},
    };
    char what[900];
    for (size_t k = 0; k < sizeof runs / sizeof *runs; k++) {
        char file[200];
        snprintf(file, sizeof file, "3DStockTheme settings 1\n%s", runs[k].line);
        settings_first[0] = settings_last[0] = 0;
        stub_log_hook = settings_lines;
        start_with(file);
        char first[300];
        snprintf(first, sizeof first, "%s", settings_first);
        key(KEY_DRIGHT);                /* the first press shows the cursor */
        key(KEY_DRIGHT);
        frames_for(60);
        stub_log_hook = NULL;
        snprintf(what, sizeof what, "the card says \"%.*s\": the start's log line names the look (\"%s\"), the line "
                 "after a change of Style the new one (\"%s\")", (int)strcspn(runs[k].line, "\n"), runs[k].line,
                 first, settings_last);
        check(strstr(first, runs[k].first) && strstr(settings_last, runs[k].after), what);
        key(KEY_B);
        frames_for(300);
    }
}

int main(void)
{
    char what[700];
    stub_each_frame = held_tick;        /* the clock goes on a frame each frame */
    const char *base = getenv("TMPDIR");
    snprintf(dir, sizeof dir, "%s/test-look-rows-XXXXXX", base && *base ? base : "/tmp");
    if (!mkdtemp(dir) || chdir(dir))
        return 3;
    mkdir("sdmc:", 0755);
    mkdir("sdmc:/theme-pack", 0755);
    lang = 0;
    check(LOOKS_HELD == 2 && LOOKS[0] == LOOK_STOCK && LOOKS[1] == LOOK_PRISM, "this build holds Stock and Prisma");

    start_with(NULL);
    snprintf(what, sizeof what, "no file: Stock; the row Style first, then the four of every build (%s)", rows_drawn());
    check(screen == SCR_SETTINGS && stub_look == LOOK_STOCK && rows_shown() == 5 && stub_choices == 5 &&
          !strcmp(rows_drawn(), "Style: Stock|Colour: White|Background music|Sounds|Background") &&
          !strcmp(stub_row_note[0], "1 / 2") && focus == 0, what);
    snprintf(what, sizeof what, "the top screen's card: the looks held (%d: %s, %s), Stock checked", stub_list_count,
             stub_list_name[0], stub_list_name[1]);
    check(stub_list_count == 2 && stub_list_chosen == 0 && !strcmp(stub_list_name[0], "Stock") &&
          !strcmp(stub_list_name[1], "Prisma"), what);

    key(KEY_DRIGHT);                    /* the first press shows the cursor */
    key(KEY_DRIGHT);
    frames_for(60);
    settings card = on_card();
    snprintf(what, sizeof what, "right: Prisma, drawn and on the card; its rows without the background's, the first "
             "colour Pearl, the cursor still on Style (%s)", rows_drawn());
    check(prefs.design == LOOK_PRISM && card.design == LOOK_PRISM && stub_look == LOOK_PRISM && focus == 0 &&
          rows_shown() == 4 && !strcmp(rows_drawn(), "Style: Prisma|Colour: Pearl|Background music|Sounds") &&
          !strcmp(stub_row_note[0], "2 / 2"), what);
    int told_at_once = stub_look;
    key(KEY_DRIGHT);
    frames_for(60);
    check(told_at_once == LOOK_PRISM && stub_look == LOOK_STOCK && on_card().design == LOOK_STOCK &&
          rows_shown() == 5 && !strcmp(stub_row_value[1], "White"),
          "right again: round to Stock, the background's row back");
    key(KEY_DDOWN);
    key(KEY_DDOWN);
    key(KEY_DDOWN);
    key(KEY_DDOWN);
    key(KEY_DDOWN);
    check(focus == 4, "down to the background's row, the fifth, and no further");
    lang = 1;
    key(KEY_DUP);
    key(KEY_DUP);
    key(KEY_DUP);
    key(KEY_DUP);
    key(KEY_DLEFT);
    frames_for(60);
    snprintf(what, sizeof what, "the same in German: Stil, Prisma, Perle (%s)", rows_drawn());
    check(focus == 0 && stub_look == LOOK_PRISM &&
          !strcmp(rows_drawn(), "Stil: Prisma|Farbe: Perle|Hintergrundmusik|Töne"), what);
    lang = 0;

    /* what the card says at start */
    static const struct {
        const char *line;
        int look, kept;
    } starts[] = {
        {"design=prism\n", LOOK_PRISM, LOOK_PRISM}, {"design=stock\n", LOOK_STOCK, LOOK_STOCK},
        {"design=frost\n", LOOK_STOCK, LOOK_FROST}, {"design=night\n", LOOK_STOCK, LOOK_NIGHT},
        {"", LOOK_STOCK, LOOK_STOCK},               {"design=Prism\n", LOOK_STOCK, LOOK_STOCK},
        {"design=prisma\n", LOOK_STOCK, LOOK_STOCK}, {"design=\n", LOOK_STOCK, LOOK_STOCK},
    };
    for (size_t k = 0; k < sizeof starts / sizeof *starts; k++) {
        char file[300];
        snprintf(file, sizeof file, "3DStockTheme settings 1\ncolour=sand\nmusic=off\nsounds=on\nglass=clear\n"
                 "pattern=bubbles\n%s", starts[k].line);
        start_with(file);
        int drawn = stub_look, rows = rows_shown();
        char row[100];
        snprintf(row, sizeof row, "%s %s", stub_row_label[0], stub_row_value[0]);
        key(KEY_DDOWN);
        key(KEY_DDOWN);
        key(KEY_DDOWN);
        key(KEY_DDOWN);
        key(KEY_DRIGHT);                /* another row's change writes the card anew */
        frames_for(60);
        card = on_card();
        snprintf(what, sizeof what, "the card says \"%.*s\": %s drawn, the row \"%s\", %d rows; after the sounds' "
                 "change the card says look %d, glass and colour kept", (int)strcspn(starts[k].line, "\n"),
                 starts[k].line, starts[k].look == LOOK_STOCK ? "Stock" : "Prisma", row, rows, card.design);
        check(drawn == starts[k].look && rows == (starts[k].look == LOOK_PRISM ? 4 : 5) &&
              !strcmp(row, starts[k].look == LOOK_PRISM ? "Style Prisma" : "Style Stock") && card.sounds == 0 &&
              card.design == starts[k].kept && card.logo == GLASS_CLEAR && card.scheme == ART_SCHEME_SAND, what);
        key(KEY_B);
        frames_for(300);
    }

    scrolling();
    stylus_scrolling();
    log_names_look();

    char cmd[1100];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", dir);
    if (chdir("/") || system(cmd))
        fprintf(stderr, "could not remove %s\n", dir);
    printf("%d failed\n", failed);
    return failed != 0;
}
