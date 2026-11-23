#include "preview.h"

#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"
#include "bcstm.h"
#include "content.h"
#include "media.h"

#define READER_STACK 0x40000    /* as main.c's worker: the zip reader keeps its buffers on the heap, zlib its own */

enum { FAIL_NONE, FAIL_MEMORY, FAIL_READ, FAIL_BODY };

static struct {
    int state;                  /* PREVIEW_* */
    const tp_theme *theme;      /* what is read: an open theme, or (NULL) the installed content index */
    uint16_t index;
    Thread reader;
    volatile int over;          /* the reader is done; everything below is its result */
    int fail, detail;           /* FAIL_*, and the core's result or the file system's answer behind it */
    int music_detail;           /* why the music file could not be read (the wallpapers show without it) */
    uint8_t *body, *music;
    size_t body_size, music_size;
    tp_body walls;
    int plays;                  /* the music was accepted as a stream */
} p;

/* The reading, on its own thread: no drawing, no log, no sound; it writes only p's result fields. */
static void read_all(void *arg)
{
    (void)arg;
    uint8_t *packed = NULL;
    size_t packed_size = 0;
    int r = 0;
    p.body = malloc(TP_BODY_MAX);
    if (!p.body) {
        p.fail = FAIL_MEMORY;
    } else if (p.theme) {
        if ((r = tp_theme_body(p.theme, p.body, TP_BODY_MAX, &p.body_size)))
            p.fail = FAIL_READ;
    } else {
        uint32_t error = 0;
        if (content_read(p.index, &packed, &packed_size, &p.music, &p.music_size, &error)) {
            p.fail = FAIL_READ;
            r = (int)error;
        } else if ((r = tp_body_decode(packed, packed_size, p.body, TP_BODY_MAX, &p.body_size))) {
            p.fail = FAIL_READ;
        }
        free(packed);
    }
    if (!p.fail && tp_body_read(p.body, p.body_size, &p.walls))
        p.fail = FAIL_BODY;
    p.detail = r;
    /* the music only when the HOME Menu would play it: a music file, and the body's byte that switches it on */
    if (!p.fail && p.theme && p.theme->has_bgm && p.walls.use_bgm) {
        p.music = malloc(p.theme->bgm_size ? p.theme->bgm_size : 1);
        p.music_detail = p.music ? tp_theme_bgm(p.theme, p.music, p.theme->bgm_size, &p.music_size) : TP_E_NOMEM;
    }
    if (p.fail || p.music_detail || !p.walls.use_bgm) {
        free(p.music);
        p.music = NULL;
        p.music_size = 0;
    }
    p.over = 1;
}

static void start_reading(const tp_theme *theme, uint16_t index)
{
    preview_close();
    p.theme = theme;
    p.index = index;
    p.state = PREVIEW_LOADING;
    s32 prio = 0x30;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    p.reader = threadCreate(read_all, NULL, READER_STACK, prio + 1 > 0x3F ? 0x3F : prio + 1, -2, false);
    if (!p.reader)
        read_all(NULL);         /* no thread to be had: read here, the frames wait */
}

void preview_open_theme(const tp_theme *theme)
{
    start_reading(theme, 0);
}

void preview_open_content(uint16_t index)
{
    start_reading(NULL, index);
}

int preview_poll(void)
{
    if (p.state != PREVIEW_LOADING || !p.over)
        return p.state;
    if (p.reader) {
        threadJoin(p.reader, U64_MAX);
        threadFree(p.reader);
        p.reader = NULL;
    }
    if (p.fail) {
        free(p.body);
        p.body = NULL;
        return p.state = PREVIEW_FAILED;
    }
    p.plays = p.music && !audio_theme_play(p.music, p.music_size);
    if (!p.plays)
        audio_theme_play(NULL, 0);
    return p.state = PREVIEW_READY;
}

const tp_body *preview_walls(void)
{
    return p.state == PREVIEW_READY ? &p.walls : NULL;
}

const uint8_t *preview_texels(int bottom)
{
    const tp_wall *w = bottom ? &p.walls.bottom : &p.walls.top;
    return p.state == PREVIEW_READY && w->kind == TP_WALL_TEXTURE ? p.body + w->offset : NULL;
}

static const char *wall_text(const tp_wall *w, char *out, size_t size)
{
    if (w->kind == TP_WALL_TEXTURE)
        snprintf(out, size, "texture %ux%u at %#x, motion %d", w->width, w->height, (unsigned)w->offset, w->motion);
    else if (w->kind == TP_WALL_COLOR)
        snprintf(out, size, "colour %02X%02X%02X", w->color[0], w->color[1], w->color[2]);
    else
        snprintf(out, size, "none");
    return out;
}

void preview_describe(char *out, size_t size)
{
    char top[64], bottom[64], what[32];
    if (p.theme)
        snprintf(what, sizeof what, "the open file");
    else
        snprintf(what, sizeof what, "installed content %u", p.index);
    if (p.state == PREVIEW_FAILED) {
        snprintf(out, size, "preview of %s: failed (%s, %d / %08X)", what,
                 p.fail == FAIL_MEMORY ? "no memory" : p.fail == FAIL_READ ? "not read" : "not a body this reads",
                 p.detail, (unsigned)p.detail);
        return;
    }
    snprintf(out, size, "preview of %s: body %u bytes, top %s, bottom %s, music byte %d, music %s (%u bytes, %d)",
             what, (unsigned)p.body_size, wall_text(&p.walls.top, top, sizeof top),
             wall_text(&p.walls.bottom, bottom, sizeof bottom), p.walls.use_bgm,
             p.plays ? "plays" : p.music ? "not played (no sound, or not a stream this plays)"
             : p.music_detail ? "not read" : "none",
             (unsigned)p.music_size, p.music_detail);
}

void preview_close(void)
{
    if (p.reader) {
        threadJoin(p.reader, U64_MAX);
        threadFree(p.reader);
    }
    if (p.state == PREVIEW_READY)
        audio_theme_stop();     /* before the music's memory goes: the audio thread reads it until here */
    free(p.body);
    free(p.music);
    memset(&p, 0, sizeof p);
}
