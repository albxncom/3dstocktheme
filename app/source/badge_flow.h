/* The Badges part's work, in plain C so the host tests run all of it: what is on the card under /Badges (a set is a
 * folder of PNG files, a zip of PNG files or one PNG), the HOME Menu's badge collection as the screens list it, and
 * the two writes: append one set, remove the last set again. The badge core (core/badge.h) decides what the bytes
 * are; this file decides when which bytes go where.
 *
 * HOME's two files (BadgeData.dat and BadgeMngFile.dat in extdata 000014d1) are reached only through a badge_store:
 * the console's is badge_console.c, the host tests' is a folder with the two files that can be told to fail. The
 * application's own files on the SD card go through a badge_card (stdio by default), so the tests can make the card
 * fail too.
 *
 * A write, in this order (append; a removal writes BadgeMngFile.dat first and the set's areas after it):
 *   1  both files are read whole and their sizes checked
 *   2  the set is read and checked, the manifest is read, the collection must take the set (not there, room left);
 *      a removal: the core decides whether the set goes (the last set, this application's, none of it placed, its
 *      undo record, its own bytes as the append wrote them)
 *   3  both files as read go to <dir>/badge-backup/<number>/, and the copies are read back and compared
 *   4  the core appends in memory; its own check that only the new set's areas change is part of that call, and this
 *      file checks BadgeMngFile.dat byte for byte once more and reads the result back as a collection
 *   5  the new manifest and the set's undo record go to the card (read back; the manifest under a temporary name)
 *   6  BadgeData.dat: only the set's areas (nothing names them yet); then BadgeMngFile.dat whole, in one call
 *   7  the store is closed and opened again, both files are read back and compared with what was meant
 *   8  if a write failed or the read-back differs: the backup is written back and read back
 * Up to step 5 a refusal leaves HOME's files untouched (BFLOW_REFUSED, wrote 0); a refusal in step 2 writes nothing at
 * all, not even the backup. */
#ifndef BADGE_FLOW_H
#define BADGE_FLOW_H

#include <stddef.h>
#include <stdint.h>

#include "badge.h"

#define BADGE_PATH 512
#define BADGE_ENTRY_NAME 256
#define BADGE_ENTRIES_MAX 500           /* entries of one folder that are looked at */
#define BADGE_SOURCE_MAX (24u << 20)    /* a set's files together */
#define BADGE_BACKUPS_KEPT 3            /* numbered backup folders left on the card after a write that read back */

/* ---------------------------------------------------------------- the two boundaries */

enum { BADGE_FILE_DATA, BADGE_FILE_MNG, BADGE_FILE_COUNT };

enum { BADGE_OPEN_READ, BADGE_OPEN_WRITE };     /* open's mode: size and read only (a write fails), or read and write */

/* HOME's two badge files. Every call returns 0, or the service's result code (never 0 for a failure). read and write
 * move exactly size bytes or fail; a write is on the card when the call returns, as far as the service can say;
 * close's result counts after a write. A listing opens with BADGE_OPEN_READ, a write with BADGE_OPEN_WRITE (each of
 * its opens: the first, the one to read back, the restore's). */
typedef struct badge_store {
    void *ctx;
    int (*open)(void *ctx, int mode);
    int (*size)(void *ctx, int file, uint64_t *size);
    int (*read)(void *ctx, int file, uint32_t offset, uint8_t *buf, uint32_t size);
    int (*write)(void *ctx, int file, uint32_t offset, const uint8_t *buf, uint32_t size);
    int (*close)(void *ctx);
} badge_store;

/* What a failed open's code says, by the code's fields as libctru's result.h lays them out (summary bits 26..21,
 * module 17..10, description 9..0): BOPEN_LOCKED another process holds a file or the archive open in a way that
 * conflicts (module FS, description 230; C92044E6 is the case seen on the console), BOPEN_MISSING not there
 * (summary 4, not found), BOPEN_OTHER any other code. */
enum { BOPEN_OTHER, BOPEN_LOCKED, BOPEN_MISSING };
int badge_open_kind(int code);

#define BADGE_CODE_MISSING ((int)0xC8804478)    /* a not-found code for the stand-in stores: FS, summary 4 */

/* The application's own files on the SD card. put: offset 0 starts the file anew, any other offset must be its
 * length so far; the bytes are on the card when it returns (flushed and closed). get: up to size bytes from offset,
 * *got of them (fewer at the file's end). Both 0, or -1. */
typedef struct badge_card {
    void *ctx;
    int (*put)(void *ctx, const char *path, uint64_t offset, const uint8_t *buf, size_t size);
    int (*get)(void *ctx, const char *path, uint64_t offset, uint8_t *buf, size_t size, size_t *got);
} badge_card;

extern const badge_card BADGE_CARD_STDIO;

enum badge_step {               /* what a write is doing, for the work screen */
    BSTEP_READ,                 /* HOME's files are read */
    BSTEP_BACKUP,               /* the backup is written */
    BSTEP_BACKUP_CHECK,         /* ... and read back */
    BSTEP_CHANGE,               /* the core works in memory; the manifest and the undo record go to the card */
    BSTEP_WRITE,                /* HOME's files are written */
    BSTEP_VERIFY,               /* ... and read back */
    BSTEP_RESTORE,              /* the backup is written back */
    BSTEP_DONE
};

typedef struct badge_env {
    const badge_store *store;
    const badge_card *card;     /* NULL: BADGE_CARD_STDIO */
    const char *dir;            /* the application's folder on the card: badge-backup/, badge-undo/, the manifest */
    void (*log)(void *ctx, const char *line);                           /* NULL: no log */
    void (*step)(void *ctx, int step, uint64_t done, uint64_t total);   /* NULL: no progress */
    void *ctx;
} badge_env;

/* ---------------------------------------------------------------- a set on the card */

enum { BADGE_SRC_FOLDER, BADGE_SRC_ZIP, BADGE_SRC_PNG };

typedef struct badge_source {
    int kind;
    char key[BADGE_ENTRY_NAME]; /* the set's name and its key in the manifest: the folder's name, a file's without
                                   ".zip" or ".png" */
    badge_file *files;
    size_t count;
} badge_source;

/* What an entry of a folder would be as a set, by its name: BADGE_SRC_*, or -1 (not a set's source). */
int badge_source_kind(const char *name, int is_dir);

/* Reads the set at path into memory. BADGE_OK; BADGE_E_FOLDER when it cannot be read from the card; BADGE_E_NOMEM
 * also when its files are over BADGE_SOURCE_MAX together; a zip's own refusals (BADGE_E_ZIP, BADGE_E_NAME).
 * badge_source_free afterwards, also after an error. */
int badge_source_load(const char *path, int kind, badge_source *src);
void badge_source_free(badge_source *src);

typedef struct badge_peek {
    int result;                 /* BADGE_OK, or why the core does not take it as a set */
    uint32_t count;             /* its badges */
    uint8_t icon[BADGE_ICON_BYTES];     /* the set icon as the core would make it: _seticon.png, else the first badge */
} badge_peek;

/* The set as the list and the check screen show it; every PNG is decoded once. */
void badge_source_peek(const badge_source *src, badge_peek *peek);

typedef struct badge_entry {
    char file[BADGE_ENTRY_NAME];
    int kind;                   /* BADGE_SRC_* */
    int folder;                 /* a folder that is not a set (it holds other things than PNG files): to be opened */
    badge_peek peek;            /* a set */
} badge_entry;

typedef struct badge_entries {
    badge_entry *items;         /* the folders first, then the sets, each group by name */
    size_t count, folders;
    size_t other;               /* files that are no set's source */
    int missing;                /* the folder does not exist */
    int more;                   /* over BADGE_ENTRIES_MAX entries: the first by name were looked at */
} badge_entries;

/* One folder of the card as the Badges part lists it; hidden names (scan_hidden) are passed over. 0, or -1 when out
 * of memory. progress, if set, is told after each entry. */
int badge_scan(const char *dir, badge_entries *list, void (*progress)(void *ctx, size_t done, size_t total),
               void *ctx);
void badge_entries_free(badge_entries *list);

/* ---------------------------------------------------------------- the collection, for the screens */

enum { BLIST_OK, BLIST_NO_DATA, BLIST_READ, BLIST_FORMAT, BLIST_NOMEM, BLIST_LOCKED, BLIST_OPEN };

typedef struct badge_listing {
    int state;                  /* BLIST_NO_DATA: the store did not open because the files are not there, or a file
                                   has the wrong size (code); BLIST_LOCKED: it did not open because another process
                                   holds the files (code); BLIST_OPEN: it did not open for any other reason (code);
                                   BLIST_READ: a read failed (code); BLIST_FORMAT: the core does not read it (badge) */
    int code;
    int badge;
    int manifest_bad;           /* the manifest on the card is damaged: nothing counts as appended by this
                                   application, and nothing is appended or removed */
    int undo;                   /* an undo record of the last set is on the card */
    badge_collection col;
    uint8_t (*icons)[BADGE_ICON_BYTES];     /* col.sets of them */
} badge_listing;

/* Reads what a listing needs (BadgeMngFile.dat, the sets' names and icons), not the badge images.
 * badge_listing_free afterwards. Returns l->state. */
int badge_flow_list(const badge_env *env, badge_listing *l);
void badge_listing_free(badge_listing *l);

enum { BPLAN_OK, BPLAN_SET, BPLAN_COLLECTION, BPLAN_MANIFEST, BPLAN_PRESENT, BPLAN_FULL };

/* Whether the collection as listed would take the set: BPLAN_SET the set does not read (peek->result),
 * BPLAN_COLLECTION the collection is not there or does not read (l->state), BPLAN_PRESENT a set of that name is in
 * the collection, BPLAN_FULL over 100 sets or 1000 badges. The write checks all of it again. */
int badge_flow_plan(const badge_listing *l, const badge_source *src, const badge_peek *peek);

/* ---------------------------------------------------------------- the two writes */

enum { BFLOW_DONE, BFLOW_REFUSED, BFLOW_RESTORED, BFLOW_UNKNOWN };

enum badge_stage {
    BSTAGE_NONE,
    BSTAGE_BUSY,                /* a write is under way already */
    BSTAGE_OPEN,                /* the store did not open (code, of a kind badge_open_kind tells) */
    BSTAGE_SIZE,                /* a file has the wrong size */
    BSTAGE_MEMORY,
    BSTAGE_READ,                /* a read of HOME's files failed (code) */
    BSTAGE_MANIFEST,            /* the manifest on the card is damaged or cannot be read */
    BSTAGE_COLLECTION,          /* the core does not read the collection, or its last set cannot go (badge) */
    BSTAGE_SET,                 /* the set does not read (badge) */
    BSTAGE_PRESENT,             /* a set of that name is in the collection */
    BSTAGE_FULL,
    BSTAGE_UNDO,                /* no undo record of the last set on the card, or a damaged one (badge) */
    BSTAGE_BACKUP,              /* the backup could not be written (the card is full, for example) */
    BSTAGE_BACKUP_CHECK,        /* the backup read back differs */
    BSTAGE_CHANGE,              /* the core refused (badge) */
    BSTAGE_CHECK,               /* this file's own check of the core's result failed */
    BSTAGE_RECORD,              /* the manifest or the undo record could not be written */
    BSTAGE_WRITE,               /* a write to HOME's files failed (code) */
    BSTAGE_VERIFY               /* HOME's files read back differ from what was meant, or could not be read back */
};

typedef struct badge_flow_result {
    int outcome;                /* BFLOW_DONE: written and read back as meant. BFLOW_REFUSED: nothing was written to
                                   HOME's files. BFLOW_RESTORED: a write was started and failed or read back
                                   differently; the backup was written back and HOME's files read back equal to it.
                                   BFLOW_UNKNOWN: as before, but the backup could not be confirmed to be back. */
    int stage;                  /* where it stopped; BSTAGE_NONE for BFLOW_DONE */
    int badge;                  /* a badge_result, see the stages */
    int code;                   /* a store result code, see the stages */
    int wrote;                  /* a write to HOME's files was started */
    char backup[BADGE_PATH];    /* the backup's folder; "" when none is left (nothing was written) */
    char name[BADGE_NAME_BYTES];        /* the set */
    uint32_t set_id, slot, start, count;
    int identical;              /* a removal: both files are byte for byte what they were before the append */
    uint32_t sets, badges;      /* BFLOW_DONE: the collection now */
} badge_flow_result;

/* Appends the set to HOME's collection. Returns res->outcome. */
int badge_flow_append(const badge_env *env, const badge_source *src, badge_flow_result *res);

/* Removes the collection's last set, if this application appended it, none of its badges is placed and its undo
 * record is on the card: the set's areas get their earlier bytes back byte for byte. Files HOME changed since are
 * taken as long as every byte the append wrote is unchanged; HOME's own fields keep what HOME left in them
 * (badge_remove_last). Every refusal comes before the backup. Returns res->outcome. */
int badge_flow_remove_last(const badge_env *env, badge_flow_result *res);

#endif
