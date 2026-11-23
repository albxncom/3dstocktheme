// AM calls (libctru 2.7.0, include/3ds/services/am.h in devkitpro/devkitarm:20260610), as the test application
// tools/theme-pack/import-test used them in console test T3 (passed 2026-11-12):
//   status, am:sys:  AMAPP_GetDLCTitleInfos, AMAPP_GetDLCContentInfoCount, AMAPP_ListDLCContentInfos,
//                    AM_GetNumPendingTitles, AM_GetPendingTitleList
//   phase 1, am:net: AMNET_InstallTitleBegin, AMNET_InstallTmdBegin, FSFILE_Write, AMNET_InstallTmdFinish,
//                    AMNET_InstallContentBegin(0), FSFILE_Write, AMNET_InstallContentFinish,
//                    AMNET_InstallTitleFinish, AMNET_CommitImportTitles
//   phase 2, am:net: AMNET_InstallTitleBegin, AMNET_CreateImportContentContexts, AMNET_InstallContentBegin,
//                    FSFILE_Write, AMNET_InstallContentFinish, AMNET_InstallTitleFinish, AMNET_CommitImportTitles
//   on a failure:    AMNET_InstallContentCancel, AMNET_InstallTmdAbort, AMNET_InstallTitleAbort (whichever is
//                    open), then AM_DeletePendingTitle for this title ID if it is still pending or unknown.
//   delete, am:sys:  AMAPP_DeleteDLCContents with one content index, the call HOME's own Delete makes (EUR 11.17
//                    wrapper 0x15e978, IPC header 0x10040102, the same as libctru's).
// Nothing here deletes a title or a ticket, and nothing names another title. HOME's SaveData.dat is only read.
#include <3ds.h>
#include <curl/curl.h>
#include <inttypes.h>
#include <malloc.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "console.h"
#include "files.h"
#include "text.h"

#define MEDIA MEDIATYPE_SD
#define WRITE_CHUNK 0x20000     // the piece size T3 wrote with
// What AM answers for a DLC title that is not installed: Anemone3DS v3.0.1 (source/themes.c l.519, dump_all_themes)
// ends its walk over the theme variations on this result of AMAPP_GetDLCContentInfoCount.
#define AM_TITLE_NOT_FOUND ((Result)0xD8A083FA)

static FILE *logfile;
static int log_strict, log_waits;

/* A fixed piece of work, timed in system ticks: about three times as fast at 804 MHz. */
static volatile u32 work_sum;

static u64 fixed_work(void)
{
    u64 began = svcGetSystemTick();
    for (u32 k = 0; k < 400000; k++)
        work_sum += k;
    return svcGetSystemTick() - began;
}

static struct {
    Result model, ptm;
    bool new3ds;
    u64 before, after;
} speed;

void console_speed_up(void)
{
    speed.model = APT_CheckNew3DS(&speed.new3ds);
    speed.ptm = ptmSysmInit();  /* only to say in the log whether the service osSetSpeedupEnable needs answers */
    if (R_SUCCEEDED(speed.ptm))
        ptmSysmExit();
    speed.before = fixed_work();
    osSetSpeedupEnable(true);
    speed.after = fixed_work();
}

void console_log_open(void)
{
    mkdir(CARD_DIR, 0777);
    struct stat st;
    if (stat(LOG_PATH, &st) == 0 && st.st_size > LOG_MAX) {
        remove(LOG_OLD);
        rename(LOG_PATH, LOG_OLD);
    }
    remove(PART_PATH);
    logfile = fopen(LOG_PATH, "a");
    if (logfile)
        setvbuf(logfile, NULL, _IOFBF, 0x4000);     /* lines wait here until console_log_flush, see console.h */
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    char version[24];
    text_version(version, sizeof version, APP_VERSION_MAJOR, APP_VERSION_MINOR, APP_VERSION_MICRO, APP_VERSION_PRE);
    console_log("==== %s %s, %04d-%02d-%02d %02d:%02d:%02d, title %016llX", APP_NAME, version,
                tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour,
                tm->tm_min, tm->tm_sec, TP_TITLE_ID);
    console_log("speed-up: %s (APT %08" PRIX32 "), ptm:sysm %08" PRIX32 " %s; a fixed piece of work took %.1f ms "
                "before and %.1f ms after asking for 804 MHz and the L2 cache", speed.new3ds ? "New 3DS" : "old 3DS",
                (u32)speed.model, (u32)speed.ptm, R_SUCCEEDED(speed.ptm) ? "ok" : "FAILED",
                (double)speed.before / CPU_TICKS_PER_MSEC, (double)speed.after / CPU_TICKS_PER_MSEC);
}

void console_log(const char *fmt, ...)
{
    if (!logfile)
        return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(logfile, fmt, ap);
    va_end(ap);
    fputc('\n', logfile);
    if (log_strict)
        fflush(logfile);
    else
        log_waits = 1;
}

void console_log_strict(int on)
{
    log_strict = on;
    if (on)
        console_log_flush();
}

void console_log_flush(void)
{
    if (logfile && log_waits)
        fflush(logfile);
    log_waits = 0;
}

void console_log_close(void)
{
    if (logfile)
        fclose(logfile);
    logfile = NULL;
}

static Result logged(const char *call, Result res)
{
    console_log("%-38s %08" PRIX32 " %s", call, (u32)res, R_SUCCEEDED(res) ? "ok" : "FAILED");
    return res;
}

void console_log_view(const char *when, const am_view *v)
{
    console_log("-- AM view %s: %s, installed %s, version %u, %u contents, pending %s", when,
                v->ok ? "read" : "NOT READ", v->exists ? "yes" : "no", v->version, (unsigned)v->count,
                v->pending == PENDING_NO ? "no" : v->pending == PENDING_YES ? "YES" : "UNKNOWN");
    for (size_t k = 0; k < v->count; k++)
        console_log("   %3u  %08" PRIX32 " %10llu  %s", v->list[k].index, v->list[k].id,
                    (unsigned long long)v->list[k].size, v->usable[k] ? "installed, owned" : "NOT installed/owned");
}

// ---------------------------------------------------------------- status (am:sys)

static int this_title_pending(void)
{
    u32 n = 0, got = 0;
    Result res = logged("AM_GetNumPendingTitles", AM_GetNumPendingTitles(&n, MEDIA));
    if (R_FAILED(res))
        return PENDING_UNKNOWN;
    if (!n)
        return PENDING_NO;
    static u64 ids[512];
    if (n > sizeof ids / sizeof *ids) {
        console_log("%" PRIu32 " pending imports, more than this application lists", n);
        return PENDING_UNKNOWN;
    }
    res = logged("AM_GetPendingTitleList", AM_GetPendingTitleList(&got, ids, n, MEDIA));
    if (R_FAILED(res))
        return PENDING_UNKNOWN;
    for (u32 i = 0; i < got; i++)
        if (ids[i] == TP_TITLE_ID)
            return PENDING_YES;
    return PENDING_NO;
}

void console_read_am(am_view *v)
{
    static AM_ContentInfo infos[PACK_CONTENTS];
    memset(v, 0, sizeof *v);
    v->pending = PENDING_UNKNOWN;
    Result res = logged("amAppInit (am:sys)", amAppInit());
    if (R_FAILED(res)) {
        v->error = (u32)res;
        return;
    }
    u64 tid = TP_TITLE_ID;
    AM_TitleInfo info;
    res = logged("AMAPP_GetDLCTitleInfos", AMAPP_GetDLCTitleInfos(&info, &tid, 1, MEDIA));
    u32 count = 0, read = 0;
    if (R_SUCCEEDED(res)) {
        v->exists = 1;
        v->version = info.version;
    } else {
        // Only AM's own "not found" means the pack is not installed; anything else is an error, shown as one, so
        // the application never advises a reinstall of an installed pack.
        Result again = logged("AMAPP_GetDLCContentInfoCount", AMAPP_GetDLCContentInfoCount(&count, MEDIA, tid));
        if (again != AM_TITLE_NOT_FOUND) {
            v->error = (u32)res;
            amExit();
            return;
        }
    }
    if (v->exists) {
        res = logged("AMAPP_GetDLCContentInfoCount", AMAPP_GetDLCContentInfoCount(&count, MEDIA, tid));
        if (R_SUCCEEDED(res) && count > PACK_CONTENTS) {
            console_log("The TMD lists %" PRIu32 " contents, more than %d", count, PACK_CONTENTS);
            res = -1;
        }
        if (R_SUCCEEDED(res))
            res = logged("AMAPP_ListDLCContentInfos", AMAPP_ListDLCContentInfos(&read, MEDIA, tid, count, 0, infos));
        if (R_FAILED(res)) {
            v->error = (u32)res;
            amExit();
            return;
        }
        for (u32 i = 0; i < read && i < PACK_CONTENTS; i++) {
            v->list[v->count] = (tp_am_content){infos[i].index, infos[i].contentId, infos[i].size};
            v->usable[v->count++] = (infos[i].flags & (AM_CONTENT_INSTALLED | AM_CONTENT_OWNED)) ==
                                    (AM_CONTENT_INSTALLED | AM_CONTENT_OWNED);
        }
    }
    v->pending = this_title_pending();
    v->ok = 1;
    amExit();
}

// ---------------------------------------------------------------- the installed content 0

static FS_Archive content_archive;
static int catalogue_mounted;

static int read_archive(void *ctx, uint8_t *buf, size_t cap, size_t *size)
{
    (void)ctx;
    int r = files_read("pack:/ContentInfoArchive_EUR_en.bin", buf, cap, size);
    if (r)
        console_log("Cannot read ContentInfoArchive_EUR_en.bin of the installed content 0 (%d)", r);
    return r ? -1 : 0;
}

static int read_icon(void *ctx, uint16_t index, uint8_t icon[TP_ICON_SIZE])
{
    (void)ctx;
    char path[48];
    size_t n = 0;
    snprintf(path, sizeof path, "pack:/icons/%u.icn", index);
    int r = files_read(path, icon, TP_ICON_SIZE, &n);
    if (r || n != TP_ICON_SIZE) {
        console_log("Cannot read %s of the installed content 0 (%d, %u bytes)", path, r, (unsigned)n);
        return -1;
    }
    return 0;
}

int console_open_catalogue(catalogue_reader *reader)
{
    static const u32 archive_path[4] = {(u32)TP_TITLE_ID, (u32)(TP_TITLE_ID >> 32), MEDIA, 0};
    static const u32 content0[5] = {0, 0, 0, 0, 0};    // [1] is the content index
    FS_Path ap = {PATH_BINARY, sizeof archive_path, archive_path};
    FS_Path fp = {PATH_BINARY, sizeof content0, content0};
    Handle file;
    if (R_FAILED(logged("FSUSER_OpenArchive (content of 09)",
                        FSUSER_OpenArchive(&content_archive, ARCHIVE_SAVEDATA_AND_CONTENT, ap))))
        return -1;
    if (R_FAILED(logged("FSUSER_OpenFile (content 0)", FSUSER_OpenFile(&file, content_archive, fp, FS_OPEN_READ, 0))) ||
        R_FAILED(logged("romfsMountFromFile (content 0)", romfsMountFromFile(file, 0, "pack")))) {
        FSUSER_CloseArchive(content_archive);
        return -1;
    }
    catalogue_mounted = 1;
    *reader = (catalogue_reader){NULL, read_archive, read_icon};
    return 0;
}

void console_close_catalogue(void)
{
    if (!catalogue_mounted)
        return;
    romfsUnmount("pack");   // closes the file handle it took
    FSUSER_CloseArchive(content_archive);
    catalogue_mounted = 0;
}

int console_content_music(uint16_t index, uint32_t *error)
{
    static const u32 archive_path[4] = {(u32)TP_TITLE_ID, (u32)(TP_TITLE_ID >> 32), MEDIA, 0};
    const u32 content[5] = {0, index, 0, 0, 0};
    FS_Path ap = {PATH_BINARY, sizeof archive_path, archive_path};
    FS_Path fp = {PATH_BINARY, sizeof content, content};
    FS_Archive archive;
    Handle file;
    *error = 0;
    if (index == 0 || index > TP_INDEX_MAX)
        return MUSIC_UNKNOWN;
    Result res = FSUSER_OpenArchive(&archive, ARCHIVE_SAVEDATA_AND_CONTENT, ap);
    if (R_SUCCEEDED(res)) {
        res = FSUSER_OpenFile(&file, archive, fp, FS_OPEN_READ, 0);
        if (R_SUCCEEDED(res)) {
            /* not closed after a failed mount, as for the catalogue: the mount may have closed it, and a closed
             * handle's number can already belong to something else */
            res = romfsMountFromFile(file, 0, "theme");
        }
        if (R_FAILED(res))
            FSUSER_CloseArchive(archive);
    }
    if (R_FAILED(res)) {
        *error = (u32)res;
        return MUSIC_UNKNOWN;
    }
    struct stat st;
    int music = stat("theme:/bgm.bcstm", &st) == 0 ? MUSIC_YES : MUSIC_NONE;
    romfsUnmount("theme");  // closes the file handle it took
    FSUSER_CloseArchive(archive);
    return music;
}

// ---------------------------------------------------------------- the importer (am:net)

static struct {
    int session, title_open, tmd_open, content_open;
    Handle tmd, content;
    u64 offset;
    size_t fill;
} imp_state;

static u8 write_buf[WRITE_CHUNK];

static int session(void)
{
    if (imp_state.session)
        return 0;
    if (R_FAILED(logged("amInit (am:net)", amInit())))
        return -1;
    imp_state.session = 1;
    return 0;
}

static void end_session(void)
{
    if (imp_state.session)
        amExit();
    imp_state.session = 0;
}

static int imp_title_begin(void *ctx)
{
    (void)ctx;
    if (session())
        return -1;
    if (R_FAILED(logged("AMNET_InstallTitleBegin", AMNET_InstallTitleBegin(MEDIA, TP_TITLE_ID, false))))
        return -1;
    imp_state.title_open = 1;
    return 0;
}

static int imp_tmd(void *ctx, const uint8_t *tmd, size_t size)
{
    (void)ctx;
    if (R_FAILED(logged("AMNET_InstallTmdBegin", AMNET_InstallTmdBegin(&imp_state.tmd))))
        return -1;
    imp_state.tmd_open = 1;
    u32 written = 0;
    Result res = logged("FSFILE_Write (TMD)", FSFILE_Write(imp_state.tmd, &written, 0, tmd, size, 0));
    if (R_FAILED(res) || written != size) {
        console_log("TMD: %" PRIu32 " of %u bytes written", written, (unsigned)size);
        return -1;
    }
    imp_state.tmd_open = 0;     // Finish takes the handle, whatever it returns
    return R_FAILED(logged("AMNET_InstallTmdFinish", AMNET_InstallTmdFinish(imp_state.tmd, true))) ? -1 : 0;
}

static int imp_contexts(void *ctx, const uint16_t *indices, size_t count)
{
    (void)ctx;
    u16 copy[8];
    if (count > 8)
        return -1;
    memcpy(copy, indices, count * sizeof *copy);
    char call[64];
    snprintf(call, sizeof call, "AMNET_CreateImportContentContexts %u", count ? copy[0] : 0);
    return R_FAILED(logged(call, AMNET_CreateImportContentContexts(count, copy))) ? -1 : 0;
}

static int imp_content_begin(void *ctx, uint16_t index)
{
    (void)ctx;
    char call[64];
    snprintf(call, sizeof call, "AMNET_InstallContentBegin %u", index);
    if (R_FAILED(logged(call, AMNET_InstallContentBegin(&imp_state.content, index))))
        return -1;
    imp_state.content_open = 1;
    imp_state.offset = 0;
    imp_state.fill = 0;
    return 0;
}

static int flush(void)
{
    if (!imp_state.fill)
        return 0;
    u32 written = 0;
    Result res = FSFILE_Write(imp_state.content, &written, imp_state.offset, write_buf, imp_state.fill, 0);
    if (R_FAILED(res) || written != imp_state.fill) {
        logged("FSFILE_Write (content)", res);
        console_log("Write stopped at byte %llu", (unsigned long long)imp_state.offset);
        return -1;
    }
    imp_state.offset += imp_state.fill;
    imp_state.fill = 0;
    return 0;
}

static int imp_content_write(void *ctx, const void *buf, size_t len)
{
    (void)ctx;
    const u8 *p = buf;
    while (len) {
        size_t n = WRITE_CHUNK - imp_state.fill < len ? WRITE_CHUNK - imp_state.fill : len;
        memcpy(write_buf + imp_state.fill, p, n);
        imp_state.fill += n;
        p += n;
        len -= n;
        if (imp_state.fill == WRITE_CHUNK && flush())
            return -1;
    }
    return 0;
}

static int imp_content_finish(void *ctx)
{
    (void)ctx;
    if (flush())
        return -1;
    console_log("FSFILE_Write (content): %llu bytes", (unsigned long long)imp_state.offset);
    imp_state.content_open = 0; // Finish takes the handle
    return R_FAILED(logged("AMNET_InstallContentFinish", AMNET_InstallContentFinish(imp_state.content))) ? -1 : 0;
}

static int imp_commit(void *ctx)
{
    (void)ctx;
    if (R_FAILED(logged("AMNET_InstallTitleFinish", AMNET_InstallTitleFinish())))
        return -1;
    imp_state.title_open = 0;   // now it waits for the commit; a failure is cleaned up through the pending list
    u64 tid = TP_TITLE_ID;
    // temp = false, as FBI and T3: other titles' imports are left alone.
    Result res = logged("AMNET_CommitImportTitles", AMNET_CommitImportTitles(MEDIA, 1, false, &tid));
    if (R_FAILED(res))
        return -1;
    end_session();
    return 0;
}

static void imp_cancel(void *ctx)
{
    (void)ctx;
    console_log("== Failure: cancelling this title's own import, nothing else");
    if (!imp_state.session && session())
        return;
    if (imp_state.content_open)
        logged("AMNET_InstallContentCancel", AMNET_InstallContentCancel(imp_state.content));
    if (imp_state.tmd_open)
        logged("AMNET_InstallTmdAbort", AMNET_InstallTmdAbort(imp_state.tmd));
    if (imp_state.title_open)
        logged("AMNET_InstallTitleAbort", AMNET_InstallTitleAbort());
    imp_state.content_open = imp_state.tmd_open = imp_state.title_open = 0;
    if (this_title_pending() != PENDING_NO)    // also when unknown: the call names this title ID only
        logged("AM_DeletePendingTitle (this ID)", AM_DeletePendingTitle(MEDIA, TP_TITLE_ID));
    end_session();
}

void console_importer(importer *imp)
{
    memset(&imp_state, 0, sizeof imp_state);
    *imp = (importer){NULL, imp_title_begin, imp_tmd, imp_contexts, imp_content_begin, imp_content_write,
                      imp_content_finish, imp_commit, imp_cancel};
}

// ---------------------------------------------------------------- HOME's data and the delete

#define HOME_EXTDATA 0x00000098u    // the EUR HOME Menu's SD extdata (Anemone3DS fs.c, region 2)

static void log_entry(const char *what, const home_entry *e)
{
    console_log("   %-9s index %3" PRIu32 ", variation %u, type %u%s", what, e->index, e->variation, e->type,
                e->type == HOME_TYPE_DLC && e->variation == HOME_PACK_VARIATION ? " (the pack)" : "");
}

void console_read_home(home_state *h)
{
    static u8 save[HOME_SAVE_SIZE + 1];
    static const u32 path[3] = {MEDIATYPE_SD, HOME_EXTDATA, 0};
    FS_Archive archive;
    Handle file;
    u64 size = 0;
    u32 read = 0;
    int got = 0;
    if (R_SUCCEEDED(logged("FSUSER_OpenArchive (HOME extdata 98)",
                           FSUSER_OpenArchive(&archive, ARCHIVE_EXTDATA, (FS_Path){PATH_BINARY, sizeof path, path})))) {
        if (R_SUCCEEDED(logged("FSUSER_OpenFile (SaveData.dat, read)",
                               FSUSER_OpenFile(&file, archive, fsMakePath(PATH_ASCII, "/SaveData.dat"), FS_OPEN_READ,
                                               0)))) {
            got = R_SUCCEEDED(logged("FSFILE_GetSize (SaveData.dat)", FSFILE_GetSize(file, &size))) &&
                  size <= HOME_SAVE_SIZE &&
                  R_SUCCEEDED(logged("FSFILE_Read (SaveData.dat)", FSFILE_Read(file, &read, 0, save, (u32)size))) &&
                  read == size;
            FSFILE_Close(file);
        }
        FSUSER_CloseArchive(archive);
    }
    home_parse(h, got ? save : NULL, (size_t)read);
    console_log("-- HOME's SaveData.dat: %s, %llu bytes, shuffle %s", h->ok ? "read" : "NOT READ",
                (unsigned long long)size, h->shuffle_on ? "on" : "off");
    if (!h->ok)
        return;
    log_entry("in use", &h->current);
    for (int i = 0; i < HOME_SHUFFLE; i++)
        if (h->shuffle[i].type)
            log_entry("shuffle", &h->shuffle[i]);
}

static uint32_t delete_content(void *ctx, uint64_t title_id, uint16_t index)
{
    (void)ctx;
    if (title_id != TP_TITLE_ID || index == 0 || index > TP_INDEX_MAX) {
        console_log("Refused to delete content %u of %016llX: not a theme of the pack", index,
                    (unsigned long long)title_id);
        return 0xFFFFFFFFu;
    }
    Result res = logged("amAppInit (am:sys)", amAppInit());
    if (R_FAILED(res))
        return (u32)res;
    u16 indices[1] = {index};
    char call[64];
    snprintf(call, sizeof call, "AMAPP_DeleteDLCContents %u", index);
    res = logged(call, AMAPP_DeleteDLCContents(indices, 1, TP_TITLE_ID, MEDIA));
    amExit();
    return R_FAILED(res) ? (u32)res : 0;
}

void console_deleter(deleter *d)
{
    *d = (deleter){NULL, delete_content};
}

int console_language(void)
{
    u8 lang = CFG_LANGUAGE_EN;
    if (R_SUCCEEDED(cfguInit())) {
        CFGU_GetSystemLanguage(&lang);
        cfguExit();
    }
    return lang == CFG_LANGUAGE_DE ? LANG_DE : LANG_EN;
}

// ---------------------------------------------------------------- network

#define SOC_SIZE 0x100000
static u32 *soc_buffer;
static int curl_ready;

int console_wifi(void)
{
    if (R_FAILED(logged("acInit", acInit())))
        return -1;
    u32 status = 0;
    Result rc = logged("ACU_GetWifiStatus", ACU_GetWifiStatus(&status));
    acExit();
    if (R_FAILED(rc))
        return -1;
    console_log("wireless: status %lu", (unsigned long)status);
    return status != 0;
}

int console_net_start(void)
{
    if (soc_buffer)
        return 0;
    soc_buffer = memalign(0x1000, SOC_SIZE);
    if (!soc_buffer)
        return -1;
    if (R_FAILED(logged("socInit", socInit(soc_buffer, SOC_SIZE)))) {
        free(soc_buffer);
        soc_buffer = NULL;
        return -1;
    }
    if (!curl_ready && curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK)
        curl_ready = 1;
    return curl_ready ? 0 : -1;
}

void console_net_stop(void)
{
    if (curl_ready)
        curl_global_cleanup();
    curl_ready = 0;
    if (soc_buffer) {
        socExit();
        free(soc_buffer);
    }
    soc_buffer = NULL;
}

const char *console_user_agent(void)
{
    static char agent[64];
    char version[24];
    text_version(version, sizeof version, APP_VERSION_MAJOR, APP_VERSION_MINOR, APP_VERSION_MICRO, APP_VERSION_PRE);
    snprintf(agent, sizeof agent, "%s/%s (Nintendo 3DS)", APP_NAME, version);
    return agent;
}
