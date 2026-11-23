#include "badge_console.h"

#include <3ds.h>
#include <inttypes.h>

#ifdef APP_DEMO

#include "badge_dir_store.h"

static badge_dir pretend;

void console_badge_store(badge_store *s)
{
    badge_dir_store(s, &pretend, BADGE_APP_DIR, 1);
}

#else

#define BADGE_EXTDATA 0x000014D1u       /* HOME's badge extdata on the SD card (the design note, section 1) */
#define SHORT_IO (-1)                   /* a read or write that moved fewer bytes than asked: no service code */

typedef struct home_badges {
    FS_Archive archive;
    Handle file[BADGE_FILE_COUNT];
    int archive_open, file_open[BADGE_FILE_COUNT];
    int write;                          /* opened read and write; read only: nothing to flush */
} home_badges;

static home_badges home;
static const char *const PATH[BADGE_FILE_COUNT] = {"/BadgeData.dat", "/BadgeMngFile.dat"};

static int failed(const char *call, int file, Result r)
{
    console_log("%-38s %08" PRIX32 " FAILED (%s)", call, (u32)r, file >= 0 ? PATH[file] + 1 : "extdata 000014d1");
    return (int)r;
}

static int home_close(void *ctx)
{
    home_badges *h = ctx;
    int first = 0;
    for (int i = 0; i < BADGE_FILE_COUNT; i++) {
        if (!h->file_open[i])
            continue;
        Result r = h->write ? FSFILE_Flush(h->file[i]) : 0;
        if (R_FAILED(r)) {
            int code = failed("FSFILE_Flush", i, r);
            first = first ? first : code;
        }
        r = FSFILE_Close(h->file[i]);
        if (R_FAILED(r)) {
            int code = failed("FSFILE_Close", i, r);
            first = first ? first : code;
        }
        h->file_open[i] = 0;
    }
    if (h->archive_open) {
        Result r = FSUSER_CloseArchive(h->archive);
        if (R_FAILED(r)) {
            int code = failed("FSUSER_CloseArchive", -1, r);
            first = first ? first : code;
        }
        h->archive_open = 0;
    }
    return first;
}

static int home_open(void *ctx, int mode)
{
    home_badges *h = ctx;
    static const u32 path[3] = {MEDIATYPE_SD, BADGE_EXTDATA, 0};
    int write = h->write = mode == BADGE_OPEN_WRITE;
    Result r = FSUSER_OpenArchive(&h->archive, ARCHIVE_EXTDATA, (FS_Path){PATH_BINARY, sizeof path, path});
    if (R_FAILED(r))
        return failed("FSUSER_OpenArchive", -1, r);
    h->archive_open = 1;
    for (int i = 0; i < BADGE_FILE_COUNT; i++) {
        r = FSUSER_OpenFile(&h->file[i], h->archive, fsMakePath(PATH_ASCII, PATH[i]),
                            write ? FS_OPEN_READ | FS_OPEN_WRITE : FS_OPEN_READ, 0);
        if (R_FAILED(r)) {
            int code = failed(write ? "FSUSER_OpenFile (read and write)" : "FSUSER_OpenFile (read only)", i, r);
            home_close(h);
            return code;
        }
        h->file_open[i] = 1;
    }
    console_log("badges: HOME's badge files open (extdata 000014d1, %s)", write ? "read and write" : "read only");
    return 0;
}

static int home_size(void *ctx, int file, uint64_t *size)
{
    home_badges *h = ctx;
    u64 n = 0;
    Result r = FSFILE_GetSize(h->file[file], &n);
    if (R_FAILED(r))
        return failed("FSFILE_GetSize", file, r);
    *size = n;
    return 0;
}

static int home_read(void *ctx, int file, uint32_t offset, uint8_t *buf, uint32_t size)
{
    home_badges *h = ctx;
    u32 got = 0;
    Result r = FSFILE_Read(h->file[file], &got, offset, buf, size);
    if (R_FAILED(r))
        return failed("FSFILE_Read", file, r);
    return got == size ? 0 : failed("FSFILE_Read (short)", file, SHORT_IO);
}

static int home_write(void *ctx, int file, uint32_t offset, const uint8_t *buf, uint32_t size)
{
    home_badges *h = ctx;
    u32 put = 0;
    Result r = FSFILE_Write(h->file[file], &put, offset, buf, size, FS_WRITE_FLUSH);
    if (R_FAILED(r))
        return failed("FSFILE_Write", file, r);
    return put == size ? 0 : failed("FSFILE_Write (short)", file, SHORT_IO);
}

void console_badge_store(badge_store *s)
{
    *s = (badge_store){&home, home_open, home_size, home_read, home_write, home_close};
}

#endif

int console_main_loop(void)
{
    return aptMainLoop();
}
