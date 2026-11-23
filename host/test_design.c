/* The Stock design in app/source/main.c, with host/test_main.c's simulated console (included, its main renamed by
 * host/badges_shim/zlib.h, so main.c's statics are reachable), with Stock's images (art.py --look stock): no
 * settings row "Glas", always the Shard slot, which its images fill, while the card's glass line is still read and
 * written back unchanged; the settings rows' heights and the gaps of R4, R10, R12.
 *
 *   test_design   prints one line per check and "N failed"; exit 1 if any failed */
#include "test_main.c"   /* its main becomes test_main_main (host/badges_shim/zlib.h) */
#undef main

static int failed;

static void check(int ok, const char *what)
{
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    failed += !ok;
}

static int glass_rows(void)
{
    int n = 0;
    for (int i = 0; i < rows_shown(); i++)
        n += setting_at(i)->label == T_GLASS;
    return n;
}

/* R4: the settings rows for rows rows, as settings_row lays them out at scroll 0: from SET_Y down, SET_GAP apart, all
 * the same height, none taller than SET_H_MAX, the last that shows whole (the SET_SHOWN-th of more) ending above the
 * bar's shadow with SET_AIR to spare. */
static void settings_rows(int rows, int h)
{
    char what[160];
    int ok = 1;
    for (int i = 0; i < rows; i++) {
        rect r = settings_row(i, rows);
        ok &= r.x == SET_X && r.w == SET_W && r.h == h && r.y == SET_Y + i * (h + SET_GAP);
    }
    rect last = settings_row((rows < SET_SHOWN ? rows : SET_SHOWN) - 1, rows);
    ok &= h <= SET_H_MAX && last.y + last.h + SET_AIR <= BAR_Y - BAR_SHADOW;
    snprintf(what, sizeof what, "R4: %d settings rows of %d px from y %d, the last ending at %g above the bar's shadow",
             rows, h, SET_Y, last.y + last.h);
    check(ok, what);
}

/* R4: a touch on the lower part of a settings row, below where 0.9.3's 34 px row ended, chooses that row. */
static void settings_touch(void)
{
    screen = SCR_SETTINGS;
    int ok = 1;
    for (int i = 0; i < rows_shown(); i++) {
        rect r = settings_rect(i);
        ok &= screen_target_at((int)(r.x + 20), (int)(r.y + r.h - 1)) == HIT_ROW0 + i &&
              screen_target_at((int)(r.x + 20), (int)r.y) == HIT_ROW0 + i;
    }
    check(ok, "R4: each settings row takes touches over its whole height");
    scroll_shape s = settings_shape();
    scroll_area a = scroll_of(&s);
    ok = rows_shown() == 4 && scroll_max(&a) == 0;
    for (int i = 0; i < rows_shown(); i++)
        ok &= settings_whole(i);
    check(ok, "one look held: four rows, all whole, nothing to scroll (G1 0.7)");
}

/* The n-th mark of kind the bottom screen's last frame drew (host/main_stubs.c), else an empty rect. */
static rect marked(char kind, int n)
{
    for (int i = 0; i < stub_marks; i++)
        if (stub_mark_kind[i] == kind && n-- == 0)
            return stub_mark_at[i];
    return (rect){0, 0, 0, 0};
}

/* The box of facts the last frame drew: its lower edge (ui.c ui_facts: FACT_PAD_Y round its rows). */
static float facts_low(void)
{
    return stub_fact_y + 2 * FACT_PAD_Y + stub_fact_rows * FACT_ROW_H;
}

/* R10: "Datei loeschen?" after an install, with the longest name: the box is as wide as the screen allows, so it
 * reaches under the help button; it stands HELP_AIR px or more below the button, and Keep and Delete keep their
 * place below it. */
static void done_gap(void)
{
    screen = SCR_DONE;
    may_clean = 1;
    memset(clean_dir, 'd', sizeof clean_dir - 1);
    clean_dir[0] = '/', clean_dir[sizeof clean_dir - 1] = 0;
    memset(clean_name, 'n', sizeof clean_name - 1);
    clean_name[sizeof clean_name - 1] = 0;
    draw();
    rect hit = marked('o', 0), keep = marked('e', 0), drop = marked('e', 1), want = ui_help_rect();
    /* the button as drawn (its touch target is larger) */
    rect help = {HELP_X + (HELP_SIZE - HELP_FACE) / 2, HELP_Y + (HELP_SIZE - HELP_FACE) / 2, HELP_FACE, HELP_FACE};
    char what[200];
    snprintf(what, sizeof what, "R10: Datei loeschen? with the longest name: %g px between the help button and the "
             "box under it, the box ending %g px above Keep", stub_fact_y - (help.y + help.h), keep.y - facts_low());
    check(hit.y == want.y && hit.h == want.h && stub_fact_rows == 1 && BOTTOM_W - TILE_X1 > help.x &&
          stub_fact_y - (help.y + help.h) >= HELP_AIR && keep.y == CHOICE_Y && drop.y == CHOICE_Y &&
          keep.y - facts_low() >= 8, what);
}

/* G1-R1 Q2 (C12): the question "Datei loeschen?" and the file's box 4 px lower than at bc61fe5 (title from y 22,
 * box at y 48), Keep and Delete where they were, the box at least 8 px above them; the box one more px lower, as
 * the help button is HELP_FACE in every look (G1-R2). main.c lays the screen out once for every look (test_look,
 * Q2). */
static void done_lower(void)
{
    extern float stub_fit_baseline;
    screen = SCR_DONE;
    may_clean = 1;
    snprintf(clean_dir, sizeof clean_dir, "/Themes");
    snprintf(clean_name, sizeof clean_name, "a.zip");
    draw();
    rect keep = marked('e', 0), drop = marked('e', 1);
    char what[200];
    snprintf(what, sizeof what, "Q2: Datei loeschen? its title's baseline at %g (bc61fe5's %g), the box at %g "
             "(48 + 5), Keep and Delete at %g, %g px under the box", stub_fit_baseline, ui_baseline(22, ASK_LH, ASK_PX),
             stub_fact_y, keep.y, keep.y - facts_low());
    check(stub_fit_baseline == ui_baseline(22 + 4, ASK_LH, ASK_PX) && stub_fact_y == 48 + 4 + 1 && keep.y == CHOICE_Y &&
          drop.y == CHOICE_Y && CHOICE_Y == 114 && keep.y - facts_low() >= 8, what);
}

/* R12: the check screen of a badge set, from the card or after a download (the same screen): the green button
 * stands as far below the box of two facts as the tiles of a theme's check below theirs, at least 8 px. */
static void check_gap(void)
{
    screen = SCR_CONFIRM;       /* a theme that is installed already: its box of two facts and the two tiles */
    theme_open = 1, theme_result = TP_OK, plan.state = PLAN_DUPLICATE, plan.duplicate = 3;
    draw();
    float tiles_gap = marked('e', 0).y - facts_low();
    int designs_rows = stub_fact_rows;
    theme_open = 0;
    screen = SCR_BADGE_SET;
    bplan = BPLAN_OK, bpeek.count = 1000;
    memset(bsrc_path, 'p', sizeof bsrc_path - 1);
    bsrc_path[0] = '/', bsrc_path[sizeof bsrc_path - 1] = 0;
    draw();
    rect go = marked('e', 0);
    char what[200];
    snprintf(what, sizeof what, "R12: a badge set's check: %g px between its box and Installieren (a theme's check: "
             "%g px to its tiles)", go.y - facts_low(), tiles_gap);
    check(designs_rows == 2 && stub_fact_rows == 2 && go.y - facts_low() == tiles_gap && tiles_gap >= 8 &&
          go.y + go.h <= BAR_Y - BAR_SHADOW && screen_target_at((int)(go.x + go.w / 2), (int)(go.y + go.h - 2)) == HIT_GO &&
          screen_target_at((int)(go.x + go.w / 2), (int)go.y - 2) == HIT_NONE, what);
}

int main(void)
{
    settings s, back;
    char text[512];
    settings_defaults(&s, "");
    for (int glass = 0; glass < SETTINGS_STYLES; glass++) {
        s.logo = glass;
        settings_apply(&s);
        check(choice_glass() == GLASS_SHARD, "Stock: the Shard slot whatever glass the card names");
        settings_defaults(&back, "");
        size_t n = settings_format(&s, text, sizeof text);
        check(n > 0 && settings_parse(text, n, &back) && back.logo == glass, "the card's glass line kept as it was");
    }
    settings_rows(2, SET_H_MAX);
    settings_rows(3, SET_H_MAX);
    settings_rows(4, 43);
    settings_rows(5, 43);       /* five scroll at the four's height (G1 0.7; 0.9.3's were 34) */
    settings_touch();
    done_gap();
    done_lower();
    check_gap();
    check(glass_rows() == 0 && rows_shown() == 4, "Stock: no row Glas, four rows");
    check(ART_STOCK.sheet_z[ART_SCHEME_WEISS] != ART_STOCK.sheet_z[ART_SCHEME_NACHT] &&
          art_pictures_of(&ART_STOCK, ART_SCHEME_WEISS).z != art_pictures_of(&ART_STOCK, ART_SCHEME_NACHT).z,
          "Stock: Stock's images, a sheet and a house per scheme");
    printf("%d failed\n", failed);
    return failed != 0;
}
