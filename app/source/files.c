#include "files.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

typedef struct file_source {
    FILE *f;
    long pos;                   /* where the next fread starts; fseek only when a read is elsewhere */
    uint64_t size;
    uint64_t read;              /* bytes read so far, every read counted */
    void (*tick)(void *ctx, uint64_t read, uint64_t size);
    void *tick_ctx;
    void (*around)(void *ctx, int begin);
    void *around_ctx;
} file_source;

static int source_read(void *ctx, uint64_t offset, void *buf, size_t len)
{
    file_source *s = ctx;
    if (offset > (uint64_t)LONG_MAX - len)
        return -1;
    if (s->around)
        s->around(s->around_ctx, 1);
    int ok = (long)offset == s->pos || !fseek(s->f, (long)offset, SEEK_SET);
    if (ok) {
        s->pos = -1;
        ok = fread(buf, 1, len, s->f) == len;
    }
    if (s->around)
        s->around(s->around_ctx, 0);
    if (!ok)
        return -1;
    s->pos = (long)(offset + len);
    s->read += len;
    if (s->tick)
        s->tick(s->tick_ctx, s->read, s->size);
    return 0;
}

int files_open_source(const char *path, tp_source *src, void (*tick)(void *ctx, uint64_t at, uint64_t size),
                      void *tick_ctx)
{
    file_source *s = calloc(1, sizeof *s);
    if (!s)
        return FILES_ERROR;
    if (!(s->f = fopen(path, "rb"))) {
        free(s);
        return FILES_MISSING;
    }
    long end = -1;
    if (!fseek(s->f, 0, SEEK_END))
        end = ftell(s->f);
    if (end < 0 || fseek(s->f, 0, SEEK_SET)) {
        fclose(s->f);
        free(s);
        return FILES_ERROR;
    }
    s->size = (uint64_t)end;
    s->read = 0;
    s->tick = tick;
    s->tick_ctx = tick_ctx;
    *src = (tp_source){s, s->size, source_read};
    return FILES_OK;
}

void files_count_restart(const tp_source *src)
{
    file_source *s = src ? src->ctx : NULL;
    if (s)
        s->read = 0;
}

void files_around_reads(const tp_source *src, void (*around)(void *ctx, int begin), void *ctx)
{
    file_source *s = src ? src->ctx : NULL;
    if (s) {
        s->around = around;
        s->around_ctx = ctx;
    }
}

void files_close_source(tp_source *src)
{
    file_source *s = src ? src->ctx : NULL;
    if (s) {
        fclose(s->f);
        free(s);
        src->ctx = NULL;
    }
}

int files_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

int files_make_dir(const char *path)
{
    return mkdir(path, 0777) == 0 ? FILES_OK : FILES_ERROR;
}

int files_read(const char *path, uint8_t *buf, size_t cap, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return FILES_MISSING;
    size_t n = fread(buf, 1, cap, f);
    int bad = ferror(f) || fgetc(f) != EOF;
    fclose(f);
    if (bad)
        return FILES_ERROR;
    *size = n;
    return FILES_OK;
}

int files_write(const char *path, const uint8_t *buf, size_t size)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return FILES_ERROR;
    int bad = fwrite(buf, 1, size, f) != size;
    bad |= fflush(f) != 0;
    bad |= fclose(f) != 0;
    return bad ? FILES_ERROR : FILES_OK;
}
