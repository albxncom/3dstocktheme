/* Downloading a badge set a QR code points at: download_fetch's transfer with all of its rules (download.h), then
 * the file is kept only if it is a zip or a PNG that the badge core takes as a set (badge_source_peek). It goes to
 * the Badges folder under a cleaned name that no set there has yet, whatever its kind; on any failure or cancel
 * nothing partial stays on the card. */
#ifndef BADGE_DOWNLOAD_H
#define BADGE_DOWNLOAD_H

#include <stddef.h>

#include "download.h"

#define BADGE_DOWNLOAD_BASE 60  /* bytes of the name before " (2)" and the extension: a HOME name has 68 units */

/* As download(), with these differences: DL_NOT_THEME is a file that is neither a zip nor a PNG (a web page, for
 * example); DL_BAD_THEME is one of the two that is no set the core takes, detail is then the badge_result; for
 * DL_OK kind is BADGE_SRC_ZIP or BADGE_SRC_PNG and file the name in dir. */
int badge_download(const char *url, const char *part_path, const char *dir, const char *ca_file,
                   const char *user_agent, const download_progress *progress, download_result *res);

/* The name in dir: download_name's cleaned name without its extension (at most BADGE_DOWNLOAD_BASE bytes; "Badges"
 * when neither the header nor the address gives one), " (2)", " (3)" ... while dir holds a set of that name as a
 * folder, a .zip or a .png, then ".zip" or ".png" by kind (BADGE_SRC_*). */
void badge_download_name(const char *disposition, const char *url, int kind, const char *dir, char *out,
                         size_t size);

#endif
