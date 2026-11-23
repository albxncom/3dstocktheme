/* The record: the TMD this application last imported (or the Mac builder installed), kept on the card because AM
 * lists each content's index, ID and size but not its hash, and a delta must carry the installed records byte for
 * byte. It is never trusted on its own: pack_assess uses it only if tp_tmd_check finds it equal to AM's list.
 *
 *   <dir>/installed.tmd       the TMD of the installed pack, raw bytes as in a CIA or a delta's tmd.bin
 *   <dir>/installed.tmd.new   the next TMD, written before the first AM write of an install and renamed over
 *                             installed.tmd once AM committed it; left in place when the install fails
 *
 *   <dir>/installed.mark      the highest content index ever handed out, as decimal text and a newline; only
 *   <dir>/installed.mark.new  ever raised, before the first AM write of an install (written to .new, read back,
 *                             renamed over installed.mark); the larger of the two files counts
 *
 * pack_assess takes whichever TMD AM's list confirms, installed.tmd first. So a power cut between AM's commit and
 * the rename, or a commit AM carried out although it reported an error, leaves a record that still matches, and a
 * .new file left by a failed install is ignored while installed.tmd matches. An install promotes a matching .new
 * file to installed.tmd before it stages the next TMD, so the only record that matches AM is never overwritten.
 *
 * The mark keeps indices from being handed out twice after the pack is reinstalled with fewer themes than were
 * handed out (a recovery with an older CIA): the catalogue and the TMD then no longer show the highest index. If
 * the mark is lost too, the next index comes from the catalogue and the TMD alone (README, "The record"). */
#ifndef STORE_H
#define STORE_H

#include <stddef.h>
#include <stdint.h>

#include "tp.h"

#define STORE_FILE "installed.tmd"
#define STORE_STAGED "installed.tmd.new"
#define MARK_FILE "installed.mark"
#define MARK_STAGED "installed.mark.new"
#define STORE_MAX (0xB04 + 0x30 * (TP_INDEX_MAX + 1))    /* tp_tmd_size for content indices 0..349 */

typedef struct store_record {
    int present;
    int result;                 /* FILES_OK, FILES_MISSING or FILES_ERROR */
    size_t size;
    uint8_t tmd[STORE_MAX];
} store_record;

/* Reads both files: rec[0] installed.tmd, rec[1] installed.tmd.new. */
void store_read(const char *dir, store_record rec[2]);

/* The steps of an install. 0 on success. store_commit renames installed.tmd.new over installed.tmd; an install
 * calls it after AM's first commit, and before staging when installed.tmd.new is the record that matched. */
int store_stage(const char *dir, const uint8_t *tmd, size_t size);
int store_commit(const char *dir);

/* The mark: the larger of the two files that hold a number from 1 to 349, 0 if neither does. */
uint16_t store_mark(const char *dir);
/* Raises the mark to index if it is lower; 0 on success (also when nothing had to be written). */
int store_mark_raise(const char *dir, uint16_t index);

#endif
