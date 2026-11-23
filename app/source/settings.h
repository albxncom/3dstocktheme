/* What the settings screen keeps on the card: the colour scheme, the menu music's track, whether the interface
 * sounds are on, the style of every glass element (the house, the tile symbols, the picture's badges, the step
 * marks; 0.9.3's, read and written back unchanged since), the start screen's background pattern and the look. A
 * small text file in the application's folder, written whenever a choice changes and read at start:
 *
 *     3DStockTheme settings 1
 *     colour=white                                 ("white", "yellow", "sand", "mint", "sky", "lilac", "rose" or
 *                                                   "night"; since 0.9.3: a file without the line gets white)
 *     music=somewhere-in-the-elevator.bcstm        (a track's file name, or "off")
 *     sounds=on                                    ("on" or "off")
 *     glass=clear                                  ("clear", "shard" or "platin")
 *     pattern=houses                               ("plus", "houses" or "bubbles")
 *     design=stock                                 (the look: "stock", "frost", "night" or "prism"; since G1, so
 *                                                   0.9.3 skips the line and drops it on its own save)
 *
 * The first line names the format and its version; a file without exactly that line is not read at all. After it
 * every line is key=value, in any order; a line this version does not know is skipped, a value it cannot use
 * leaves that choice at its default. So a missing, cut, damaged or foreign file gives the defaults and never an
 * error: the worst a bad file costs is the choice. Plain C, so the host tests read and write it. */
#ifndef SETTINGS_H
#define SETTINGS_H

#include <stddef.h>

#define SETTINGS_NAME 48        /* a track's file name with its NUL */
#define SETTINGS_MAX 512        /* a settings file is never larger; a larger one is not read */

/* There are SETTINGS_STYLES glass styles and as many patterns, each 0 to 2 in the order of the file's words above,
 * which is the drawing layer's order (ui.h: GLASS_CLEAR, GLASS_SHARD, GLASS_PLATIN; PATTERN_PLUS, PATTERN_HOUSES,
 * PATTERN_BUBBLES; main.c refuses to build otherwise). settings_logo() is the glass style. */
#define SETTINGS_STYLES 3
#define SETTINGS_LOGO_DEFAULT 1         /* Shard (0.9.3; Clear before: a file that says "glass=clear" keeps it) */
#define SETTINGS_PATTERN_DEFAULT 1      /* the small houses */
/* The colour schemes, 0 to SETTINGS_SCHEMES - 1 in the order of the file's words above, which is the drawing
 * layer's (ui_art.h, ART_SCHEME_*; main.c refuses to build otherwise). White is the standard. */
#define SETTINGS_SCHEMES 8
#define SETTINGS_SCHEME_DEFAULT 0
/* The looks, 0 to SETTINGS_DESIGNS - 1 in the order of the file's words above, which is look.h's (LOOK_*; main.c
 * refuses to build otherwise). Stock is the standard: a file without the line, or with another word, gets it; one a
 * build does not hold is drawn as Stock and kept on the card until another look is chosen. */
#define SETTINGS_DESIGNS 4
#define SETTINGS_DESIGN_DEFAULT 0

typedef struct settings {
    int scheme;                 /* the colour scheme, 0 to SETTINGS_SCHEMES - 1 */
    char music[SETTINGS_NAME];  /* the menu music: a track's file name (tracks.h), or "" for none */
    int sounds;                 /* the interface sounds and the working loop are on */
    int logo;                   /* the glass style, 0 to SETTINGS_STYLES - 1 */
    int pattern;                /* its background pattern, 0 to SETTINGS_STYLES - 1 */
    int design;                 /* the look, 0 to SETTINGS_DESIGNS - 1 */
} settings;

/* The defaults: the default scheme, music default_music (a track's file name), sounds on, the default logo,
 * pattern and look. */
void settings_defaults(settings *s, const char *default_music);

/* The look's choices as they stand, for whoever draws: the scheme 0 to SETTINGS_SCHEMES - 1, the glass style and
 * the pattern 0 to SETTINGS_STYLES - 1 each; the defaults until settings_apply was called. */
int settings_scheme(void);
int settings_logo(void);
int settings_pattern(void);
/* The look's word on the card ("stock", "frost", "night", "prism") by its number; the default's for any other. */
const char *settings_design_word(int design_number);
/* Makes s what the three answer: after the file was read, and after every change. A value out of range counts as
 * its default. */
void settings_apply(const settings *s);

/* Reads the text of a settings file over the defaults already in s. 1 when it is a settings file of this version
 * (whatever it then holds), 0 when it is not: s is untouched then. The text is hostile bytes. */
int settings_parse(const char *text, size_t size, settings *s);

/* The file's text for s, NUL-terminated, into out; its length, or 0 when cap is too small. */
size_t settings_format(const settings *s, char *out, size_t cap);

/* The file at path over the defaults: 1 when it was read, 0 when it is missing, too large or not a settings file
 * (s then holds the defaults). */
int settings_load(const char *path, settings *s, const char *default_music);

/* Writes the file anew. 0, or -1 when it could not be written (the choice then holds until the application ends). */
int settings_save(const char *path, const settings *s);

#endif
