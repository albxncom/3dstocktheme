/* test_texts_fit (review of the join): every text the information box and the notes show, German and English, in
 * each look, stands whole: no line cut with "..." (a text that holds a literal "..." keeps exactly its own), every
 * line inside the box or the note, no size under 10 px; the box text only (C135); every text that carries a name at
 * every length of the name (C110-F). ui.c and app_ui.c with test_ui.c's recorder (test_ui.c's main
 * renamed) and the application's texts; the probe build's fit probe (UI_FIT_PROBE) checked against it. */
#define UI_FIT_PROBE
#define main test_ui_main
#include "test_ui.c"
#undef main
#include "text.h"
#include "install.h"
#include "deletion.h"
#include "download.h"
#include "plan.h"
#include "scan.h"
#include "badge_text.h"
#include "badge_flow.h"
#include "tracks.c"     /* the build's tracks (tracks.h), for the sounds' credits */
#include <time.h>

/* Inserted names and words at their real maximum lengths. The host measures every byte alike (test_ui.c), so the
 * widest is the longest in bytes: a theme's name is at most 64 UTF-16 units (core/tp.h, TP_NAME_BYTES), a set's 68
 * (core/badge.h, BADGE_NAME_UNITS), each as two-byte letters; a file's name up to SCAN_NAME - 1 bytes (scan.h);
 * libcurl's words up to 255 bytes (download.h, error[256]); the scan screen's line up to 159 (main.c, scan_note). */
static char theme_name[64 * 2 + 1], set_name[68 * 2 + 1], file_name[SCAN_NAME], curl_words[256], scanned[160];

static int boxes, box_bad, notes, note_bad;
/* C110-F: 0 every text; else only the texts that carry the names named in it */
enum { NAME_THEME = 1, NAME_SET = 2, NAME_FILE = 4 };
static int sweep;
static int varied(int names)
{
    return !sweep || (sweep & names);
}
static int signs, placed_bad;    /* C135: signs drawn in the boxes; boxes not text only from the left, centred */
static char first_bad[400];

/* badge_flow.c's, which reads the card: here the kind each code is taken for, every kind in turn */
static int open_kind;
int badge_open_kind(int code)
{
    (void)code;
    return open_kind;
}

static int dots(const char *s)
{
    int n = 0;
    for (; s && (s = strstr(s, "...")); s += 3)
        n++;
    return n;
}

/* A block cut: it holds more "..." than the text it was wrapped from */
static int block_cut(const ui_block *b, const char *src)
{
    int n = 0;
    for (int i = 0; i < b->count; i++)
        n += dots(b->line[i]);
    return n > dots(src);
}

/* The box between top and bottom (0, 0: the whole height above the bar, as problem_bottom draws it); C135 text only:
 * no sign in it, the text from its inner left edge, the block centred in its height (test_ui.c's info_text_only) */
static void box_at(const char *what, const char *head, const char *body, const char *path, float top, float bottom)
{
    frame_begin();
    float low = BAR_Y - BAR_SHADOW - INFO_AIR, b = bottom > 0 && bottom < low ? bottom : low;
    float h = ui_info_height(head, body, path, bottom > 0 ? b - (top > INFO_AIR ? top : INFO_AIR) : 0);
    float y = ui_info_box(head, body, path, top, bottom);
    ui_end();
    float x = roundf((BOTTOM_W - info_w) / 2), text_w = info_w - 2 * info_pad_x;
    int bad = info_text.px < 10 || y < 0 || y + h > BAR_Y - BAR_SHADOW || x < 0 || x + info_w > BOTTOM_W ||
              block_cut(&info_text, body);
    for (int i = 0; i < info_text.count; i++)
        bad |= ui_text_width(info_text.line[i], info_text.px) > text_w + 0.5f;
    for (int i = 0; i < recs; i++)
        if (rec[i].kind == 'T')
            bad |= rec[i].x < x || rec[i].x > x + info_w || rec[i].y < y || rec[i].y > y + h;
    boxes++;
    int n;
    float off;
    if (!info_text_only(x, y, h, &n, &off) && !placed_bad++ && !first_bad[0])
        snprintf(first_bad, sizeof first_bad, "box %s: %d signs, the block %g px off the middle, wide %d", what, n,
                 off, info_wide);
    signs += n;
    if (bad && !box_bad++)
        snprintf(first_bad, sizeof first_bad, "box %s: wide %d px %d lines %d at %g, %g x %g", what, info_wide,
                 info_text.px, info_text.count, y, info_w, h);
}

static void box(const char *what, const char *head, const char *body)
{
    box_at(what, head, body, NULL, 0, 0);
}

/* The note as show_note lays it out: with the sign when the text leaves it room, else without */
static void note(const char *what, const char *head, const char *body)
{
    ui_popup p;
    const char *paras[2] = {head, body};
    int count = body && body[0] ? 2 : 1, signed_ = ui_popup_text_signed(&p, paras, count), bad = 0;
    if (!signed_)
        bad |= !ui_popup_text(&p, paras, count);
    for (int k = 0; k < p.count; k++) {
        bad |= p.para[k].px < 10 || block_cut(&p.para[k], paras[k]);
        for (int i = 0; i < p.para[k].count; i++)
            bad |= ui_text_width(p.para[k].line[i], p.para[k].px) > DLG_TEXT_W + 0.5f;
    }
    notes++;
    if (bad && !note_bad++)
        snprintf(first_bad, sizeof first_bad, "note %s: signed %d", what, signed_);
}

/* FIX-1 X2: under the sounds' two choices the credits name Kenney and every track of the build (tracks.h) by title
 * and author, each with its licence, and every line stands whole at the card's lower end, at least CREDIT_ROOM_MIN
 * below the choices at its top (ui_choices_top, ui_choices_credits; FIX-2) */
static const char *off_on(int k) { return k ? "On" : "Off"; }
static void credits_fit(const char *look, int lang)
{
    char tracks[TRACK_COUNT][160], what[300];
    const char *lines[1 + TRACK_COUNT] = {text(lang, T_SOUNDS_CREDIT)};
    int named = strstr(lines[0], "Kenney") && strstr(lines[0], "CC0");
    for (int k = 0; k < TRACK_COUNT; k++) {
        const track *t = &TRACK_LIST[k];
        text_track_credit(lang, t->title, t->author, t->licence, tracks[k], sizeof tracks[k]);
        lines[1 + k] = tracks[k];
        const char *shown = strrchr(tracks[k], ',') + 2;      /* the licence as shown: up to its first comma */
        named &= strstr(tracks[k], t->title) && strstr(tracks[k], t->author) &&
                 strlen(shown) == strcspn(t->licence, ",") && !strncmp(shown, t->licence, strlen(shown));
    }
    frame_begin();
    int fits = ui_choices_credits(ui_choices_top(2, 1, off_on), lines, 1 + TRACK_COUNT);
    ui_end();
    snprintf(what, sizeof what, "%s, language %d: the sounds' credits name Kenney and the %d tracks with title, author "
             "and licence (\"%s\", \"%s\"), every line whole and %d px or more below the choices", look, lang,
             TRACK_COUNT, lines[0], lines[1], CREDIT_ROOM_MIN);
    check(named && fits, what);
}

static void texts_of(int lang)
{
    static install_plan plan;
    static pack_view pv;
    static badge_listing bl;
    static badge_peek pk;
    char title[300], body[2000], head[400], why[1000], what[120];
    const char *names[2] = {theme_name, "Waldweg"};
    for (int id = 0; id < T_COUNT && !sweep; id++)
        note("T", text(lang, id), NULL);
    for (int id = 0; id < BT_COUNT && !sweep; id++)
        note("BT", badge_text(lang, id), NULL);
    for (int r = 0; r <= 40 && !sweep; r++) {     /* every tp_result and badge_result (both counted from 0) and beyond */
        snprintf(what, sizeof what, "result %d lang %d", r, lang);
        box(what, "x", text_result(lang, r));
        note(what, text(lang, T_CANNOT_INSTALL), text_result(lang, r));
        note(what, badge_text_result(lang, r), NULL);
        box(what, "x", badge_text_result(lang, r));
    }
    for (int st = 0; st <= 6 && !sweep; st++)
        for (int d = 0; d <= 40; d++) {
            plan.state = st, plan.detail = d, plan.index = 349, plan.duplicate = 349;
            text_plan(lang, &plan, body, sizeof body);
            snprintf(what, sizeof what, "plan %d detail %d lang %d", st, d, lang);
            box(what, "x", body);
        }
    for (int st = PACK_OK; st <= PACK_NOMEM + 1 && !sweep; st++)
        for (int d = 0; d <= 40; d += 4)
            for (int dw = 0; dw <= 6; dw++) {
                memset(&pv, 0, sizeof pv);
                pv.state = st, pv.detail = d, pv.diff.what = dw, pv.diff.index = 349, pv.diff.record = 99999;
                pv.error = 0xFFFFFFFFu, pv.version = 65535, pv.count = 99999, pv.usable = 99999;
                text_pack(lang, &pv, title, sizeof title, body, sizeof body);
                snprintf(what, sizeof what, "pack %d detail %d diff %d lang %d", st, d, dw, lang);
                box(what, title, body);
            }
    const char *errs[3] = {NULL, "connection reset by peer", curl_words};
    for (int st = DL_OK; st <= DL_WRITE + 1 && !sweep; st++)
        for (int d = 0; d <= 40; d += 8)
            for (int e = 0; e < 3; e++) {
                text_download(lang, st, e == 2 ? 2147483647L : 404, d, errs[e], body, sizeof body);
                snprintf(what, sizeof what, "download %d detail %d err %d lang %d", st, d, e, lang);
                box(what, "x", body);
            }
    for (int nm = 0; nm < (sweep ? 1 : 2) && varied(NAME_THEME); nm++)
        for (int out = OUT_DONE; out <= OUT_UNKNOWN; out++)
            for (int step = STEP_NONE; step <= STEP_DONE; step++)
                for (int pending = PENDING_NO; pending <= PENDING_UNKNOWN; pending++)
                    for (int lost = 1; lost <= 12; lost += 11)
                        for (int k = 0; k < 4; k++) {
                            judgement j = {out, k & 1, (size_t)lost, k >> 1, pending};
                            install_result res;
                            memset(&res, 0, sizeof res);
                            res.failed = step;
                            res.detail = k ? TP_E_NOMEM : TP_E_ZIP;
                            text_outcome(lang, &j, &res, names[nm], title, sizeof title, body, sizeof body);
                            snprintf(what, sizeof what, "outcome %d step %d pending %d lost %d k %d name %d lang %d",
                                     out, step, pending, lost, k, nm, lang);
                            box(what, title, body);
                        }
    static const int CODES[3] = {0, (int)0xC92044E6u, (int)0xFFFFFFFFu};
    for (int oc = BFLOW_DONE; oc <= BFLOW_UNKNOWN && varied(NAME_SET); oc++)
        for (int stage = 0; stage <= 19; stage++)
            for (int c = 0; c < 9; c++)
                for (int b = 0; b <= 30; b += 6) {
                    open_kind = c % 3;
                    badge_flow_result br;
                    memset(&br, 0, sizeof br);
                    br.outcome = oc, br.stage = stage, br.code = CODES[c / 3], br.badge = b;
                    br.wrote = oc != BFLOW_REFUSED;
                    br.sets = 4294967295u, br.badges = 4294967295u;
                    snprintf(br.backup, sizeof br.backup, "%s", "sdmc:/theme-pack/badge-backup/4294967295");
                    snprintf(br.name, sizeof br.name, "%s", set_name);
                    badge_text_outcome(lang, &br, title, sizeof title, body, sizeof body);
                    snprintf(what, sizeof what, "badge outcome %d stage %d code %d badge %d lang %d", oc, stage, c, b,
                             lang);
                    box(what, title, body);
                }
    for (int plan_ = BPLAN_OK; plan_ <= BPLAN_FULL && !sweep; plan_++)
        for (int st = BLIST_OK; st <= BLIST_OPEN; st++)
            for (int b = 0; b <= 30; b++) {
                memset(&bl, 0, sizeof bl);
                bl.state = st, bl.code = (int)0xFFFFFFFFu, bl.badge = b;
                pk.result = b;
                badge_text_plan(lang, plan_, &bl, &pk, why, sizeof why);
                snprintf(what, sizeof what, "badge plan %d list %d badge %d lang %d", plan_, st, b, lang);
                box(what, text(lang, T_CANNOT_INSTALL), why);
                if (plan_ == BPLAN_COLLECTION && st != BLIST_OK) {    /* the sets list, as badges_sets_bottom */
                    char status[160];
                    badge_text_status(lang, &bl, status, sizeof status);
                    size_t said = strlen(status);
                    const char *rest = why;
                    if (said && !strncmp(why, status, said) && why[said] == '.' && why[said + 1]) {
                        rest = why + said + 1;
                        while (*rest == ' ')
                            rest++;
                    }
                    box_at(what, "x", rest, NULL, 0, LIST_BOTTOM);
                }
            }
    if (varied(NAME_FILE)) {   /* the empty states in their rooms, as main.c and badges_ui.h place them */
        char line[400];
        snprintf(line, sizeof line, "%s.\n", text(lang, T_EMPTY_FOLDER));
        size_t n = strlen(line);
        snprintf(line + n, sizeof line - n, text(lang, T_OTHER_FILES), "4294967295+");
        box_at("empty folder", file_name, line, NULL, LIST_INSET, LIST_BOTTOM);
        box_at("no themes", "x", text(lang, T_NO_THEMES_INSTALLED), NULL, 0, LIST_BOTTOM);
        box_at("no sets", "x", badge_text(lang, BT_NO_SETS), NULL, 0, LIST_BOTTOM);
        box_at("badge empty folder", "x", badge_text(lang, BT_EMPTY_FOLDER), NULL, LIST_INSET,
               LIST_BOTTOM);
        box_at("badge folder missing", "x", badge_text(lang, BT_FOLDER_MISSING), NULL, LIST_INSET,
               LIST_BOTTOM);
    }
    if (!sweep) {   /* the scan screen's box under its hint, as scan_bottom places it */
        ui_block hint;
        ui_wrap(&hint, text(lang, T_SCAN_HINT), BOTTOM_W - 2 * BODY_PAD, SCAN_PX, SCAN_PX - 3, 2, 0, SCAN_LH);
        float hh = ui_block_height(&hint), room = BAR_Y - BAR_SHADOW - 2 * INFO_AIR;
        text_download(lang, DL_BAD_URL, 0, 0, NULL, why, sizeof why);
        float bh = ui_info_height(text(lang, T_SCAN_QR), why, scanned, room - hh - SCAN_GAP);
        float under = INFO_AIR + roundf((room - hh - SCAN_GAP - bh) / 2) + hh + SCAN_GAP;
        box_at("scan", text(lang, T_SCAN_QR), why, scanned, under, under + bh);
    }
    for (int oc = DEL_DONE; oc <= DEL_UNKNOWN && varied(NAME_THEME); oc++)
        for (int lost = 1; lost <= 12; lost += 11)
            for (int k = 0; k < 8; k++) {
                deletion_judgement dj = {oc, (size_t)lost, k & 1};
                text_deleted(lang, &dj, k >> 1 & 1, 0xD8E0806Au, k >> 2, theme_name, head, sizeof head, why,
                             sizeof why);
                snprintf(what, sizeof what, "deleted %d lost %d k %d lang %d", oc, lost, k, lang);
                note(what, head, why);
            }
    for (int st = DEL_TITLE; st <= DEL_SHUFFLE && varied(NAME_THEME); st++) {
        text_deletion(lang, st, theme_name, head, sizeof head, why, sizeof why);
        snprintf(what, sizeof what, "deletion refused %d lang %d", st, lang);
        note(what, head, why);
    }
    if (varied(NAME_FILE)) {
        snprintf(head, sizeof head, text(lang, T_DELETE_FAILED), file_name);
        note("file not deleted", head, NULL);
    }
    if (varied(NAME_SET)) {
        snprintf(head, sizeof head, badge_text(lang, BT_REMOVED_HEAD), set_name);
        note("set removed", head, badge_text(lang, BT_REMOVED_NOTE));
    }
}

/* The probe build's fit probe reports what the box drew for the tightest text: German OUT_UNKNOWN with a pending
 * import, 12 lost, the longest step, the longest theme name */
static void probe_matches(void)
{
    char title[300], body[2000], best[2000] = "", best_title[300] = "";
    judgement j = {OUT_UNKNOWN, 0, 12, 0, PENDING_YES};
    install_result res;
    memset(&res, 0, sizeof res);
    for (int step = STEP_NONE; step <= STEP_DONE; step++) {
        res.failed = step;
        text_outcome(1, &j, &res, theme_name, title, sizeof title, body, sizeof body);
        if (strlen(body) > strlen(best)) {
            snprintf(best, sizeof best, "%s", body);
            snprintf(best_title, sizeof best_title, "%s", title);
        }
    }
    box("probe", best_title, best);
    int layout = info_wide ? 2 : 1;
    int px = info_text.px, lines = info_text.count, cut_ = block_cut(&info_text, best);
    ui_fit f;
    ui_info_probe(best_title, best, &f);
    char what[200];
    snprintf(what, sizeof what, "the fit probe on the tightest text: layout %d (box %d), %d px (%d), %d lines (%d), "
             "cut %d (%d)", f.layout, layout, f.px, px, f.lines, lines, f.cut, cut_);
    check(f.layout == layout && f.px == px && f.lines == lines && f.cut == cut_ && !f.cut, what);
}

/* A name of n bytes (at most its longest, size - 1) of the letter given (one byte or two): whole letters only */
static void name_fill(char *name, size_t size, int n, const char *letter)
{
    size_t k = strlen(letter), at = 0;
    memset(name, 0, size);
    for (; at + k <= (size_t)n && at + k < size; at += k)
        memcpy(name + at, letter, k);
}

/* C110-F (review of the join): a name's length decides where a text breaks, so every text that carries a name
 * (texts_of with sweep) again at each byte length of its name from 1 to its longest, in ASCII and in two-byte letters:
 * the theme's name (to 128 bytes), the set's (136), the file's (SCAN_NAME - 1); a name past its longest is not run
 * again. The same checks as the whole run: nothing cut, outside or under 10 px, text only from the left, centred. */
static void name_lengths(int id, int lang)
{
    static char theme_keep[sizeof theme_name], set_keep[sizeof set_name], file_keep[sizeof file_name];
    memcpy(theme_keep, theme_name, sizeof theme_name);
    memcpy(set_keep, set_name, sizeof set_name);
    memcpy(file_keep, file_name, sizeof file_name);
    int runs = 0, all = 0, all_notes = 0, bad = 0, bad_notes = 0, misplaced = 0, all_signs = 0;
    char first[500] = "";
    clock_t t0 = clock();
    for (int two = 0; two < 2; two++)
        for (int n = two ? 2 : 1; n < (int)sizeof file_name; n += two ? 2 : 1) {
            sweep = (n < (int)sizeof theme_name ? NAME_THEME : 0) | (n < (int)sizeof set_name ? NAME_SET : 0) |
                    NAME_FILE;
            name_fill(theme_name, sizeof theme_name, n, two ? "Ä" : "a");
            name_fill(set_name, sizeof set_name, n, two ? "Ö" : "o");
            name_fill(file_name, sizeof file_name, n, two ? "Ü" : "w");
            boxes = box_bad = notes = note_bad = signs = placed_bad = 0;
            first_bad[0] = 0;
            texts_of(lang);
            runs++, all += boxes, all_notes += notes, bad += box_bad, bad_notes += note_bad;
            misplaced += placed_bad, all_signs += signs;
            if (first_bad[0] && !first[0])
                snprintf(first, sizeof first, "%d bytes %s: %s", n, two ? "two-byte" : "ASCII", first_bad);
        }
    sweep = 0;
    memcpy(theme_name, theme_keep, sizeof theme_name);
    memcpy(set_name, set_keep, sizeof set_name);
    memcpy(file_name, file_keep, sizeof file_name);
    char what[800];
    snprintf(what, sizeof what, "C110-F %s, language %d, every name length: %d runs, %d boxes and %d notes in %.1f s; "
             "%d boxes with a line cut, outside or under 10 px, %d not text only from the left and centred (%d signs), "
             "%d notes so%s%s", look_get(id)->name, lang, runs, all, all_notes,
             (double)(clock() - t0) / CLOCKS_PER_SEC, bad, misplaced, all_signs, bad_notes, first[0] ? "; first: " : "",
             first);
    check(!bad && !misplaced && !all_signs && !bad_notes && runs == (int)sizeof file_name - 1 +
          ((int)sizeof file_name - 1) / 2 && all > 100000, what);
}

int main(void)
{
    ui_part(&ART_APP_PART);
    if (ui_init()) {
        printf("FAIL ui_init\n1 failed\n");
        return 1;
    }
    for (int i = 0; i < 64; i++)
        memcpy(theme_name + 2 * i, "Ä", 2);
    for (int i = 0; i < 68; i++)
        memcpy(set_name + 2 * i, "Ö", 2);
    memset(file_name, 'w', sizeof file_name - 1);
    memset(curl_words, 'e', sizeof curl_words - 1);
    memset(scanned, 'q', sizeof scanned - 1);
    for (int id = 0; id < LOOK_COUNT; id++) {
        if (!look_built(id))
            continue;
        ui_look(id, 0, 1, 0);
        for (int pass = 0; pass < 2; pass++) {
            frame_begin();
            ui_end();
        }
        for (int lang = 0; lang < 2; lang++) {
            boxes = box_bad = notes = note_bad = signs = placed_bad = 0;
            first_bad[0] = 0;
            texts_of(lang);
            char what[600];
            snprintf(what, sizeof what, "%s, language %d: %d boxes, %d with a line cut, outside or under 10 px; C135 "
                     "%d not text only from the left and centred (%d signs); %d notes, %d so%s%s", look_get(id)->name,
                     lang, boxes, box_bad, placed_bad, signs, notes, note_bad, first_bad[0] ? "; first: " : "",
                     first_bad);
            check(!box_bad && !placed_bad && !signs && !note_bad && boxes > 10000 && notes > 300, what);
            name_lengths(id, lang);
            credits_fit(look_get(id)->name, lang);
        }
        if (id == 0)
            probe_matches();
    }
    printf("%d failed\n", failed);
    return failed != 0;
}
