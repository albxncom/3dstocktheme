/* What is installed, and what installing one theme zip would do. Plain C on top of the core: the console layer
 * fills an am_view from AM and reads the installed catalogue through a catalogue_reader; the host tests do the
 * same from files. Nothing here writes. */
#ifndef PLAN_H
#define PLAN_H

#include <stddef.h>
#include <stdint.h>

#include "store.h"
#include "tp.h"

#define PACK_CONTENTS (TP_INDEX_MAX + 1)    /* content indices 0..349 */

enum { PENDING_NO, PENDING_YES, PENDING_UNKNOWN };

/* What AM reports for the pack title (AMAPP_GetDLCTitleInfos, AMAPP_ListDLCContentInfos, the pending list). */
typedef struct am_view {
    int ok;                     /* every status call answered; when 0 nothing else here counts */
    uint32_t error;             /* the first failing result code */
    int exists;                 /* AM knows the title */
    uint16_t version;
    size_t count;
    tp_am_content list[PACK_CONTENTS];
    uint8_t usable[PACK_CONTENTS];  /* per list entry: installed and owned, which is what HOME lists */
    int pending;                /* PENDING_*: an import of this title that was not committed */
} am_view;

/* The installed content 0, read by the console layer (romfs of the content) or a test (files). */
typedef struct catalogue_reader {
    void *ctx;
    /* ContentInfoArchive_EUR_en.bin, whole, into a buffer of cap bytes; 0 on success. */
    int (*archive)(void *ctx, uint8_t *buf, size_t cap, size_t *size);
    /* icons/<index>.icn, exactly TP_ICON_SIZE bytes; 0 on success. */
    int (*icon)(void *ctx, uint16_t index, uint8_t icon[TP_ICON_SIZE]);
} catalogue_reader;

#define PACK_ARCHIVE_MAX (8 + 0xC8 * (TP_INDEX_MAX + 1))

typedef struct pack_theme {     /* a theme record of the installed TMD with its catalogue entry */
    uint16_t index;
    int usable;
    char name[0x41];
    char description[0x81];
    uint8_t icon[TP_ICON_SIZE];
} pack_theme;

enum pack_state {
    PACK_OK,
    PACK_AM_ERROR,              /* AM did not answer the status calls */
    PACK_NOT_INSTALLED,         /* no pack title: this version only adds to an installed pack */
    PACK_PENDING,               /* an import of this title is pending */
    PACK_PENDING_UNKNOWN,       /* whether one is pending is unknown */
    PACK_NO_STORE,              /* no record on the card */
    PACK_STORE_BAD,             /* the record is not a TMD of this title (detail: tp_result) */
    PACK_STALE,                 /* the record does not say what AM reports (diff) */
    PACK_NO_CATALOGUE,          /* content 0 is not installed */
    PACK_CATALOGUE_BAD,         /* content 0's catalogue is unreadable or lacks a record's entry (detail) */
    PACK_FULL_THEMES,           /* TP_THEMES_MAX theme records: no room in the catalogue */
    PACK_FULL_INDEX,            /* index 349 is handed out */
    PACK_NOMEM
};

typedef struct pack_view {
    int state;
    int detail;                 /* tp_result for PACK_STORE_BAD and PACK_CATALOGUE_BAD */
    tp_difference diff;         /* for PACK_STALE: the first difference of installed.tmd */
    uint32_t error;             /* for PACK_AM_ERROR: AM's result code */
    uint16_t version;           /* title version AM reports */
    uint16_t mark;              /* the record's high-water mark (store_mark), 0 if none */
    uint16_t top;               /* highest index ever handed out: the mark, the catalogue's count, the TMD */
    size_t count;               /* theme records (index >= 1) of the installed TMD */
    size_t usable;              /* of those, installed and owned */
    pack_theme *themes;         /* count entries in TMD order (ascending index); freed by pack_free */
    const uint8_t *tmd;         /* the record that matches AM: points into the store_record given */
    size_t tmd_size;
    int from_staged;            /* that record is installed.tmd.new */
} pack_view;

/* Works out the state of the pack: AM first, then the record (installed.tmd, else installed.tmd.new, whichever
 * tp_tmd_check finds equal to AM's list), then the catalogue for every theme record. mark is store_mark's value.
 * Only PACK_OK allows an install. */
int pack_assess(pack_view *pv, const am_view *am, const store_record rec[2], uint16_t mark,
                const catalogue_reader *cat);
void pack_free(pack_view *pv);

/* The installed theme a zip already is: same name as the catalogue holds it (cut to 63 bytes) and same icon.
 * Returns the index or 0; *usable tells whether that one is installed (HOME lists it) or was deleted in HOME. */
uint16_t pack_find(const pack_view *pv, const char *name, const uint8_t icon[TP_ICON_SIZE], int *usable);

/* The name as the catalogue stores it: at most 63 bytes, cut at a UTF-8 character boundary. */
void pack_cut_name(char out[0x41], const char *name);

enum plan_state {
    PLAN_OK,
    PLAN_DUPLICATE,             /* the zip is installed already (duplicate: its index) */
    PLAN_FULL_THEMES,
    PLAN_FULL_INDEX,
    PLAN_CORE                   /* a core call refused (detail) */
};

typedef struct install_plan {
    int state;
    int detail;
    uint16_t duplicate;
    uint16_t index;             /* the new theme's content index */
    tp_entry entries[TP_THEMES_MAX];    /* the new catalogue: every theme record of the TMD, then the new one */
    size_t count;
    uint16_t top;
    tp_delta delta;             /* content 0's new ID, the new version, the one content to add */
    const pack_view *pv;
    const tp_theme *theme;
} install_plan;

/* Plans adding one opened theme to a PACK_OK pack, with the core's rules (tp_next_index, tp_delta_plan). The
 * plan points into pv and theme; keep both until the install is over. */
int plan_install(install_plan *plan, const pack_view *pv, const tp_theme *theme);

/* The catalogue of a plan, with an icon callback that serves the installed icons and the new theme's. */
void plan_catalogue(const install_plan *plan, tp_catalogue *cat);

#endif
