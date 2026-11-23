/* Installing one theme into the installed pack: the AM sequence that passed on the console in T3, in two commits.
 *
 *   before any AM write   measure content 0 and the theme (size, SHA-256), build the new TMD, check it, write it
 *                         to the record's staging file
 *   phase 1               title import begin, TMD, content 0, title import finish, commit; then the record
 *   phase 2               title import begin, import context for the new index, the theme content, finish, commit
 *   on a failure          the importer cancels what is open and removes only this title's pending import; the
 *                         staging file stays (store.h: it counts only if AM's list confirms it)
 *
 * The AM calls sit behind an importer (am.c on the console, a folder of files in the host tests), so this file and
 * everything it decides run in the host tests. */
#ifndef INSTALL_H
#define INSTALL_H

#include <stddef.h>
#include <stdint.h>

#include "plan.h"
#include "tp.h"

typedef struct importer {
    void *ctx;
    int (*title_begin)(void *ctx);                                      /* AMNET_InstallTitleBegin */
    int (*tmd)(void *ctx, const uint8_t *tmd, size_t size);             /* TmdBegin, write, TmdFinish */
    int (*contexts)(void *ctx, const uint16_t *indices, size_t count);  /* CreateImportContentContexts */
    int (*content_begin)(void *ctx, uint16_t index);                    /* InstallContentBegin */
    int (*content_write)(void *ctx, const void *buf, size_t len);       /* the content's bytes, in order */
    int (*content_finish)(void *ctx);                                   /* InstallContentFinish */
    int (*commit)(void *ctx);                                           /* InstallTitleFinish, CommitImportTitles */
    void (*cancel)(void *ctx);  /* cancel what is open, then remove this title's pending import if there is one */
} importer;

enum install_step {
    STEP_NONE,
    STEP_MEASURE,               /* the core measured content 0 and the theme, built the TMD */
    STEP_STAGE,                 /* the new TMD written to installed.tmd.new */
    STEP_TITLE1,
    STEP_TMD,
    STEP_CONTENT0,
    STEP_COMMIT1,
    STEP_RECORD,                /* installed.tmd.new renamed to installed.tmd (a failure here is not fatal) */
    STEP_TITLE2,
    STEP_CONTEXT,
    STEP_CONTENT,
    STEP_COMMIT2,
    STEP_DONE
};

typedef struct install_progress {
    void *ctx;
    void (*show)(void *ctx, int step, uint64_t done, uint64_t total);
} install_progress;

typedef struct install_result {
    int done;                   /* both commits went through */
    int failed;                 /* the step that failed, STEP_NONE if none */
    int detail;                 /* tp_result when a core call failed */
    int wrote;                  /* AM began this title's import: from here on a failure is cancelled */
    int tmd_accepted;           /* AM took the new TMD */
    int committed1;             /* phase 1 committed: the new TMD and catalogue are installed */
    int record_ok;              /* installed.tmd names the new TMD (else installed.tmd.new does) */
    tp_content content0, theme; /* the measured records */
} install_result;

int install_run(const install_plan *plan, const tp_theme *theme, const importer *imp, const char *store_dir,
                const install_progress *progress, install_result *res);

/* What happened, judged from AM's view after the install against the view before. */
enum outcome {
    OUT_DONE,                   /* both commits, and AM lists the new theme as installed, nothing lost */
    OUT_DONE_UNCONFIRMED,       /* both commits, but AM's view afterwards does not confirm it */
    OUT_NOTHING_WRITTEN,        /* failed before AM was asked to write anything */
    OUT_CANCELLED_KEPT,         /* failed in phase 1 and cancelled; every installed theme still installed */
    OUT_PHASE2_FAILED_KEPT,     /* phase 1 committed, phase 2 failed; every installed theme still installed */
    OUT_LOST,                   /* failed, and AM lists installed themes as gone */
    OUT_UNKNOWN                 /* failed, and AM's view afterwards could not be read */
};

typedef struct judgement {
    int outcome;
    int seen;                   /* AM's view afterwards could be read */
    size_t lost;                /* themes installed before that AM no longer lists as installed */
    int new_installed;          /* AM lists the new index as installed and owned */
    int pending;                /* a pending import of this title is left (PENDING_*) */
} judgement;

void install_judge(const am_view *before, const am_view *after, const install_plan *plan,
                   const install_result *res, judgement *j);

/* May the application offer to delete the file the theme came from? Only when both commits went through, AM's
 * view afterwards confirms the theme, and the record was renamed: never after a failure. */
int install_may_clean(const judgement *j, const install_result *res);

#endif
