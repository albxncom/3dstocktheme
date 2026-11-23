/* The badge core on the host: its unit tests, and a command line that does what tools/badges/badge_sets.py does with
 * the same output, so that host/test_badges.py can compare the two on the same files.
 *
 * Run: test_badge unit
 *      test_badge list        --in TREE [--manifest FILE]
 *      test_badge sets        --in TREE [--manifest FILE] [--icons DIR]     (the listing a screen gets)
 *      test_badge append      --in TREE --out TREE [--manifest FILE] --manifest-out FILE --undo-out FILE
 *                             [--name NAME] SET                             (SET: a folder, a .zip or a .png)
 *      test_badge remove-last --in TREE --out TREE [--manifest FILE] [--undo FILE]... [--allow-home-changes]
 *      test_badge check-slots --in TREE --slots TREE
 *      test_badge png FILE OUT        (OUT: "width height\n" and the RGBA bytes)
 *      test_badge files SET           (the entries a folder, zip or PNG gives, and the badges in their order)
 *      test_badge order NAME...       (the names in the core's order)
 *      test_badge points              (the code points the core takes in names, their casefold, the whitespace)
 * A TREE is a save3ds extract (user/BadgeData.dat, user/BadgeMngFile.dat; user/MngFile.dat for --slots). The
 * manifest is a line file the comparison script converts from and to manifest.json: "next_set_id N",
 * "next_badge_id N", "set ID<TAB>FOLDER<TAB>NAME", then that set's "badge TILE<TAB>ID<TAB>FILE" lines. Errors go
 * to stderr as "error: BADGE_E_...: text", exit 2. */
#define _POSIX_C_SOURCE 200809L
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <zlib.h>

#include "badge_internal.h"

static int failures, checks;

#define CHECK(cond) do { checks++; if (!(cond)) { failures++; fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_EQ(a, b) do { long long a_ = (long long)(a), b_ = (long long)(b); checks++; if (a_ != b_) { failures++; fprintf(stderr, "%s:%d: %s = %lld, want %lld\n", __FILE__, __LINE__, #a, a_, b_); } } while (0)

/* ---------------------------------------------------------------- files */

static int read_file(const char *path, uint8_t **buf, size_t *size)
{
    FILE *f = fopen(path, "rb");
    *buf = NULL;
    *size = 0;
    if (!f)
        return -1;
    size_t cap = 1 << 16, n = 0;
    uint8_t *b = malloc(cap);
    for (size_t got; b && (got = fread(b + n, 1, cap - n, f)) > 0;) {
        n += got;
        if (n == cap) {
            uint8_t *bigger = realloc(b, cap *= 2);
            if (!bigger) {
                free(b);
                b = NULL;
            }
            b = bigger;
        }
    }
    int bad = ferror(f) || !b;
    fclose(f);
    if (bad) {
        free(b);
        return -1;
    }
    *buf = b;
    *size = n;
    return 0;
}

static int write_file(const char *path, const void *buf, size_t size)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;
    int bad = fwrite(buf, 1, size, f) != size;
    bad |= fclose(f) != 0;
    return bad ? -1 : 0;
}

static char *join(const char *a, const char *b)
{
    size_t n = strlen(a) + strlen(b) + 2;
    char *p = malloc(n);
    if (p)
        snprintf(p, n, "%s/%s", a, b);
    return p;
}

static int fail(int r)
{
    fprintf(stderr, "error: %s: %s\n", badge_result_name(r), badge_result_text(r));
    return 2;
}

static int fail_text(const char *what, const char *path)
{
    fprintf(stderr, "error: %s %s\n", path, what);
    return 2;
}

typedef struct tree {
    uint8_t *data, *mng;
    size_t data_size, mng_size;
} tree;

static int read_tree(const char *dir, tree *t)
{
    char *d = join(dir, "user/BadgeData.dat"), *m = join(dir, "user/BadgeMngFile.dat");
    int bad = !d || !m || read_file(d, &t->data, &t->data_size) || read_file(m, &t->mng, &t->mng_size);
    free(d);
    free(m);
    return bad ? -1 : 0;
}

static int write_tree(const char *dir, const tree *t)
{
    struct stat st;
    if (stat(dir, &st) == 0)
        return -1;      /* never overwrites */
    char *user = join(dir, "user"), *d = join(dir, "user/BadgeData.dat"), *m = join(dir, "user/BadgeMngFile.dat");
    int bad = !user || !d || !m || mkdir(dir, 0755) || mkdir(user, 0755) ||
              write_file(d, t->data, t->data_size) || write_file(m, t->mng, t->mng_size);
    free(user);
    free(d);
    free(m);
    return bad ? -1 : 0;
}

/* ---------------------------------------------------------------- the manifest as a line file */

typedef struct manifest {
    badge_manifest m;
    badge_manifest_set *sets;
    badge_known **badges;       /* per set */
    size_t cap;
} manifest;

static char *dup_of(const char *s, size_t n)
{
    char *p = malloc(n + 1);
    memcpy(p, s, n);
    p[n] = 0;
    return p;
}

static void manifest_add_set(manifest *mf, uint32_t set_id, const char *folder, const char *name)
{
    if (mf->m.count == mf->cap) {
        mf->cap = mf->cap ? mf->cap * 2 : 8;
        mf->sets = realloc(mf->sets, mf->cap * sizeof *mf->sets);
        mf->badges = realloc(mf->badges, mf->cap * sizeof *mf->badges);
    }
    mf->sets[mf->m.count] = (badge_manifest_set){strdup(folder), strdup(name), set_id, NULL, 0};
    mf->badges[mf->m.count] = NULL;
    mf->m.count++;
    mf->m.sets = mf->sets;
}

static void manifest_add_badge(manifest *mf, size_t set, const char *file, uint32_t tile, uint32_t id)
{
    badge_manifest_set *s = &mf->sets[set];
    mf->badges[set] = realloc(mf->badges[set], (s->count + 1) * sizeof **mf->badges);
    mf->badges[set][s->count++] = (badge_known){strdup(file), tile, id};
    s->badges = mf->badges[set];
}

static int manifest_read(const char *path, manifest *mf)
{
    memset(mf, 0, sizeof *mf);
    mf->m.next_set_id = BADGE_SET_ID_FIRST;
    mf->m.next_badge_id = BADGE_ID_FIRST;
    if (!path)
        return 0;
    uint8_t *buf;
    size_t size;
    if (read_file(path, &buf, &size))
        return -1;
    char *text = dup_of((const char *)buf, size);
    free(buf);
    for (char *line = text, *next; line && *line; line = next) {
        next = strchr(line, '\n');
        if (next)
            *next++ = 0;
        char *f1 = strchr(line, ' ');
        if (!f1)
            continue;
        *f1++ = 0;
        if (!strcmp(line, "next_set_id")) {
            mf->m.next_set_id = (uint32_t)strtoul(f1, NULL, 0);
        } else if (!strcmp(line, "next_badge_id")) {
            mf->m.next_badge_id = (uint32_t)strtoul(f1, NULL, 0);
        } else {
            char *f2 = strchr(f1, '\t'), *f3 = f2 ? strchr(f2 + 1, '\t') : NULL;
            if (!f3) {
                free(text);
                return -1;
            }
            *f2++ = 0;
            *f3++ = 0;
            if (!strcmp(line, "set"))
                manifest_add_set(mf, (uint32_t)strtoul(f1, NULL, 0), f2, f3);
            else if (!strcmp(line, "badge") && mf->m.count)
                manifest_add_badge(mf, mf->m.count - 1, f3, (uint32_t)strtoul(f1, NULL, 0),
                                   (uint32_t)strtoul(f2, NULL, 0));
        }
    }
    free(text);
    return 0;
}

static int manifest_write(const char *path, const manifest *mf)
{
    FILE *f = fopen(path, "w");
    if (!f)
        return -1;
    fprintf(f, "next_set_id %u\nnext_badge_id %u\n", mf->m.next_set_id, mf->m.next_badge_id);
    for (size_t k = 0; k < mf->m.count; k++) {
        const badge_manifest_set *s = &mf->sets[k];
        fprintf(f, "set %u\t%s\t%s\n", s->set_id, s->folder, s->name);
        for (size_t i = 0; i < s->count; i++)
            fprintf(f, "badge %u\t%u\t%s\n", s->badges[i].tile, s->badges[i].badge_id, s->badges[i].file);
    }
    return fclose(f) ? -1 : 0;
}

static void manifest_free(manifest *mf)
{
    for (size_t k = 0; k < mf->m.count; k++) {
        free((void *)(uintptr_t)mf->sets[k].folder);
        free((void *)(uintptr_t)mf->sets[k].name);
        for (size_t i = 0; i < mf->sets[k].count; i++)
            free((void *)(uintptr_t)mf->badges[k][i].file);
        free(mf->badges[k]);
    }
    free(mf->sets);
    free(mf->badges);
    memset(mf, 0, sizeof *mf);
}

/* ---------------------------------------------------------------- a set source: folder, zip or PNG */

typedef struct source {
    badge_file *files;
    size_t count;
    int from_zip;
    char key[512];              /* the manifest key: the folder's name, the zip's or PNG's without its extension */
} source;

static int ends_with(const char *s, const char *tail)
{
    size_t a = strlen(s), b = strlen(tail);
    if (a < b)
        return 0;
    for (size_t i = 0; i < b; i++)
        if ((s[a - b + i] | 0x20) != tail[i])
            return 0;
    return 1;
}

static int mem_read(void *ctx, uint64_t offset, void *buf, size_t len)
{
    memcpy(buf, (const uint8_t *)ctx + offset, len);
    return 0;
}

static void source_free(source *s)
{
    if (s->from_zip) {
        badge_files_free(s->files, s->count);
    } else {
        for (size_t i = 0; i < s->count; i++) {
            free((void *)(uintptr_t)s->files[i].name);
            free((void *)(uintptr_t)s->files[i].data);
        }
        free(s->files);
    }
    memset(s, 0, sizeof *s);
}

static int source_read(const char *path, source *s)
{
    memset(s, 0, sizeof *s);
    char copy[512];
    snprintf(copy, sizeof copy, "%s", path);
    size_t len = strlen(copy);
    while (len > 1 && copy[len - 1] == '/')
        copy[--len] = 0;
    const char *base = strrchr(copy, '/') ? strrchr(copy, '/') + 1 : copy;
    snprintf(s->key, sizeof s->key, "%s", base);
    struct stat st;
    if (stat(copy, &st))
        return -1;
    if (S_ISDIR(st.st_mode)) {
        DIR *d = opendir(copy);
        if (!d)
            return -1;
        size_t cap = 0;
        for (struct dirent *e; (e = readdir(d)) != NULL;) {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
                continue;
            if (s->count == cap) {
                cap = cap ? 2 * cap : 16;
                s->files = realloc(s->files, cap * sizeof *s->files);
            }
            badge_file *f = &s->files[s->count++];
            memset(f, 0, sizeof *f);
            f->name = strdup(e->d_name);
            char *full = join(copy, e->d_name);
            struct stat fs;
            f->is_dir = stat(full, &fs) == 0 && S_ISDIR(fs.st_mode);
            if (!f->is_dir) {
                uint8_t *buf;
                size_t size;
                if (read_file(full, &buf, &size) == 0) {
                    f->data = buf;
                    f->size = size;
                }
            }
            free(full);
        }
        closedir(d);
        return 0;
    }
    uint8_t *buf;
    size_t size;
    if (read_file(copy, &buf, &size))
        return -1;
    if (ends_with(copy, ".zip")) {
        s->key[strlen(s->key) - 4] = 0;
        tp_source zip = {buf, size, mem_read};
        s->from_zip = 1;
        int r = badge_zip_files(&zip, &s->files, &s->count);
        free(buf);
        return r ? r : 0;
    }
    if (ends_with(copy, ".png"))
        s->key[strlen(s->key) - 4] = 0;
    s->files = malloc(sizeof *s->files);
    s->files[0] = (badge_file){strdup(base), 0, buf, size};
    s->count = 1;
    return 0;
}

/* ---------------------------------------------------------------- output as badge_sets.py prints it */

/* repr() of a str, for the names: exact for the code points the core takes (Unicode 16, as Python 3.14 has it:
 * not printable there are the controls, U+00A0, U+00AD, U+3000 and three unassigned runs among the emoji) */
static int printable(uint32_t c)
{
    return !(c < 0x20 || (c >= 0x7F && c <= 0xA0) || c == 0xAD || c == 0x3000 || (c >= 0x1F6D8 && c <= 0x1F6DB) ||
             (c >= 0x1F6ED && c <= 0x1F6EF) || (c >= 0x1F6FD && c <= 0x1F6FF));
}

static void print_repr(const char *s)
{
    const char *quote = strchr(s, '\'') && !strchr(s, '"') ? "\"" : "'";
    putchar(*quote);
    for (const uint8_t *p = (const uint8_t *)s; *p;) {
        uint32_t c = *p;
        int n = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
        if (n > 1) {
            c &= 0x3F >> (n - 1);
            for (int k = 1; k < n; k++)
                c = c << 6 | (p[k] & 0x3F);
        }
        if (c == '\\' || c == (uint32_t)*quote)
            printf("\\%c", (char)c);
        else if (c == '\n')
            printf("\\n");
        else if (c == '\r')
            printf("\\r");
        else if (c == '\t')
            printf("\\t");
        else if (printable(c))
            fwrite(p, 1, (size_t)n, stdout);
        else if (c <= 0xFF)
            printf("\\x%02x", c);
        else if (c <= 0xFFFF)
            printf("\\u%04x", c);
        else
            printf("\\U%08x", c);
        p += n;
    }
    putchar(*quote);
}

static void print_warnings(unsigned warnings, uint32_t unknown, uint32_t placed, uint32_t layout, const char *prefix)
{
    if (warnings & BADGE_WARN_UNKNOWN)
        printf("%s%u placements name no badge of the collection; HOME empties them\n", prefix, unknown);
    if (warnings & BADGE_WARN_PLACED)
        printf("%sthe header counts %u placed badges, the layout holds %u; HOME recounts\n", prefix, placed, layout);
    if (warnings & BADGE_WARN_COUNTS)
        printf("%sper-badge placed counts disagree with the layout; HOME recounts\n", prefix);
}

static const char *option(int argc, char **argv, const char *name)
{
    for (int i = 2; i + 1 < argc; i++)
        if (!strcmp(argv[i], name))
            return argv[i + 1];
    return NULL;
}

static int flag(int argc, char **argv, const char *name)
{
    for (int i = 2; i < argc; i++)
        if (!strcmp(argv[i], name))
            return 1;
    return 0;
}

static const char *positional(int argc, char **argv)
{
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--allow-home-changes"))
            continue;
        if (!strncmp(argv[i], "--", 2)) {
            i++;
            continue;
        }
        return argv[i];
    }
    return NULL;
}

static int cmd_list(int argc, char **argv)
{
    const char *in = option(argc, argv, "--in");
    tree t = {0};
    manifest mf;
    if (!in || read_tree(in, &t))
        return fail_text("not found (BadgeData.dat, BadgeMngFile.dat)", in ? in : "--in");
    if (manifest_read(option(argc, argv, "--manifest"), &mf))
        return fail_text("cannot be read", "manifest");
    badge_collection *col = malloc(sizeof *col);
    int r = badge_read(t.data, t.data_size, t.mng, t.mng_size, &mf.m, col);
    if (r)
        return fail(r);
    if (!strcmp(argv[1], "sets")) {
        const char *icons = option(argc, argv, "--icons");
        printf("remove %s\n", badge_result_name(col->remove));
        for (uint32_t k = 0; k < col->sets; k++) {
            const badge_set_info *s = &col->set[k];
            uint8_t icon[BADGE_ICON_BYTES], digest[32];
            CHECK_EQ(badge_set_icon(t.data, t.data_size, s, icon), BADGE_OK);
            sha256(icon, sizeof icon, digest);
            printf("%u\t%u\t%u\t%d\t%d\t", s->slot, s->index, s->count, s->mine, s->removable);
            for (int i = 0; i < 32; i++)
                printf("%02x", digest[i]);
            printf("\t%s\n", s->name);
            if (icons) {
                char name[32];
                snprintf(name, sizeof name, "%u.bin", s->slot);
                char *path = join(icons, name);
                write_file(path, icon, sizeof icon);
                free(path);
            }
        }
        return failures ? 1 : 0;
    }
    printf("%s\n", in);
    printf("sets %u/%d, badges %u/%d, placed %u/%d, NNID field: %s\n", col->sets, BADGE_SETS_MAX, col->badges,
           BADGE_BADGES_MAX, col->placed, BADGE_PLACED_MAX, col->nnid_set ? "set" : "not set (0)");
    printf("%4s  %10s  %6s  %5s  %6s  name\n", "slot", "set id", "badges", "from", "placed");
    for (uint32_t k = 0; k < col->sets; k++) {
        const badge_set_info *s = &col->set[k];
        char id[16];
        snprintf(id, sizeof id, "0x%x", s->set_id);
        printf("%4u  %10s  %6u  %5u  %6u  %s%s\n", s->slot, id, s->count, s->start, s->placed, s->name,
               s->mine ? "  (appended by this tool)" : "");
    }
    printf("free: %u sets, %u badges\n", BADGE_SETS_MAX - col->sets, BADGE_BADGES_MAX - col->badges);
    print_warnings(col->warnings, col->unknown, col->placed, col->layout, "warning: ");
    return 0;
}

static int cmd_append(int argc, char **argv)
{
    const char *in = option(argc, argv, "--in"), *out = option(argc, argv, "--out");
    const char *mout = option(argc, argv, "--manifest-out"), *uout = option(argc, argv, "--undo-out");
    const char *set = positional(argc, argv);
    tree t = {0};
    manifest mf;
    source src;
    if (!in || !out || !mout || !uout || !set)
        return fail(BADGE_E_ARG);
    if (read_tree(in, &t))
        return fail_text("not found (BadgeData.dat, BadgeMngFile.dat)", in);
    if (manifest_read(option(argc, argv, "--manifest"), &mf))
        return fail_text("cannot be read", "manifest");
    int sr = source_read(set, &src);
    if (sr < 0)
        return fail_text("is not a folder, zip or PNG", set);
    if (sr) {
        /* badge_sets.py checks the collection first; a zip that does not read is a refusal of the set */
        badge_collection *col = malloc(sizeof *col);
        int c = badge_read(t.data, t.data_size, t.mng, t.mng_size, NULL, col);
        free(col);
        return fail(c ? c : sr);
    }
    badge_append_result res;
    int r = badge_append(t.data, t.data_size, t.mng, t.mng_size, src.files, src.count, &mf.m, src.key,
                         option(argc, argv, "--name"), &res);
    if (r)
        return fail(r);
    if (write_tree(out, &t))
        return fail_text("exists or cannot be written; this tool never overwrites", out);
    if (write_file(uout, res.undo, res.undo_size))
        return fail_text("cannot be written", uout);
    /* the manifest after the append */
    size_t k = 0;
    while (k < mf.m.count && strcmp(mf.sets[k].folder, src.key))
        k++;
    if (res.new_set)
        manifest_add_set(&mf, res.set_id, src.key, res.name);
    else {
        free((void *)(uintptr_t)mf.sets[k].name);
        mf.sets[k].name = strdup(res.name);
    }
    for (uint32_t i = 0; i < res.count; i++)
        if (res.badges[i].is_new)
            manifest_add_badge(&mf, k, src.files[res.badges[i].file].name, res.badges[i].tile,
                               res.badges[i].badge_id);
    mf.m.next_set_id = res.next_set_id;
    mf.m.next_badge_id = res.next_badge_id;
    if (manifest_write(mout, &mf))
        return fail_text("cannot be written", mout);

    printf("appended set ");
    print_repr(res.name);
    printf(" (ID 0x%x, set slot %u) with %u badges at indices %u-%u:\n", res.set_id, res.slot, res.count, res.start,
           res.start + res.count - 1);
    for (uint32_t i = 0; i < res.count; i++) {
        const badge_written *w = &res.badges[i];
        const char *file = src.files[w->file].name, *dot = strchr(file, '.');
        printf("  %4u  0x%04x  %.*s  (%s tile %u)\n", w->index, w->badge_id, (int)(dot - file), file, file, w->tile);
    }
    printf("set icon: %s; used-set bit %s\n", res.icon_from_file ? "_seticon.png" : "first badge, scaled to 48x48",
           res.set_bit ? "set" : "clear, like the sets before it");
    printf("checked: every byte outside the new set's records, names and images is unchanged; sets %u -> %u, "
           "badges %u -> %u\n", res.slot, res.slot + 1, res.start, res.start + res.count);
    badge_collection *col = malloc(sizeof *col);
    badge_read(t.data, t.data_size, t.mng, t.mng_size, NULL, col);
    print_warnings(res.warnings, res.unknown, col->placed, col->layout, "warning (unchanged from the input): ");
    printf("wrote %s\n", out);
    printf("undo record %s (new)\n", uout);
    free(col);
    badge_append_free(&res);
    source_free(&src);
    manifest_free(&mf);
    return 0;
}

static int cmd_remove(int argc, char **argv)
{
    const char *in = option(argc, argv, "--in"), *out = option(argc, argv, "--out");
    tree t = {0};
    manifest mf;
    if (!in || !out)
        return fail(BADGE_E_ARG);
    if (read_tree(in, &t))
        return fail_text("not found (BadgeData.dat, BadgeMngFile.dat)", in);
    if (manifest_read(option(argc, argv, "--manifest"), &mf))
        return fail_text("cannot be read", "manifest");
    badge_undo records[64];
    const char *names[64];
    size_t count = 0;
    for (int i = 2; i + 1 < argc && count < 64; i++)
        if (!strcmp(argv[i], "--undo")) {
            uint8_t *buf;
            size_t size;
            if (read_file(argv[i + 1], &buf, &size))
                return fail_text("cannot be read", argv[i + 1]);
            names[count] = argv[i + 1];
            records[count++] = (badge_undo){buf, size};
        }
    badge_remove_result res;
    uint8_t mng_before[BADGE_MNG_SIZE];
    if (t.mng_size == BADGE_MNG_SIZE)
        memcpy(mng_before, t.mng, BADGE_MNG_SIZE);
    int r = badge_remove_last(t.data, t.data_size, t.mng, t.mng_size, &mf.m, records, count,
                              flag(argc, argv, "--allow-home-changes"), &res);
    if (r)
        return fail(r);
    if (write_tree(out, &t))
        return fail_text("exists or cannot be written; this tool never overwrites", out);
    const char *record = strrchr(names[res.record], '/') ? strrchr(names[res.record], '/') + 1 : names[res.record];
    printf("removed set ");
    print_repr(res.name);
    printf(" (ID 0x%x, %u badges at indices %u-%u); its regions hold their bytes from before the append again "
           "(%s)\n", res.set_id, res.count, res.start, res.start + res.count - 1, record);
    if (res.view_reset)
        printf("the badge case's view (selected set and column, header 0x10 and 0x14) was changed since the append; "
               "it is back to its state before the append\n");
    if (res.identical)
        printf("both files are byte-identical to the files before the append (sha256 compared with the undo "
               "record)\n");
    else
        printf("not byte-identical to the files before the append: the input had changed since the append "
               "(--allow-home-changes); the set's regions and the case's view hold their bytes from before the "
               "append, everything else is kept as it is in the input\n");
    printf("wrote %s\n", out);
    return 0;
}

static void print_layout(const char *label, const char *saved, const badge_layout_check *c)
{
    if (c->damaged) {
        printf("%-8s%-7sdamaged copy (header word 0x0 not 0)\n", label, saved);
        return;
    }
    printf("%-8s%-7s%11u%14u%9u%15u", label, saved, c->placements, c->folder_icons, c->missing, c->differs);
    if (c->header_placed != c->placements)
        printf(" (header says %u)", c->header_placed);
    printf("\n");
}

static int cmd_check_slots(int argc, char **argv)
{
    const char *in = option(argc, argv, "--in"), *slots_dir = option(argc, argv, "--slots");
    tree t = {0};
    if (!in || !slots_dir)
        return fail(BADGE_E_ARG);
    if (read_tree(in, &t))
        return fail_text("not found (BadgeData.dat, BadgeMngFile.dat)", in);
    if (t.data_size != BADGE_DATA_SIZE || t.mng_size != BADGE_MNG_SIZE)
        return fail(BADGE_E_SIZE);  /* read_tree checks both sizes */
    if (get_le32(t.mng) || get_le32(t.mng + BM_UNIQUE) > BADGE_BADGES_MAX)
        return fail(BADGE_E_SLOTS);
    uint8_t *slots;
    size_t size;
    char *path = join(slots_dir, "user/MngFile.dat");
    int missing = read_file(path, &slots, &size);
    free(path);
    if (missing)
        return fail_text("not found", "MngFile.dat");
    badge_slots_check c;
    int r = badge_check_slots(t.mng, t.mng_size, slots, size, &c);
    if (r)
        return fail(r);
    printf("live collection: %u badges (%s)\n", c.badges, in);
    printf("%-8s%-7s%11s%14s%9s%15s\n", "layout", "saved", "placements", "folder icons", "missing", "index differs");
    print_layout("live", "-", &c.live);
    for (int n = 0; n < BADGE_LAYOUT_SLOTS; n++) {
        char label[16];
        snprintf(label, sizeof label, "slot %d", n + 1);
        print_layout(label, c.slot[n].saved ? "yes" : "no", &c.slot[n]);
    }
    printf("missing: no badge with that badge ID, set ID and sub ID (HOME empties the placement when it loads the "
           "slot); index differs: the badge exists, other identity bytes differ\n");
    printf("%s\n", c.ok ? "OK: every placement in the live layout and the saved slots points at a badge of the live "
                          "collection" : "FOUND: placements that point at no badge of the live collection (see the "
                          "table)");
    free(slots);
    return c.ok ? 0 : 1;
}

static int cmd_png(int argc, char **argv)
{
    uint8_t *buf, *rgba;
    size_t size;
    uint32_t w, h;
    if (argc < 4 || read_file(argv[2], &buf, &size))
        return fail(BADGE_E_ARG);
    int r = badge_png_decode(buf, size, &w, &h, &rgba);
    free(buf);
    if (r)
        return fail(r);
    FILE *f = fopen(argv[3], "wb");
    if (!f)
        return fail(BADGE_E_ARG);
    fprintf(f, "%u %u\n", w, h);
    fwrite(rgba, 4, (size_t)w * h, f);
    fclose(f);
    tp_free(rgba);
    return 0;
}

static int cmd_files(int argc, char **argv)
{
    source src;
    if (argc < 3)
        return fail(BADGE_E_ARG);
    int r = source_read(argv[2], &src);
    if (r < 0)
        return fail_text("is not a folder, zip or PNG", argv[2]);
    if (r)
        return fail(r);
    printf("key %s\n", src.key);
    for (size_t i = 0; i < src.count; i++)
        printf("entry %s\t%d\t%zu\n", src.files[i].name, src.files[i].is_dir, src.files[i].size);
    badge_folder folder;
    r = badge_folder_read(src.files, src.count, &folder);
    if (r)
        return fail(r);
    for (size_t i = 0; i < folder.count; i++) {
        const badge_tile *t = &folder.tiles[i];
        printf("tile %s\t%u\t%u\t%u\t%.*s\n", src.files[t->file].name, t->tile, t->x0, t->y0, (int)t->name_len,
               src.files[t->file].name);
    }
    printf("icon %s\n", folder.icon >= 0 ? src.files[folder.icon].name : "-");
    badge_folder_free(&folder);
    source_free(&src);
    return 0;
}

/* The code points the core takes in names, with their casefold where it is not themselves and the ones that are
 * whitespace, for host/test_badges.py to check against Python's unicodedata */
static int cmd_points(void)
{
    uint32_t start = 0;
    for (uint32_t c = 1; c <= 0x110000; c++) {
        int in = c <= 0x10FFFF && name_accepted(c);
        if (in && !start)
            start = c;
        if (!in && start) {
            printf("range %x %x\n", start, c - 1);
            start = 0;
        }
        if (!in)
            continue;
        uint32_t f[2];
        int k = name_fold(c, f);
        if (k == 2)
            printf("fold %x %x %x\n", c, f[0], f[1]);
        else if (f[0] != c)
            printf("fold %x %x\n", c, f[0]);
        if (name_space(c))
            printf("space %x\n", c);
    }
    return 0;
}

static int cmd_order(int argc, char **argv)
{
    int n = argc - 2;
    char **names = argv + 2;
    for (int i = 0; i < n; i++) {
        size_t points;
        if (name_points(names[i], strlen(names[i]), NULL, 0, &points, NULL))
            return fail(BADGE_E_NAME);
    }
    for (int i = 1; i < n; i++)
        for (int j = i; j > 0 && name_order(names[j - 1], names[j]) > 0; j--) {
            char *t = names[j];
            names[j] = names[j - 1];
            names[j - 1] = t;
        }
    for (int i = 0; i < n; i++)
        printf("%s\n", names[i]);
    return 0;
}

/* ---------------------------------------------------------------- unit tests: generated data */

static uint32_t rng = 1;
static uint32_t next_random(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

/* A PNG: RGBA rows cycling through the five filters, as fixtures.png writes them */
static uint8_t *make_png(uint32_t w, uint32_t h, const uint8_t *rgba, size_t *size)
{
    size_t stride = (size_t)w * 4, raw_size = h * (stride + 1);
    uint8_t *raw = malloc(raw_size), *prev = calloc(stride, 1);
    for (uint32_t y = 0; y < h; y++) {
        const uint8_t *row = rgba + y * stride;
        uint8_t kind = (uint8_t)(y % 5), *o = raw + y * (stride + 1);
        o[0] = kind;
        for (size_t i = 0; i < stride; i++) {
            int a = i >= 4 ? row[i - 4] : 0, b = prev[i], c = i >= 4 ? prev[i - 4] : 0, pred = 0;
            if (kind == 1)
                pred = a;
            else if (kind == 2)
                pred = b;
            else if (kind == 3)
                pred = (a + b) >> 1;
            else if (kind == 4) {
                int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
                pred = pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
            }
            o[1 + i] = (uint8_t)(row[i] - pred);
        }
        memcpy(prev, row, stride);
    }
    uLongf zsize = compressBound(raw_size);
    uint8_t *z = malloc(zsize);
    compress2(z, &zsize, raw, raw_size, 6);
    size_t total = 8 + 25 + 12 + zsize + 12;
    uint8_t *png = malloc(total), *p = png;
    memcpy(p, "\x89PNG\r\n\x1a\n", 8);
    p += 8;
    uint8_t ihdr[13];
    put_be32(ihdr, w);
    put_be32(ihdr + 4, h);
    memcpy(ihdr + 8, "\x08\x06\x00\x00\x00", 5);
    const struct { const char *kind; const uint8_t *body; size_t len; } chunks[3] = {
        {"IHDR", ihdr, 13}, {"IDAT", z, zsize}, {"IEND", NULL, 0}};
    for (int i = 0; i < 3; i++) {
        put_be32(p, (uint32_t)chunks[i].len);
        memcpy(p + 4, chunks[i].kind, 4);
        if (chunks[i].len)
            memcpy(p + 8, chunks[i].body, chunks[i].len);
        put_be32(p + 8 + chunks[i].len, (uint32_t)crc32(0, p + 4, (uInt)(4 + chunks[i].len)));
        p += 12 + chunks[i].len;
    }
    free(raw);
    free(prev);
    free(z);
    *size = (size_t)(p - png);
    return png;
}

static uint8_t *pattern(uint32_t w, uint32_t h, uint32_t seed)
{
    uint8_t *out = malloc((size_t)w * h * 4);
    for (uint32_t y = 0; y < h; y++)
        for (uint32_t x = 0; x < w; x++) {
            uint8_t *p = out + ((size_t)y * w + x) * 4;
            p[0] = (uint8_t)(x * 4 + seed * 37);
            p[1] = (uint8_t)(y * 4 + seed * 91);
            p[2] = (uint8_t)((x ^ y) * 8 + seed * 13);
            p[3] = x < 6 && y < 6 ? 0 : (x + y + seed) % 11 == 0 ? 0x80 : 0xFF;
        }
    return out;
}

/* A collection laid out as Anemone3DS writes one (fixtures.collection): sets of the given sizes, set IDs 1.., badge
 * ID = index + 1, two placed folder icons, random bytes in the unused areas */
static void make_collection(tree *t, const uint32_t *sizes, uint32_t n_sets, int placed)
{
    t->data_size = BADGE_DATA_SIZE;
    t->mng_size = BADGE_MNG_SIZE;
    t->data = malloc(BADGE_DATA_SIZE);
    t->mng = calloc(BADGE_MNG_SIZE, 1);
    rng = 12345;
    for (size_t i = 0; i < BADGE_DATA_SIZE; i += 4)
        put_le32(t->data + i, next_random());
    uint32_t index = 0;
    for (uint32_t s = 0; s < BADGE_SETS_MAX; s++) {
        uint8_t *si = t->mng + BM_SET_INFO + s * BM_SET_INFO_SIZE;
        memset(si, 0xFF, 8);
        put_le32(si + 0xC, B_SET_UNKNOWN_0C);
        if (s >= n_sets) {
            memset(si + 0x10, 0xFF, 12);
            memset(si + 0x24, 0xFF, 4);
            continue;
        }
        char name[16];
        snprintf(name, sizeof name, "Set %u", s);
        name_block(name, strlen(name), t->data + BD_SET_TITLES + s * BD_TITLE_BLOCK);
        put_le32(si + 0x10, s + 1);
        put_le32(si + 0x14, s);
        put_le32(si + 0x18, B_NONE);
        put_le32(si + 0x1C, sizes[s]);
        put_le32(si + 0x20, sizes[s] * B_QUANTITY);
        put_le32(si + 0x24, index);
        for (uint32_t k = 0; k < sizes[s]; k++, index++) {
            uint8_t *b = t->mng + BM_BADGE_INFO + index * BM_BADGE_INFO_SIZE;
            put_le32(b + 4, index + 1);
            put_le32(b + 8, s + 1);
            put_le16(b + 12, (uint16_t)index);
            put_le16(b + 18, B_QUANTITY);
            memset(b + 24, 0xFF, 16);
            t->mng[BM_USED_BADGES + index / 8] |= (uint8_t)(1u << (index % 8));
            snprintf(name, sizeof name, "B%u", index);
            name_block(name, strlen(name), t->data + BD_BADGE_TITLES + index * BD_TITLE_BLOCK);
        }
    }
    put_le32(t->mng + BM_SET_COUNT, n_sets);
    put_le32(t->mng + BM_UNIQUE, index);
    memset(t->mng + BM_VIEW, 0xFF, 4);
    put_le32(t->mng + BM_TOTAL, index * B_QUANTITY);
    put_le32(t->mng + BM_NNID, 0x5EED1234u);
    for (unsigned n = 0; n < BADGE_PLACED_MAX; n++) {
        uint8_t *r = t->mng + BM_LAYOUT + n * BM_LAYOUT_RECORD;
        memset(r + 4, 0xFF, 10);
        memset(r + 16, 0xFF, 8);
    }
    for (int p = 0; p < placed && p < (int)index; p++) {
        uint8_t *r = t->mng + BM_LAYOUT + p * BM_LAYOUT_RECORD, *b = t->mng + BM_BADGE_INFO + p * BM_BADGE_INFO_SIZE;
        memcpy(r, b, 16);
        put_le32(r + 16, 0xD - p);
        put_le32(r + 20, B_FOLDER_ICON);
        put_le16(b + 16, 1);
        put_le32(t->mng + BM_PLACED, get_le32(t->mng + BM_PLACED) + 1);
    }
}

static size_t live, allocs, fail_alloc_at = (size_t)-1;
#define ALLOC_TRIES 256u     /* an allocation-failure loop that has not ended by then fails instead of hanging */

static void *t_alloc(void *ctx, size_t size)
{
    (void)ctx;
    if (allocs++ == fail_alloc_at)
        return NULL;
    void *p = malloc(size);
    if (p)
        live++;
    return p;
}

static void t_free(void *ctx, void *p)
{
    (void)ctx;
    live--;
    free(p);
}

typedef struct mem {
    const uint8_t *p;
    size_t reads, fail_at;
} mem;

static int counted_read(void *ctx, uint64_t offset, void *buf, size_t len)
{
    mem *m = ctx;
    if (m->reads++ == m->fail_at)
        return -1;
    memcpy(buf, m->p + offset, len);
    return 0;
}

/* A zip of stored members: name and bytes, with the UTF-8 flag if utf8 */
static uint8_t *make_zip(const char **names, const uint8_t **data, const size_t *sizes, int n, int utf8, size_t *size)
{
    size_t total = 22;
    for (int i = 0; i < n; i++)
        total += 30 + 46 + 2 * strlen(names[i]) + sizes[i];
    uint8_t *z = calloc(total, 1), *p = z;
    uint32_t *local = malloc(n * sizeof *local);
    for (int i = 0; i < n; i++) {
        size_t len = strlen(names[i]);
        local[i] = (uint32_t)(p - z);
        put_le32(p, 0x04034b50u);
        put_le16(p + 4, 20);
        put_le16(p + 6, utf8 ? 0x800 : 0);
        put_le32(p + 14, (uint32_t)crc32(0, data[i], (uInt)sizes[i]));
        put_le32(p + 18, (uint32_t)sizes[i]);
        put_le32(p + 22, (uint32_t)sizes[i]);
        put_le16(p + 26, (uint16_t)len);
        memcpy(p + 30, names[i], len);
        memcpy(p + 30 + len, data[i], sizes[i]);
        p += 30 + len + sizes[i];
    }
    uint8_t *dir = p;
    for (int i = 0; i < n; i++) {
        size_t len = strlen(names[i]);
        put_le32(p, 0x02014b50u);
        put_le16(p + 4, 20);
        put_le16(p + 6, 20);
        put_le16(p + 8, utf8 ? 0x800 : 0);
        put_le32(p + 16, (uint32_t)crc32(0, data[i], (uInt)sizes[i]));
        put_le32(p + 20, (uint32_t)sizes[i]);
        put_le32(p + 24, (uint32_t)sizes[i]);
        put_le16(p + 28, (uint16_t)len);
        put_le32(p + 42, local[i]);
        memcpy(p + 46, names[i], len);
        p += 46 + len;
    }
    put_le32(p, 0x06054b50u);
    put_le16(p + 8, (uint16_t)n);
    put_le16(p + 10, (uint16_t)n);
    put_le32(p + 12, (uint32_t)(p - dir));
    put_le32(p + 16, (uint32_t)(dir - z));
    p += 22;
    free(local);
    *size = (size_t)(p - z);
    return z;
}

static void test_names(void)
{
    size_t n;
    CHECK_EQ(name_check("Badge 01", 8), BADGE_OK);
    CHECK_EQ(name_check("", 0), BADGE_E_NAME);
    CHECK_EQ(name_check(" \t", 2), BADGE_E_NAME);
    CHECK_EQ(name_check("\xc2\xa0", 2), BADGE_E_NAME);          /* NBSP only: blank to Python's strip() */
    CHECK_EQ(name_check("Gr\xc3\xb6\xc3\x9f" "e", 7), BADGE_OK);   /* "Grosse" with o-umlaut and sharp s */
    CHECK_EQ(name_check("\xe2\x82\xac", 3), BADGE_OK);           /* the euro sign */
    CHECK_EQ(name_check("\xd0\x94", 2), BADGE_E_NAME);           /* Cyrillic: a cased script beyond Latin-1 */
    CHECK_EQ(name_check("e\xcc\x81", 3), BADGE_E_NAME);          /* e and a combining acute: NFC would compose */
    CHECK_EQ(name_check("\xe3\x81\x8b\xe3\x82\x99", 6), BADGE_E_NAME);   /* ka and the combining voicing mark */
    CHECK_EQ(name_check("\xe3\x81\x8c", 3), BADGE_OK);           /* ga, precomposed */
    CHECK_EQ(name_check("\xef\xa4\x80", 3), BADGE_E_NAME);       /* a CJK compatibility ideograph: NFC changes it */
    CHECK_EQ(name_check("\xed\xa0\x80", 3), BADGE_E_NAME);       /* a surrogate in UTF-8 */
    CHECK_EQ(name_check("\xe3\x80\x80", 3), BADGE_E_NAME);       /* only the ideographic space: blank */
    CHECK_EQ(name_check("a\xc3", 2), BADGE_E_NAME);              /* cut UTF-8 */
    CHECK_EQ(name_check("\xc1\x81", 2), BADGE_E_NAME);           /* overlong */
    char long_name[80];
    memset(long_name, 'x', sizeof long_name);
    CHECK_EQ(name_check(long_name, 68), BADGE_OK);
    CHECK_EQ(name_check(long_name, 69), BADGE_E_NAME);
    size_t units;
    CHECK_EQ(name_points("\xc3\xa9t\xc3\xa9", 5, NULL, 0, &n, &units), BADGE_OK);
    CHECK(n == 3 && units == 3);
    CHECK_EQ(name_points("a\xf0\x9f\x98\x80", 5, NULL, 0, &n, &units), BADGE_OK);    /* a smiley: a pair */
    CHECK(n == 2 && units == 3);
    memcpy(long_name + 67, "\xf0\x9f\x98\x80", 4);           /* 67 units and a pair: 69 */
    CHECK_EQ(name_check(long_name, 71), BADGE_E_NAME);
    CHECK_EQ(name_check(long_name + 1, 70), BADGE_OK);          /* 66 and a pair: 68 */
    CHECK(name_order("a", "B") < 0);
    CHECK(name_order("B", "b") < 0);                            /* same fold: then by code point */
    CHECK(name_order("ss", "\xc3\x9f") < 0);                    /* sharp s folds to "ss" */
    CHECK(name_order("\xc3\x9f", "st") < 0);
    CHECK(name_order("\xc3\x84", "b") > 0);                     /* A-umlaut folds to U+00E4, after "b" */
    CHECK(name_order("\xc2\xb5", "z") > 0);                     /* micro sign folds to U+03BC */
    CHECK(name_order("Badge 2", "Badge 10") > 0);               /* by characters, not by number */
    CHECK_EQ(name_order("x", "x"), 0);
    uint8_t block[BD_TITLE_BLOCK];
    name_block("\xc3\xa9t", 3, block);
    CHECK(block[0] == 0xE9 && block[1] == 0 && block[2] == 't' && block[4] == 0 && block[5] == 0);
    CHECK(!memcmp(block, block + 15 * BD_TITLE_SIZE, BD_TITLE_SIZE));
    name_block("\xe6\xbc\xa2\xf0\x9f\x98\x80", 7, block);    /* a kanji, then U+1F600 as a surrogate pair */
    CHECK(get_le16(block) == 0x6F22 && get_le16(block + 2) == 0xD83D && get_le16(block + 4) == 0xDE00 &&
          get_le16(block + 6) == 0);
    CHECK(name_order("\xef\xbc\xa1", "\xef\xbd\x81") < 0);    /* fullwidth A and a: one fold, then the code */
    CHECK(name_order("\xef\xbc\xa2", "\xef\xbd\x81") > 0);    /* fullwidth B folds after fullwidth a */
}

static void test_regions_and_check(void)
{
    badge_region r[REGIONS];
    badge_regions(3, 12, 2, r);
    CHECK(r[3].file == REG_MNG && r[3].offset == BM_USED_BADGES + 1 && r[3].size == 1);
    CHECK(r[5].offset == BM_BADGE_INFO + 12 * BM_BADGE_INFO_SIZE && r[5].size == 2 * BM_BADGE_INFO_SIZE);
    CHECK(r[11].file == REG_DATA && r[11].offset == BD_SMALL_ICONS + 12 * BD_SMALL_ICON_SIZE);
    badge_regions(0, 7, 2, r);                                  /* indices 7 and 8: two bitfield bytes */
    CHECK_EQ(r[3].size, 2);
    /* unchanged_outside: bit 13 (new) and, wrongly, bit 9 (old) changed in one byte of the region */
    uint8_t *a = calloc(BADGE_MNG_SIZE, 1), *b = calloc(BADGE_MNG_SIZE, 1);
    badge_regions(3, 12, 2, r);
    a[BM_USED_BADGES + 1] = 0x02;
    b[BM_USED_BADGES + 1] = 0x20;
    CHECK_EQ(badge_mng_changed(a, b, r, NULL, 3, 0, 12, 2), 1);
    b[BM_USED_BADGES + 1] = 0x22;
    CHECK_EQ(badge_mng_changed(a, b, r, NULL, 3, 0, 12, 2), 0);
    b[BM_USED_SETS] = 0x08;                                     /* set 3's bit: only when allowed */
    CHECK_EQ(badge_mng_changed(a, b, r, NULL, 3, 0, 12, 2), 1);
    CHECK_EQ(badge_mng_changed(a, b, r, NULL, 3, 1, 12, 2), 0);
    b[BM_NNID] = 1;                                             /* outside every region */
    CHECK_EQ(badge_mng_changed(a, b, r, NULL, 3, 1, 12, 2), 1);
    b[BM_NNID] = 0;
    b[BM_VIEW] = 1;                                             /* the view: only as the extra span */
    const badge_region view = {REG_MNG, BM_VIEW, 8};
    CHECK_EQ(badge_mng_changed(a, b, r, NULL, 3, 1, 12, 2), 1);
    CHECK_EQ(badge_mng_changed(a, b, r, &view, 3, 1, 12, 2), 0);
    free(a);
    free(b);
}

static void test_png(void)
{
    uint8_t *rgba = pattern(64, 128, 3), *out;
    size_t size;
    uint8_t *png = make_png(64, 128, rgba, &size);
    uint32_t w, h;
    CHECK_EQ(badge_png_decode(png, size, &w, &h, &out), BADGE_OK);
    CHECK(w == 64 && h == 128 && !memcmp(out, rgba, 64 * 128 * 4));
    tp_free(out);

    /* every allocation failing in turn */
    for (fail_alloc_at = 0;; fail_alloc_at++) {
        allocs = 0;
        int r = badge_png_decode(png, size, &w, &h, &out);
        CHECK(r == BADGE_OK || r == BADGE_E_NOMEM);
        CHECK(r != BADGE_OK || out);
        tp_free(out);
        CHECK_EQ(live, 0);
        if (r == BADGE_OK)
            break;
    }
    fail_alloc_at = (size_t)-1;

    /* every truncation, and random damage with and without fixed CRCs: refused or decoded, never past a buffer */
    for (size_t cut = 0; cut < size; cut++) {
        int r = badge_png_decode(png, cut, &w, &h, &out);
        CHECK(r != BADGE_OK);
        CHECK(out == NULL);
    }
    uint8_t *bad = malloc(size);
    int refused = 0;
    for (int round = 0; round < 3000; round++) {
        memcpy(bad, png, size);
        for (int k = 0; k < 1 + round % 4; k++)
            bad[next_random() % size] ^= (uint8_t)(1u << (next_random() % 8));
        if (round % 2) {                                        /* fix the CRCs: the damage reaches the decoder */
            for (size_t pos = 8; pos + 12 <= size;) {
                uint32_t len = get_be32(bad + pos);
                if (len > size - pos - 12)
                    break;
                put_be32(bad + pos + 8 + len, (uint32_t)crc32(0, bad + pos + 4, 4 + len));
                pos += 12 + len;
            }
        }
        int r = badge_png_decode(bad, size, &w, &h, &out);
        refused += r != BADGE_OK;
        if (r == BADGE_OK)
            tp_free(out);
    }
    CHECK(refused > 1000);
    CHECK_EQ(live, 0);
    free(bad);

    /* the images: a solid colour gives that colour; the 32x32 one is the truncated mean */
    uint8_t solid[64 * 64 * 4], big[BD_BADGE_ICON_SIZE], small[BD_SMALL_ICON_SIZE];
    for (int i = 0; i < 64 * 64; i++)
        memcpy(solid + 4 * i, (const uint8_t[]){0xFF, 0x80, 0x08, 0xF0}, 4);
    badge_tile_images(solid, 64, 0, 0, big, small);
    CHECK_EQ(get_le16(big), 0x1F << 11 | 0x20 << 5 | 0x01);
    CHECK_EQ(big[0x2000], 0xFF);
    CHECK_EQ(get_le16(small + 2 * 1023), 0x1F << 11 | 0x20 << 5 | 0x01);
    solid[0] = 0;                                               /* pixel (0, 0): 0, three times 0xFF: mean 0xBF */
    badge_tile_images(solid, 64, 0, 0, big, small);
    CHECK_EQ(get_le16(small) >> 11, 0xBF >> 3);
    CHECK_EQ(badge_tiled(1, 1, 64), 3);
    CHECK_EQ(badge_tiled(8, 0, 64), 64);
    CHECK_EQ(badge_tiled(0, 8, 64), 512);
    CHECK_EQ(badge_tiled(0, 8, 48), 384);
    free(png);
    free(rgba);
}

typedef struct set_files {
    badge_file f[4];
    uint8_t *png[4];
} set_files;

static void two_badges(set_files *s, int icon)
{
    size_t size;
    uint8_t *a = pattern(64, 64, 1), *b = pattern(128, 64, 2), *c = pattern(48, 48, 99);
    memset(s, 0, sizeof *s);
    s->png[0] = make_png(64, 64, a, &size);
    s->f[0] = (badge_file){"Badge 01.png", 0, s->png[0], size};
    s->png[1] = make_png(128, 64, b, &size);
    s->f[1] = (badge_file){"Badge 02.png", 0, s->png[1], size};
    s->f[2] = (badge_file){"._Badge 01.png", 0, NULL, 0};      /* AppleDouble: skipped */
    if (icon) {
        s->png[3] = make_png(48, 48, c, &size);
        s->f[3] = (badge_file){"_SetIcon.PNG", 0, s->png[3], size};
    }
    free(a);
    free(b);
    free(c);
}

static void free_files(set_files *s)
{
    for (int i = 0; i < 4; i++)
        free(s->png[i]);
}

static void test_folder_rules(void)
{
    set_files s;
    two_badges(&s, 1);
    badge_folder folder;
    CHECK_EQ(badge_folder_read(s.f, 4, &folder), BADGE_OK);
    CHECK_EQ(folder.count, 3);                                  /* 1 + the two columns of the 128x64 sheet */
    CHECK_EQ(folder.icon, 3);
    CHECK(folder.tiles[1].file == 1 && folder.tiles[1].tile == 0 && folder.tiles[2].x0 == 64 &&
          folder.tiles[2].name_len == 8);
    badge_folder_free(&folder);
    badge_file f[4];
    memcpy(f, s.f, sizeof f);
    f[2] = (badge_file){"notes.txt", 0, (const uint8_t *)"x", 1};
    CHECK_EQ(badge_folder_read(f, 4, &folder), BADGE_E_FOLDER);
    f[2] = (badge_file){"sub", 1, NULL, 0};
    CHECK_EQ(badge_folder_read(f, 4, &folder), BADGE_E_FOLDER);
    f[2] = (badge_file){".hidden", 1, NULL, 0};
    CHECK_EQ(badge_folder_read(f, 4, &folder), BADGE_OK);
    badge_folder_free(&folder);
    f[2] = (badge_file){"Badge 01.png", 0, s.png[0], s.f[0].size};
    CHECK_EQ(badge_folder_read(f, 4, &folder), BADGE_E_FOLDER); /* the same name twice */
    f[2] = (badge_file){"Caf\xd0\x94.png", 0, s.png[0], s.f[0].size};
    CHECK_EQ(badge_folder_read(f, 4, &folder), BADGE_E_NAME);
    CHECK_EQ(badge_folder_read(&f[3], 1, &folder), BADGE_E_FOLDER); /* only an icon: no badge */
    f[0].data = s.png[3];                                       /* a 48x48 badge */
    f[0].size = s.f[3].size;
    f[2] = (badge_file){".x", 0, NULL, 0};
    CHECK_EQ(badge_folder_read(f, 4, &folder), BADGE_E_IMAGE_SIZE);
    f[0] = s.f[0];
    f[3].data = s.png[0];                                       /* a 64x64 icon */
    f[3].size = s.f[0].size;
    CHECK_EQ(badge_folder_read(f, 4, &folder), BADGE_E_IMAGE_SIZE);
    f[3] = s.f[3];
    f[1].size = 20;                                             /* a damaged badge */
    CHECK_EQ(badge_folder_read(f, 4, &folder), BADGE_E_PNG);
    /* two icon names that differ in case (a zip can hold both): neither is a badge, the last is the icon */
    badge_file icons[5] = {s.f[0], s.f[1], {"_SETICON.png", 0, s.png[0], s.f[0].size}, s.f[3], s.f[2]};
    icons[3].name = "_seticon.png";
    CHECK_EQ(badge_folder_read(icons, 5, &folder), BADGE_OK);
    CHECK(folder.count == 3 && folder.icon == 3);
    badge_folder_free(&folder);
    icons[2] = (badge_file){"_SETICON.png", 0, s.png[3], s.f[3].size};
    icons[3] = (badge_file){"_seticon.png", 0, s.png[0], s.f[0].size};
    CHECK_EQ(badge_folder_read(icons, 5, &folder), BADGE_E_IMAGE_SIZE);    /* the last one, 64x64, is the icon */
    free_files(&s);
}

static void test_zip(void)
{
    set_files s;
    two_badges(&s, 0);
    const char *names[5] = {"Set/", "Set/Badge 02.png", "__MACOSX/Set/._Badge 02.png", "Set/Badge 01.png",
                            "Set/.DS_Store"};
    const uint8_t *data[5] = {(const uint8_t *)"", s.png[1], (const uint8_t *)"junk", s.png[0],
                              (const uint8_t *)"x"};
    size_t sizes[5] = {0, s.f[1].size, 4, s.f[0].size, 1}, size;
    uint8_t *zip = make_zip(names, data, sizes, 5, 0, &size);
    mem m = {zip, 0, (size_t)-1};
    tp_source src = {&m, size, counted_read};
    badge_file *files;
    size_t count;
    CHECK_EQ(badge_zip_files(&src, &files, &count), BADGE_OK);
    CHECK_EQ(count, 3);                                         /* the folder entry and __MACOSX/ are gone */
    CHECK(!strcmp(files[0].name, "Badge 02.png") && files[0].size == s.f[1].size &&
          !memcmp(files[0].data, s.png[1], files[0].size));
    CHECK(!strcmp(files[2].name, ".DS_Store") && !files[2].data);
    badge_folder folder;
    CHECK_EQ(badge_folder_read(files, count, &folder), BADGE_OK);
    CHECK(folder.count == 3 && !strcmp(files[folder.tiles[0].file].name, "Badge 01.png"));
    badge_folder_free(&folder);
    badge_files_free(files, count);

    /* every allocation and every read failing in turn */
    for (fail_alloc_at = 0;; fail_alloc_at++) {
        allocs = 0;
        int r = badge_zip_files(&src, &files, &count);
        CHECK(r == BADGE_OK || r == BADGE_E_NOMEM);
        if (r == BADGE_OK)
            badge_files_free(files, count);
        CHECK_EQ(live, 0);
        if (r == BADGE_OK)
            break;
    }
    fail_alloc_at = (size_t)-1;
    for (m.fail_at = 0;; m.fail_at++) {
        m.reads = 0;
        int r = badge_zip_files(&src, &files, &count);
        CHECK(r == BADGE_OK || r == BADGE_E_ZIP);
        if (r == BADGE_OK)
            badge_files_free(files, count);
        CHECK_EQ(live, 0);
        if (r == BADGE_OK)
            break;
    }
    m.fail_at = (size_t)-1;

    /* every truncation and random damage: refused or read, never past the buffer */
    uint8_t *bad = malloc(size);
    for (size_t cut = 0; cut < size; cut += 7) {
        src.size = cut;
        if (badge_zip_files(&src, &files, &count) == BADGE_OK)
            badge_files_free(files, count);
    }
    src.size = size;
    m.p = bad;
    for (int round = 0; round < 2000; round++) {
        memcpy(bad, zip, size);
        for (int k = 0; k < 1 + round % 3; k++)
            bad[size - 1 - next_random() % (size < 400 ? size : 400)] ^= (uint8_t)(1u << (next_random() % 8));
        if (badge_zip_files(&src, &files, &count) == BADGE_OK)
            badge_files_free(files, count);
    }
    CHECK_EQ(live, 0);
    free(bad);
    free(zip);

    /* a member in another subfolder is a subfolder; a legacy-code-page name is refused */
    const char *mixed[2] = {"a.png", "sub/b.png"};
    const uint8_t *two[2] = {s.png[0], s.png[0]};
    size_t two_sizes[2] = {s.f[0].size, s.f[0].size};
    zip = make_zip(mixed, two, two_sizes, 2, 0, &size);
    m = (mem){zip, 0, (size_t)-1};
    src = (tp_source){&m, size, counted_read};
    CHECK_EQ(badge_zip_files(&src, &files, &count), BADGE_OK);
    CHECK(count == 2 && !strcmp(files[1].name, "sub") && files[1].is_dir);
    CHECK_EQ(badge_folder_read(files, count, &folder), BADGE_E_FOLDER);
    badge_files_free(files, count);
    free(zip);
    const char *latin[1] = {"Caf\xe9.png"};
    zip = make_zip(latin, two, two_sizes, 1, 0, &size);
    m = (mem){zip, 0, (size_t)-1};
    src = (tp_source){&m, size, counted_read};
    CHECK_EQ(badge_zip_files(&src, &files, &count), BADGE_E_NAME);
    free(zip);
    const char *utf8[1] = {"Caf\xc3\xa9.png"};
    zip = make_zip(utf8, two, two_sizes, 1, 1, &size);
    m = (mem){zip, 0, (size_t)-1};
    src = (tp_source){&m, size, counted_read};
    CHECK_EQ(badge_zip_files(&src, &files, &count), BADGE_OK);
    CHECK_EQ(badge_folder_read(files, count, &folder), BADGE_OK);
    badge_folder_free(&folder);
    badge_files_free(files, count);
    free(zip);
    free_files(&s);
}

static void entry(badge_manifest_set *set, badge_known *known, uint32_t set_id, const char *folder)
{
    *set = (badge_manifest_set){folder, folder, set_id, known, 0};
}

static void test_manifest(void)
{
    badge_known known[2] = {{"a.png", 0, 0x4000}, {"a.png", 1, 0x4001}};
    badge_manifest_set sets[2];
    entry(&sets[0], known, 0x7000, "A");
    sets[0].count = 2;
    entry(&sets[1], NULL, 0x7001, "B");
    badge_manifest m = {0x7002, 0x4002, sets, 2};
    CHECK_EQ(badge_manifest_check(&m), BADGE_OK);
    CHECK_EQ(badge_manifest_check(NULL), BADGE_OK);
    m.next_set_id = 0x7001;
    CHECK_EQ(badge_manifest_check(&m), BADGE_E_MANIFEST);
    m.next_set_id = 0x7002;
    m.next_badge_id = 0x4001;
    CHECK_EQ(badge_manifest_check(&m), BADGE_E_MANIFEST);
    m.next_badge_id = 0x4002;
    sets[1].set_id = 0x7000;
    CHECK_EQ(badge_manifest_check(&m), BADGE_E_MANIFEST);       /* a set ID twice */
    sets[1].set_id = 0x8000;
    CHECK_EQ(badge_manifest_check(&m), BADGE_E_MANIFEST);       /* out of range */
    sets[1].set_id = 0x7001;
    sets[1].folder = "A";
    CHECK_EQ(badge_manifest_check(&m), BADGE_E_MANIFEST);       /* a folder twice */
    sets[1].folder = "B";
    known[1].badge_id = 0x4000;
    CHECK_EQ(badge_manifest_check(&m), BADGE_E_MANIFEST);       /* a badge ID twice */
    known[1].badge_id = 0x3FFF;
    CHECK_EQ(badge_manifest_check(&m), BADGE_E_MANIFEST);
}

static int same_tree(const tree *a, const tree *b)
{
    return !memcmp(a->data, b->data, BADGE_DATA_SIZE) && !memcmp(a->mng, b->mng, BADGE_MNG_SIZE);
}

static void copy_tree(tree *dst, const tree *src)
{
    memcpy(dst->data, src->data, BADGE_DATA_SIZE);
    memcpy(dst->mng, src->mng, BADGE_MNG_SIZE);
}

static void test_append_remove(void)
{
    const uint32_t sizes[3] = {5, 3, 4};
    tree orig, t, after;
    make_collection(&orig, sizes, 3, 2);
    make_collection(&t, sizes, 3, 2);
    make_collection(&after, sizes, 3, 2);
    badge_collection *col = malloc(sizeof *col);
    CHECK_EQ(badge_read(t.data, t.data_size, t.mng, t.mng_size, NULL, col), BADGE_OK);
    CHECK(col->sets == 3 && col->badges == 12 && col->placed == 2 && col->layout == 2 && col->nnid_set);
    CHECK(col->set[1].start == 5 && col->set[0].placed == 2 && !strcmp(col->set[2].name, "Set 2"));
    CHECK_EQ(col->remove, BADGE_E_NOT_OURS);
    CHECK_EQ(col->warnings, 0);
    CHECK_EQ(badge_read(t.data, t.data_size - 1, t.mng, t.mng_size, NULL, col), BADGE_E_SIZE);

    set_files s;
    two_badges(&s, 0);
    badge_append_result res;
    CHECK_EQ(badge_append(t.data, t.data_size, t.mng, t.mng_size, s.f, 3, NULL, "New Set", NULL, &res), BADGE_OK);
    CHECK(res.set_id == 0x7000 && res.new_set && res.slot == 3 && res.start == 12 && res.count == 3);
    CHECK(res.next_set_id == 0x7001 && res.next_badge_id == 0x4003 && !res.set_bit && !res.icon_from_file);
    CHECK(res.badges[2].badge_id == 0x4002 && res.badges[2].file == 1 && res.badges[2].tile == 1 &&
          res.badges[2].is_new && !strcmp(res.name, "New Set"));
    copy_tree(&after, &t);
    /* nothing that existed moved: the old sets' names, the layout and what follows it */
    CHECK(!memcmp(t.data, orig.data, BD_SET_TITLES + 3 * BD_TITLE_BLOCK));
    CHECK(!memcmp(t.mng + BM_LAYOUT, orig.mng + BM_LAYOUT, BADGE_MNG_SIZE - BM_LAYOUT));

    /* the listing: the new set is ours and removable, its icon is the first badge's */
    badge_known known[3];
    for (int i = 0; i < 3; i++)
        known[i] = (badge_known){s.f[res.badges[i].file].name, res.badges[i].tile, res.badges[i].badge_id};
    badge_manifest_set mset = {"New Set", "New Set", 0x7000, known, 3};
    badge_manifest m = {res.next_set_id, res.next_badge_id, &mset, 1};
    CHECK_EQ(badge_read(t.data, t.data_size, t.mng, t.mng_size, &m, col), BADGE_OK);
    CHECK(col->sets == 4 && col->set[3].mine && col->set[3].removable && col->remove == BADGE_OK);
    uint8_t icon[BADGE_ICON_BYTES];
    CHECK_EQ(badge_set_icon(t.data, t.data_size, &col->set[3], icon), BADGE_OK);
    uint8_t *px = pattern(64, 64, 1);
    const uint8_t *p = px + (4 * 47 / 3 * 64 + 4 * 47 / 3) * 4;   /* pixel (47, 47) of the icon */
    CHECK_EQ(get_le16(icon + 2 * badge_tiled(47, 47, 48)), (p[0] >> 3) << 11 | (p[1] >> 2) << 5 | p[2] >> 3);
    free(px);

    /* refusals of a second append: the buffers stay as they are */
    badge_append_result other;
    CHECK_EQ(badge_append(t.data, t.data_size, t.mng, t.mng_size, s.f, 3, &m, "New Set", NULL, &other),
             BADGE_E_PRESENT);
    CHECK(same_tree(&t, &after));
    char long_name[80];
    memset(long_name, 'y', sizeof long_name);
    long_name[69] = 0;
    CHECK_EQ(badge_append(t.data, t.data_size, t.mng, t.mng_size, s.f, 3, &m, "Other", long_name, &other),
             BADGE_E_NAME);
    badge_manifest_set full_set = {"Other", "Other", 0x7FFF, NULL, 0};
    badge_manifest_set both[2] = {mset, full_set};
    badge_manifest no_ids = {0x8000, 0x4003, both, 2};
    CHECK_EQ(badge_append(t.data, t.data_size, t.mng, t.mng_size, s.f, 3, &no_ids, "Third", NULL, &other),
             BADGE_E_IDS);
    CHECK(same_tree(&t, &after));

    /* remove the last set: back to the exact bytes */
    badge_undo undo = {res.undo, res.undo_size};
    badge_remove_result rr;
    CHECK_EQ(badge_remove_last(t.data, t.data_size, t.mng, t.mng_size, NULL, &undo, 1, 0, &rr), BADGE_E_NOT_OURS);
    CHECK_EQ(badge_remove_last(t.data, t.data_size, t.mng, t.mng_size, &m, NULL, 0, 0, &rr), BADGE_E_NO_UNDO);
    CHECK(same_tree(&t, &after));
    /* a placement naming the new set */
    memcpy(t.mng + BM_LAYOUT + 5 * BM_LAYOUT_RECORD, t.mng + BM_BADGE_INFO + 12 * BM_BADGE_INFO_SIZE, 16);
    CHECK_EQ(badge_remove_last(t.data, t.data_size, t.mng, t.mng_size, &m, &undo, 1, 0, &rr), BADGE_E_PLACED);
    CHECK_EQ(badge_read(t.data, t.data_size, t.mng, t.mng_size, &m, col), BADGE_OK);
    CHECK(col->remove == BADGE_E_PLACED && !col->set[3].removable);
    copy_tree(&t, &after);
    t.mng[BM_VIEW] = 3;                                         /* HOME moved the case's view: the files changed */
    CHECK_EQ(badge_remove_last(t.data, t.data_size, t.mng, t.mng_size, &m, &undo, 1, 0, &rr), BADGE_E_CHANGED);
    CHECK_EQ(badge_remove_last(t.data, t.data_size, t.mng, t.mng_size, &m, &undo, 1, 1, &rr), BADGE_OK);
    CHECK(rr.view_reset && !rr.exact && rr.identical);         /* only the view had changed: it is back */
    CHECK(same_tree(&t, &orig));
    copy_tree(&t, &after);
    t.data[BD_BADGE_ICONS + 12 * BD_BADGE_ICON_SIZE] ^= 1;      /* the new set's own image changed */
    CHECK_EQ(badge_remove_last(t.data, t.data_size, t.mng, t.mng_size, &m, &undo, 1, 1, &rr), BADGE_E_SET_CHANGED);
    copy_tree(&t, &after);
    badge_undo twice[2] = {undo, undo};
    CHECK_EQ(badge_remove_last(t.data, t.data_size, t.mng, t.mng_size, &m, twice, 2, 0, &rr), BADGE_E_AMBIGUOUS);
    uint8_t *damaged = malloc(undo.size);
    memcpy(damaged, undo.data, undo.size);
    damaged[undo.size - 1] ^= 1;
    badge_undo bad_undo = {damaged, undo.size};
    CHECK_EQ(badge_remove_last(t.data, t.data_size, t.mng, t.mng_size, &m, &bad_undo, 1, 0, &rr), BADGE_E_UNDO);
    memcpy(damaged, undo.data, undo.size);
    damaged[0x20] ^= 1;                                         /* the view it would put back */
    CHECK_EQ(badge_remove_last(t.data, t.data_size, t.mng, t.mng_size, &m, &bad_undo, 1, 0, &rr), BADGE_E_UNDO);
    bad_undo.size = 10;
    CHECK_EQ(badge_remove_last(t.data, t.data_size, t.mng, t.mng_size, &m, &bad_undo, 1, 0, &rr), BADGE_E_UNDO);
    free(damaged);
    CHECK(same_tree(&t, &after));
    CHECK_EQ(badge_remove_last(t.data, t.data_size, t.mng, t.mng_size, &m, &undo, 1, 0, &rr), BADGE_OK);
    CHECK(rr.exact && rr.identical && !rr.view_reset && rr.set_id == 0x7000 && rr.count == 3);
    CHECK(same_tree(&t, &orig));
    CHECK_EQ(badge_remove_last(t.data, t.data_size, t.mng, t.mng_size, &m, &undo, 1, 0, &rr), BADGE_E_NOT_OURS);

    /* every allocation failing in turn: NOMEM and the buffers as before, or done; nothing left allocated */
    badge_append_free(&res);
    for (fail_alloc_at = 0;; fail_alloc_at++) {
        allocs = 0;
        copy_tree(&t, &orig);
        int rc = badge_append(t.data, t.data_size, t.mng, t.mng_size, s.f, 3, NULL, "New Set", NULL, &res);
        CHECK(rc == BADGE_OK || rc == BADGE_E_NOMEM);
        CHECK(rc == BADGE_OK ? same_tree(&t, &after) : same_tree(&t, &orig));
        if (rc != BADGE_E_NOMEM || fail_alloc_at >= ALLOC_TRIES)
            break;
        CHECK_EQ(live, 0);
    }
    CHECK(fail_alloc_at < ALLOC_TRIES);                         /* done within the bound: no endless loop */
    fail_alloc_at = (size_t)-1;
    undo = (badge_undo){res.undo, res.undo_size};
    for (fail_alloc_at = 0;; fail_alloc_at++) {
        allocs = 0;
        copy_tree(&t, &after);
        int rc = badge_remove_last(t.data, t.data_size, t.mng, t.mng_size, &m, &undo, 1, 0, &rr);
        CHECK(rc == BADGE_OK || rc == BADGE_E_NOMEM);
        CHECK(rc == BADGE_OK ? same_tree(&t, &orig) : same_tree(&t, &after));
        if (rc != BADGE_E_NOMEM || fail_alloc_at >= ALLOC_TRIES)
            break;
    }
    CHECK(fail_alloc_at < ALLOC_TRIES);
    fail_alloc_at = (size_t)-1;
    badge_append_free(&res);
    CHECK_EQ(live, 0);

    /* a manifest set appended again after its removal keeps its IDs; a tile gone from the folder is not written */
    copy_tree(&t, &orig);
    CHECK_EQ(badge_append(t.data, t.data_size, t.mng, t.mng_size, &s.f[1], 1, &m, "New Set", "Renamed", &res),
             BADGE_OK);
    CHECK(!res.new_set && res.count == 2 && res.badges[0].badge_id == 0x4001 && res.badges[1].badge_id == 0x4002 &&
          !res.badges[0].is_new && res.next_badge_id == 0x4003 && !strcmp(res.name, "Renamed"));
    badge_append_free(&res);

    /* the limits: 100 sets, 1000 badges; a full collection is not changed */
    uint32_t big = 998, many[100];
    tree lim;
    make_collection(&lim, &big, 1, 0);
    CHECK_EQ(badge_append(lim.data, lim.data_size, lim.mng, lim.mng_size, s.f, 3, NULL, "N", NULL, &res), BADGE_E_FULL);
    CHECK_EQ(badge_append(lim.data, lim.data_size, lim.mng, lim.mng_size, s.f, 1, NULL, "N", NULL, &res), BADGE_OK);
    badge_append_free(&res);
    free(lim.data);
    free(lim.mng);
    for (int i = 0; i < 100; i++)
        many[i] = 1;
    make_collection(&lim, many, 100, 0);
    CHECK_EQ(badge_append(lim.data, lim.data_size, lim.mng, lim.mng_size, s.f, 1, NULL, "N", NULL, &res), BADGE_E_FULL);
    free(lim.data);
    free(lim.mng);

    /* the strict reader refuses what does not add up */
    copy_tree(&t, &orig);
    put_le32(t.mng, 1);
    CHECK_EQ(badge_read(t.data, t.data_size, t.mng, t.mng_size, NULL, col), BADGE_E_FORMAT);
    copy_tree(&t, &orig);
    t.mng[BM_USED_BADGES] &= (uint8_t)~0x04;
    CHECK_EQ(badge_read(t.data, t.data_size, t.mng, t.mng_size, NULL, col), BADGE_E_FORMAT);
    copy_tree(&t, &orig);
    put_le32(t.mng + BM_SET_INFO + BM_SET_INFO_SIZE + 0x24, 4);
    CHECK_EQ(badge_read(t.data, t.data_size, t.mng, t.mng_size, NULL, col), BADGE_E_FORMAT);
    copy_tree(&t, &orig);
    put_le32(t.mng + BM_TOTAL, 7);
    CHECK_EQ(badge_read(t.data, t.data_size, t.mng, t.mng_size, NULL, col), BADGE_E_FORMAT);
    copy_tree(&t, &orig);
    put_le32(t.mng + BM_BADGE_INFO + 3 * BM_BADGE_INFO_SIZE + 4, 1);
    CHECK_EQ(badge_read(t.data, t.data_size, t.mng, t.mng_size, NULL, col), BADGE_E_FORMAT);
    copy_tree(&t, &orig);
    put_le32(t.mng + BM_PLACED, 0);                             /* a placement mismatch is a warning */
    CHECK_EQ(badge_read(t.data, t.data_size, t.mng, t.mng_size, NULL, col), BADGE_OK);
    CHECK(col->warnings == (BADGE_WARN_PLACED));

    /* check-slots: the live layout and the copies */
    uint8_t *slots = calloc(BADGE_SLOTS_SIZE, 1);
    memcpy(slots + SL_BADGES, orig.mng, BADGE_MNG_SIZE);
    slots[SL_FLAGS] = 1;
    memcpy(slots + SL_BADGES + BADGE_MNG_SIZE, orig.mng, BADGE_MNG_SIZE);
    put_le32(slots + SL_BADGES + BADGE_MNG_SIZE + BM_LAYOUT + 4, 0x999);   /* slot 2: a placement whose badge is gone */
    put_le32(slots + SL_BADGES + 2 * BADGE_MNG_SIZE, 1);                    /* slot 3: damaged, not saved */
    badge_slots_check c;
    CHECK_EQ(badge_check_slots(orig.mng, BADGE_MNG_SIZE, slots, BADGE_SLOTS_SIZE, &c), BADGE_OK);
    CHECK(c.badges == 12 && c.live.placements == 2 && c.live.folder_icons == 2 && c.slot[0].saved && c.ok);
    CHECK(c.slot[1].missing == 1 && !c.slot[1].saved && c.slot[2].damaged);
    slots[SL_FLAGS + 1] = 1;
    CHECK_EQ(badge_check_slots(orig.mng, BADGE_MNG_SIZE, slots, BADGE_SLOTS_SIZE, &c), BADGE_OK);
    CHECK(!c.ok);
    CHECK_EQ(badge_check_slots(orig.mng, BADGE_MNG_SIZE, slots, BADGE_SLOTS_SIZE - 1, &c), BADGE_E_SIZE);
    put_le32(slots, 1);
    CHECK_EQ(badge_check_slots(orig.mng, BADGE_MNG_SIZE, slots, BADGE_SLOTS_SIZE, &c), BADGE_E_SLOTS);
    free(slots);

    free_files(&s);
    free(col);
    tree *all[3] = {&orig, &t, &after};
    for (int i = 0; i < 3; i++) {
        free(all[i]->data);
        free(all[i]->mng);
    }
    CHECK_EQ(live, 0);
}

/* ---------------------------------------------------------------- a removal after HOME changed its file (B2) */

/* HOME's changes after an append, each as seen on the console (2026-11-15 R13 and test B4): the placed counts lower
 * (the header's and a badge's own), the case's view moved from the set before onto the new set, set record + 0x28
 * changed on the last two sets (the new set's own record among them), a layout record freed. */
enum { HOME_PLACED = 1, HOME_VIEW = 2, HOME_28 = 4, HOME_LAYOUT = 8, HOME_ALL = 15 };

static const uint8_t FREED[BM_LAYOUT_RECORD] = {0, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                                0xFF, 0xFF, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static void home_changes(tree *t, unsigned which, uint32_t s)
{
    if (which & HOME_PLACED) {
        put_le32(t->mng + BM_PLACED, get_le32(t->mng + BM_PLACED) - 1);
        put_le16(t->mng + BM_BADGE_INFO + 1 * BM_BADGE_INFO_SIZE + 0x10, 0);
    }
    if (which & HOME_VIEW)
        put_le32(t->mng + BM_VIEW, s);
    if (which & HOME_28) {
        put_le32(t->mng + BM_SET_INFO + (s - 1) * BM_SET_INFO_SIZE + 0x28, 0x11);
        put_le32(t->mng + BM_SET_INFO + s * BM_SET_INFO_SIZE + 0x28, 3);
    }
    if (which & HOME_LAYOUT)
        memcpy(t->mng + BM_LAYOUT + 1 * BM_LAYOUT_RECORD, FREED, BM_LAYOUT_RECORD);
}

/* What the removal must leave, written out from the two lists (the progress file sr-b2.md): HOME's files as they are
 * now, with the application's areas (counts, used bits, the set's records, names and images) as before the append,
 * and the view as it was before the append only where it names the set or no set */
static void expect_removed(const tree *home, const tree *before, uint32_t s, uint32_t u, uint32_t n, tree *want)
{
    badge_region r[REGIONS];
    copy_tree(want, home);
    badge_regions(s, u, n, r);
    for (int i = 0; i < REGIONS; i++)
        memcpy((r[i].file == REG_DATA ? want->data : want->mng) + r[i].offset,
               (r[i].file == REG_DATA ? before->data : before->mng) + r[i].offset, r[i].size);
    uint32_t v = get_le32(home->mng + BM_VIEW);
    if (v != B_NONE && v >= s)
        memcpy(want->mng + BM_VIEW, before->mng + BM_VIEW, 8);
}

/* badge_remove_check and badge_remove_last give the same answer; a refusal leaves the buffers as they were */
static int both_refuse(tree *t, const badge_manifest *m, const badge_undo *undo, size_t count, int allow)
{
    badge_remove_result rr;
    tree was;
    was.data = malloc(BADGE_DATA_SIZE);
    was.mng = malloc(BADGE_MNG_SIZE);
    copy_tree(&was, t);
    int c = badge_remove_check(t->data, t->data_size, t->mng, t->mng_size, m, undo, count, allow, &rr);
    int r = badge_remove_last(t->data, t->data_size, t->mng, t->mng_size, m, undo, count, allow, &rr);
    CHECK_EQ(c, r);
    CHECK(r != BADGE_OK && same_tree(t, &was));
    free(was.data);
    free(was.mng);
    return r;
}

static void test_remove_after_home(void)
{
    const uint32_t sizes[3] = {5, 3, 4};
    tree orig, t, after, home, want;
    make_collection(&orig, sizes, 3, 2);
    put_le32(orig.mng + BM_VIEW, 2);                            /* the case shows the last set, as on the console */
    make_collection(&t, sizes, 3, 2);
    make_collection(&after, sizes, 3, 2);
    make_collection(&home, sizes, 3, 2);
    make_collection(&want, sizes, 3, 2);
    copy_tree(&t, &orig);
    set_files f;
    two_badges(&f, 0);
    badge_append_result res;
    CHECK_EQ(badge_append(t.data, t.data_size, t.mng, t.mng_size, f.f, 3, NULL, "New Set", NULL, &res), BADGE_OK);
    const uint32_t s = res.slot, u = res.start, n = res.count;
    CHECK(s == 3 && u == 12 && n == 3);
    copy_tree(&after, &t);
    badge_known known[3];
    for (int i = 0; i < 3; i++)
        known[i] = (badge_known){f.f[res.badges[i].file].name, res.badges[i].tile, res.badges[i].badge_id};
    badge_manifest_set mset = {"New Set", "New Set", res.set_id, known, 3};
    badge_manifest m = {res.next_set_id, res.next_badge_id, &mset, 1};
    badge_undo undo = {res.undo, res.undo_size};
    badge_remove_result rr;
    badge_region r[REGIONS];
    badge_regions(s, u, n, r);

    /* each of HOME's changes alone and every combination: the removal goes; the result byte for byte */
    for (unsigned which = 0; which <= HOME_ALL; which++) {
        copy_tree(&home, &after);
        home_changes(&home, which, s);
        copy_tree(&t, &home);
        CHECK_EQ(badge_remove_check(t.data, t.data_size, t.mng, t.mng_size, &m, &undo, 1, 1, &rr), BADGE_OK);
        CHECK(same_tree(&t, &home));                            /* the check changes nothing */
        CHECK(rr.set_id == res.set_id && rr.slot == s && rr.start == u && rr.count == n && !rr.identical);
        CHECK_EQ(rr.exact, which == 0);
        CHECK_EQ(rr.view_reset, (which & HOME_VIEW) != 0);
        CHECK_EQ(badge_remove_last(t.data, t.data_size, t.mng, t.mng_size, &m, &undo, 1, 1, &rr), BADGE_OK);
        CHECK_EQ(rr.exact, which == 0);
        CHECK_EQ(rr.identical, (which & ~(unsigned)HOME_VIEW) == 0);
        expect_removed(&home, &orig, s, u, n, &want);
        CHECK(same_tree(&t, &want));
        /* the application's fields as before the append */
        for (int i = 0; i < REGIONS; i++)
            CHECK(!memcmp((r[i].file == REG_DATA ? t.data : t.mng) + r[i].offset,
                          (r[i].file == REG_DATA ? orig.data : orig.mng) + r[i].offset, r[i].size));
        CHECK(get_le32(t.mng + BM_SET_COUNT) == 3 && get_le32(t.mng + BM_UNIQUE) == 12 &&
              get_le32(t.mng + BM_TOTAL) == 12 * B_QUANTITY);
        /* HOME's fields as HOME left them: the placed counts, the layout, the other sets' + 0x28 */
        CHECK_EQ(get_le32(t.mng + BM_PLACED), get_le32(home.mng + BM_PLACED));
        CHECK(!memcmp(t.mng + BM_LAYOUT, home.mng + BM_LAYOUT, BADGE_PLACED_MAX * BM_LAYOUT_RECORD));
        CHECK_EQ(get_le32(t.mng + BM_SET_INFO + (s - 1) * BM_SET_INFO_SIZE + 0x28), which & HOME_28 ? 0x11 : 0);
        for (uint32_t i = 0; i < u; i++)
            CHECK_EQ(get_le16(t.mng + BM_BADGE_INFO + i * BM_BADGE_INFO_SIZE + 0x10),
                     get_le16(home.mng + BM_BADGE_INFO + i * BM_BADGE_INFO_SIZE + 0x10));
        /* the view: on the set that went, it is back to the set before (2) */
        CHECK_EQ(get_le32(t.mng + BM_VIEW), 2);
        badge_collection *col = malloc(sizeof *col);
        CHECK_EQ(badge_read(t.data, t.data_size, t.mng, t.mng_size, &m, col), BADGE_OK);
        CHECK(col->sets == 3 && col->badges == 12);
        CHECK(get_le32(t.mng + BM_VIEW) == B_NONE || get_le32(t.mng + BM_VIEW) < col->sets);  /* names a set there */
        free(col);
    }

    /* the view is HOME's: on a set that stays, or none, it stays as HOME left it (column too); on no set at all it
     * gets its value from before the append */
    static const uint32_t VIEWS[4][2] = {{1, 7}, {0, 0}, {B_NONE, 0}, {0x7000, 0}};
    for (int k = 0; k < 4; k++) {
        copy_tree(&home, &after);
        home_changes(&home, HOME_ALL & ~(unsigned)HOME_VIEW, s);
        put_le32(home.mng + BM_VIEW, VIEWS[k][0]);
        put_le32(home.mng + BM_VIEW + 4, VIEWS[k][1]);
        copy_tree(&t, &home);
        CHECK_EQ(badge_remove_last(t.data, t.data_size, t.mng, t.mng_size, &m, &undo, 1, 1, &rr), BADGE_OK);
        CHECK_EQ(rr.view_reset, k == 3);
        CHECK_EQ(get_le32(t.mng + BM_VIEW), k == 3 ? 2 : VIEWS[k][0]);
        CHECK_EQ(get_le32(t.mng + BM_VIEW + 4), k == 3 ? 0 : VIEWS[k][1]);
        expect_removed(&home, &orig, s, u, n, &want);
        CHECK(same_tree(&t, &want));
    }

    /* set record + 0x28 is HOME's as a whole u32 on the set's own record; the record goes whole */
    copy_tree(&home, &after);
    put_le32(home.mng + BM_SET_INFO + s * BM_SET_INFO_SIZE + 0x28, 0x01020304);
    copy_tree(&t, &home);
    CHECK_EQ(badge_remove_last(t.data, t.data_size, t.mng, t.mng_size, &m, &undo, 1, 1, &rr), BADGE_OK);
    CHECK(!memcmp(t.mng + BM_SET_INFO + s * BM_SET_INFO_SIZE, orig.mng + BM_SET_INFO + s * BM_SET_INFO_SIZE,
                  BM_SET_INFO_SIZE));

    /* with all of HOME's changes, one byte the application wrote changed: refused, nothing written */
    static const struct {
        int file;
        uint32_t offset;
    } OWN[] = {
        {REG_MNG, BM_SET_INFO + 3 * BM_SET_INFO_SIZE + 0x2C},  /* the set record's last word */
        {REG_MNG, BM_SET_INFO + 3 * BM_SET_INFO_SIZE + 0x0C},
        {REG_MNG, BM_SET_INFO + 3 * BM_SET_INFO_SIZE + 0x00},
        {REG_MNG, BM_BADGE_INFO + 13 * BM_BADGE_INFO_SIZE + 0x00},
        {REG_MNG, BM_BADGE_INFO + 13 * BM_BADGE_INFO_SIZE + 0x14},     /* a badge's flags */
        {REG_MNG, BM_BADGE_INFO + 14 * BM_BADGE_INFO_SIZE + 0x18},     /* its shortcut */
        {REG_DATA, BD_SET_TITLES + 3 * BD_TITLE_BLOCK + 2},             /* the set's name */
        {REG_DATA, BD_BADGE_TITLES + 12 * BD_TITLE_BLOCK + 0x8A},
        {REG_DATA, BD_SET_ICONS + 3 * BD_SET_ICON_SIZE + 9},
        {REG_DATA, BD_BADGE_ICONS + 14 * BD_BADGE_ICON_SIZE + 0x27FF},  /* a badge image */
        {REG_DATA, BD_SMALL_ICONS + 12 * BD_SMALL_ICON_SIZE},
    };
    for (size_t k = 0; k < sizeof OWN / sizeof *OWN; k++) {
        copy_tree(&t, &after);
        home_changes(&t, HOME_ALL, s);
        (OWN[k].file == REG_DATA ? t.data : t.mng)[OWN[k].offset] ^= 0x21;
        CHECK_EQ(both_refuse(&t, &m, &undo, 1, 1), BADGE_E_SET_CHANGED);
    }
    /* a badge of the set placed (its own count, or a layout record naming it) */
    copy_tree(&t, &after);
    home_changes(&t, HOME_ALL, s);
    put_le16(t.mng + BM_BADGE_INFO + 12 * BM_BADGE_INFO_SIZE + 0x10, 1);
    CHECK_EQ(both_refuse(&t, &m, &undo, 1, 1), BADGE_E_PLACED);
    copy_tree(&t, &after);
    home_changes(&t, HOME_ALL, s);
    memcpy(t.mng + BM_LAYOUT + 1 * BM_LAYOUT_RECORD, t.mng + BM_BADGE_INFO + 14 * BM_BADGE_INFO_SIZE, 16);
    CHECK_EQ(both_refuse(&t, &m, &undo, 1, 1), BADGE_E_PLACED);
    /* the rest of the refusals, on files HOME changed */
    copy_tree(&t, &after);
    home_changes(&t, HOME_ALL, s);
    CHECK_EQ(both_refuse(&t, &m, &undo, 1, 0), BADGE_E_CHANGED);
    CHECK_EQ(both_refuse(&t, NULL, &undo, 1, 1), BADGE_E_NOT_OURS);
    CHECK_EQ(both_refuse(&t, &m, NULL, 0, 1), BADGE_E_NO_UNDO);
    badge_undo twice[2] = {undo, undo};
    CHECK_EQ(both_refuse(&t, &m, twice, 2, 1), BADGE_E_AMBIGUOUS);
    uint8_t *damaged = malloc(undo.size);
    memcpy(damaged, undo.data, undo.size);
    damaged[0xE8 + 5] ^= 1;                                     /* an earlier byte it would put back */
    badge_undo bad = {damaged, undo.size};
    CHECK_EQ(both_refuse(&t, &m, &bad, 1, 1), BADGE_E_UNDO);
    memcpy(damaged, undo.data, undo.size);
    put_le32(damaged + 0x0C, res.set_id + 1);                   /* a record of another set */
    CHECK_EQ(both_refuse(&t, &m, &bad, 1, 1), BADGE_E_NO_UNDO);
    free(damaged);
    put_le32(t.mng + BM_SET_INFO + 3 * BM_SET_INFO_SIZE + 0x1C, 2); /* a count that does not add up: not understood */
    CHECK_EQ(both_refuse(&t, &m, &undo, 1, 1), BADGE_E_FORMAT);
    /* a set numbered other than its slot: whether the view means the slot or the number is not known */
    copy_tree(&t, &after);
    home_changes(&t, HOME_ALL, s);
    put_le32(t.mng + BM_SET_INFO + 1 * BM_SET_INFO_SIZE + 0x14, 7);
    CHECK_EQ(both_refuse(&t, &m, &undo, 1, 1), BADGE_E_FORMAT);
    copy_tree(&t, &after);
    put_le32(t.mng + BM_SET_INFO + s * BM_SET_INFO_SIZE + 0x14, 5);   /* the set itself */
    put_le32(t.mng + BM_VIEW, 5);
    put_le32(t.mng + BM_VIEW + 4, 7);
    CHECK_EQ(both_refuse(&t, &m, &undo, 1, 1), BADGE_E_FORMAT);

    /* the check with every allocation failing in turn: NOMEM or done, the buffers as they were */
    copy_tree(&home, &after);
    home_changes(&home, HOME_ALL, s);
    for (fail_alloc_at = 0;; fail_alloc_at++) {
        allocs = 0;
        copy_tree(&t, &home);
        int rc = badge_remove_check(t.data, t.data_size, t.mng, t.mng_size, &m, &undo, 1, 1, &rr);
        CHECK(rc == BADGE_OK || rc == BADGE_E_NOMEM);
        CHECK(same_tree(&t, &home));
        if (rc != BADGE_E_NOMEM || fail_alloc_at >= ALLOC_TRIES)
            break;
    }
    CHECK(fail_alloc_at < ALLOC_TRIES);
    fail_alloc_at = (size_t)-1;
    CHECK_EQ(badge_remove_check(t.data, t.data_size - 1, t.mng, t.mng_size, &m, &undo, 1, 1, &rr), BADGE_E_SIZE);
    CHECK_EQ(badge_remove_check(NULL, t.data_size, t.mng, t.mng_size, &m, &undo, 1, 1, &rr), BADGE_E_ARG);
    CHECK_EQ(badge_remove_check(t.data, t.data_size, t.mng, t.mng_size, &m, &undo, 1, 1, NULL), BADGE_E_ARG);

    badge_append_free(&res);
    free_files(&f);
    tree *all[5] = {&orig, &t, &after, &home, &want};
    for (int i = 0; i < 5; i++) {
        free(all[i]->data);
        free(all[i]->mng);
    }
    CHECK_EQ(live, 0);
}

static int unit(void)
{
    tp_set_allocator(t_alloc, t_free, NULL);
    test_names();
    test_regions_and_check();
    test_png();
    test_folder_rules();
    test_zip();
    test_manifest();
    test_append_remove();
    test_remove_after_home();
    CHECK(!strcmp(badge_result_name(BADGE_E_ZIP), "BADGE_E_ZIP"));
    CHECK(!strcmp(badge_result_name(99), "BADGE_E_UNKNOWN"));
    printf("test_badge: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: test_badge unit | list | sets | append | remove-last | check-slots | png | files | "
                        "order | points (see the top of host/test_badge.c)\n");
        return 2;
    }
    const char *cmd = argv[1];
    if (!strcmp(cmd, "unit"))
        return unit();
    if (!strcmp(cmd, "list") || !strcmp(cmd, "sets"))
        return cmd_list(argc, argv);
    if (!strcmp(cmd, "append"))
        return cmd_append(argc, argv);
    if (!strcmp(cmd, "remove-last"))
        return cmd_remove(argc, argv);
    if (!strcmp(cmd, "check-slots"))
        return cmd_check_slots(argc, argv);
    if (!strcmp(cmd, "png"))
        return cmd_png(argc, argv);
    if (!strcmp(cmd, "files"))
        return cmd_files(argc, argv);
    if (!strcmp(cmd, "order"))
        return cmd_order(argc, argv);
    if (!strcmp(cmd, "points"))
        return cmd_points();
    fprintf(stderr, "unknown command %s\n", cmd);
    return 2;
}
