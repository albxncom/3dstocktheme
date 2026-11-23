/* The files of one theme content installed in the pack, for the preview: read as console_content_music (console.c)
 * looks into a content, its RomFS mounted under a name of its own. Only title 0004008C00009809 is named. */
#ifndef CONTENT_H
#define CONTENT_H

#include <stddef.h>
#include <stdint.h>

/* body_LZ.bin and bgm.bcstm of installed content index (1 to 349), each in memory from malloc that the caller
 * frees; *music is NULL when the content has no music file, or one that is empty, larger than the core accepts or
 * not read whole (the preview then shows the wallpapers in silence). 0, or -1 without the body: the file system's
 * answer in *error, 0 when the body is missing, too large, cut short, or memory ran out. Writes no log, so a worker
 * can call it. */
int content_read(uint16_t index, uint8_t **body, size_t *body_size, uint8_t **music, size_t *music_size,
                 uint32_t *error);

#endif
