/* Deleting one installed theme of the pack, the way the HOME Menu's own Delete does it: AMAPP_DeleteDLCContents with
 * one content index of 0004008C00009809 (HOME's wrapper at 0x15e978 of the EUR 11.17 code sends the same command,
 * 0x10040102). Plain C: the console layer reads AM's view and HOME's SaveData.dat and calls AM through a deleter;
 * the host tests do the same with buffers and fakes.
 *
 * Before its own Delete, HOME looks at two things (README, "Deleting a theme"): whether the theme is the one in use,
 * and whether it is one of the ten shuffle entries. It then switches to no theme, or empties the whole shuffle
 * selection, before deleting. This application never writes HOME's data, so it refuses both instead, and it refuses
 * when it cannot read that data. It deletes only theme contents of the pack, never content 0 (the catalogue).
 *
 * Nothing on the card is written for a delete except the log: AM keeps the TMD as it is, so installed.tmd and
 * installed.mark stay true, the theme is listed as deleted in HOME (AM: not installed), and a later install carries
 * its record on, as for a theme deleted with HOME's own Delete. If the console stops half-way, the next start reads
 * AM's list and finds the theme either installed or deleted; both are states it knows. */
#ifndef DELETION_H
#define DELETION_H

#include <stddef.h>
#include <stdint.h>

#include "plan.h"

/* HOME's SaveData.dat (SD extdata 00000098 of the EUR HOME Menu): 0x2DA0 bytes since 4.0.0, 0x2CB0 before
 * (3dbrew, Home Menu, format versions; HOME never grows an old file). The theme fields, as HOME 11.17 reads them:
 * the theme in use at 0x13B8, ten shuffle entries from 0x13C0, the shuffle switch at 0x141B. */
#define HOME_SAVE_SIZE 0x2DA0
#define HOME_SAVE_OLD_SIZE 0x2CB0
#define HOME_THEME_AT 0x13B8
#define HOME_SHUFFLE_AT 0x13C0
#define HOME_SHUFFLE_ON_AT 0x141B
#define HOME_SHUFFLE 10
#define HOME_TYPE_DLC 2             /* an entry naming a content of a theme DLC title */
#define HOME_PACK_VARIATION 9       /* 0004008C000098xx: the pack is variation 09 */

typedef struct home_entry {     /* 8 bytes: u32 content index, u8 DLC variation, u8 type, two unused */
    uint32_t index;
    uint8_t variation, type;
} home_entry;

typedef struct home_state {
    int ok;                     /* SaveData.dat was read whole and has one of HOME's sizes */
    home_entry current;
    home_entry shuffle[HOME_SHUFFLE];
    int shuffle_on;
} home_state;

/* save: the file's bytes (NULL when it could not be read). */
void home_parse(home_state *h, const uint8_t *save, size_t size);

/* Whether e names content index of the pack, compared as HOME compares entries (0x1fd4bc: type 2 entries are equal
 * when index and variation are). */
int home_names(const home_entry *e, uint16_t index);

enum deletion_state {
    DEL_OK,
    DEL_TITLE,                  /* a title other than 0004008C00009809 */
    DEL_CATALOGUE,              /* content 0: the catalogue */
    DEL_INDEX,                  /* above 349 */
    DEL_PACK,                   /* the pack was not read whole, AM's view is missing, or the record no longer matches */
    DEL_NOT_IN_PACK,            /* the record has no theme with this index */
    DEL_NOT_INSTALLED,          /* AM does not list it as installed and owned: it is deleted already */
    DEL_PENDING,                /* an import of the pack is pending, or whether one is cannot be told */
    DEL_HOME_UNKNOWN,           /* HOME's SaveData.dat could not be read */
    DEL_ACTIVE,                 /* HOME uses this theme now */
    DEL_SHUFFLE                 /* it is one of HOME's ten shuffle entries */
};

typedef struct deletion_plan {
    int state;
    uint16_t index;
    const pack_theme *theme;    /* in pv, for DEL_OK and the HOME refusals */
} deletion_plan;

/* What deleting content index of title_id would do, on the pack as pack_assess read it, AM's view now and HOME's
 * data now. Only DEL_OK lets deletion_run call AM. */
int deletion_plan_make(deletion_plan *p, const pack_view *pv, const am_view *am, const home_state *home,
                       uint64_t title_id, uint16_t index);

typedef struct deleter {
    void *ctx;
    /* AMAPP_DeleteDLCContents(&index, 1, title_id, SD); 0 on success, else AM's result code. */
    uint32_t (*delete_content)(void *ctx, uint64_t title_id, uint16_t index);
} deleter;

/* Calls the deleter once for a DEL_OK plan: -1 refused (nothing called), 0 AM reported success, 1 AM reported
 * an error (*error: its code). */
int deletion_run(const deletion_plan *p, const deleter *d, uint32_t *error);

enum deletion_outcome {
    DEL_DONE,                   /* AM lists the theme as no longer installed, every other theme as before */
    DEL_NOT_DONE,               /* AM still lists it as installed */
    DEL_LOST,                   /* other themes AM listed as installed are no longer */
    DEL_UNKNOWN                 /* AM's view afterwards could not be read */
};

typedef struct deletion_judgement {
    int outcome;
    size_t lost;
    int record_ok;              /* installed.tmd still matches AM's list (else the next start says what differs) */
} deletion_judgement;

/* What happened, from AM's view after the call against the view the plan was made on. */
void deletion_judge(const am_view *before, const am_view *after, const pack_view *pv, const deletion_plan *p,
                    deletion_judgement *j);

#endif
