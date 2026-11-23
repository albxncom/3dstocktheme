/* A theme installer CIA: a CIA whose content 0 is a plain NCCH with a RomFS holding config.ini and a theme zip.
 * Every length and offset comes from the file and is checked before it is used; the zip itself is then read by
 * the zip reader through a window on the CIA. Layouts (3dbrew "CIA", "Title metadata", "NCCH", "RomFS") as the
 * console-tested pack and the user's example CIA have them: sections at 0x40 boundaries after a 0x2020-byte header,
 * content data in TMD record order, level 3 of the RomFS at the first block boundary after the IVFC header and the
 * master hash. */
#include "internal.h"

#define CIA_HEADER 0x2020u
#define CIA_ALIGN 0x40u
#define TMD_RECORDS_MAX 512u    /* a TMD this reader looks at; theme installer CIAs have one content */
#define CONFIG_MAX 0x1000u
#define NO_ENTRY 0xFFFFFFFFu
#define DEFAULT_LOCATION "theme.zip"

static uint64_t get_le64(const uint8_t *p) { return (uint64_t)get_le32(p + 4) << 32 | get_le32(p); }

static int fits(uint64_t offset, uint64_t length, uint64_t limit)
{
    return offset <= limit && length <= limit - offset;
}

static int rd(const tp_source *s, uint64_t offset, void *buf, size_t size)
{
    if (!fits(offset, size, s->size))
        return TP_E_CIA;
    return s->read(s->ctx, offset, buf, size) ? TP_E_READ : TP_OK;
}

int tp_kind(const tp_source *src)
{
    uint8_t h[8];
    if (!src || !src->read || src->size < sizeof h || src->read(src->ctx, 0, h, sizeof h))
        return TP_KIND_NONE;
    if (!memcmp(h, "PK\3\4", 4))
        return TP_KIND_ZIP;
    if (get_le32(h) == CIA_HEADER && get_le16(h + 4) == 0 && get_le16(h + 6) == 0)
        return TP_KIND_CIA;
    return TP_KIND_NONE;
}

static int window_read(void *ctx, uint64_t offset, void *buf, size_t len)
{
    const tp_package *p = ctx;
    if (!fits(offset, len, p->size))
        return -1;
    return p->cia.read(p->cia.ctx, p->offset + offset, buf, len);
}

/* Level 3 of the RomFS: its place in the CIA, its size and the tables it holds. */
typedef struct level3 {
    uint64_t at, size;
    uint32_t dir_meta, dir_meta_size, file_meta, file_meta_size, data;
} level3;

/* The root directory's file called name (ASCII): where its data lies in the CIA. TP_E_MISSING if there is none. */
static int root_file(const tp_source *cia, const level3 *l3, const char *name, uint64_t *offset, uint64_t *size)
{
    uint8_t dir[0x18], entry[0x20], utf16[2 * TP_CIA_NAME];
    int r;
    if (l3->dir_meta_size < sizeof dir)
        return TP_E_CIA;
    if ((r = rd(cia, l3->at + l3->dir_meta, dir, sizeof dir)) != TP_OK)
        return r;
    size_t want = strlen(name);
    uint32_t f = get_le32(dir + 0x0C);
    for (uint32_t steps = 0; f != NO_ENTRY; steps++) {
        if (steps > l3->file_meta_size / sizeof entry || !fits(f, sizeof entry, l3->file_meta_size))
            return TP_E_CIA;   /* a loop, or an entry outside the table */
        if ((r = rd(cia, l3->at + l3->file_meta + f, entry, sizeof entry)) != TP_OK)
            return r;
        uint32_t next = get_le32(entry + 0x04), name_len = get_le32(entry + 0x1C);
        uint64_t data_off = get_le64(entry + 0x08), data_size = get_le64(entry + 0x10);
        if (name_len % 2 || !fits((uint64_t)f + sizeof entry, name_len, l3->file_meta_size))
            return TP_E_CIA;
        if (name_len == 2 * want) {
            if ((r = rd(cia, l3->at + l3->file_meta + f + sizeof entry, utf16, name_len)) != TP_OK)
                return r;
            int same = 1;
            for (size_t i = 0; i < want && same; i++)
                same = get_le16(utf16 + 2 * i) == (uint8_t)name[i];
            if (same) {
                if (!fits(l3->data, 0, l3->size) || !fits(data_off, data_size, l3->size - l3->data))
                    return TP_E_CIA;
                *offset = l3->at + l3->data + data_off;
                *size = data_size;
                return TP_OK;
            }
        }
        f = next;
    }
    return TP_E_MISSING;
}

static int is_space(char c) { return c == ' ' || c == '\t' || c == '\r'; }

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

/* A key of config.ini, compared without regard to ASCII case. */
static int key_is(const char *key, size_t n, const char *want)
{
    if (strlen(want) != n)
        return 0;
    for (size_t i = 0; i < n; i++)
        if (lower((unsigned char)key[i]) != want[i])
            return 0;
    return 1;
}

/* A file name at the RomFS root: letters, digits, space, ". _ - ( )", not starting with a dot. */
static int plain_name(const char *s, size_t n)
{
    if (n == 0 || n >= TP_CIA_NAME || s[0] == '.')
        return 0;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '.' ||
              c == '_' || c == '-' || c == '(' || c == ')'))
            return 0;
    }
    return 1;
}

/* "key=value" lines; location must be a plain root file name, destination is kept only if printable ASCII. */
static int parse_config(const char *text, size_t size, tp_package *p)
{
    size_t at = 0;
    while (at < size) {
        size_t end = at;
        while (end < size && text[end] != '\n')
            end++;
        size_t a = at, b = end;
        at = end + 1;
        while (a < b && is_space(text[a]))
            a++;
        while (b > a && is_space(text[b - 1]))
            b--;
        size_t eq = a;
        while (eq < b && text[eq] != '=')
            eq++;
        if (a == b || eq == b || text[a] == ';' || text[a] == '#' || text[a] == '[')
            continue;
        size_t ke = eq, vs = eq + 1;
        while (ke > a && is_space(text[ke - 1]))
            ke--;
        while (vs < b && is_space(text[vs]))
            vs++;
        const char *value = text + vs;
        size_t n = b - vs;
        if (key_is(text + a, ke - a, "location")) {
            if (!plain_name(value, n))
                return TP_E_CIA;
            memcpy(p->location, value, n);
            p->location[n] = 0;
        } else if (key_is(text + a, ke - a, "destination")) {
            int printable = n < sizeof p->destination;
            for (size_t i = 0; i < n && printable; i++)
                printable = value[i] >= 0x20 && value[i] < 0x7F;
            if (printable) {
                memcpy(p->destination, value, n);
                p->destination[n] = 0;
            }
        }
    }
    return TP_OK;
}

static int check_hash(const tp_source *cia, uint64_t offset, uint64_t size, const uint8_t want[32])
{
    uint8_t *buf = tp_alloc(CHUNK), digest[32];
    if (!buf)
        return TP_E_NOMEM;
    sha256_ctx c;
    sha256_init(&c);
    int r = TP_OK;
    for (uint64_t done = 0; done < size && !r; done += CHUNK) {
        size_t n = size - done < CHUNK ? (size_t)(size - done) : CHUNK;
        if (!(r = rd(cia, offset + done, buf, n)))
            sha256_update(&c, buf, n);
    }
    tp_free(buf);
    if (r)
        return r;
    sha256_final(&c, digest);
    return memcmp(digest, want, 32) ? TP_E_CIA : TP_OK;
}

int tp_cia_open(const tp_source *cia, int verify, tp_package *p)
{
    if (!p)
        return TP_E_ARG;
    memset(p, 0, sizeof *p);
    if (!cia || !cia->read)
        return TP_E_ARG;
    uint8_t h[0x21];
    int r = rd(cia, 0, h, sizeof h);
    if (r)
        return r;
    if (get_le32(h) != CIA_HEADER || get_le16(h + 4) != 0 || get_le16(h + 6) != 0 || !(h[0x20] & 0x80))
        return TP_E_CIA;       /* not a CIA, or content index 0 is not in it */

    /* the sections, each at a 0x40 boundary */
    uint64_t cert = get_le32(h + 0x08), tik = get_le32(h + 0x0C), tmd_size = get_le32(h + 0x10);
    uint64_t content_size = get_le64(h + 0x18);
    uint64_t tmd = align_up(CIA_HEADER, CIA_ALIGN) + align_up(cert, CIA_ALIGN) + align_up(tik, CIA_ALIGN);
    uint64_t content = tmd + align_up(tmd_size, CIA_ALIGN);
    if (!fits(tmd, tmd_size, cia->size) || !fits(content, content_size, cia->size))
        return TP_E_CIA;

    /* the TMD: its signature type, the title, content 0's record */
    uint8_t sig[4], body[0xC4], rec[0x30];
    if ((r = rd(cia, tmd, sig, sizeof sig)) != TP_OK)
        return r;
    uint64_t at;
    switch (get_be32(sig)) {
    case 0x10003: at = 0x240; break;     /* RSA-4096, SHA-256 */
    case 0x10004: at = 0x140; break;     /* RSA-2048, SHA-256 */
    case 0x10005: at = 0x80; break;      /* ECDSA, SHA-256 */
    default: return TP_E_CIA;
    }
    if (tmd_size < at + sizeof body + 0x900 + sizeof rec)
        return TP_E_CIA;
    if ((r = rd(cia, tmd + at, body, sizeof body)) != TP_OK)
        return r;
    uint16_t count = get_be16(body + 0x9E);
    if (count == 0 || count > TMD_RECORDS_MAX || tmd_size < at + sizeof body + 0x900 + (uint64_t)sizeof rec * count)
        return TP_E_CIA;
    if ((r = rd(cia, tmd + at + sizeof body + 0x900, rec, sizeof rec)) != TP_OK)
        return r;
    uint64_t size0 = get_be64(rec + 0x08);
    if (get_be16(rec + 0x04) != 0)
        return TP_E_CIA;       /* the first record is not content 0, so content 0's data is not first */
    if (get_be16(rec + 0x06) & 1)
        return TP_E_ENCRYPTED;
    if (size0 < 0x200 || size0 > content_size)
        return TP_E_CIA;

    /* content 0: a plain NCCH with a RomFS */
    uint8_t n[0x200];
    if ((r = rd(cia, content, n, sizeof n)) != TP_OK)
        return r;
    uint64_t ncch_size = (uint64_t)get_le32(n + 0x104) * MEDIA;
    uint64_t romfs = (uint64_t)get_le32(n + 0x1B0) * MEDIA, romfs_size = (uint64_t)get_le32(n + 0x1B4) * MEDIA;
    if (memcmp(n + 0x100, "NCCH", 4) || ncch_size > size0)
        return TP_E_CIA;
    if (!(n[0x18F] & 0x04))
        return TP_E_ENCRYPTED; /* NoCrypto is not set */
    if ((n[0x18F] & 0x02) || romfs_size < 0x60 || !fits(romfs, romfs_size, ncch_size))
        return TP_E_CIA;       /* NoRomFS, or a RomFS outside the NCCH */
    romfs += content;

    /* the RomFS: IVFC header, then level 3 at the first block boundary after it and the master hash */
    uint8_t ivfc[0x5C], l3h[0x28];
    if ((r = rd(cia, romfs, ivfc, sizeof ivfc)) != TP_OK)
        return r;
    uint32_t master = get_le32(ivfc + 0x08), log2 = get_le32(ivfc + 0x4C);
    level3 l3;
    l3.size = get_le64(ivfc + 0x44);
    if (memcmp(ivfc, "IVFC", 4) || get_le32(ivfc + 0x04) != 0x10000 || log2 < 6 || log2 > 20 ||
        master > romfs_size)
        return TP_E_CIA;
    uint64_t l3_start = align_up(0x60 + (uint64_t)master, (uint64_t)1 << log2);
    if (!fits(l3_start, l3.size, romfs_size) || l3.size < sizeof l3h)
        return TP_E_CIA;
    l3.at = romfs + l3_start;
    if ((r = rd(cia, l3.at, l3h, sizeof l3h)) != TP_OK)
        return r;
    l3.dir_meta = get_le32(l3h + 0x0C);
    l3.dir_meta_size = get_le32(l3h + 0x10);
    l3.file_meta = get_le32(l3h + 0x1C);
    l3.file_meta_size = get_le32(l3h + 0x20);
    l3.data = get_le32(l3h + 0x24);
    if (get_le32(l3h) != sizeof l3h || !fits(l3.dir_meta, l3.dir_meta_size, l3.size) ||
        !fits(l3.file_meta, l3.file_meta_size, l3.size) || l3.data > l3.size)
        return TP_E_CIA;

    /* config.ini (optional), then the zip it names */
    memcpy(p->location, DEFAULT_LOCATION, sizeof DEFAULT_LOCATION);
    uint64_t cfg, cfg_size;
    r = root_file(cia, &l3, "config.ini", &cfg, &cfg_size);
    if (r == TP_OK) {
        if (cfg_size > CONFIG_MAX)
            return TP_E_CIA;
        char *text = tp_alloc(CONFIG_MAX);
        if (!text)
            return TP_E_NOMEM;
        if (!(r = rd(cia, cfg, text, (size_t)cfg_size)))
            r = parse_config(text, (size_t)cfg_size, p);
        tp_free(text);
        if (r)
            return r;
    } else if (r != TP_E_MISSING) {
        return r;
    }
    r = root_file(cia, &l3, p->location, &p->offset, &p->size);
    if (r == TP_E_MISSING)
        return TP_E_CIA;       /* no theme zip where config.ini says */
    if (r)
        return r;
    if (verify && (r = check_hash(cia, content, size0, rec + 0x10)) != TP_OK)
        return r;
    p->title_id = get_be64(body + 0x4C);
    p->cia = *cia;
    p->zip = (tp_source){p, p->size, window_read};
    return TP_OK;
}

int tp_theme_in(const tp_source *file, int verify, tp_package *package, const tp_source **zip, int *kind)
{
    if (!file || !package || !zip || !kind)
        return TP_E_ARG;
    *zip = NULL;
    *kind = tp_kind(file);
    if (*kind == TP_KIND_ZIP) {
        *zip = file;
        return TP_OK;
    }
    if (*kind != TP_KIND_CIA)
        return TP_E_KIND;
    int r = tp_cia_open(file, verify, package);
    if (r == TP_OK)
        *zip = &package->zip;
    return r;
}
