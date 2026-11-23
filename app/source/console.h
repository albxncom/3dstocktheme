/* Everything that talks to the console's services: AM's view of the pack (am:sys), the installed content 0 (the
 * title's content archive, as Anemone3DS v3.0.1 dump_all_themes opens it), the importer (am:net, the T3 sequence),
 * the delete of one theme content (am:sys), HOME's SaveData.dat (read only), the system language, and the log on
 * the card. Only title 0004008C00009809 on the SD card is ever named. */
#ifndef CONSOLE_H
#define CONSOLE_H

#include "deletion.h"
#include "designs.h"
#include "install.h"
#include "plan.h"

#define CARD_DIR "sdmc:/theme-pack"
#define THEMES_DIR "sdmc:/Themes"                /* where the folder view starts and downloads go */
#define CARD_ROOT "sdmc:/"                       /* where the folder view starts when THEMES_DIR is missing */
#define LOG_PATH CARD_DIR "/install.log"
#define LOG_OLD CARD_DIR "/install.log.old"
#define LOG_MAX 0x40000                      /* above this, install.log becomes install.log.old at start */
#define PART_PATH CARD_DIR "/download.part"  /* a download in progress; removed on failure and at start */
#define CA_PATH CARD_DIR "/cacert.pem"       /* trusted certificates for https (README, Downloads) */
#define THEMES_DECLINED CARD_DIR "/themes-folder.declined"  /* empty: the user said no to creating THEMES_DIR */

/* Opens the log (keeping at most two files, see LOG_MAX) and removes a part file a power cut left behind. */
void console_log_open(void);
void console_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
/* A line goes into the log's buffer and to the card with console_log_flush, which the main loop calls when nothing
 * moves on the screens: a write to the card takes long enough to lose frames, and most lines are written right when
 * a screen changes (0.9.2). Strict (console_log_strict(1)): every line goes to the card at once, as before, while
 * the pack or the card is written (an install, a delete), so the log is whole up to the last call whatever happens.
 * Lines stay in their order either way; closing the log writes what waits. */
void console_log_strict(int on);
void console_log_flush(void);
void console_log_close(void);
void console_log_view(const char *when, const am_view *v);

/* AM's view of the pack title through am:sys; when AM does not know the title, also whether the console has a
 * ticket for it (if that cannot be told, the view is not read). Every call and its result go to the log. */
void console_read_am(am_view *v);

/* Mounts the installed content 0 and points the reader at its catalogue; console_close_catalogue undoes it.
 * 0 on success (the reader may still fail on a file). */
int console_open_catalogue(catalogue_reader *reader);
void console_close_catalogue(void);

/* Whether installed content index (1..349) of the pack holds bgm.bcstm: MUSIC_YES or MUSIC_NONE, or MUSIC_UNKNOWN
 * with FS's result code in *error when it could not be read. Its RomFS is mounted as the catalogue's is, under its
 * own name, and unmounted again. Writes no log, so the worker can call it. */
int console_content_music(uint16_t index, uint32_t *error);

/* The importer over am:net, in the T3 sequence; for a first install on a console without a ticket for the title,
 * the ticket before it (install.h). */
void console_importer(importer *imp);

/* HOME's SaveData.dat, read (never written) from the EUR HOME Menu's SD extdata 00000098, as Anemone3DS v3.0.1
 * opens it (source/fs.c, open_archives); the theme fields go to the log. */
void console_read_home(home_state *h);

/* The deleter: AMAPP_DeleteDLCContents through am:sys for one theme content of the pack; it refuses, without asking
 * AM, any other title, content 0 and an index above 349. */
void console_deleter(deleter *d);

int console_language(void);

/* A New 3DS at 804 MHz with its L2 cache instead of 268 MHz (libctru's osSetSpeedupEnable, which asks ptm:sysm and
 * changes nothing on an old 3DS or without that service). What it did is in the log's first lines: the model, whether
 * ptm:sysm answered, and the time of one fixed piece of work before and after (about a third at the higher clock). */
void console_speed_up(void);

/* Whether the console is connected to a wireless network: 1 yes, 0 no, -1 unknown (the ac service did not answer).
 * ac:u's GetWifiStatus, which reports 0 while there is no connection (libctru 2.7.0, source/services/ac.c). */
int console_wifi(void);

/* The network for downloads: the socket service (1 MiB buffer) and libcurl; console_net_stop releases both. */
int console_net_start(void);
void console_net_stop(void);

/* The User-Agent: the application's name and version. */
const char *console_user_agent(void);

#endif
