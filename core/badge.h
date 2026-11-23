/* Badge core: the HOME Menu's badge collection in extdata 000014d1 (BadgeData.dat, BadgeMngFile.dat) and the saved
 * layouts in extdata 0000165c (MngFile.dat), handled the way the Mac tool tools/badges/badge_sets.py handles them:
 * list the sets, append one set without changing anything that exists, remove the last set back to its earlier
 * bytes, check that every placement points at a badge. For the same input the results are the same byte for byte
 * (host/test_badges.py compares them).
 *
 * Plain C11 plus zlib, like tp.h: buffers in, buffers out, no file system and no console calls. The caller reads
 * the decrypted files, hands them in, and writes back what comes out. Every call returns BADGE_OK (0) or a
 * badge_result; after an error the caller's buffers hold what they held before the call (BADGE_E_REFUSED aside, see
 * badge_append). Memory goes through the functions of tp_set_allocator.
 *
 * Names: file and set names are UTF-8 of U+0001..U+00FF (ASCII, Latin-1) and of the blocks that need no Unicode
 * normalisation: kana (not the combining voicing marks U+3099, U+309A), CJK ideographs, Hangul syllables,
 * fullwidth forms, common punctuation and symbols, emoji (badge.c TAKEN lists them). There the Mac tool's NFC
 * changes nothing and its casefold is known, so the result is the same; a name with anything else (a combining
 * mark, a cased script beyond Latin-1, a character NFC would change) is BADGE_E_NAME, where the Mac tool would
 * normalise it. At most 68 UTF-16 units: a character beyond U+FFFF counts two, as a surrogate pair in the file.
 */
#ifndef BADGE_H
#define BADGE_H

#include <stddef.h>
#include <stdint.h>

#include "tp.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum badge_result {
    BADGE_OK = 0,
    BADGE_E_ARG,        /* a caller argument is out of range */
    BADGE_E_NOMEM,      /* an allocation failed */
    BADGE_E_SIZE,       /* a badge file has the wrong size: not a decrypted copy, or another file */
    BADGE_E_FORMAT,     /* the collection is not laid out the way a set can be appended to it safely */
    BADGE_E_PNG,        /* a damaged PNG */
    BADGE_E_PNG_KIND,   /* a PNG kind that is not read: not 8 bits per channel, interlaced, non-standard */
    BADGE_E_IMAGE_SIZE, /* a badge image whose sides are not multiples of 64, a set icon not 48x48, or over 4096x4096 */
    BADGE_E_FOLDER,     /* not a set: a file that is not a PNG, a subfolder, no badge PNG */
    BADGE_E_NAME,       /* an empty name, one over 68 UTF-16 units, or one with a code point the core does not take */
    BADGE_E_MANIFEST,   /* the manifest has missing, repeated or out-of-range IDs or counters */
    BADGE_E_IDS,        /* no set ID or badge ID left in the ranges of this tool */
    BADGE_E_PRESENT,    /* the set is in the collection already */
    BADGE_E_FULL,       /* the collection would hold more than 100 sets or 1000 badges */
    BADGE_E_INDEX,      /* the new set's index is used by an existing set */
    BADGE_E_TAKEN,      /* a badge ID of the manifest is in the collection already */
    BADGE_E_REFUSED,    /* the result would not keep the existing collection intact (the check after writing) */
    BADGE_E_EMPTY,      /* the collection has no set */
    BADGE_E_NOT_OURS,   /* the last set was not appended by this tool (not in the manifest) */
    BADGE_E_LAST,       /* the last set has no badges, or does not end at the last badge */
    BADGE_E_PLACED,     /* badges of the last set are placed in the live layout */
    BADGE_E_NO_UNDO,    /* no undo record of the last set: its earlier bytes are unknown */
    BADGE_E_CHANGED,    /* the files changed since the append (HOME, for example); allow_changes takes them */
    BADGE_E_SET_CHANGED,/* a byte the append wrote in the set's areas changed since: no undo record fits */
    BADGE_E_AMBIGUOUS,  /* several undo records fit: pass the one the append made */
    BADGE_E_UNDO,       /* an undo record is damaged */
    BADGE_E_SLOTS,      /* the live BadgeMngFile.dat header, or the MngFile.dat header, is not valid */
    BADGE_E_ZIP         /* a zip this reader does not accept, or a damaged member */
} badge_result;

const char *badge_result_name(int result);   /* "BADGE_E_PNG" */
const char *badge_result_text(int result);   /* one sentence */

#define BADGE_DATA_SIZE    0xF4DF80u     /* BadgeData.dat */
#define BADGE_MNG_SIZE     0xD4A8u       /* BadgeMngFile.dat */
#define BADGE_SLOTS_SIZE   0x4D1280u     /* MngFile.dat of extdata 0000165c */
#define BADGE_SETS_MAX     100
#define BADGE_BADGES_MAX   1000
#define BADGE_PLACED_MAX   360
#define BADGE_LAYOUT_SLOTS 8
#define BADGE_NAME_UNITS   68            /* UTF-16 units of a name, before its terminating NUL */
#define BADGE_NAME_BYTES   208           /* a name read back as UTF-8, with its NUL */
#define BADGE_ICON_BYTES   TP_ICON_SIZE  /* a set icon for the screen: 48x48 tiled RGB565, like an SMDH icon */
/* The IDs this tool hands out (badge_sets.py SET_IDS, BADGE_IDS): away from the 1..100 and 1..1000 of Anemone3DS
 * and Advanced Badge Editor and the 0xEFBE of GYTB. */
#define BADGE_SET_ID_FIRST 0x7000u
#define BADGE_SET_ID_END   0x8000u
#define BADGE_ID_FIRST     0x4000u
#define BADGE_ID_END       0x7000u

/* ---------------------------------------------------------------- the manifest
 * Which sets this tool appended and the IDs it gave: a set keeps its ID and every (file, tile) its badge ID for
 * good. Kept by the caller (badge_sets.py: <data>/manifest.json); the core takes and returns it as plain values. */

typedef struct badge_known {
    const char *file;           /* the PNG's file name, as the set source names it */
    uint32_t tile;              /* the tile in that file, column by column */
    uint32_t badge_id;
} badge_known;

typedef struct badge_manifest_set {
    const char *folder;         /* the key: the set folder's name */
    const char *name;           /* the set's name in HOME */
    uint32_t set_id;
    const badge_known *badges;
    size_t count;
} badge_manifest_set;

typedef struct badge_manifest {
    uint32_t next_set_id;       /* BADGE_SET_ID_FIRST for a new manifest */
    uint32_t next_badge_id;     /* BADGE_ID_FIRST for a new manifest */
    const badge_manifest_set *sets;
    size_t count;
} badge_manifest;

/* The checks of badge_sets.py load_manifest: IDs unique and in range, folders unique, the counters above every ID
 * handed out. BADGE_E_MANIFEST if not. A NULL manifest is an empty one, everywhere. */
int badge_manifest_check(const badge_manifest *manifest);

/* ---------------------------------------------------------------- the collection */

enum {
    BADGE_WARN_UNKNOWN = 1,     /* placements that name no badge of the collection (HOME empties them) */
    BADGE_WARN_PLACED  = 2,     /* the header's placed count differs from the layout (HOME recounts) */
    BADGE_WARN_COUNTS  = 4      /* per-badge placed counts differ from the layout (HOME recounts) */
};

typedef struct badge_set_info {
    uint32_t slot;              /* its record in the set table */
    uint32_t set_id;
    uint32_t index;             /* where its name and icon are in BadgeData.dat */
    uint32_t count;             /* its badges */
    uint32_t start;             /* the index of its first badge */
    uint32_t total;             /* copies of its badges */
    uint32_t placed;            /* its badges' placed counts added up */
    int mine;                   /* in the manifest: appended by this tool */
    int removable;              /* badge_remove_last would take it off as far as the files and the manifest say
                                   (the collection's remove is BADGE_OK and this is the last set); the undo record
                                   is checked only by badge_remove_check and badge_remove_last */
    char name[BADGE_NAME_BYTES];/* UTF-8: the English name, else the Japanese one, as badge_sets.py shows it */
} badge_set_info;

typedef struct badge_collection {
    uint32_t sets;              /* header counts */
    uint32_t badges;
    uint32_t placed;
    int nnid_set;               /* the NNID field is not 0 (its value is never given out) */
    uint32_t layout;            /* placement records in use in the live layout */
    uint32_t unknown;           /* of those, how many name no badge */
    unsigned warnings;          /* BADGE_WARN_* */
    int remove;                 /* what badge_remove_last says before it looks at undo records: BADGE_OK, or
                                   BADGE_E_EMPTY, BADGE_E_NOT_OURS, BADGE_E_LAST, BADGE_E_PLACED */
    badge_set_info set[BADGE_SETS_MAX];
} badge_collection;             /* about 25 KB: not for a small stack */

/* Reads the collection the strict way badge_sets.py parse does (sizes; header; the used-badge bits exactly
 * 0..badges-1; set records, IDs and indices; every badge in one contiguous block per set, in record order; the
 * totals). BADGE_E_SIZE, BADGE_E_FORMAT, then BADGE_E_MANIFEST. Placement mismatches are only warnings. */
int badge_read(const uint8_t *data, size_t data_size, const uint8_t *mng, size_t mng_size,
               const badge_manifest *manifest, badge_collection *collection);

/* A set's icon for a screen: the 48x48 part HOME shows, as 48x48 tiled RGB565 (the layout of an SMDH icon). */
int badge_set_icon(const uint8_t *data, size_t data_size, const badge_set_info *set, uint8_t icon[BADGE_ICON_BYTES]);

/* ---------------------------------------------------------------- a set to append
 * A set comes as the files of a folder (the caller lists it: names, which are folders, the bytes of each file),
 * as a zip (badge_zip_files turns it into that list), or as one PNG (a list of one file). The rules of
 * badge_sets.py read_set_folder, as Anemone3DS takes a folder: names starting with "." are skipped; every other
 * entry must be a PNG file (its name ends in ".png", any case); a file named "_seticon.png" in any case is not a
 * badge, and the last of them in the order is the set icon, 48x48; every other PNG is cut into 64x64 badges,
 * column by column, each named after its file up to the first dot. Files in the order of their names under
 * Unicode casefold, ties by the names themselves. */

typedef struct badge_file {
    const char *name;           /* UTF-8, no "/" */
    int is_dir;
    const uint8_t *data;        /* the file's bytes; not read for a folder or a skipped name */
    size_t size;
} badge_file;

typedef struct badge_tile {
    uint32_t file;              /* files[file] */
    uint32_t tile;              /* the tile in that file, column by column */
    uint32_t x0, y0;            /* its top-left pixel */
    uint32_t name_len;          /* its name: the first name_len bytes of the file's name */
} badge_tile;

typedef struct badge_folder {
    badge_tile *tiles;          /* in file order, each file's tiles in tile order */
    size_t count;
    long icon;                  /* files[icon] is _seticon.png, or -1 */
} badge_folder;

/* The set's badges, every PNG decoded once to check it (and freed again). BADGE_E_FOLDER, BADGE_E_NAME, PNG
 * errors, BADGE_E_IMAGE_SIZE. badge_folder_free afterwards, also after an error. */
int badge_folder_read(const badge_file *files, size_t count, badge_folder *folder);
void badge_folder_free(badge_folder *folder);

/* A zip of PNG files as the folder it would unpack to: members under "__MACOSX/" are left out; when every other
 * member lies in one top folder, that folder is the set folder (a zipped folder); a member in any other subfolder
 * becomes a folder entry (so it is refused like a subfolder, unless its name starts with "."). Member names in
 * UTF-8 need the zip's UTF-8 flag; without it they must be ASCII (BADGE_E_NAME). A member over 32 MiB, ZIP64,
 * encryption or a method other than stored and deflate: BADGE_E_ZIP. Order in the zip does not count: the
 * folder rules sort. The list and its bytes are allocated: badge_files_free. */
int badge_zip_files(const tp_source *zip, badge_file **files, size_t *count);
void badge_files_free(badge_file *files, size_t count);

/* One PNG: 8-bit grey, RGB, palette, grey with alpha and RGBA, not interlaced, tRNS honoured; everything else is
 * BADGE_E_PNG_KIND, damage BADGE_E_PNG. *rgba (width * height * 4 bytes) is allocated: tp_free it. */
int badge_png_decode(const uint8_t *png, size_t size, uint32_t *width, uint32_t *height, uint8_t **rgba);

/* ---------------------------------------------------------------- append */

typedef struct badge_written {  /* one badge of the new set, in index order */
    uint32_t index;
    uint32_t badge_id;
    uint32_t file, tile;        /* where it comes from */
    int is_new;                 /* a new manifest entry {file name, tile, badge_id} of the set, in this order */
} badge_written;

typedef struct badge_append_result {
    uint32_t set_id;
    int new_set;                /* the manifest gets a new set {folder, name, set_id, the new badges} */
    uint32_t slot;              /* its set record, also its index */
    uint32_t start, count;      /* its badges' indices */
    int set_bit;                /* its used-set bit was set (it follows the set before it) */
    int icon_from_file;         /* the icon is _seticon.png, else the first badge scaled to 48x48 */
    uint32_t next_set_id;       /* the manifest's counters after the append */
    uint32_t next_badge_id;
    char name[BADGE_NAME_BYTES];/* the set's name as written; also the manifest set's name from now on */
    badge_written *badges;      /* count entries */
    unsigned warnings;          /* of the result, unchanged from the input: BADGE_WARN_* */
    uint32_t unknown;
    uint8_t *undo;              /* the undo record for badge_remove_last; the caller keeps it (on the card) */
    size_t undo_size;
} badge_append_result;

/* Appends the set in files to the collection, in place, after the checks of badge_sets.py cmd_append in its order
 * (the collection, the set's files, the manifest, the name, the IDs; then already present, 100 sets, 1000
 * badges, the set index, the badge IDs). folder is the manifest key; name NULL takes the manifest set's name, else
 * the folder. Afterwards it checks on the finished buffers that every byte outside the new set's areas is
 * unchanged and that the collection reads back with the old sets and badges unchanged; if not, BADGE_E_REFUSED:
 * the set's areas and BadgeMngFile.dat are put back, but as that check is there to catch a fault of this code,
 * never write the buffers back after it (read the files again). Decodes every PNG once more, one at a time; needs
 * about 110 KB besides that, and the undo record (about 0x2A00 bytes plus 0x3AC8 per badge, 1.5 MB for 100).
 * badge_append_free afterwards, also after an error. */
int badge_append(uint8_t *data, size_t data_size, uint8_t *mng, size_t mng_size, const badge_file *files,
                 size_t count, const badge_manifest *manifest, const char *folder, const char *name,
                 badge_append_result *result);
void badge_append_free(badge_append_result *result);

/* ---------------------------------------------------------------- remove the last set */

typedef struct badge_undo {     /* an undo record as badge_append gave it */
    const uint8_t *data;
    size_t size;
} badge_undo;

typedef struct badge_remove_result {
    uint32_t set_id, slot, start, count;
    char name[BADGE_NAME_BYTES];
    size_t record;              /* the record that was used */
    int exact;                  /* the files were what that append wrote */
    int identical;              /* both files are now byte-identical to the files before the append (only
                                   badge_remove_last says) */
    int view_reset;             /* the badge case's view (header 0x10..0x17) named the set or no set and gets its
                                   value from before the append */
} badge_remove_result;

/* Removes the last set, in place: only a set of the manifest, with none of its badges placed, only while every set's
 * index is its slot (else BADGE_E_FORMAT: the view names a set by one of them, which one is not known), and only with
 * an undo record of it. Files exactly what the record says the append wrote (both SHA-256) are taken as they are; files that
 * changed since only with allow_changes, and then only if every byte the application wrote in the set's areas is
 * unchanged: HOME's own two fields in them (each badge's placed count, which must be 0, and the set record's + 0x28)
 * are not compared, every byte the append wrote is (the record's sum of the areas). Only what takes the set out is
 * written: the set's areas get their bytes from before the append (the counts, its used bits, its records, names
 * and images), the badge case's view only if it names the set or no set (it gets its value from before the append);
 * every other field keeps what HOME left in it (the placed counts, the other sets' records, the layout). Checked
 * afterwards like an append (BADGE_E_REFUSED: as there). Needs about 110 KB and the size of the set's areas. */
int badge_remove_last(uint8_t *data, size_t data_size, uint8_t *mng, size_t mng_size,
                      const badge_manifest *manifest, const badge_undo *records, size_t count, int allow_changes,
                      badge_remove_result *result);

/* What badge_remove_last would answer, without changing a byte: the same refusals in the same order, or BADGE_OK
 * with the result filled as it would be (identical aside). For a caller that must know before it writes anything
 * else (the application's backup). Needs about 25 KB. */
int badge_remove_check(const uint8_t *data, size_t data_size, const uint8_t *mng, size_t mng_size,
                       const badge_manifest *manifest, const badge_undo *records, size_t count, int allow_changes,
                       badge_remove_result *result);

/* ---------------------------------------------------------------- the saved layouts (test B2) */

typedef struct badge_layout_check {
    int saved;                  /* MngFile.dat marks the slot as saved (the live layout: 1) */
    int damaged;                /* the copy's header word 0x0 is not 0 */
    uint32_t placements, folder_icons;
    uint32_t missing;           /* no badge with that badge ID, set ID and sub ID (HOME empties the placement) */
    uint32_t differs;           /* the badge exists, other identity bytes differ */
    uint32_t header_placed;     /* the copy's own placed count */
} badge_layout_check;

typedef struct badge_slots_check {
    uint32_t badges;            /* badges of the live collection (used bits) */
    badge_layout_check live;
    badge_layout_check slot[BADGE_LAYOUT_SLOTS];
    int ok;                     /* every placement of the live layout and of every saved slot points at a badge */
} badge_slots_check;

/* Every placement in the live BadgeMngFile.dat and in the eight copies of MngFile.dat against the live
 * collection. BADGE_E_SIZE, or BADGE_E_SLOTS when a header word 0x0 is not 0 or the live header counts over 1000
 * badges; a damaged slot copy is reported, not refused. */
int badge_check_slots(const uint8_t *mng, size_t mng_size, const uint8_t *slots, size_t slots_size,
                      badge_slots_check *check);

#ifdef __cplusplus
}
#endif

#endif
