#include "settings.h"

#include <stdio.h>
#include <string.h>

#include "files.h"

#define HEADER "3DStockTheme settings 1"

/* the file's words for the colour schemes, the glass styles and the patterns, by their number */
static const char *const SCHEME_WORDS[SETTINGS_SCHEMES] = {"white", "yellow", "sand", "mint", "sky", "lilac", "rose",
                                                           "night"};
static const char *const LOGO_WORDS[SETTINGS_STYLES] = {"clear", "shard", "platin"};
static const char *const PATTERN_WORDS[SETTINGS_STYLES] = {"plus", "houses", "bubbles"};
static const char *const DESIGN_WORDS[SETTINGS_DESIGNS] = {"stock", "frost", "night", "prism"};

static int scheme_now = SETTINGS_SCHEME_DEFAULT, logo_now = SETTINGS_LOGO_DEFAULT,
           pattern_now = SETTINGS_PATTERN_DEFAULT;

static int style(int v, int fallback)
{
    return v >= 0 && v < SETTINGS_STYLES ? v : fallback;
}

static int scheme(int v)
{
    return v >= 0 && v < SETTINGS_SCHEMES ? v : SETTINGS_SCHEME_DEFAULT;
}

static int design(int v)
{
    return v >= 0 && v < SETTINGS_DESIGNS ? v : SETTINGS_DESIGN_DEFAULT;
}

const char *settings_design_word(int design_number)
{
    return DESIGN_WORDS[design(design_number)];
}

int settings_scheme(void)
{
    return scheme_now;
}

int settings_logo(void)
{
    return logo_now;
}

int settings_pattern(void)
{
    return pattern_now;
}

void settings_apply(const settings *s)
{
    scheme_now = scheme(s->scheme);
    logo_now = style(s->logo, SETTINGS_LOGO_DEFAULT);
    pattern_now = style(s->pattern, SETTINGS_PATTERN_DEFAULT);
}

void settings_defaults(settings *s, const char *default_music)
{
    memset(s, 0, sizeof *s);
    snprintf(s->music, sizeof s->music, "%s", default_music ? default_music : "");
    s->sounds = 1;
    s->scheme = SETTINGS_SCHEME_DEFAULT;
    s->logo = SETTINGS_LOGO_DEFAULT;
    s->pattern = SETTINGS_PATTERN_DEFAULT;
    s->design = SETTINGS_DESIGN_DEFAULT;
}

/* A file name as a track has one: letters, digits, dot, dash and underscore, no path. */
static int is_name(const char *v, size_t n)
{
    if (n == 0 || n >= SETTINGS_NAME)
        return 0;
    for (size_t i = 0; i < n; i++) {
        char c = v[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-' ||
              c == '_'))
            return 0;
    }
    return 1;
}

static int is(const char *v, size_t n, const char *word)
{
    return n == strlen(word) && !memcmp(v, word, n);
}

/* The number of the word v among the count words, or fallback. */
static int word_of(const char *v, size_t n, const char *const words[], int count, int fallback)
{
    for (int k = 0; k < count; k++)
        if (is(v, n, words[k]))
            return k;
    return fallback;
}

int settings_parse(const char *text, size_t size, settings *s)
{
    size_t head = strlen(HEADER);
    if (size > SETTINGS_MAX || size < head + 1 || memcmp(text, HEADER, head))
        return 0;
    size_t at = head;
    if (text[at] == '\r' && at + 1 < size)
        at++;
    if (text[at] != '\n')
        return 0;
    at++;
    while (at < size) {
        size_t end = at;
        while (end < size && text[end] != '\n')
            end++;
        size_t n = end - at;
        if (n && text[at + n - 1] == '\r')
            n--;
        const char *line = text + at, *eq = memchr(line, '=', n);
        if (eq) {
            size_t key = (size_t)(eq - line), len = n - key - 1;
            const char *v = eq + 1;
            if (is(line, key, "music")) {
                if (is(v, len, "off")) {
                    s->music[0] = 0;
                } else if (is_name(v, len)) {
                    memcpy(s->music, v, len);
                    s->music[len] = 0;
                }
            } else if (is(line, key, "sounds")) {
                if (is(v, len, "on"))
                    s->sounds = 1;
                else if (is(v, len, "off"))
                    s->sounds = 0;
            } else if (is(line, key, "colour")) {
                s->scheme = word_of(v, len, SCHEME_WORDS, SETTINGS_SCHEMES, s->scheme);
            } else if (is(line, key, "glass")) {
                s->logo = word_of(v, len, LOGO_WORDS, SETTINGS_STYLES, s->logo);
            } else if (is(line, key, "pattern")) {
                s->pattern = word_of(v, len, PATTERN_WORDS, SETTINGS_STYLES, s->pattern);
            } else if (is(line, key, "design")) {
                s->design = word_of(v, len, DESIGN_WORDS, SETTINGS_DESIGNS, SETTINGS_DESIGN_DEFAULT);
            }
        }
        at = end + 1;
    }
    return 1;
}

size_t settings_format(const settings *s, char *out, size_t cap)
{
    int n = snprintf(out, cap, HEADER "\ncolour=%s\nmusic=%s\nsounds=%s\nglass=%s\npattern=%s\ndesign=%s\n",
                     SCHEME_WORDS[scheme(s->scheme)], s->music[0] ? s->music : "off", s->sounds ? "on" : "off",
                     LOGO_WORDS[style(s->logo, SETTINGS_LOGO_DEFAULT)],
                     PATTERN_WORDS[style(s->pattern, SETTINGS_PATTERN_DEFAULT)], DESIGN_WORDS[design(s->design)]);
    return n > 0 && (size_t)n < cap ? (size_t)n : 0;
}

int settings_load(const char *path, settings *s, const char *default_music)
{
    char text[SETTINGS_MAX];
    size_t size = 0;
    settings_defaults(s, default_music);
    if (files_read(path, (uint8_t *)text, sizeof text, &size) != FILES_OK)
        return 0;
    return settings_parse(text, size, s);
}

int settings_save(const char *path, const settings *s)
{
    char text[SETTINGS_MAX];
    size_t size = settings_format(s, text, sizeof text);
    return size && files_write(path, (const uint8_t *)text, size) == FILES_OK ? 0 : -1;
}
