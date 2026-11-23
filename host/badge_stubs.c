/* The console's badge boundary on the host (app/source/badge_console.h): HOME's two badge files are the folder
 * "badge-home" in the working folder (a test makes it when it wants a collection there); the system's requests are
 * a model of libctru 2.7.0's: a POWER press always asks for HOME (aptMainLoop then goes there, whatever
 * aptSetHomeAllowed said), and a test can make that press, or a failed write, happen at a given call to the store,
 * and have the opens answer with a code (HOME holding its files). */
#include "badge_console.h"
#include "badge_dir_store.h"

int badge_jump;                 /* the system asks for HOME: a POWER press, or a HOME press while HOME was allowed */
int badge_close;                /* the system orders the application to close */
int badge_home_jumps;           /* the main loop went to HOME */
int badge_jump_at_read;         /* > 0: badge_jump is set at that read of HOME's files (counted in badge_reads) */
int badge_jump_at_write;        /* > 0: ... at that write (counted in badge_writes), before it is made */
int badge_fail_write;           /* > 0: that write fails, once */
int badge_fail_open;            /* HOME's files cannot be opened: -1, a code of no known kind (R11's slow look) */
int badge_reads, badge_writes;
int badge_open_code;            /* != 0: every open returns it */
int badge_open_write_code;      /* != 0: every open to read and write returns it (one to read only goes on) */
int badge_opens_read, badge_opens_write;
static badge_dir home;
static badge_store inner;

static int open_home(void *ctx, int mode)
{
    (void)ctx;
    ++*(mode == BADGE_OPEN_WRITE ? &badge_opens_write : &badge_opens_read);
    if (badge_fail_open)
        return -1;
    if (badge_open_code)
        return badge_open_code;
    if (badge_open_write_code && mode == BADGE_OPEN_WRITE)
        return badge_open_write_code;
    return inner.open(inner.ctx, mode);
}

static int size_home(void *ctx, int file, uint64_t *size)
{
    (void)ctx;
    return inner.size(inner.ctx, file, size);
}

static int read_home(void *ctx, int file, uint32_t offset, uint8_t *buf, uint32_t size)
{
    (void)ctx;
    if (++badge_reads == badge_jump_at_read)
        badge_jump = 1;
    return inner.read(inner.ctx, file, offset, buf, size);
}

static int write_home(void *ctx, int file, uint32_t offset, const uint8_t *buf, uint32_t size)
{
    (void)ctx;
    if (++badge_writes == badge_jump_at_write)
        badge_jump = 1;
    if (badge_writes == badge_fail_write)
        return -1;
    return inner.write(inner.ctx, file, offset, buf, size);
}

static int close_home(void *ctx)
{
    (void)ctx;
    return inner.close(inner.ctx);
}

void console_badge_store(badge_store *s)
{
    badge_dir_store(&inner, &home, "badge-home", 0);
    *s = (badge_store){NULL, open_home, size_home, read_home, write_home, close_home};
}

int console_main_loop(void)
{
    if (badge_jump) {           /* aptHandleJumpToHome; the application is back when HOME lets it go on */
        badge_home_jumps++;
        badge_jump = 0;
    }
    return !badge_close;
}
