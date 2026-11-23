#include "badge_dir_store.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static const char *const NAME[BADGE_FILE_COUNT] = {"BadgeData.dat", "BadgeMngFile.dat"};
static const uint32_t SIZE[BADGE_FILE_COUNT] = {BADGE_DATA_SIZE, BADGE_MNG_SIZE};

static int zeros(FILE *f, uint32_t size)
{
    static const uint8_t none[4096];
    for (uint32_t at = 0; at < size; at += sizeof none) {
        size_t n = size - at < sizeof none ? size - at : sizeof none;
        if (fwrite(none, 1, n, f) != n)
            return -1;
    }
    return fflush(f) ? -1 : 0;
}

static int dir_close(void *ctx)
{
    badge_dir *d = ctx;
    int bad = 0;
    for (int i = 0; i < BADGE_FILE_COUNT; i++) {
        if (d->f[i])
            bad |= fclose(d->f[i]) != 0;
        d->f[i] = NULL;
    }
    return bad ? -1 : 0;
}

static int dir_open(void *ctx, int mode)
{
    badge_dir *d = ctx;
    const char *how = mode == BADGE_OPEN_WRITE ? "r+b" : "rb";
    for (int i = 0; i < BADGE_FILE_COUNT; i++) {
        char path[BADGE_PATH + 32];
        snprintf(path, sizeof path, "%s/%s", d->dir, NAME[i]);
        d->f[i] = fopen(path, how);
        int missing = !d->f[i] && errno == ENOENT;
        if (!d->f[i] && d->create) {
            mkdir(d->dir, 0777);
            FILE *f = fopen(path, "wb");
            int bad = !f || zeros(f, SIZE[i]);
            if (f && fclose(f))
                bad = 1;
            if (bad)
                remove(path);
            else
                d->f[i] = fopen(path, how);
        }
        if (!d->f[i]) {
            dir_close(d);
            return missing && !d->create ? BADGE_CODE_MISSING : -1;
        }
    }
    return 0;
}

static int dir_size(void *ctx, int file, uint64_t *size)
{
    badge_dir *d = ctx;
    long n;
    if (fseek(d->f[file], 0, SEEK_END) || (n = ftell(d->f[file])) < 0)
        return -1;
    *size = (uint64_t)n;
    return 0;
}

static int dir_read(void *ctx, int file, uint32_t offset, uint8_t *buf, uint32_t size)
{
    badge_dir *d = ctx;
    return fseek(d->f[file], (long)offset, SEEK_SET) || fread(buf, 1, size, d->f[file]) != size ? -1 : 0;
}

static int dir_write(void *ctx, int file, uint32_t offset, const uint8_t *buf, uint32_t size)
{
    badge_dir *d = ctx;
    return fseek(d->f[file], (long)offset, SEEK_SET) || fwrite(buf, 1, size, d->f[file]) != size ||
                   fflush(d->f[file])
               ? -1
               : 0;
}

void badge_dir_store(badge_store *s, badge_dir *d, const char *dir, int create)
{
    memset(d, 0, sizeof *d);
    snprintf(d->dir, sizeof d->dir, "%s", dir);
    d->create = create;
    *s = (badge_store){d, dir_open, dir_size, dir_read, dir_write, dir_close};
}
