#include "content.h"

#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>

#include "tp.h"

#define MOUNT "prev"            /* not "theme", console_content_music's: the two never share a mount */

/* The whole file, 1 to cap bytes, or NULL. */
static uint8_t *whole(const char *path, size_t cap, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    long end = -1;
    if (!fseek(f, 0, SEEK_END))
        end = ftell(f);
    uint8_t *buf = end > 0 && (size_t)end <= cap && !fseek(f, 0, SEEK_SET) ? malloc((size_t)end) : NULL;
    if (buf && fread(buf, 1, (size_t)end, f) != (size_t)end) {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    if (buf)
        *size = (size_t)end;
    return buf;
}

int content_read(uint16_t index, uint8_t **body, size_t *body_size, uint8_t **music, size_t *music_size,
                 uint32_t *error)
{
    static const u32 archive_path[4] = {(u32)TP_TITLE_ID, (u32)(TP_TITLE_ID >> 32), MEDIATYPE_SD, 0};
    const u32 content[5] = {0, index, 0, 0, 0};
    FS_Path ap = {PATH_BINARY, sizeof archive_path, archive_path};
    FS_Path fp = {PATH_BINARY, sizeof content, content};
    FS_Archive archive;
    Handle file;
    *error = 0;
    *body = *music = NULL;
    *body_size = *music_size = 0;
    if (index == 0 || index > TP_INDEX_MAX)
        return -1;
    Result res = FSUSER_OpenArchive(&archive, ARCHIVE_SAVEDATA_AND_CONTENT, ap);
    if (R_SUCCEEDED(res)) {
        res = FSUSER_OpenFile(&file, archive, fp, FS_OPEN_READ, 0);
        if (R_SUCCEEDED(res))
            res = romfsMountFromFile(file, 0, MOUNT);   /* a failed mount keeps or closes the handle itself */
        if (R_FAILED(res))
            FSUSER_CloseArchive(archive);
    }
    if (R_FAILED(res)) {
        *error = (u32)res;
        return -1;
    }
    *body = whole(MOUNT ":/body_LZ.bin", TP_BODY_MAX, body_size);
    if (*body)
        *music = whole(MOUNT ":/bgm.bcstm", TP_BGM_MAX, music_size);
    romfsUnmount(MOUNT);        /* closes the file handle it took */
    FSUSER_CloseArchive(archive);
    return *body ? 0 : -1;
}
