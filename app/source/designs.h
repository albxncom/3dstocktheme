/* The designs installed in the pack, for the screen "Installierte Designs": what the HOME Menu lists of
 * 0004008C00009809, not the files on the card. Plain C on top of pack_assess, so the host tests check it.
 *
 * Where it comes from, and who wins when the sources disagree:
 * - The theme records of the installed TMD and their catalogue entries (name, icon), as pack_assess read them. They
 *   count only when the record file matches AM's content list (tp_tmd_check) and the catalogue has every record's
 *   entry; any other pack state shows the reason instead of a list. A full pack is read whole and is listed.
 * - AM decides what is installed: a record AM lists as not installed and owned (deleted in HOME or here) is not
 *   shown, although it keeps its record, its catalogue entry and its index. AM's content count is the TMD's
 *   (installed or not), so the list never relies on it, only on each content's flags.
 * - The music tag: whether the installed content holds bgm.bcstm (read from the content itself, not from a file on
 *   the card, so a design installed without music shows none).
 * - The file: a theme file in the folder /Themes whose name and icon equal the design's (as the folder view finds
 *   installed themes). */
#ifndef DESIGNS_H
#define DESIGNS_H

#include <stddef.h>
#include <stdint.h>

#include "plan.h"
#include "scan.h"

enum { MUSIC_UNKNOWN, MUSIC_NONE, MUSIC_YES };

typedef struct design_row {
    uint16_t index;
    const pack_theme *theme;    /* in the pack view the list was made from */
    int music;                  /* MUSIC_* */
    char file[SCAN_NAME];       /* its file in /Themes, "" if none was found */
} design_row;

typedef struct design_list {
    design_row rows[TP_THEMES_MAX];
    size_t count;
} design_list;

/* Whether the pack was read whole, so its designs can be listed (and deleted): PACK_OK or full. */
int designs_readable(const pack_view *pv);

/* The installed designs of a readable pack in index order (the order they were added); none otherwise. Music from
 * music_of (by content index, MUSIC_*), no files yet. */
void designs_list(design_list *d, const pack_view *pv, const uint8_t music_of[PACK_CONTENTS]);

/* The file in the folder that list shows for each design: an entry the folder view marks as this installed design. */
void designs_files(design_list *d, const scan_list *folder);

/* How many more designs the pack can take: theme records (deleted ones count) up to TP_THEMES_MAX, and indices,
 * which are never handed out twice, up to TP_INDEX_MAX. 0 for a pack that was not read whole. */
unsigned designs_room(const pack_view *pv);

/* The room worth a warning: designs_room while the pack still takes designs (PACK_OK) but fewer than
 * DESIGNS_ROOM_LOW more; 0 otherwise: plenty is left and nothing is said, or the pack is full, which its state says
 * itself. */
#define DESIGNS_ROOM_LOW 5
unsigned designs_room_low(const pack_view *pv);

#endif
