/* Files through stdio: the same code on the console (sdmc:/ through libctru's devoptab) and on the Mac. */
#ifndef FILES_H
#define FILES_H

#include <stddef.h>
#include <stdint.h>

#include "tp.h"

enum { FILES_OK = 0, FILES_MISSING = -1, FILES_ERROR = -2 };

/* A tp_source over a file. tick, if set, is told after every read how many bytes were read so far (every read
 * counted, wherever it is; a part read twice counts twice) and the file's size, for a progress bar. */
int files_open_source(const char *path, tp_source *src, void (*tick)(void *ctx, uint64_t read, uint64_t size),
                      void *tick_ctx);
void files_close_source(tp_source *src);
/* The count of bytes read starts again at 0: a new pass over the file begins (the install measures what the check
 * read). */
void files_count_restart(const tp_source *src);
/* For measuring only: around is called before every read of the file (begin 1) and after it (begin 0), so a caller
 * with a clock can time the reads; NULL: no longer. */
void files_around_reads(const tp_source *src, void (*around)(void *ctx, int begin), void *ctx);

/* Whether path is there (a file or a folder). */
int files_exists(const char *path);
/* Makes the one folder path: FILES_OK, or FILES_ERROR (also when something of that name is there already). */
int files_make_dir(const char *path);

/* The whole file into buf: FILES_MISSING if it does not open, FILES_ERROR if it is larger than cap or a read
 * fails. */
int files_read(const char *path, uint8_t *buf, size_t cap, size_t *size);

/* Writes the file anew and checks that every byte went out (fflush and fclose included). */
int files_write(const char *path, const uint8_t *buf, size_t size);

#endif
