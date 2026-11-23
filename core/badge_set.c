/* A set to append: the files of a folder in the order and with the names badge_sets.py read_set_folder gives
 * them, and a zip of PNG files read as the folder it would unpack to. */
#include "badge_internal.h"

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

/* Path(name).suffix.lower() == ".png" for a name not starting with "." */
static int is_png(const char *name)
{
    size_t len = strlen(name);
    if (len < 5 || name[len - 4] != '.')
        return 0;
    return lower(name[len - 3]) == 'p' && lower(name[len - 2]) == 'n' && lower(name[len - 1]) == 'g';
}

static int is_set_icon(const char *name)
{
    static const char icon[] = "_seticon.png";
    for (size_t i = 0; i < sizeof icon; i++)
        if (lower((uint8_t)name[i]) != icon[i])
            return 0;
    return 1;
}

void badge_folder_free(badge_folder *folder)
{
    if (!folder)
        return;
    tp_free(folder->tiles);
    memset(folder, 0, sizeof *folder);
    folder->icon = -1;
}

int badge_folder_read(const badge_file *files, size_t count, badge_folder *folder)
{
    if (!folder)
        return BADGE_E_ARG;
    memset(folder, 0, sizeof *folder);
    folder->icon = -1;
    if (count && !files)
        return BADGE_E_ARG;
    if (count > 0xFFFFFFFFu)
        return BADGE_E_ARG;
    for (size_t f = 0; f < count; f++) {
        if (!files[f].name || strchr(files[f].name, '/') || (!files[f].data && files[f].size))
            return BADGE_E_ARG;
        if (files[f].name[0] == '.')
            continue;               /* .DS_Store, AppleDouble ._ files */
        if (files[f].is_dir || !is_png(files[f].name))
            return BADGE_E_FOLDER;  /* only PNG files belong in a set folder */
    }
    size_t shown = 0, n;
    for (size_t f = 0; f < count; f++) {
        if (files[f].name[0] == '.')
            continue;
        if (name_points(files[f].name, strlen(files[f].name), NULL, 0, &n, NULL))
            return BADGE_E_NAME;
        shown++;
    }

    /* The order of names: insertion sort, the lists are short (Anemone reads 1024 entries at most) */
    uint32_t *order = tp_alloc(shown * sizeof *order);
    if (!order)
        return BADGE_E_NOMEM;
    size_t k = 0;
    int r = BADGE_OK;
    for (size_t f = 0; f < count; f++) {
        if (files[f].name[0] == '.')
            continue;
        size_t at = k++;
        while (at > 0 && name_order(files[order[at - 1]].name, files[f].name) > 0) {
            order[at] = order[at - 1];
            at--;
        }
        if (at > 0 && !strcmp(files[order[at - 1]].name, files[f].name))
            r = BADGE_E_FOLDER;     /* the same name twice */
        order[at] = (uint32_t)f;
    }
    size_t pngs = 0, tiles = 0;
    /* Every file named _seticon.png in any case is not a badge; the last of them in the order is the icon (a zip
     * or a case-sensitive card can hold several), as badge_sets.py reads a folder */
    for (k = 0; !r && k < shown; k++) {
        if (is_set_icon(files[order[k]].name))
            folder->icon = (long)order[k];
        else
            pngs++;
    }
    if (!r && !pngs)
        r = BADGE_E_FOLDER;         /* no badge PNG */

    /* Each badge PNG in order: decoded (then freed), its sides, its name */
    for (k = 0; !r && k < shown; k++) {
        const badge_file *file = &files[order[k]];
        if (is_set_icon(file->name))
            continue;
        uint32_t w, h;
        uint8_t *rgba;
        if ((r = badge_png_decode(file->data, file->size, &w, &h, &rgba)) != BADGE_OK)
            break;
        tp_free(rgba);
        if (w % B_TILE || h % B_TILE) {
            r = BADGE_E_IMAGE_SIZE;
            break;
        }
        const char *dot = strchr(file->name, '.');
        if ((r = name_check(file->name, (size_t)(dot - file->name))) != BADGE_OK)
            break;
        tiles += (size_t)(w / B_TILE) * (h / B_TILE);
    }
    if (!r && tiles > SIZE_MAX / sizeof *folder->tiles)
        r = BADGE_E_NOMEM;
    if (!r && !(folder->tiles = tp_alloc(tiles * sizeof *folder->tiles)))
        r = BADGE_E_NOMEM;
    for (k = 0; !r && k < shown; k++) {
        const badge_file *file = &files[order[k]];
        if (is_set_icon(file->name))
            continue;
        uint32_t w, h;
        uint8_t *rgba;
        if ((r = badge_png_decode(file->data, file->size, &w, &h, &rgba)) != BADGE_OK)
            break;              /* only an allocation can fail the second time */
        tp_free(rgba);
        uint32_t columns = w / B_TILE, rows = h / B_TILE;
        uint32_t name_len = (uint32_t)(strchr(file->name, '.') - file->name);
        for (uint32_t tx = 0; tx < columns; tx++)
            for (uint32_t ty = 0; ty < rows; ty++)
                folder->tiles[folder->count++] = (badge_tile){order[k], tx * rows + ty, tx * B_TILE, ty * B_TILE,
                                                              name_len};
    }
    if (!r && folder->icon >= 0) {
        uint32_t w, h;
        uint8_t *rgba;
        const badge_file *icon = &files[folder->icon];
        if ((r = badge_png_decode(icon->data, icon->size, &w, &h, &rgba)) == BADGE_OK) {
            tp_free(rgba);
            if (w != B_ICON || h != B_ICON)
                r = BADGE_E_IMAGE_SIZE;     /* a set icon is 48x48 */
        }
    }
    tp_free(order);
    if (r) {
        tp_free(folder->tiles);
        folder->tiles = NULL;
        folder->count = 0;
    }
    return r;
}

/* ---------------------------------------------------------------- a zip of PNG files
 * The central directory walk of zip.c (zip_find only looks for the three theme members), the member data through
 * zip.c's zip_stream (inflate, size and CRC-32 checked). The same refusals: ZIP64, split archives, encryption,
 * methods other than stored and deflate, sizes or offsets outside the archive. */

#define SIG_LOCAL    0x04034b50u
#define SIG_CENTRAL  0x02014b50u
#define SIG_END      0x06054b50u
#define END_SIZE     22u
#define CENTRAL_SIZE 46u
#define LOCAL_SIZE   30u
#define NAME_MAX_LEN 0x300u
#define MEMBER_MAX   (32u << 20)
#define FLAG_UTF8    0x800u

typedef struct zip_entry {
    char *path;
    uint16_t flags, method;
    uint32_t crc, csize, size, local;
} zip_entry;

static int zread(const tp_source *src, uint64_t offset, void *buf, size_t size)
{
    if (offset > src->size || size > src->size - offset)
        return BADGE_E_ZIP;         /* the zip points past its own end */
    return src->read(src->ctx, offset, buf, size) ? BADGE_E_ZIP : BADGE_OK;
}

/* The end record: the last one whose comment ends the file, else the last one (as zip_find). */
static int find_end(const tp_source *src, uint64_t *directory, uint64_t *end_at, uint16_t *entries)
{
    if (src->size < END_SIZE)
        return BADGE_E_ZIP;
    size_t tail = src->size < END_SIZE + 0xFFFFu ? (size_t)src->size : END_SIZE + 0xFFFFu;
    uint8_t *buf = tp_alloc(tail);
    if (!buf)
        return BADGE_E_NOMEM;
    int r = zread(src, src->size - tail, buf, tail);
    size_t at = tail, last = tail;
    for (size_t i = tail - END_SIZE + 1; !r && i-- > 0;)
        if (get_le32(buf + i) == SIG_END) {
            if (last == tail)
                last = i;
            if (i + END_SIZE + get_le16(buf + i + 20) == tail) {
                at = i;
                break;
            }
        }
    if (at == tail)
        at = last;
    uint8_t end[END_SIZE] = {0};
    if (!r && at < tail)
        memcpy(end, buf + at, END_SIZE);
    tp_free(buf);
    if (r)
        return r;
    if (at == tail)
        return BADGE_E_ZIP;
    *end_at = src->size - tail + at;
    uint16_t disk = get_le16(end + 4), directory_disk = get_le16(end + 6);
    uint16_t here = get_le16(end + 8);
    uint32_t directory_size = get_le32(end + 12);
    *entries = get_le16(end + 10);
    *directory = get_le32(end + 16);
    if (disk || directory_disk || here != *entries)
        return BADGE_E_ZIP;
    if (*entries == 0xFFFFu || directory_size == 0xFFFFFFFFu || *directory == 0xFFFFFFFFu)
        return BADGE_E_ZIP;         /* ZIP64 */
    if (*directory + directory_size != *end_at)
        return BADGE_E_ZIP;
    return BADGE_OK;
}

/* Where a member's data is, from its local header (as zip.c locate). */
static int member(const tp_source *src, const zip_entry *e, uint64_t directory, tp_member *m)
{
    size_t name_len = strlen(e->path);
    uint8_t h[LOCAL_SIZE + NAME_MAX_LEN];
    if (e->flags & 1)
        return BADGE_E_ZIP;         /* encrypted */
    if (e->method != 0 && e->method != 8)
        return BADGE_E_ZIP;
    if ((e->method == 0 && e->csize != e->size) || e->size > MEMBER_MAX)
        return BADGE_E_ZIP;
    if (e->csize == 0xFFFFFFFFu || e->size == 0xFFFFFFFFu || e->local == 0xFFFFFFFFu)
        return BADGE_E_ZIP;         /* ZIP64 */
    if ((uint64_t)e->local + LOCAL_SIZE + name_len > directory)
        return BADGE_E_ZIP;
    int r = zread(src, e->local, h, LOCAL_SIZE + name_len);
    if (r)
        return r;
    if (get_le32(h) != SIG_LOCAL || get_le16(h + 26) != name_len || memcmp(h + LOCAL_SIZE, e->path, name_len))
        return BADGE_E_ZIP;
    uint64_t data = (uint64_t)e->local + LOCAL_SIZE + name_len + get_le16(h + 28);
    if (data > directory || e->csize > directory - data)
        return BADGE_E_ZIP;         /* member data must end before the central directory */
    memset(m, 0, sizeof *m);
    m->offset = data;
    m->csize = e->csize;
    m->size = e->size;
    m->crc = e->crc;
    m->method = e->method;
    m->present = 1;
    return BADGE_OK;
}

struct fill {
    uint8_t *buf;
    size_t have;
};

static int fill_fn(void *ctx, const uint8_t *data, size_t size)
{
    struct fill *f = ctx;
    memcpy(f->buf + f->have, data, size);   /* zip_stream never gives more than the member's size */
    f->have += size;
    return 0;
}

void badge_files_free(badge_file *files, size_t count)
{
    if (!files)
        return;
    for (size_t i = 0; i < count; i++) {
        tp_free((void *)(uintptr_t)files[i].name);
        tp_free((void *)(uintptr_t)files[i].data);
    }
    tp_free(files);
}

static char *copy_of(const char *s, size_t len)
{
    char *c = tp_alloc(len + 1);
    if (c) {
        memcpy(c, s, len);
        c[len] = 0;
    }
    return c;
}


static int is_macos_metadata(const char *path) { return !strncmp(path, "__MACOSX/", 9); }

int badge_zip_files(const tp_source *zip, badge_file **files, size_t *count)
{
    if (!zip || !files || !count || !zip->read)
        return BADGE_E_ARG;
    *files = NULL;
    *count = 0;
    uint64_t directory, end_at;
    uint16_t entries;
    int r = find_end(zip, &directory, &end_at, &entries);
    if (r)
        return r;
    size_t slots = entries ? entries : 1;
    zip_entry *e = tp_alloc(slots * sizeof *e);
    badge_file *out = tp_alloc(slots * sizeof *out);
    if (!e || !out) {
        tp_free(e);
        tp_free(out);
        return BADGE_E_NOMEM;
    }
    memset(e, 0, slots * sizeof *e);
    memset(out, 0, slots * sizeof *out);

    /* The central directory */
    size_t n = 0;
    for (uint64_t pos = directory; pos < end_at;) {
        uint8_t h[CENTRAL_SIZE];
        char name[NAME_MAX_LEN];
        if (end_at - pos < CENTRAL_SIZE || n == entries) {
            r = BADGE_E_ZIP;
            break;
        }
        if ((r = zread(zip, pos, h, CENTRAL_SIZE)) != BADGE_OK)
            break;
        uint16_t name_len = get_le16(h + 28);
        uint64_t next = pos + CENTRAL_SIZE + name_len + get_le16(h + 30) + get_le16(h + 32);
        if (get_le32(h) != SIG_CENTRAL || next > end_at || name_len == 0 || name_len > NAME_MAX_LEN) {
            r = BADGE_E_ZIP;
            break;
        }
        if ((r = zread(zip, pos + CENTRAL_SIZE, name, name_len)) != BADGE_OK)
            break;
        if (memchr(name, 0, name_len)) {
            r = BADGE_E_ZIP;
            break;
        }
        zip_entry *z = &e[n++];
        z->flags = get_le16(h + 8);
        z->method = get_le16(h + 10);
        z->crc = get_le32(h + 16);
        z->csize = get_le32(h + 20);
        z->size = get_le32(h + 24);
        z->local = get_le32(h + 42);
        if (!(z->path = copy_of(name, name_len))) {
            r = BADGE_E_NOMEM;
            break;
        }
        pos = next;
    }
    if (!r && n != entries)
        r = BADGE_E_ZIP;

    /* One folder around every member (macOS metadata aside) is the set folder: "Set/a.png" reads as "a.png" */
    size_t strip = 0;
    const char *first = NULL;
    for (size_t i = 0; !r && i < n; i++) {
        if (is_macos_metadata(e[i].path))
            continue;
        if (!first) {
            const char *slash = strchr(e[i].path, '/');
            first = e[i].path;
            strip = slash && slash != first ? (size_t)(slash - first) + 1 : 0;
        } else if (strip && strncmp(e[i].path, first, strip)) {
            strip = 0;
        }
    }

    /* The folder's entries: files at its top, and a folder entry for anything inside a subfolder */
    size_t k = 0;
    for (size_t i = 0; !r && i < n; i++) {
        if (is_macos_metadata(e[i].path))
            continue;
        const char *p = e[i].path + strip, *slash = strchr(p, '/');
        if (!*p)
            continue;               /* the set folder itself */
        size_t len = slash ? (size_t)(slash - p) : strlen(p);
        badge_file *f = &out[k];
        if (!(f->name = copy_of(p, len))) {
            r = BADGE_E_NOMEM;
            break;
        }
        k++;
        f->is_dir = slash != NULL;
        if (f->name[0] == '.')
            continue;               /* skipped by the folder rules: not read */
        if (!(e[i].flags & FLAG_UTF8))
            for (size_t c = 0; c < len; c++)
                if ((uint8_t)f->name[c] >= 0x80)
                    r = BADGE_E_NAME;   /* a name in the zip's legacy code page */
        if (r || f->is_dir)
            continue;
        tp_member m;
        if ((r = member(zip, &e[i], directory, &m)) != BADGE_OK)
            break;
        struct fill fill = {tp_alloc(m.size), 0};
        if (!fill.buf) {
            r = BADGE_E_NOMEM;
            break;
        }
        f->data = fill.buf;
        f->size = m.size;
        int zr = zip_stream(zip, &m, fill_fn, &fill);
        if (zr)
            r = zr == TP_E_NOMEM ? BADGE_E_NOMEM : BADGE_E_ZIP;
    }
    for (size_t i = 0; i < n; i++)
        tp_free(e[i].path);
    tp_free(e);
    if (r) {
        badge_files_free(out, k);
        return r;
    }
    *files = out;
    *count = k;
    return BADGE_OK;
}
