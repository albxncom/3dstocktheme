// Host build of the application's delta check, for the Mac-side test (test_delta.py). Not part of the 3DS build.
//
// check DELTA_DIR INSTALLED_TMD [NOT_INSTALLED_INDEX ...]
//   The installed state is the TMD file (what AM would list), every content installed and owned except the indices
//   given after it; INSTALLED_TMD "-" means the title is not installed. Prints what the application prints and the
//   plan, exits 0 (import may go ahead), 1 (nothing to do) or 2 (refused).
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "delta.h"

static void say(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    putchar('\n');
}

static uint8_t raw[DELTA_TMD_MAX], installed_raw[DELTA_TMD_MAX];
static Tmd delta, installed_tmd;
static bool present[DELTA_MAX_CONTENTS];
static Installed inst;
static Plan plan;

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: check DELTA_DIR INSTALLED_TMD|- [NOT_INSTALLED_INDEX ...]\n");
        return 2;
    }
    if (strcmp(argv[2], "-")) {
        FILE *f = fopen(argv[2], "rb");
        if (!f) {
            perror(argv[2]);
            return 2;
        }
        size_t n = fread(installed_raw, 1, sizeof installed_raw, f);
        fclose(f);
        if (tmd_parse(installed_raw, n, &installed_tmd, say)) return 2;
        inst.exists = true;
        inst.version = installed_tmd.version;
        inst.title_type = installed_tmd.title_type;
        inst.count = installed_tmd.count;
        for (unsigned i = 0; i < inst.count; i++) {
            const TmdContent *t = &installed_tmd.contents[i];
            InstalledContent *c = &inst.contents[i];
            c->index = t->index;
            c->type = t->type;
            c->id = t->id;
            c->size = t->size;
            c->installed = c->owned = true;
            for (int a = 3; a < argc; a++)
                if (atoi(argv[a]) == t->index) c->installed = false;
        }
    }
    size_t len;
    if (delta_load(argv[1], raw, &len, &delta, present, say)) return 2;
    int r = delta_plan(&delta, present, &inst, &plan, say);
    if (r < 0) return 2;
    printf("plan: phase1 %s, phase2", plan.phase1 ? "yes" : "no");
    for (unsigned i = 0; i < plan.count; i++) printf(" %u", plan.indices[i]);
    printf("\n");
    return r;
}
