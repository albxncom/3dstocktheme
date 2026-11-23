/* What a preview needs of a theme besides its name and icon: the decoded body (its wallpapers are read by body.h)
 * and the music file (played through bcstm.h). The caller holds the memory: TP_BODY_MAX bytes hold every body the
 * core accepts, tp_theme.bgm_size bytes the music of an open theme. */
#ifndef TP_MEDIA_H
#define TP_MEDIA_H

#include "tp.h"

/* The bytes a body_LZ.bin stream decodes to, into out (cap bytes; TP_E_TOO_BIG when the stream declares more, or
 * more than TP_BODY_MAX). For a body that did not come through tp_theme_open: an installed content's. */
int tp_body_decode(const uint8_t *lz, size_t n, uint8_t *out, size_t cap, size_t *size);

/* An open theme's body, decoded: the zip's body_LZ.bin, or after tp_theme_drop_bgm the body that call made. Reads
 * the member again (its size and CRC-32 checked) and needs its stored size once more while it runs. */
int tp_theme_body(const tp_theme *theme, uint8_t *out, size_t cap, size_t *size);

/* An open theme's bgm.bcstm as it is in the zip, into out. TP_E_MISSING when the theme has none (also after
 * tp_theme_drop_bgm), TP_E_TOO_BIG when cap is below tp_theme.bgm_size. */
int tp_theme_bgm(const tp_theme *theme, uint8_t *out, size_t cap, size_t *size);

#endif
