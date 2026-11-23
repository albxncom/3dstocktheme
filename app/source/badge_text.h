/* Every word of the Badges part that text.h does not have already, in English and German (text.h's languages).
 * Plain C, so the host tests print and check every message. */
#ifndef BADGE_TEXT_H
#define BADGE_TEXT_H

#include <stddef.h>

#include "badge_flow.h"

enum badge_text_id {
    BT_INSTALLED_BADGES,        /* the Badges screen's wide tile, and that screen's header */
    BT_INSTALL_BADGES,          /* the check screen's header */
    BT_REMOVE_BADGES,           /* the work screen's header for a removal */
    BT_EMPTY_FOLDER,
    BT_FOLDER_MISSING,
    BT_NO_SETS,
    BT_ASK_REMOVE,              /* the question before a removal: "%s" the set */
    BT_ASK_REMOVE_NOTE,         /* "%u" its badges */
    BT_REMOVED_HEAD,            /* FB-E: the note over the list after a removal: "%s" the set */
    BT_REMOVED_NOTE,
    BT_PLACE_BADGES,            /* the HOME Menu's entry for badges, on a set's Done screen (DN; English a proposal) */
    BT_REMOVE,                  /* the question's button */
    BT_WORK_READ,               /* the work screen's line, by badge_step: BT_WORK_READ + step */
    BT_WORK_BACKUP,
    BT_WORK_BACKUP_CHECK,
    BT_WORK_CHANGE,
    BT_WORK_WRITE,
    BT_WORK_VERIFY,
    BT_WORK_RESTORE,
    BT_STEP_READ,               /* the installing screen: the four steps listed beside the box, short (BS) */
    BT_STEP_BACKUP,
    BT_STEP_WRITE,
    BT_STEP_VERIFY,
    BT_ONLY_LAST,               /* why a set of the list cannot be removed */
    BT_NOT_OURS,
    BT_PLACED,
    BT_NO_UNDO,
    BT_NOT_A_SET,               /* a download that is no badge set */
    BT_NOTHING_CHANGED,         /* the problem screen's titles */
    BT_PUT_BACK,
    BT_WRITE_FAILED,
    BT_COUNT
};

const char *badge_text(int lang, int id);

/* "1 Badge", "12 Badges". */
void badge_text_count(int lang, unsigned badges, char *out, size_t size);

/* The Badges screens' status line: "3 Sets, 288 Badges", or what is wrong with the collection. Returns 1 for the
 * counts, 0 for a problem. */
int badge_text_status(int lang, const badge_listing *l, char *out, size_t size);

/* One sentence for a badge_result: why a set does not read, why the core refuses. */
const char *badge_text_result(int lang, int badge);

/* Why the collection as listed does not take the set (badge_flow_plan's answer, not BPLAN_OK). */
void badge_text_plan(int lang, int plan, const badge_listing *l, const badge_peek *peek, char *out, size_t size);

/* Title and body for a write that did not end as BFLOW_DONE. */
void badge_text_outcome(int lang, const badge_flow_result *res, char *title, size_t title_size, char *body,
                        size_t body_size);

#endif
