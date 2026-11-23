/* The console's side of the Badges part, the only one with libctru calls (badge_console.c): HOME's two badge files
 * in its extdata 000014d1 on the SD card as a badge_store, and the main loop's turn with the system. On the host
 * host/badge_stubs.c stands in for it. */
#ifndef BADGE_CONSOLE_H
#define BADGE_CONSOLE_H

#include "badge_flow.h"
#include "console.h"

#define BADGE_SETS_DIR "sdmc:/Badges"           /* where sets are looked for, and where downloads go */
#ifdef APP_DEMO
#define BADGE_APP_DIR CARD_DIR "/demo-badges"   /* the pretend collection and its backups, records and manifest */
#else
#define BADGE_APP_DIR CARD_DIR                  /* badge-backup/, badge-undo/, badge-manifest.txt */
#endif

/* HOME's two files, opened read only or read and write as the open's mode says (never created). DEMO=1: a folder
 * store over BADGE_APP_DIR (an empty collection on its first use); HOME's files are never opened. */
void console_badge_store(badge_store *s);

/* The main loop's turn with the system: aptMainLoop (sleep, HOME, POWER, an order to close); 0 when the application
 * is to end. A write to HOME's badge files runs in job_here, which does not come back to it, so what the system asks
 * for meanwhile waits until the write is over. */
int console_main_loop(void);

#endif
