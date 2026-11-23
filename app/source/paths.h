/* The card's folders as the lists browse them (the Designs part's folder view and the Badges part's folder list):
 * their paths and the names they hide. Defined in scan.c; this header needs nothing else, so the Badges part's reader
 * (badge_flow.c) takes them without the folder view's types. */
#ifndef PATHS_H
#define PATHS_H

#include <stddef.h>

/* dir + "/" + file ("sdmc:/" + file at the card's root). */
void scan_path(char *out, size_t size, const char *dir, const char *file);

/* Whether dir is the root ("sdmc:/", or "/" on the Mac). */
int scan_is_root(const char *dir);

/* Whether a folder of the card hides its entry name from the lists: a name that starts with a dot (macOS leaves
 * "._name" files on cards), and at the root the system's folder "Nintendo 3DS". */
int scan_hidden(const char *dir, const char *name);

/* dir one folder up, in place; the folder it was in goes to child. 0, or -1 at the root. */
int scan_up(char *dir, char *child, size_t child_size);

/* dir as the screens show it: without the device ("sdmc:/Themes" shows as "/Themes"). */
const char *scan_shown(const char *dir);

#endif
