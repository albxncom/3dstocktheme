/* L1-0: the sheet, what the two walks do not draw, one draw list per page. First the application's own screens and
 * input where the walks never go: the stylus on a list, its scroll bar and the settings rows, the settings screen row
 * by row, the main screen with the 3D slider up, every glass style and pattern. Then each drawing function of ui.h and
 * app_ui.h the walks miss (or miss in a state), called with fixed arguments on a page of its own; last ui_exit, which
 * unloads the look. Stand-in content only: the theme zips of make_zips.py (as designs.c), a made-up icon, camera frame
 * and wallpaper. driver OUT_DIR A.zip .. E.zip SCHEME GLASS PATTERN TAG */
extern float rec_slider;

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
static void expect(int scr, const char *what)
{
    if (screen != scr) {
        fprintf(stderr, "%s: screen %d, popup %d, job %d\n", what, screen, popup, job);
        exit(1);
    }
}
/* one frame drawn by the page alone, both screens (both eyes), then captured */
#define PAGE(name, top, bottom)                                                                                      \
    do {                                                                                                             \
        ui_begin(1.0f);                                                                                              \
        do {                                                                                                         \
            ui_top();                                                                                                \
            top;                                                                                                     \
            ui_bottom();                                                                                             \
            bottom;                                                                                                  \
        } while (ui_next_eye());                                                                                     \
        ui_end();                                                                                                    \
        sim_now += 16;                                                                                               \
        capture(name);                                                                                               \
    } while (0)

static uint8_t icon[TP_ICON_SIZE];
static uint16_t camera[400 * 240];
static uint8_t wall[1024 * 256 * 2];
static const char *choice_name(int k)
{
    static const char *const N[] = {"Weiss", "Gelb", "Sand", "Minze", "Himmel", "Flieder", "Rose", "Nacht", "Grau"};
    return N[k];
}
static ui_pic sheet_pic(size_t i)
{
    return (ui_pic){i % 2 ? icon : NULL, i % 2 ? -1 : ART_FOLDER_FOOT};
}

static void the_list(void)
{
    go_designs(0);
    frames_for(800);
    press(KEY_A, 300);
    press(KEY_A, 900);
    expect(SCR_LIST, "SD-Karte");
    shot("s-01-list");
    /* a row held by the stylus, then dragged up: the list scrolls under it */
    rect r = row_rect(1);
    stylus((int)(r.x + r.w / 2), (int)(r.y + r.h / 2));
    frames_for(200);
    capture("s-02-row-held");
    for (int k = 1; k <= 6; k++) {
        stylus((int)(r.x + r.w / 2), (int)(r.y + r.h / 2) - 12 * k);
        frame();
    }
    capture("s-03-row-dragged");
    lift();
    frames_for(600);
    capture("s-04-row-let-go");
    /* the scroll bar: the stylus on the track's foot (the thumb jumps there), dragged to its head */
    stylus((int)(TRACK.x + TRACK.w / 2), (int)(TRACK.y + TRACK.h - 4));
    frames_for(200);
    capture("s-05-thumb-held");
    for (int k = 1; k <= 8; k++) {
        stylus((int)(TRACK.x + TRACK.w / 2), (int)(TRACK.y + TRACK.h - 4 - TRACK.h * k / 9));
        frame();
    }
    capture("s-06-thumb-dragged");
    lift();
    frames_for(600);
    shot("s-07-thumb-let-go");
    /* the bar's left tab held, then let go outside it: nothing happens */
    stylus(20, BAR_Y + BAR_H / 2);
    frames_for(200);
    capture("s-08-tab-held");
    stylus(BOTTOM_W / 2, 60);
    frame();
    lift();
    frames_for(600);
    press(KEY_B, 900);
    press(KEY_B, 900);
}

static void the_settings(void)
{
    char name[64];
    go_main(0);
    frames_for(600);
    open_settings();
    ring = 1;
    for (int i = 0; i < rows_shown(); i++) {
        focus = i;
        snprintf(name, sizeof name, "s-10-setting-%d", i);
        shot(name);
    }
    focus = 0;
    rect r = settings_rect(0);
    stylus((int)(r.x + r.w / 2), (int)(r.y + r.h / 2));
    frames_for(200);
    capture("s-11-setting-held");
    stylus((int)(r.x + r.w / 2), 2);        /* off the row before it is let go: no change */
    frame();
    lift();
    frames_for(600);
    go_main(0);
    frames_for(600);
}

static void the_looks(void)
{
    char name[64];
    rec_slider = 1.0f;
    go_main(0);
    shot("s-20-3d-main");
    go_designs(0);
    shot("s-21-3d-designs");
    rec_slider = 0;
    int logo = prefs.logo, pattern = prefs.pattern;
    for (int g = 0; g < GLASS_COUNT; g++)
        for (int p = 0; p < PATTERN_COUNT; p++) {
            if (g == logo && p == pattern)
                continue;
            prefs.logo = g, prefs.pattern = p;
            settings_apply(&prefs);
            go_main(0);
            snprintf(name, sizeof name, "s-22-main-glass-%d-pattern-%d", g, p);
            shot(name);
            if (p == pattern) {
                go_designs(0);
                snprintf(name, sizeof name, "s-23-designs-glass-%d", g);
                shot(name);
                snprintf(name, sizeof name, "s-24-progress-glass-%d", g);
                PAGE(name, ui_busy_card(icon, -1, "Morgenlicht", 0.5f, "Schreiben"), ui_bar(UI_KEY_B, "Abbrechen", 0));
            }
        }
    prefs.logo = logo, prefs.pattern = pattern;
    settings_apply(&prefs);
    go_main(0);
    frames_for(600);
}

static void the_functions(void)
{
    static const char *const steps[4] = {"Prüfen", "Katalog", "Design", "Abschluss"};
    static const float none[4] = {0, 0, 0, 0}, some[4] = {2, 1.5f, 0.5f, 0}, all[4] = {2, 2, 2, 2};
    static const char *const long_paras[4] = {
        "Ein langer Absatz, der die Breite des Fensters mehrmals füllt und darum in mehreren Zeilen steht, damit "
        "das Zeichen keinen Platz mehr findet.",
        "Ein zweiter Absatz, ebenso lang, der weiter erklärt, was geschieht, und noch eine Zeile braucht und noch "
        "eine.",
        "Ein dritter Absatz.", "Ein vierter Absatz, der zuletzt steht."};
    static const char *const short_paras[2] = {"Die Datei wird gelöscht.", "Das kann nicht rückgängig gemacht werden."};
    static const ui_fact facts[4] = {{ART_FACT_HOME, "Morgenlicht", 0},
                                     {ART_FACT_CHECK, "12 Badges", 0},
                                     {ART_FACT_FILE, "sdmc:/Themes/Sammlung/Unterordner/Morgenlicht.zip", 1},
                                     {ART_FACT_FILE, "a fourth row, cut off", 0}};
    const char *path = "sdmc:/Themes/ein/sehr/langer/Pfad/mit/vielen/Ordnern/bis/zur/Datei/Morgenlicht.zip";
    char stats[256];
    ui_popup pops;
    ui_block block;
    for (size_t i = 0; i < sizeof icon; i++)
        icon[i] = (uint8_t)(i * 7 + (i >> 8));
    for (size_t i = 0; i < sizeof camera / sizeof camera[0]; i++)
        camera[i] = (uint16_t)(i * 31);
    for (size_t i = 0; i < sizeof wall; i++)
        wall[i] = (uint8_t)(i >> 5);

    PAGE("s-30-done-card", (ui_head("Installiert", NULL), ui_done_card(icon, "Morgenlicht", "HOME-Menü",
                                                                       "Design ändern"), ui_foot_warn("Neu starten")),
         ui_bar_right(UI_KEY_A, "OK", 0));
    PAGE("s-31-done-card-long", ui_done_card(NULL, "Waldweg im ersten Morgenlicht nach dem langen Regen der Nacht",
                                             NULL, NULL),
         ui_bar_right(UI_KEY_A, "OK", 1));
    PAGE("s-32-steps-none", ui_steps("Wird installiert", "Morgenlicht", steps, none, 0), ui_bar(UI_KEY_B, "", 0));
    PAGE("s-33-steps-some", ui_steps("Wird installiert", "Waldweg im ersten Morgenlicht nach dem langen Regen",
                                     steps, some, 0.6f), ui_dots(100));
    PAGE("s-34-steps-all", ui_steps("Installiert", "Morgenlicht", steps, all, 1), (void)0);
    PAGE("s-35-cards", (ui_detail(icon, -1, "Morgenlicht", "Mit Musik", MUTED),
                        ui_busy_card(NULL, ART_GLOBE_SMALL, "beispiel.example", -1, NULL)),
         ui_busy_card(icon, -1, "Morgenlicht", 1.5f, "Schreiben"));
    PAGE("s-36-camera", (ui_camera(camera), ui_brackets()), ui_camera(NULL));
    PAGE("s-37-url", ui_url_card("https://beispiel.example/ein/sehr/langer/Pfad/zu/einem/Design/mit/einem/langen/"
                                 "Namen/und/noch/mehr/Ordnern/Morgenlicht-im-Wald-nach-dem-Regen.zip"),
         (ui_facts(facts, 4, 40), ui_corner(path, 1, 1), ui_help(1)));
    PAGE("s-38-scrollbars", ui_black(0.5f),
         (ui_scrollbar((rect){10, 20, 8, 180}, 0, 0, 0), ui_scrollbar((rect){30, 20, 8, 180}, 0.5f, 0.001f, 1),
          ui_scrollbar((rect){50, 20, 8, 20}, 0.2f, 2, 0), ui_scrollbar((rect){70, 20, 8, 180}, 0.99f, 0.5f, 1)));
    PAGE("s-39-loaders", (ui_loader(NULL, "Nicht ausschalten", 0.5f, 1), ui_head("Lesen", "rechts")),
         (ui_loader("Wird gelesen ...", "Nicht ausschalten", 1, 0.5f), ui_loader("x", NULL, 0, 0)));
    ui_popup_text_signed(&pops, long_paras, 4);
    PAGE("s-40-popup-long", ui_hero(ART_DESIGNS, "Designs", NULL, NULL),
         (ui_popup_tone(1), ui_popup_sign(ART_WARN), ui_popup_draw(&pops, "Abbrechen", "Löschen", 1, 1, 1)));
    ui_popup_text_signed(&pops, short_paras, 2);
    PAGE("s-41-popup-signed", ui_way_pill("HOME-Menü", "Design ändern"),
         (ui_popup_tone(0), ui_popup_sign(ART_INFO_WARN), ui_popup_draw(&pops, NULL, "Okay", 0, 0, 0.5f)));
    PAGE("s-42-info", ui_info_box("Kopf", "Ein Text, der in der Box steht und umbricht, wenn er lang genug ist, "
                                  "wie dieser hier.", path, 40, 120),
         ui_info_box("Kopf", "Kurz.", NULL, 150, 100));
    ui_wall_set(0, wall, 1024);
    ui_wall_set(1, wall, 512);
    PAGE("s-43-walls", (ui_wall(0, 100), ui_foot(FOOT_DOTS_Y, ART_FOLDER_FOOT, path, MUTED, 1, "rechts")),
         (ui_wall(1, 0), ui_bar_y("Zurück", "Vorschau", 1)));
    static const uint8_t up[3] = {10, 80, 160}, low[3] = {200, 220, 240};
    PAGE("s-44-wall-colour", ui_wall_color(up, low), (ui_wall_color(low, up), ui_bar_y("Zurück", "Vorschau", 0)));
    PAGE("s-45-settings", (ui_backdrop(), ui_choices(9, 4, choice_name), ui_choices_word()),
         (ui_row_setting((rect){SET_X, 10, SET_W, SET_H + 10}, 0, "Farbe", "Himmel", "5 / 8"),
          ui_row_setting((rect){SET_X, 80, SET_W, SET_H + 10}, UI_DOWN, "Muster", "Häuser", "2 / 3"),
          ui_row_setting((rect){SET_X, 150, SET_W, SET_H / 2}, UI_OFF, "Stil", "Stock", "1 / 4"),
          ui_settings_button(1)));
    PAGE("s-46-faces",
         (ui_gallery_empty(), ui_swarm(sheet_pic, 7, 3, icon, "Morgenlicht", 1)),
         (ui_tile(FACE_TILE, (rect){10, 10, 140, 60}, UI_DOWN, ART_DESIGNS, "Designs", 16, 18, 4, TILE_INK),
          ui_tile(FACE_TILE, (rect){170, 10, 140, 60}, UI_OFF, ART_BADGES, "Badges", 16, 18, 4, TILE_INK),
          ui_wide(FACE_WIDE, (rect){10, 80, 300, 40}, UI_DOWN, ART_FOLDER_FOOT, "Ordner", 14, 4, TILE_INK),
          ui_go((rect){10, 130, 140, 40}, UI_DOWN, ART_FACT_CHECK, "Installieren"),
          ui_alt((rect){170, 130, 140, 40}, UI_OFF, ART_NO_MUSIC, "Ohne Musik"),
          ui_row_folder((rect){10, 176, 300, 30}, UI_DOWN, "Sammlung"),
          ui_bar2("Zurück", "Weiter", 1)));
    PAGE("s-47-rows",
         (ui_select((rect){40, 40, 120, 60}, 8), ui_select_corners((rect){200, 40, 120, 60}, (const int[4]){0, 4, 8, 12})),
         (ui_row_theme((rect){10, 10, 300, 50}, UI_DOWN, icon, "Morgenlicht", "Mit Musik", 1, MUTED, 1),
          ui_row_theme((rect){10, 70, 300, 50}, UI_OFF, NULL, "Waldweg", NULL, 0, MUTED, 0),
          ui_bar_wide(UI_KEY_START, "Beenden", 1), ui_corner("sdmc:/", 0, 0)));
    ui_wrap(&block, "Ein Satz, der nach dem Komma bricht, aber nicht in einem Wort, das zu lang ist: "
                    "Donaudampfschifffahrtsgesellschaftskapitän.", 120, 18, 12, 3, 80, 20);
    ui_stats(stats, sizeof stats);
    PAGE("s-48-text", (ui_block_draw(&block, 20, 20, INK, UI_LEFT), ui_text(stats, 10, 200, 12, INK, UI_LEFT),
                       ui_text_fit(path, 10, 230, 200, 14, MUTED, UI_LEFT, 1)),
         (ui_sprite_part(ART_TRACK, (rect){10, 10, 8, 100}, 0.25f, 0.75f), ui_card_glass((rect){30, 20, 200, 80}),
          ui_text(ui_stats_alarm() ? "Alarm" : "ruhig", 160, 200, 14, INK, UI_CENTER),
          ui_progress((rect){30, 150, 200, 12}, 0.3f)));
    /* where the bar's tabs and the help button take touches, drawn as cursors so their places are in the list */
    PAGE("s-49-hit-places", ui_select(ui_help_rect(), 4),
         (ui_select(ui_bar_rect(), 4), ui_select(ui_bar_wide_rect(), 4), ui_select(ui_popup_button(1, 1), 4)));
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
    snprintf(dir, sizeof dir, "%s/fl-sheet-XXXXXX", base && *base ? base : "/tmp");
    if (!mkdtemp(dir) || chdir(dir))
        return 3;
    ui_part(&ART_APP_PART);
    if (ui_init())
        return 2;
    version_set(APP_VERSION_PRE);
    lang = LANG_DE;
    make_pack(argv[2], argv[3], 0);
    mkdir("sdmc:/Themes", 0755);
    mkdir("sdmc:/Themes/Sammlung", 0755);
    copy_to(argv[2], "sdmc:/Themes/Morgenlicht.zip");
    copy_to(argv[4], "sdmc:/Themes/Waldweg.zip");
    copy_to(argv[3], "sdmc:/Themes/Sammlung/Kieselgrau.zip");
    copy_to(argv[5], "sdmc:/Themes/Tiefsee.zip");
    copy_to(argv[6], "sdmc:/Themes/Papierflieger.zip");
    copy_to(argv[6], "sdmc:/Themes/Kopie.zip");
    copy_to(argv[5], "sdmc:/Themes/Tiefsee-2.zip");
    settings_start();
    prefs.scheme = atoi(argv[7]);
    prefs.logo = atoi(argv[8]);
    prefs.pattern = atoi(argv[9]);
    if (getenv("REC_LOOK"))
        look_pick = atoi(getenv("REC_LOOK")), prefs.design = LOOKS[look_pick];
    settings_apply(&prefs);
    load_pack();
    sound_start();
    go_main(0);
    themes_start();
    frames_for(1500);

    the_list();
    the_settings();
    the_looks();
    the_functions();
    ui_exit();
    return 0;
}
