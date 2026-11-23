/* Every word the application shows, in English and German (the console's system language picks; German for
 * German, English for everything else). Plain C, so the host tests can print and check every message. */
#ifndef TEXT_H
#define TEXT_H

#include <stddef.h>

#include "deletion.h"
#include "install.h"
#include "plan.h"

enum lang { LANG_EN, LANG_DE };

enum text_id {
    T_DESIGNS,                  /* the start screen's two entries */
    T_BADGES,
    T_FROM_SD,                  /* the Designs screen's entries (and T_INSTALLED_DESIGNS) */
    T_SCAN_QR,
    T_REMOVE_SET,               /* the Badges part's screen of one installed set */
    T_EXIT,                     /* the bar */
    T_BACK,
    T_OK,
    T_SELECT,
    T_OPEN,
    T_INSTALL,
    T_LOAD,
    T_CANCEL,
    T_READING_PACK,
    T_READING_FOLDER,
    T_CHECKING_THEME,
    T_SD_CARD,                  /* the folder view */
    T_EMPTY_FOLDER,
    T_OTHER_FILES,              /* %s: how many ("500+" when not all were looked at) */
    T_INSTALLED_ALREADY,
    T_INSTALLED_AS,             /* %u: the index */
    T_UNREADABLE,
    T_MUSIC,                    /* the tags */
    T_WARN_BGM_UNUSED,
    T_WARN_BGM_MISSING,
    T_CANNOT_INSTALL,
    T_INSTALL_THEME,
    T_INSTALLING,
    T_STEP_CHECK,               /* the four steps of an install */
    T_STEP_CATALOGUE,
    T_STEP_THEME,
    T_STEP_FINISH,
    T_DONT_SWITCH_OFF,
    T_DONT_REMOVE_SD,
    T_INSTALLED,                /* after an install */
    T_HOME_MENU,
    T_CHANGE_THEME,
    T_DELETE_ASK,
    T_KEEP,
    T_DELETE,
    T_DELETED,                  /* %s: the file */
    T_DELETE_FAILED,            /* %s: the file */
    T_HELP_FILE,                /* the help on the cleanup question; %s: the file's folder */
    T_HELP_KEEP,
    T_HELP_DELETE,
    T_OKAY,                     /* the popup's button */
    T_THEMES_INSTALLED,         /* the status line; %u: themes HOME lists, 2 or more */
    T_THEME_INSTALLED_1,
    T_NO_THEMES_INSTALLED,
    T_SCAN_HINT,                /* QR code and download */
    T_CAMERA_FAILED,
    T_QR_FOUND,
    T_DOWNLOAD,
    T_DOWNLOADING,
    T_DOWNLOAD_FAILED,
    T_NO_WIFI,                  /* before a download: the wireless is off */
    T_NO_WIFI_HINT,
    T_INSTALL_NO_MUSIC,         /* the check screen's second install button */
    T_LEAVING_OUT_MUSIC,        /* while the content without music is prepared */
    T_DELETE_THEME,             /* the check screen of a theme that is installed: the two buttons */
    T_DELETE_FILE,
    T_ASK_DELETE_THEME,         /* the confirmations; %s: the theme's name */
    T_ASK_DELETE_THEME_NOTE,
    T_ASK_DELETE_FILE_WHERE,    /* %s: the file's folder */
    T_ASK_DELETE_FILE,          /* %s: the file */
    T_ASK_DELETE_FILE_NOTE,
    T_INSTALLED_DESIGNS,        /* the Designs screen's third entry and its screen */
    T_DESIGN_IN_HOME,           /* %u: the index */
    T_ROOM,                     /* %u: how many more the pack can take, shown only when few (designs.h) */
    T_ASK_DELETE_THEME_NO_FILE, /* the confirmation's note when no file of the theme is in /Themes */
    T_DELETING_THEME,           /* while a delete runs */
    T_DELETING_FILE,
    /* ---- flow (0.9.2) */
    T_ASK_THEMES,               /* the question whether to create the folder /Themes, and what it is for */
    T_ASK_THEMES_NOTE,
    T_CREATE,                   /* its button */
    T_THEMES_NOT_CREATED,
    T_CREATING_FOLDER,          /* the loader's line meanwhile */
    /* the audio lead's, 0.9.2 */
    T_PREVIEW,                  /* the footer's Y tab, and the preview's header while it loads */
    T_PREVIEW_LOADING,
    T_PREVIEW_FAILED,
    T_PREVIEW_NO_WALL,          /* a screen the theme gives no wallpaper */
    T_SETTINGS,                 /* the settings screen and its rows */
    T_MENU_MUSIC,               /* its choices: the tracks (tracks.h), then T_NO_MUSIC */
    T_NO_MUSIC,
    T_SOUNDS,                   /* the interface sounds: T_OFF or T_ON */
    T_ON,
    T_OFF,
    T_GLASS,                    /* the glass style of every glass element: the three, in settings.h's order */
    T_GLASS_CLEAR,
    T_GLASS_SHARD,
    T_GLASS_PLATIN,
    T_PATTERN,                  /* the start screen's background: the three patterns, in settings.h's order */
    T_PATTERN_PLUS,
    T_PATTERN_HOUSES,
    T_PATTERN_BUBBLES,
    T_SCHEME,                   /* 0.9.3, the colour scheme: the eight, in settings.h's order, the standard first */
    T_SCHEME_WHITE,
    T_SCHEME_YELLOW,
    T_SCHEME_SAND,
    T_SCHEME_MINT,
    T_SCHEME_SKY,
    T_SCHEME_LILAC,
    T_SCHEME_ROSE,
    T_SCHEME_NIGHT,
    T_SCHEME_PEARL,             /* the first colour's name in a look whose ground is pearl (main.c, LOOK_FACTS) */
    T_LOOK,                     /* G1, the look: the four, in look.h's order */
    T_LOOK_STOCK,
    T_LOOK_FROST,
    T_LOOK_NIGHT,
    T_LOOK_PRISM,
    T_READING_BADGES,           /* the Badges part's loaders */
    T_CHECKING_SET,
    T_INSTALL_SUCCESS,          /* the installing screen's line at the end */
    T_TRY_AGAIN,                /* FB-E: the box's text of a problem that has no explanation of its own */
    T_SOUNDS_CREDIT,            /* FIX-1: under the sounds' choices, the interface sounds' maker; each track's line
                                   is text_track_credit's */
    T_COUNT
};

/* A fixed text. */
const char *text(int lang, int id);

/* The status line: "3 Designs installiert", "1 Design installiert", "Kein Design installiert". */
void text_installed(int lang, unsigned n, char *out, size_t size);

/* The version as the settings' foot, the log's first line and the user agent show it: "0.9.4", or with a pre-release
 * mark (pre not empty) "0.9.4-beta". */
void text_version(char *out, size_t size, int major, int minor, int micro, const char *pre);

/* A menu track's credit under the sounds' choices: "Music: "Urban Shop" by MintoDog, CC0" ("Musik: ... von ..."),
 * from the track's line in tracks.h; its licence as taken up to the first comma (the rest is a note for CREDITS.md). */
void text_track_credit(int lang, const char *title, const char *author, const char *licence, char *out, size_t size);

/* The short German or English sentence for a tp_result. */
const char *text_result(int lang, int result);

/* Title and body for a pack that cannot take a theme (pack_view.state != PACK_OK). */
void text_pack(int lang, const pack_view *pv, char *title, size_t title_size, char *body, size_t body_size);

/* The body for a plan that refuses for a reason other than a duplicate (PLAN_FULL_*, PLAN_CORE), or says the
 * index for PLAN_OK. A duplicate gets the two delete buttons instead. */
void text_plan(int lang, const install_plan *plan, char *body, size_t body_size);

/* in as it can be shown truthfully: valid UTF-8 as it is; every byte the font would not draw as itself (a byte
 * outside a valid sequence, a control character, U+007F to U+009F) as "%XX", so an address shows the bytes it
 * really holds. Ends in "..." when out is too small. */
void text_shown(char *out, size_t size, const char *in);

/* The body for a download that did not end in a theme (download.h, DL_*); detail and error as in its result. */
void text_download(int lang, int status, long http, int detail, const char *error, char *body, size_t body_size);

/* Why a theme is not deleted (deletion_plan.state != DEL_OK), as the popup's two paragraphs; name: the theme. */
void text_deletion(int lang, int state, const char *name, char *head, size_t head_size, char *body, size_t body_size);

/* What a delete did, from the judgement on AM's view afterwards; am_failed and error: what AM answered; file_kept:
 * a file of the theme is known to be on the card. */
void text_deleted(int lang, const deletion_judgement *j, int am_failed, uint32_t error, int file_kept,
                  const char *name, char *head, size_t head_size, char *body, size_t body_size);

/* Title and body for what an install did, from the judgement on AM's view afterwards. */
void text_outcome(int lang, const judgement *j, const install_result *res, const char *theme_name,
                  char *title, size_t title_size, char *body, size_t body_size);

#endif
