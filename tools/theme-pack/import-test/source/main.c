// Theme import test (console test T3): imports a delta made by theme_pack.py from sdmc:/theme-pack/delta/ into the
// installed theme DLC title 0004008C00009809 without deleting it, and shows what AM reports before and after.
// Only this title, only the SD card. It never deletes a title or a ticket; on a failure it removes only this
// title's own pending import. Every AM result goes to the top screen and to sdmc:/theme-pack/import-test.log.
//
// AM calls (libctru 2.7.0, include/3ds/services/am.h in devkitpro/devkitarm:20260610), in the order FBI's CDN
// installer used them for DLC (FBI, source/fbi/action/installcdn.c at dd0ccff^, l.88-115):
//   status, am:sys:  AMAPP_GetDLCTitleInfos, AMAPP_GetDLCContentInfoCount, AMAPP_ListDLCContentInfos,
//                    AM_GetNumPendingTitles, AM_GetPendingTitleList, AM_GetPendingTitleInfo,
//                    AM_GetNumImportContentContexts, AM_GetImportContentContextList
//   phase 1, am:net: AMNET_InstallTitleBegin, AMNET_InstallTmdBegin, FSFILE_Write, AMNET_InstallTmdFinish,
//                    AMNET_InstallContentBegin(0), FSFILE_Write, AMNET_InstallContentFinish,
//                    AMNET_InstallTitleFinish, AMNET_CommitImportTitles
//   phase 2, am:net: AMNET_InstallTitleBegin, AMNET_CreateImportContentContexts, per content
//                    AMNET_InstallContentBegin, FSFILE_Write, AMNET_InstallContentFinish; AMNET_InstallTitleFinish,
//                    AMNET_CommitImportTitles
//   on a failure:    AMNET_InstallContentCancel, AMNET_InstallTmdAbort, AMNET_InstallTitleAbort (whichever is open),
//                    then AM_DeletePendingTitle for this title ID if it is still pending (or the query failed).
//                    What AM does with the installed contents on such a cancel is untested: Azahar's AM deletes
//                    the title's content/ folder when an import whose TMD was accepted is cancelled.
#include <3ds.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "delta.h"

#define TITLE_ID DELTA_TITLE_ID
#define MEDIA MEDIATYPE_SD
#define BASE_DIR "sdmc:/theme-pack"
#define DELTA_DIR BASE_DIR "/delta"
#define LOG_PATH BASE_DIR "/import-test.log"
#define CHUNK 0x20000

static PrintConsole top, bottom;
static FILE *logfile;
static u8 io[CHUNK];

static u8 tmd_raw[DELTA_TMD_MAX];
static size_t tmd_len;
static Tmd delta_tmd;
static bool present[DELTA_MAX_CONTENTS];
static Plan plan;
static Installed now, before, middle;
enum { PENDING_NO, PENDING_YES, PENDING_UNKNOWN };
static int pending;  // whether AM has an import (import.db entry) for this title

static Handle tmd_handle, content_handle;
static bool tmd_open, content_open, title_open;

static void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void say(const char *fmt, ...) {
    char line[320];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    consoleSelect(&top);
    printf("%s\n", line);
    if (logfile) {
        fprintf(logfile, "%s\n", line);
        fflush(logfile);
    }
}

static Result am(const char *call, Result res) {
    say("%-30s %08" PRIX32 " %s", call, (u32)res, R_SUCCEEDED(res) ? "ok" : "FAILED");
    return res;
}

static void menu(const char *text) {
    consoleSelect(&bottom);
    consoleClear();
    printf("Theme import test\nTitle %016llX, SD only\n\n%s", TITLE_ID, text);
}

static const char *import_state(u8 status) {
    static const char *names[] = {"none", "waiting for import", "resumable", "waiting for commit",
                                  "already exists", "deleting", "needs cleanup"};
    return status < 7 ? names[status] : "unknown";
}

static const InstalledContent *find(const Installed *st, u16 index) {
    for (unsigned i = 0; i < st->count; i++)
        if (st->contents[i].index == index) return &st->contents[i];
    return NULL;
}

static int this_title_pending(void) {
    u32 n = 0, got = 0;
    Result res = am("AM_GetNumPendingTitles", AM_GetNumPendingTitles(&n, MEDIA));
    if (R_SUCCEEDED(res) && n) {
        u64 *ids = calloc(n, sizeof *ids);
        if (!ids) {
            say("Pending import of this title: UNKNOWN (no memory for %" PRIu32 " title IDs)", n);
            return PENDING_UNKNOWN;
        }
        res = am("AM_GetPendingTitleList", AM_GetPendingTitleList(&got, ids, n, MEDIA));
        bool mine = false;
        for (u32 i = 0; R_SUCCEEDED(res) && i < got; i++) mine |= ids[i] == TITLE_ID;
        free(ids);
        if (R_SUCCEEDED(res)) {
            say("Pending imports on SD: %" PRIu32 ", of this title: %s", n, mine ? "YES" : "no");
            return mine ? PENDING_YES : PENDING_NO;
        }
    }
    if (R_FAILED(res)) {
        say("Pending import of this title: UNKNOWN (AM error %08" PRIX32 ")", (u32)res);
        return PENDING_UNKNOWN;
    }
    say("Pending imports on SD: 0, of this title: no");
    return PENDING_NO;
}

// ---------------------------------------------------------------- status (read only, am:sys)

static void read_status(Installed *st) {
    memset(st, 0, sizeof *st);
    pending = PENDING_UNKNOWN;
    say("== Status (read only)");
    if (R_FAILED(am("amAppInit (am:sys)", amAppInit()))) return;
    u64 tid = TITLE_ID;
    AM_TitleInfo info;
    if (R_SUCCEEDED(am("AMAPP_GetDLCTitleInfos", AMAPP_GetDLCTitleInfos(&info, &tid, 1, MEDIA)))) {
        st->exists = true;
        st->version = info.version;
        st->title_type = info.titleType;
        say("Installed: yes, title version %u, title type %08" PRIX32, info.version, info.titleType);
    } else {
        say("Installed: no (or AM refused, see the code above)");
    }
    u32 count = 0;
    if (st->exists && R_SUCCEEDED(am("AMAPP_GetDLCContentInfoCount", AMAPP_GetDLCContentInfoCount(&count, MEDIA, tid)))) {
        static AM_ContentInfo infos[DELTA_MAX_CONTENTS];
        u32 read = 0;
        if (count > DELTA_MAX_CONTENTS) {
            say("The TMD lists %" PRIu32 " contents, more than %d", count, DELTA_MAX_CONTENTS);
        } else if (R_SUCCEEDED(am("AMAPP_ListDLCContentInfos",
                                  AMAPP_ListDLCContentInfos(&read, MEDIA, tid, count, 0, infos)))) {
            say("TMD contents: %" PRIu32, read);
            say(" index content ID      size  installed owned");
            for (u32 i = 0; i < read && i < DELTA_MAX_CONTENTS; i++) {
                InstalledContent *c = &st->contents[st->count++];
                c->index = infos[i].index;
                c->type = infos[i].type;
                c->id = infos[i].contentId;
                c->size = infos[i].size;
                c->installed = infos[i].flags & AM_CONTENT_INSTALLED;
                c->owned = infos[i].flags & AM_CONTENT_OWNED;
                say(" %5u  %08" PRIX32 " %9llu  %-9s %s", c->index, c->id, c->size, c->installed ? "yes" : "NO",
                    c->owned ? "yes" : "NO");
            }
        }
    }
    pending = this_title_pending();
    if (pending == PENDING_YES) {
        AM_PendingTitleInfo p;
        if (R_SUCCEEDED(am("AM_GetPendingTitleInfo", AM_GetPendingTitleInfo(1, MEDIA, &tid, &p))))
            say("  pending import: version %u, state %u (%s)", p.version, p.status, import_state(p.status));
        u32 n = 0;
        if (R_SUCCEEDED(am("AM_GetNumImportContentContexts", AM_GetNumImportContentContexts(&n, tid, MEDIA))) && n) {
            static u16 indices[DELTA_MAX_CONTENTS];
            u32 got = 0;
            if (R_SUCCEEDED(am("AM_GetImportContentContextList",
                               AM_GetImportContentContextList(&got, indices, n < DELTA_MAX_CONTENTS ? n : DELTA_MAX_CONTENTS,
                                                              tid, MEDIA))))
                for (u32 i = 0; i < got; i++) say("  pending content %u", indices[i]);
        }
    }
    amExit();
}

// ---------------------------------------------------------------- delta check (read only)

static bool check_delta(void) {
    say("== Checking %s (nothing is written)", DELTA_DIR);
    memset(present, 0, sizeof present);
    if (delta_load(DELTA_DIR, tmd_raw, &tmd_len, &delta_tmd, present, say)) return false;
    if (pending == PENDING_YES) {
        say("REFUSED: this title has a pending import. Remove it first (L+R+Y), then check again.");
        return false;
    }
    if (pending != PENDING_NO) {
        say("REFUSED: whether this title has a pending import is unknown (AM error in the status). SELECT reads "
            "the status again.");
        return false;
    }
    if (delta_plan(&delta_tmd, present, &now, &plan, say)) return false;
    if (plan.phase1)
        say("Phase 1: TMD version %u -> %u and content 0 (ID %08" PRIX32 "), commit.", now.version,
            delta_tmd.version, delta_tmd.contents[0].id);
    char list[160] = "";
    for (unsigned i = 0; i < plan.count; i++)
        snprintf(list + strlen(list), sizeof list - strlen(list), " %u", plan.indices[i]);
    say("Phase 2: content(s)%s, commit.", list);
    say("Nothing is deleted; contents not listed stay as they are.");
    return true;
}

// ---------------------------------------------------------------- import (am:net)

static bool write_file(Handle h, const char *path, u64 size) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        say("Cannot open %s", path);
        return false;
    }
    u64 offset = 0;
    bool ok = true;
    while (ok && offset < size) {
        size_t n = fread(io, 1, CHUNK, f);
        u32 written = 0;
        Result res = n ? FSFILE_Write(h, &written, offset, io, n, 0) : 0;
        if (!n || R_FAILED(res) || written != n) {
            say("Write of %s stopped at byte %llu", path, offset);
            if (n) am("FSFILE_Write", res);
            ok = false;
        }
        offset += n;
    }
    fclose(f);
    if (ok) say("FSFILE_Write %s: %llu bytes", path, offset);
    return ok;
}

static void log_contexts(void) {
    u32 n = 0;
    if (R_FAILED(am("AMNET_GetNumCurrentImportContentContexts", AMNET_GetNumCurrentImportContentContexts(&n)))) return;
    static u16 indices[DELTA_MAX_CONTENTS];
    u32 got = 0;
    char list[160] = "";
    if (n && R_SUCCEEDED(am("AMNET_GetCurrentImportContentContextsList",
                            AMNET_GetCurrentImportContentContextsList(&got, indices,
                                                                      n < DELTA_MAX_CONTENTS ? n : DELTA_MAX_CONTENTS))))
        for (u32 i = 0; i < got; i++) snprintf(list + strlen(list), sizeof list - strlen(list), " %u", indices[i]);
    say("  import contexts AM holds now: %" PRIu32 ":%s", n, list);
}

static bool import_content(u16 index) {
    const TmdContent *c = NULL;
    for (unsigned i = 0; i < delta_tmd.count; i++)
        if (delta_tmd.contents[i].index == index) c = &delta_tmd.contents[i];
    char path[64], call[48];
    snprintf(path, sizeof path, DELTA_DIR "/%u.app", index);
    snprintf(call, sizeof call, "AMNET_InstallContentBegin %u", index);
    if (!c || R_FAILED(am(call, AMNET_InstallContentBegin(&content_handle, index)))) return false;
    content_open = true;
    if (!write_file(content_handle, path, c->size)) return false;
    content_open = false;  // Finish takes the handle, whatever it returns
    snprintf(call, sizeof call, "AMNET_InstallContentFinish %u", index);
    return R_SUCCEEDED(am(call, AMNET_InstallContentFinish(content_handle)));
}

static bool finish_and_commit(void) {
    if (R_FAILED(am("AMNET_InstallTitleFinish", AMNET_InstallTitleFinish()))) return false;
    title_open = false;  // now it waits for the commit; cleanup removes a leftover through the pending list
    u64 tid = TITLE_ID;
    // temp/cleanup = false, as FBI: no cleanup of other titles' imports.
    return R_SUCCEEDED(am("AMNET_CommitImportTitles", AMNET_CommitImportTitles(MEDIA, 1, false, &tid)));
}

static bool phase1(void) {
    say("== Phase 1: new TMD and content 0, then commit");
    if (R_FAILED(am("AMNET_InstallTitleBegin", AMNET_InstallTitleBegin(MEDIA, TITLE_ID, false)))) return false;
    title_open = true;
    if (R_FAILED(am("AMNET_InstallTmdBegin", AMNET_InstallTmdBegin(&tmd_handle)))) return false;
    tmd_open = true;
    u32 written = 0;
    if (R_FAILED(am("FSFILE_Write (TMD)", FSFILE_Write(tmd_handle, &written, 0, tmd_raw, tmd_len, 0))) ||
        written != tmd_len) {
        say("TMD: %" PRIu32 " of %u bytes written", written, (unsigned)tmd_len);
        return false;
    }
    tmd_open = false;  // Finish takes the handle
    if (R_FAILED(am("AMNET_InstallTmdFinish", AMNET_InstallTmdFinish(tmd_handle, true)))) return false;
    log_contexts();
    return import_content(0) && finish_and_commit();
}

static bool phase2(void) {
    say("== Phase 2: %u new content(s), then commit", plan.count);
    if (R_FAILED(am("AMNET_InstallTitleBegin", AMNET_InstallTitleBegin(MEDIA, TITLE_ID, false)))) return false;
    title_open = true;
    if (R_FAILED(am("AMNET_CreateImportContentContexts", AMNET_CreateImportContentContexts(plan.count, plan.indices))))
        return false;
    log_contexts();
    for (unsigned i = 0; i < plan.count; i++)
        if (!import_content(plan.indices[i])) return false;
    return finish_and_commit();
}

static void cleanup(void) {
    say("== Failure: removing this title's own import, nothing else");
    if (content_open) am("AMNET_InstallContentCancel", AMNET_InstallContentCancel(content_handle));
    if (tmd_open) am("AMNET_InstallTmdAbort", AMNET_InstallTmdAbort(tmd_handle));
    if (title_open) am("AMNET_InstallTitleAbort", AMNET_InstallTitleAbort());
    content_open = tmd_open = title_open = false;
    if (this_title_pending() != PENDING_NO)  // also when unknown: the call names this title ID only
        am("AM_DeletePendingTitle (this ID)", AM_DeletePendingTitle(MEDIA, TITLE_ID));
}

static void compare(const Installed *a, const Installed *b, const char *when, bool final) {
    int lost = 0, missing = 0;
    say("== %s, per content of the delta TMD:", when);
    for (unsigned i = 0; i < delta_tmd.count; i++) {
        const TmdContent *t = &delta_tmd.contents[i];
        const InstalledContent *x = find(a, t->index), *y = find(b, t->index);
        bool was = x && x->installed, is = y && y->installed;
        const char *verdict;
        if (t->index == 0)
            verdict = is && y->id == t->id ? "NEW CATALOGUE" : is ? "old catalogue" : "NO CATALOGUE";
        else if (was && is) verdict = "KEPT";
        else if (was) verdict = "LOST";
        else if (is) verdict = "NEW";
        else verdict = present[t->index] ? "not installed (yet)" : "absent before and after";
        lost += t->index && was && !is;
        missing += t->index && !is && present[t->index];
        say(" %3u  before %-3s after %-3s owned %-3s %s", t->index, was ? "yes" : "no", is ? "yes" : "no",
            y && y->owned ? "yes" : "no", verdict);
    }
    say("Contents installed before are all still installed: %s.", lost ? "NO" : "yes");
    if (final) say("The new contents are installed: %s.", missing ? "NO" : "yes");
}

static void do_import(void) {
    say("== Import into %016llX", TITLE_ID);
    read_status(&before);
    if (pending != PENDING_NO || delta_plan(&delta_tmd, present, &before, &plan, say)) {
        say("The state changed since the check. Nothing was written.");
        return;
    }
    aptSetHomeAllowed(false);
    aptSetSleepAllowed(false);
    bool committed1 = !plan.phase1;
    bool ok = R_SUCCEEDED(am("amInit (am:net)", amInit()));
    if (ok && plan.phase1) {
        ok = phase1();
        if (!ok) cleanup();
        amExit();
        if (ok) {
            committed1 = true;
            read_status(&middle);
            compare(&before, &middle, "After phase 1", false);
            ok = R_SUCCEEDED(am("amInit (am:net)", amInit()));
        }
    }
    if (ok) {
        ok = phase2();
        if (!ok) cleanup();
        amExit();
    }
    aptSetHomeAllowed(true);
    aptSetSleepAllowed(true);
    read_status(&now);
    compare(&before, &now, ok ? "After the import" : "After the failure", true);
    if (ok)
        say("Done. Restart the console, then check HOME's theme list. On the Mac: theme_pack.py installed "
            "<data>/out/delta");
    else if (!committed1)
        say("Phase 1 did not complete and its import was cancelled. The installed themes may be gone with that "
            "cancel (what real AM does then is untested; an emulator's AM deletes the title's contents). The "
            "KEPT/LOST lines above are what AM reports now. If anything is LOST: reinstall the pack with FBI from "
            "/theme-pack/theme-pack.cia.");
    else
        say("Phase 2 did not complete and its import was cancelled. Phase 1 was committed: new TMD and catalogue. "
            "The KEPT/LOST lines above are what AM reports now. Nothing LOST: check the delta again to retry phase "
            "2. Anything LOST: reinstall the pack with FBI from /theme-pack/theme-pack.cia.");
}

static void remove_pending(void) {
    say("== Removing the pending import of %016llX (this title only)", TITLE_ID);
    if (R_SUCCEEDED(am("amInit (am:net)", amInit()))) {
        am("AM_DeletePendingTitle (this ID)", AM_DeletePendingTitle(MEDIA, TITLE_ID));
        amExit();
    }
    read_status(&now);
}

int main(void) {
    gfxInitDefault();
    consoleInit(GFX_TOP, &top);
    consoleInit(GFX_BOTTOM, &bottom);
    mkdir(BASE_DIR, 0777);
    logfile = fopen(LOG_PATH, "a");
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    say("==== theme import test %04d-%02d-%02d %02d:%02d:%02d, title %016llX", tm->tm_year + 1900, tm->tm_mon + 1,
        tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec, TITLE_ID);
    if (!logfile) say("Cannot write %s: the screen is the only record.", LOG_PATH);
    read_status(&now);

    enum { MENU, READY, REFUSED } state = MENU;
    bool redraw = true;
    while (aptMainLoop()) {
        if (redraw) {
            if (state == MENU)
                menu(pending == PENDING_YES ? "A: check the delta (reads only)\nSELECT: read the status again\n"
                                            "L+R+Y: remove this title's\n  pending import\nSTART: exit\n"
                                          : "A: check the delta in\n  sdmc:/theme-pack/delta/ (reads only)\n"
                                            "SELECT: read the status again\nSTART: exit\n");
            else if (state == READY)
                menu("The plan is on the top screen.\n\nHold L and R, then press A:\n  import (writes this title)\n\n"
                     "B: back, nothing is written\n");
            else
                menu("Nothing can be imported (top screen).\nNothing was written.\n\nB: back\n");
            redraw = false;
        }
        hidScanInput();
        u32 down = hidKeysDown(), held = hidKeysHeld();
        bool chord = (held & KEY_L) && (held & KEY_R);
        if (down & KEY_START) break;
        if (state == MENU && (down & KEY_SELECT)) {
            read_status(&now);
            redraw = true;
        } else if (state == MENU && (down & KEY_A)) {
            state = check_delta() ? READY : REFUSED;
            redraw = true;
        } else if (state == MENU && pending == PENDING_YES && (down & KEY_Y) && chord) {
            remove_pending();
            redraw = true;
        } else if (state == READY && (down & KEY_A) && chord) {
            do_import();
            state = MENU;
            redraw = true;
        } else if (state != MENU && (down & KEY_B)) {
            state = MENU;
            redraw = true;
        }
        gspWaitForVBlank();
    }
    if (logfile) fclose(logfile);
    gfxExit();
    return 0;
}
