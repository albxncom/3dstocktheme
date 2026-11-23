/* tpack: the core on the Mac, for the tests and for trying a pack without the console application.
 *
 *   tpack theme ZIP...                      what the core reads from each zip, one JSON line each; exit 1 if any fails
 *   tpack pack  --themes DIR --next N --certs-from CIA --cia OUT --result JSON ENTRY...
 *   tpack delta --themes DIR --installed-tmd TMD --next N --dir OUT --result JSON ENTRY...
 *   tpack installed ARCHIVE [ICONS]          an installed content 0: ContentInfoArchive_EUR_<lang>.bin and the folder
 *                                           with <index>.icn, one JSON line per index
 *   tpack sha256 FILE...
 *   tpack cia FILE...                       what the core reads from each theme installer CIA (kind, the zip's
 *                                           place, config.ini's values, then the zip as tp_theme_open reads it),
 *                                           one JSON line each; exit 1 if any fails
 *   tpack nobgm ZIP OUT [BODY]              the zip's theme without its music (tp_theme_drop_bgm) as content 1 in
 *                                           OUT, one JSON line; exit 1 if the core refuses. BODY: the new
 *                                           body_LZ.bin, when the body was encoded again (for a zip the Mac's
 *                                           adopt can match the content with)
 *
 * ENTRY is INDEX=ZIP (the zip keeps that index; reserved if DIR has no such zip) or ZIP (a new theme: the next
 * index by the core's rule, new zips in name order). --next is the manifest's next_index (one above the highest
 * index handed out). The title is always 0004008C00009809, the only one the core accepts. pack writes a CIA (its
 * certificate chain copied from an existing CIA, so this repo holds none); delta writes OUT/tmd.bin, 0.app,
 * <index>.app and delta.json as theme_pack.py delta does. --result gets the themes as the core read them and the
 * peak memory, for the test driver to merge into manifest.json.
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "sha256.h"
#include "tp.h"

/* ---------------------------------------------------------------- memory accounting */

static struct {
    size_t now, peak;
} mem;

static void *count_alloc(void *ctx, size_t size)
{
    (void)ctx;
    size_t *p = malloc(size + 16);
    if (!p)
        return NULL;
    *p = size;
    mem.now += size;
    if (mem.now > mem.peak)
        mem.peak = mem.now;
    return (char *)p + 16;
}

static void count_free(void *ctx, void *ptr)
{
    (void)ctx;
    size_t *p = (size_t *)((char *)ptr - 16);
    mem.now -= *p;
    free(p);
}

static size_t call_peak;        /* the most one content call needed above what was allocated before it */

static void call_begin(size_t *base)
{
    *base = mem.now;
    mem.peak = mem.now;
}

static void call_end(size_t base)
{
    if (mem.peak - base > call_peak)
        call_peak = mem.peak - base;
}

/* ---------------------------------------------------------------- files */

static void die(const char *what, const char *detail)
{
    fprintf(stderr, "tpack: %s%s%s\n", what, detail ? ": " : "", detail ? detail : "");
    exit(2);
}

static int file_read(void *ctx, uint64_t offset, void *buf, size_t len)
{
    FILE *f = ctx;
    if (fseeko(f, (off_t)offset, SEEK_SET) || fread(buf, 1, len, f) != len)
        return -1;
    return 0;
}

static int file_write(void *ctx, const void *buf, size_t len)
{
    return fwrite(buf, 1, len, ctx) == len ? 0 : -1;
}

static int open_source(const char *path, tp_source *src)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return -1;
    if (fseeko(f, 0, SEEK_END)) {
        fclose(f);
        return -1;
    }
    src->ctx = f;
    src->size = (uint64_t)ftello(f);
    src->read = file_read;
    return 0;
}

static uint8_t *slurp(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        die("cannot open", path);
    fseeko(f, 0, SEEK_END);
    *size = (size_t)ftello(f);
    fseeko(f, 0, SEEK_SET);
    uint8_t *p = malloc(*size ? *size : 1);
    if (!p || fread(p, 1, *size, f) != *size)
        die("cannot read", path);
    fclose(f);
    return p;
}

struct mem_sink {
    uint8_t *p;
    size_t size, cap;
};

static int mem_write(void *ctx, const void *buf, size_t len)
{
    struct mem_sink *m = ctx;
    if (m->size + len > m->cap) {
        size_t cap = (m->size + len) * 2;
        uint8_t *p = realloc(m->p, cap);
        if (!p)
            return -1;
        m->p = p;
        m->cap = cap;
    }
    memcpy(m->p + m->size, buf, len);
    m->size += len;
    return 0;
}

static void put32(uint8_t *p, uint32_t v)
{
    for (int b = 0; b < 4; b++)
        p[b] = (uint8_t)(v >> (8 * b));
}

static void hex(const uint8_t *p, size_t n, char *out)
{
    for (size_t i = 0; i < n; i++)
        sprintf(out + 2 * i, "%02x", p[i]);
}

static void json_string(FILE *f, const char *s)
{
    fputc('"', f);
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\')
            fprintf(f, "\\%c", c);
        else if (c < 0x20)
            fprintf(f, "\\u%04x", c);
        else
            fputc(c, f);
    }
    fputc('"', f);
}

static void today(char out[11])
{
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    strftime(out, 11, "%Y-%m-%d", &tm);
}

/* ---------------------------------------------------------------- themes and entries */

typedef struct entry {
    char *zip;                  /* file name in the themes dir */
    uint16_t index;
    int fixed, present;
    tp_theme theme;
    tp_source src;
} entry;

static void stem(const char *zip, char *out, size_t size)
{
    snprintf(out, size, "%s", zip);
    char *dot = strrchr(out, '.');
    if (dot && dot != out)
        *dot = 0;
}

static int open_entry(const char *dir, entry *e)
{
    char path[4096], fallback[1024];
    snprintf(path, sizeof path, "%s/%s", dir, e->zip);
    if (open_source(path, &e->src))
        return TP_OK;           /* not there: a reserved index */
    e->present = 1;
    stem(e->zip, fallback, sizeof fallback);
    return tp_theme_open(&e->theme, &e->src, fallback);
}

static void close_entry(entry *e)
{
    if (e->present) {
        tp_theme_close(&e->theme);
        fclose(e->src.ctx);
    }
}

static void fail(int r, const char *what)
{
    fprintf(stderr, "tpack: %s: %s\n", what, tp_result_text(r));
    printf("%s\n", tp_result_name(r));
    exit(1);
}

static int by_name(const void *a, const void *b) { return strcmp(((const entry *)a)->zip, ((const entry *)b)->zip); }
static int by_index(const void *a, const void *b) { return ((const entry *)a)->index - ((const entry *)b)->index; }

/* Fixed entries keep their index; new ones get the core's next index after everything in used[], the fixed
 * ones and the manifest's mark (next_index, one above the highest index handed out). Indices out of range are
 * left for the core to refuse. */
static void assign(entry *e, size_t n, uint16_t *used, size_t n_used, uint16_t *next_index)
{
    uint32_t mark = *next_index;
    uint16_t top = *next_index ? (uint16_t)(*next_index - 1) : 0;
    qsort(e, n, sizeof *e, by_name);
    for (size_t i = 0; i < n; i++)
        if (e[i].fixed)
            used[n_used++] = e[i].index;
    for (size_t i = 0; i < n; i++) {
        if (e[i].fixed)
            continue;
        int r = tp_next_index(used, n_used, top, &e[i].index);
        if (r)
            fail(r, e[i].zip);
        used[n_used++] = e[i].index;
    }
    for (size_t i = 0; i < n_used; i++)
        if ((uint32_t)used[i] + 1 > mark)
            mark = (uint32_t)used[i] + 1;
    *next_index = mark > 0xFFFF ? 0xFFFF : (uint16_t)mark;
    qsort(e, n, sizeof *e, by_index);
}

static int theme_icon(void *ctx, uint16_t index, uint8_t icon[TP_ICON_SIZE])
{
    struct { entry *e; size_t n; } *themes = ctx;
    for (size_t i = 0; i < themes->n; i++)
        if (themes->e[i].index == index && themes->e[i].present) {
            memcpy(icon, themes->e[i].theme.icon, TP_ICON_SIZE);
            return 0;
        }
    return -1;
}

static void write_result(const char *path, uint64_t title_id, uint16_t next, const entry *e, size_t n)
{
    FILE *f = fopen(path, "w");
    if (!f)
        die("cannot write", path);
    fprintf(f, "{\"title_id\": \"%016llX\", \"next_index\": %u, \"peak_bytes\": %zu, \"call_peak_bytes\": %zu,\n"
               " \"themes\": [", (unsigned long long)title_id, next, mem.peak, call_peak);
    for (size_t i = 0; i < n; i++) {
        fprintf(f, "%s\n  {\"index\": %u, \"zip\": ", i ? "," : "", e[i].index);
        json_string(f, e[i].zip);
        fprintf(f, ", \"present\": %s, \"new\": %s", e[i].present ? "true" : "false", e[i].fixed ? "false" : "true");
        if (e[i].present) {
            fprintf(f, ", \"name\": ");
            json_string(f, e[i].theme.name);
            fprintf(f, ", \"description\": ");
            json_string(f, e[i].theme.description);
            fprintf(f, ", \"warnings\": %u", e[i].theme.warnings);
        }
        fputc('}', f);
    }
    fprintf(f, "]}\n");
    fclose(f);
}

static void content_json(FILE *f, const tp_content *c)
{
    char sha[65];
    hex(c->sha256, 32, sha);
    fprintf(f, "\"index\": %u, \"id\": \"%08X\", \"type\": \"%04X\", \"size\": %llu, \"sha256\": \"%s\"", c->index,
            c->id, c->type, (unsigned long long)c->size, sha);
}

/* Writes one content and checks it against its measured record. */
static void emit(const char *what, const tp_theme *theme, const tp_catalogue *cat, uint64_t title_id,
                 const tp_content *measured, const tp_sink *sink)
{
    tp_content got;
    size_t base;
    call_begin(&base);
    int r = theme ? tp_theme_content(theme, title_id, measured->index, sink, &got)
                  : tp_catalogue_content(cat, title_id, measured->id, sink, &got);
    call_end(base);
    if (r)
        fail(r, what);
    if (got.id != measured->id || got.index != measured->index || got.type != measured->type ||
        got.size != measured->size || memcmp(got.sha256, measured->sha256, 32))
        die("written content differs from its measurement", what);
}

/* ---------------------------------------------------------------- commands */

static entry *parse_entries(char **argv, int argc, size_t *n)
{
    entry *e = calloc((size_t)argc + 1, sizeof *e);
    for (int i = 0; i < argc; i++) {
        char *eq = strchr(argv[i], '=');
        if (eq) {
            char *end;
            long v = strtol(argv[i], &end, 10);
            if (end != eq || v < 0 || v > 0xFFFF)
                die("bad entry", argv[i]);
            e[i].index = (uint16_t)v;
            e[i].fixed = 1;
            e[i].zip = eq + 1;
        } else {
            e[i].zip = argv[i];
        }
    }
    if (argc > TP_INDEX_MAX)
        die("more entries than content indices", NULL);
    *n = (size_t)argc;
    return e;
}

static const char *opt(char **argv, int argc, int *i, const char *name)
{
    if (strcmp(argv[*i], name))
        return NULL;
    if (*i + 1 >= argc)
        die("missing value for", name);
    return argv[++*i];
}

static int cmd_theme(int argc, char **argv)
{
    int failed = 0;
    for (int i = 0; i < argc; i++) {
        tp_source src;
        tp_theme t;
        tp_content rec = {0};
        char fallback[1024], sha[65] = "";
        if (open_source(argv[i], &src))
            die("cannot open", argv[i]);
        const char *base = strrchr(argv[i], '/');
        stem(base ? base + 1 : argv[i], fallback, sizeof fallback);
        int r = tp_theme_open(&t, &src, fallback);
        if (!r) {
            r = tp_theme_content(&t, TP_TITLE_ID, 1, NULL, &rec);
            hex(rec.sha256, 32, sha);
        }
        printf("{\"zip\": ");
        json_string(stdout, argv[i]);
        printf(", \"result\": \"%s\"", tp_result_name(r));
        if (!r) {
            char icon[65];
            uint8_t d[32];
            sha256(t.icon, TP_ICON_SIZE, d);
            hex(d, 32, icon);
            printf(", \"name\": ");
            json_string(stdout, t.name);
            printf(", \"description\": ");
            json_string(stdout, t.description);
            printf(", \"body\": %u, \"bgm\": ", t.body_size);
            if (t.has_bgm)
                printf("%u", t.bgm_size);
            else
                printf("null");
            printf(", \"use_bgm\": %s, \"warnings\": %u, \"icon_sha256\": \"%s\", \"content_size\": %llu, "
                   "\"content_sha256\": \"%s\"", t.use_bgm ? "true" : "false", t.warnings, icon,
                   (unsigned long long)rec.size, sha);
        }
        printf("}\n");
        failed |= r != TP_OK;
        tp_theme_close(&t);
        fclose(src.ctx);
    }
    if (mem.now)
        die("core memory not released after theme", NULL);
    return failed;
}

static int cmd_pack(int argc, char **argv)
{
    const char *themes = NULL, *certs_from = NULL, *cia = NULL, *result = NULL, *v;
    int i;
    uint16_t next = 1;
    for (i = 0; i < argc && argv[i][0] == '-'; i++) {
        if ((v = opt(argv, argc, &i, "--themes"))) themes = v;
        else if ((v = opt(argv, argc, &i, "--next"))) next = (uint16_t)atoi(v);
        else if ((v = opt(argv, argc, &i, "--certs-from"))) certs_from = v;
        else if ((v = opt(argv, argc, &i, "--cia"))) cia = v;
        else if ((v = opt(argv, argc, &i, "--result"))) result = v;
        else die("unknown option", argv[i]);
    }
    if (!themes || !certs_from || !cia || !result || i == argc)
        die("pack needs --themes, --certs-from, --cia, --result and entries", NULL);
    uint64_t title_id = TP_TITLE_ID;
    int r;
    size_t n;
    entry *e = parse_entries(argv + i, argc - i, &n);
    uint16_t used[2 * TP_INDEX_MAX + 2];
    assign(e, n, used, 0, &next);

    tp_entry *cat_entries = calloc(n, sizeof *cat_entries);
    size_t count = 0;
    for (size_t k = 0; k < n; k++) {
        if ((r = open_entry(themes, &e[k])) != TP_OK)
            fail(r, e[k].zip);
        if (e[k].present)
            cat_entries[count++] = (tp_entry){e[k].index, e[k].theme.name, e[k].theme.description};
    }
    struct { entry *e; size_t n; } icon_ctx = {e, n};
    tp_catalogue cat = {cat_entries, count, e[n - 1].index, theme_icon, &icon_ctx};

    /* Measure, then ticket and TMD, then write: the order AM needs the pieces in. */
    tp_content *recs = calloc(count + 1, sizeof *recs);
    size_t base;
    call_begin(&base);
    if ((r = tp_catalogue_content(&cat, title_id, 0, NULL, &recs[0])) != TP_OK)
        fail(r, "content 0");
    call_end(base);
    for (size_t k = 0, c = 1; k < n; k++)
        if (e[k].present) {
            call_begin(&base);
            if ((r = tp_theme_content(&e[k].theme, title_id, e[k].index, NULL, &recs[c++])) != TP_OK)
                fail(r, e[k].zip);
            call_end(base);
        }
    struct mem_sink tmd = {0}, tik = {0};
    tp_sink tmd_sink = {&tmd, mem_write}, tik_sink = {&tik, mem_write};
    if ((r = tp_tmd(title_id, 0, recs, count + 1, &tmd_sink)) != TP_OK)
        fail(r, "TMD");
    uint8_t digest[32];
    sha256(tmd.p, tmd.size, digest);    /* ticket ID and title key: reproducible, different per pack */
    uint64_t ticket_id = 0x0004000000000000ull;
    for (int k = 0; k < 6; k++)
        ticket_id |= (uint64_t)digest[k] << (8 * (5 - k));
    if ((r = tp_ticket(title_id, ticket_id, digest + 16, &tik_sink)) != TP_OK)
        fail(r, "ticket");

    size_t cia_size;
    uint8_t *source_cia = slurp(certs_from, &cia_size);
    uint32_t cert_size = cia_size >= 0x2020 ? (uint32_t)(source_cia[8] | source_cia[9] << 8 | source_cia[10] << 16 |
                                                          (uint32_t)source_cia[11] << 24) : 0;
    if (!cert_size || 0x2040 + (size_t)cert_size > cia_size)
        die("no certificate chain in", certs_from);

    uint8_t header[0x2020] = {0};
    uint64_t content_size = 0;
    for (size_t k = 0; k <= count; k++) {
        content_size += (recs[k].size + 0xF) & ~0xFull;
        header[0x20 + recs[k].index / 8] |= (uint8_t)(0x80 >> (recs[k].index & 7));
    }
    put32(header, 0x2020);                          /* header size; type and version 0 */
    put32(header + 0x08, cert_size);
    put32(header + 0x0C, (uint32_t)tik.size);
    put32(header + 0x10, (uint32_t)tmd.size);
    put32(header + 0x18, (uint32_t)content_size);   /* meta size 0, content size as a 64-bit value */
    put32(header + 0x1C, (uint32_t)(content_size >> 32));

    FILE *out = fopen(cia, "wb");
    if (!out)
        die("cannot write", cia);
    tp_sink sink = {out, file_write};
    static const uint8_t zeros[64];
    long at = 0;
#define PUT(p, len) do { if (fwrite((p), 1, (len), out) != (size_t)(len)) die("write failed", cia); at += (long)(len); } while (0)
#define ALIGN64() PUT(zeros, (64 - at % 64) % 64)
    PUT(header, sizeof header);
    ALIGN64();
    PUT(source_cia + 0x2040, cert_size);
    ALIGN64();
    PUT(tik.p, tik.size);
    ALIGN64();
    PUT(tmd.p, tmd.size);
    ALIGN64();
    fflush(out);
    emit("content 0", NULL, &cat, title_id, &recs[0], &sink);
    for (size_t k = 0, c = 1; k < n; k++)
        if (e[k].present)
            emit(e[k].zip, &e[k].theme, NULL, title_id, &recs[c++], &sink);
    if (fclose(out))
        die("write failed", cia);

    for (size_t k = 0; k < n; k++)
        printf("%4u  %s\n", e[k].index, e[k].present ? e[k].theme.name : "(reserved)");
    printf("wrote %s: %zu themes, title %016llX, peak %zu bytes in the core, %zu for one content call\n", cia,
           count, (unsigned long long)title_id, mem.peak, call_peak);
    write_result(result, title_id, next, e, n);
    for (size_t k = 0; k < n; k++)
        close_entry(&e[k]);
    if (mem.now)
        die("core memory not released", NULL);
    free(source_cia);
    free(tmd.p);
    free(tik.p);
    free(recs);
    free(cat_entries);
    free(e);
    return 0;
}

static void write_file(const char *dir, const char *name, const uint8_t *p, size_t size)
{
    char path[4096];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "wb");
    if (!f || fwrite(p, 1, size, f) != size || fclose(f))
        die("cannot write", path);
}

static int cmd_delta(int argc, char **argv)
{
    const char *themes = NULL, *installed = NULL, *dir = NULL, *result = NULL, *v;
    uint16_t next = 1;
    int i;
    for (i = 0; i < argc && argv[i][0] == '-'; i++) {
        if ((v = opt(argv, argc, &i, "--themes"))) themes = v;
        else if ((v = opt(argv, argc, &i, "--installed-tmd"))) installed = v;
        else if ((v = opt(argv, argc, &i, "--next"))) next = (uint16_t)atoi(v);
        else if ((v = opt(argv, argc, &i, "--dir"))) dir = v;
        else if ((v = opt(argv, argc, &i, "--result"))) result = v;
        else die("unknown option", argv[i]);
    }
    if (!themes || !installed || !dir || !result || i == argc)
        die("delta needs --themes, --installed-tmd, --dir, --result and entries", NULL);
    size_t tmd_size, installed_count;
    uint8_t *tmd = slurp(installed, &tmd_size);
    uint64_t title_id;
    uint16_t version;
    int r = tp_tmd_read(tmd, tmd_size, &title_id, &version, &installed_count);
    if (r)
        fail(r, installed);
    uint16_t used[2 * TP_INDEX_MAX + 2];
    size_t n_used = 0;
    for (size_t k = 0; k < installed_count; k++) {
        tp_content c;
        tp_tmd_record(tmd, tmd_size, k, &c);
        used[n_used++] = c.index;
    }
    size_t n;
    entry *e = parse_entries(argv + i, argc - i, &n);
    assign(e, n, used, n_used, &next);

    tp_entry *cat_entries = calloc(n, sizeof *cat_entries);
    size_t count = 0;
    for (size_t k = 0; k < n; k++) {
        if ((r = open_entry(themes, &e[k])) != TP_OK)
            fail(r, e[k].zip);
        if (e[k].present)
            cat_entries[count++] = (tp_entry){e[k].index, e[k].theme.name, e[k].theme.description};
    }
    struct { entry *e; size_t n; } icon_ctx = {e, n};
    tp_catalogue cat = {cat_entries, count, e[n - 1].index, theme_icon, &icon_ctx};
    tp_delta plan;
    if ((r = tp_delta_plan(tmd, tmd_size, title_id, &cat, &plan)) != TP_OK)
        fail(r, "delta");

    tp_content content0, *added = calloc(plan.count + 1, sizeof *added);
    entry **add = calloc(plan.count + 1, sizeof *add);
    size_t base;
    call_begin(&base);
    if ((r = tp_catalogue_content(&cat, title_id, plan.content0_id, NULL, &content0)) != TP_OK)
        fail(r, "content 0");
    call_end(base);
    for (size_t a = 0; a < plan.count; a++) {
        for (size_t k = 0; k < n; k++)
            if (e[k].index == plan.add[a])
                add[a] = &e[k];
        call_begin(&base);
        if ((r = tp_theme_content(&add[a]->theme, title_id, plan.add[a], NULL, &added[a])) != TP_OK)
            fail(r, add[a]->zip);
        call_end(base);
    }
    struct mem_sink new_tmd = {0};
    tp_sink tmd_sink = {&new_tmd, mem_write};
    if ((r = tp_delta_tmd(tmd, tmd_size, &plan, &content0, added, &tmd_sink)) != TP_OK)
        fail(r, "delta TMD");
    if (new_tmd.size != tp_delta_tmd_size(&plan, installed_count))
        die("delta TMD size", NULL);

    if (mkdir(dir, 0755) && errno != EEXIST)
        die("cannot create", dir);
    write_file(dir, "tmd.bin", new_tmd.p, new_tmd.size);
    char path[4096], name[32];
    for (size_t a = 0; a <= plan.count; a++) {
        snprintf(name, sizeof name, "%u.app", a ? plan.add[a - 1] : 0);
        snprintf(path, sizeof path, "%s/%s", dir, name);
        FILE *f = fopen(path, "wb");
        if (!f)
            die("cannot write", path);
        tp_sink sink = {f, file_write};
        if (a)
            emit(add[a - 1]->zip, &add[a - 1]->theme, NULL, title_id, &added[a - 1], &sink);
        else
            emit("content 0", NULL, &cat, title_id, &content0, &sink);
        if (fclose(f))
            die("write failed", path);
    }

    uint8_t digest[32];
    char base_sha[65], new_sha[65], date[11];
    sha256(tmd, tmd_size, digest);
    hex(digest, 32, base_sha);
    sha256(new_tmd.p, new_tmd.size, digest);
    hex(digest, 32, new_sha);
    today(date);
    snprintf(path, sizeof path, "%s/delta.json", dir);
    FILE *f = fopen(path, "w");
    if (!f)
        die("cannot write", path);
    fprintf(f, "{\n  \"title_id\": \"%016llX\",\n  \"made\": \"%s\",\n  \"base_title_version\": %u,\n"
               "  \"base_tmd_sha256\": \"%s\",\n  \"title_version\": %u,\n  \"tmd_sha256\": \"%s\",\n  \"import\": [",
            (unsigned long long)title_id, date, version, base_sha, plan.version, new_sha);
    for (size_t a = 0; a <= plan.count; a++) {
        fprintf(f, "%s\n    {", a ? "," : "");
        content_json(f, a ? &added[a - 1] : &content0);
        fprintf(f, ", \"file\": \"%u.app\", \"name\": ", a ? plan.add[a - 1] : 0);
        json_string(f, a ? add[a - 1]->theme.name : "catalogue (content 0)");
        fputc('}', f);
    }
    fprintf(f, "\n  ],\n  \"kept\": [");
    for (size_t k = 1, first = 1; k < installed_count; k++, first = 0) {
        tp_content c;
        tp_tmd_record(tmd, tmd_size, k, &c);
        fprintf(f, "%s%u", first ? "" : ", ", c.index);
    }
    fprintf(f, "]\n}\n");
    fclose(f);

    printf("   0  import  catalogue, content ID %08X\n", plan.content0_id);
    for (size_t k = 0; k < n; k++) {
        int imported = 0;
        for (size_t a = 0; a < plan.count; a++)
            imported |= plan.add[a] == e[k].index;
        printf("%4u  %s  %s\n", e[k].index, imported ? "import" : e[k].present ? "kept  " : "      ",
               e[k].present ? e[k].theme.name : "(reserved)");
    }
    printf("wrote %s: title %016llX version %u -> %u, %zu new, %zu kept, peak %zu bytes in the core, %zu for one "
           "content call\n", dir, (unsigned long long)title_id, version, plan.version, plan.count, plan.kept,
           mem.peak, call_peak);
    write_result(result, title_id, next, e, n);
    for (size_t k = 0; k < n; k++)
        close_entry(&e[k]);
    if (mem.now)
        die("core memory not released", NULL);
    free(tmd);
    free(new_tmd.p);
    free(added);
    free(add);
    free(cat_entries);
    free(e);
    return 0;
}

static int cmd_installed(int argc, char **argv)
{
    if (argc < 1 || argc > 2)
        die("installed needs ARCHIVE [ICONS]", NULL);
    tp_source archive;
    uint16_t top;
    if (open_source(argv[0], &archive))
        die("cannot open", argv[0]);
    int r = tp_installed_top(&archive, &top);
    if (r)
        fail(r, argv[0]);
    for (uint16_t index = 0; index <= top; index++) {
        char path[4096], sha[65] = "";
        tp_source icon;
        tp_installed e;
        int has_file = 0;
        if (argc == 2 && index) {
            snprintf(path, sizeof path, "%s/%u.icn", argv[1], index);
            has_file = !open_source(path, &icon);
        }
        r = tp_installed_entry(&archive, has_file ? &icon : NULL, index, &e);
        if (has_file)
            fclose(icon.ctx);
        if (r)
            fail(r, argv[0]);
        printf("{\"index\": %u, \"enabled\": %s, \"name\": ", e.index, e.enabled ? "true" : "false");
        json_string(stdout, e.name);
        printf(", \"description\": ");
        json_string(stdout, e.description);
        if (e.has_icon) {
            uint8_t d[32];
            sha256(e.icon, TP_ICON_SIZE, d);
            hex(d, 32, sha);
            printf(", \"icon_sha256\": \"%s\"}\n", sha);
        } else {
            printf(", \"icon_sha256\": null}\n");
        }
    }
    fclose(archive.ctx);
    return 0;
}

static int cmd_cia(int argc, char **argv)
{
    int failed = 0;
    for (int i = 0; i < argc; i++) {
        tp_source src;
        if (open_source(argv[i], &src))
            die("cannot open", argv[i]);
        tp_package pkg;
        int kind = tp_kind(&src);
        int r = tp_cia_open(&src, 1, &pkg);
        printf("{\"kind\": %d, \"result\": \"%s\"", kind, tp_result_name(r));
        if (r == TP_OK) {
            printf(", \"title_id\": \"%016llX\", \"location\": ", (unsigned long long)pkg.title_id);
            json_string(stdout, pkg.location);
            printf(", \"destination\": ");
            json_string(stdout, pkg.destination);
            printf(", \"offset\": %llu, \"size\": %llu", (unsigned long long)pkg.offset,
                   (unsigned long long)pkg.size);
            tp_theme t;
            memset(&t, 0, sizeof t);
            int z = tp_kind(&pkg.zip) == TP_KIND_ZIP ? tp_theme_open(&t, &pkg.zip, pkg.location) : TP_E_ZIP;
            printf(", \"theme\": \"%s\"", tp_result_name(z));
            if (z == TP_OK) {
                printf(", \"name\": ");
                json_string(stdout, t.name);
                printf(", \"has_bgm\": %d, \"use_bgm\": %d", t.has_bgm, t.use_bgm);
            }
            tp_theme_close(&t);
            failed |= z != TP_OK;
        }
        printf("}\n");
        failed |= r != TP_OK;
        fclose(src.ctx);
    }
    if (mem.now)
        die("core memory not released", NULL);
    return failed;
}

static int cmd_sha256(int argc, char **argv)
{
    for (int i = 0; i < argc; i++) {
        size_t size;
        uint8_t *p = slurp(argv[i], &size), d[32];
        char out[65];
        sha256(p, size, d);
        hex(d, 32, out);
        printf("%s  %s\n", out, argv[i]);
        free(p);
    }
    return 0;
}

static int cmd_nobgm(int argc, char **argv)
{
    if (argc != 2 && argc != 3)
        die("usage: tpack nobgm ZIP OUT [BODY]", NULL);
    tp_source src;
    tp_theme t;
    tp_content rec = {0};
    char sha[65] = "";
    if (open_source(argv[0], &src))
        die("cannot open", argv[0]);
    size_t base;
    call_begin(&base);
    int r = tp_theme_open(&t, &src, "Theme");
    uint32_t stored = t.body_size;
    if (!r)
        r = tp_theme_drop_bgm(&t);
    call_end(base);
    if (!r) {
        FILE *out = fopen(argv[1], "wb");
        if (!out)
            die("cannot write", argv[1]);
        tp_sink sink = {out, file_write};
        r = tp_theme_content(&t, TP_TITLE_ID, 1, &sink, &rec);
        if (fclose(out))
            r = TP_E_WRITE;
        hex(rec.sha256, 32, sha);
    }
    if (!r && argc == 3 && t.body_mem) {
        FILE *body = fopen(argv[2], "wb");
        if (!body || fwrite(t.body_mem, 1, t.body_mem_size, body) != t.body_mem_size || fclose(body))
            die("cannot write", argv[2]);
    }
    printf("{\"zip\": ");
    json_string(stdout, argv[0]);
    printf(", \"result\": \"%s\"", tp_result_name(r));
    if (!r)
        printf(", \"body_before\": %u, \"body\": %u, \"has_bgm\": %s, \"use_bgm\": %s, \"warnings\": %u, "
               "\"content_size\": %llu, \"content_sha256\": \"%s\", \"peak\": %zu", stored, t.body_size,
               t.has_bgm ? "true" : "false", t.use_bgm ? "true" : "false", t.warnings,
               (unsigned long long)rec.size, sha, call_peak);
    printf("}\n");
    tp_theme_close(&t);
    fclose(src.ctx);
    if (mem.now)
        die("core memory not released after nobgm", NULL);
    return r != TP_OK;
}

int main(int argc, char **argv)
{
    tp_set_allocator(count_alloc, count_free, NULL);
    if (argc >= 2 && !strcmp(argv[1], "theme"))
        return cmd_theme(argc - 2, argv + 2);
    if (argc >= 2 && !strcmp(argv[1], "pack"))
        return cmd_pack(argc - 2, argv + 2);
    if (argc >= 2 && !strcmp(argv[1], "delta"))
        return cmd_delta(argc - 2, argv + 2);
    if (argc >= 2 && !strcmp(argv[1], "installed"))
        return cmd_installed(argc - 2, argv + 2);
    if (argc >= 2 && !strcmp(argv[1], "sha256"))
        return cmd_sha256(argc - 2, argv + 2);
    if (argc >= 2 && !strcmp(argv[1], "cia"))
        return cmd_cia(argc - 2, argv + 2);
    if (argc >= 2 && !strcmp(argv[1], "nobgm"))
        return cmd_nobgm(argc - 2, argv + 2);
    fprintf(stderr, "usage: tpack theme ZIP... | pack ... | delta ... | installed ARCHIVE [ICONS] | sha256 FILE... "
                    "| cia FILE... | nobgm ZIP OUT (see the comment in tpack.c)\n");
    return 2;
}
