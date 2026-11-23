/* Theme pack core: turns theme zips into the pieces of the 3DS theme DLC title 0004008C00009809 (EUR, the spare
 * variation 09; the official themes use 00 to 04, and no other title is accepted).
 *
 * Plain C11 plus zlib (inflate, CRC-32). No file system, no console calls: the caller hands in byte sources
 * (a theme zip) and byte sinks (a file, an AM content handle) and gets back what goes where.
 *
 * A full install:  tp_theme_open per zip -> tp_catalogue_content and tp_theme_content with out == NULL
 *                  (measures: size and SHA-256) -> tp_ticket, tp_tmd -> the same content calls with a sink.
 * Adding themes:   tp_delta_plan on the installed TMD -> measure content 0 (ID plan.content0_id) and the new
 *                  themes -> tp_delta_tmd -> write content 0 and the new themes. No ticket.
 *
 * Every call returns TP_OK (0) or a tp_result. After an error a sink may have received part of a content:
 * cancel that import. Inputs from a zip are treated as hostile; inputs from the caller (pointers, sizes of
 * its own buffers) are trusted to be what they say.
 */
#ifndef TP_H
#define TP_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum tp_result {
    TP_OK = 0,
    TP_E_ARG,       /* a caller argument is out of range */
    TP_E_NOMEM,     /* an allocation failed */
    TP_E_READ,      /* the source failed */
    TP_E_WRITE,     /* the sink, or the icon callback, failed */
    TP_E_ZIP,       /* not a zip this reader accepts, or a member is damaged */
    TP_E_MISSING,   /* body_LZ.bin or info.smdh is not at the zip root */
    TP_E_TOO_BIG,   /* body_LZ.bin over 0x150000 bytes stored or decoded, or bgm.bcstm over 0x337000 */
    TP_E_LZ11,      /* body_LZ.bin does not decode as LZ11 to exactly its declared size */
    TP_E_SMDH,      /* info.smdh is not 0x36C0 bytes starting with "SMDH" */
    TP_E_INDEX,     /* a content index outside 1..349, twice, or no index left */
    TP_E_TMD,       /* the installed TMD is malformed */
    TP_E_GONE,      /* the installed TMD lists a theme the new catalogue does not have */
    TP_E_NOTHING,   /* the delta would add no theme */
    TP_E_VERSION,   /* the title version is at its maximum */
    TP_E_ID,        /* the new content 0 ID is already used by an installed content */
    TP_E_CHANGED,   /* the source gave other bytes on the second pass */
    TP_E_TITLE,     /* a title ID other than TP_TITLE_ID, as argument or in an installed TMD */
    TP_E_CATALOGUE, /* an installed catalogue file is malformed */
    TP_E_STALE,     /* the stored TMD does not say what AM reports */
    TP_E_CIA,       /* not a theme installer CIA this reader accepts (header, TMD, NCCH, RomFS, config.ini, zip) */
    TP_E_ENCRYPTED, /* the CIA's content is encrypted: only plain contents can be read */
    TP_E_KIND       /* neither a theme zip nor a theme installer CIA */
} tp_result;

const char *tp_result_name(int result);   /* "TP_E_ZIP" */
const char *tp_result_text(int result);   /* one sentence */

/* ---------------------------------------------------------------- limits and the title */

#define TP_BODY_MAX     0x150000u   /* body_LZ.bin, both its file and its decoded size (BodyCache.bin) */
#define TP_BGM_MAX      0x337000u   /* bgm.bcstm (BgmCache.bin) */
#define TP_SMDH_SIZE    0x36C0u
#define TP_ICON_SIZE    0x1200u     /* 48x48 tiled RGB565, the SMDH large icon */
#define TP_INDEX_MAX    349         /* HOME 11.17 keeps a 350-byte row per variation */
#define TP_THEMES_MAX   100         /* HOME lists at most 100 themes, official ones included */
#define TP_TICKET_SIZE  0x350u

/* The only title the core writes or reads. Every call that takes a title ID, and every installed TMD, must
 * carry this one; anything else is TP_E_TITLE, so an official theme title cannot be touched by mistake. */
#define TP_TITLE_ID     0x0004008C00009809ull

/* ---------------------------------------------------------------- caller I/O and memory */

typedef struct tp_source {
    void *ctx;
    uint64_t size;
    /* Reads exactly len bytes at offset into buf, 0 on success. The core never reads past size. */
    int (*read)(void *ctx, uint64_t offset, void *buf, size_t len);
} tp_source;

typedef struct tp_sink {
    void *ctx;
    /* Appends len bytes, 0 on success. Writes arrive in order, in pieces of up to 16 KiB. */
    int (*write)(void *ctx, const void *buf, size_t len);
} tp_sink;

/* Every allocation of the core and of its zlib streams goes through these (default: malloc and free); one
 * allocator for the process. A content call needs about 90 KB, an open theme keeps 32 bytes per 4 KiB of its
 * data (at most 38 KB); see README, Memory. */
void tp_set_allocator(void *(*alloc)(void *ctx, size_t size), void (*release)(void *ctx, void *ptr), void *ctx);

/* For measuring only: tell is called whenever the work on a theme's files changes its kind (TP_PHASE_*), so a caller
 * with a clock can book the time between two calls to the kind named first; the core reads no clock. One hook for
 * the process, like the allocator; NULL (the default): nothing is told. Reading the source is not a kind of its own
 * here: the source's reader can tell that itself. */
enum {
    TP_PHASE_OTHER,             /* everything not named below */
    TP_PHASE_UNZIP,             /* inflating a zip member and its CRC-32 */
    TP_PHASE_LZ11,              /* decoding body_LZ.bin to check it */
    TP_PHASE_HASH               /* the RomFS's block hashes (SHA-256) */
};
void tp_set_phase(void (*tell)(void *ctx, int phase), void *ctx);

/* ---------------------------------------------------------------- one theme zip */

#define TP_NAME_BYTES 0x100     /* UTF-8 from 64 UTF-16 units fits in 192 bytes */
#define TP_DESC_BYTES 0x200     /* and from 128 units in 384 */

enum {
    TP_WARN_BGM_UNUSED  = 1,    /* bgm.bcstm present, but the body's use-BGM byte (0x5) is 0: HOME stays silent */
    TP_WARN_BGM_MISSING = 2,    /* use-BGM byte set, but no bgm.bcstm */
    TP_WARN_NAME_FALLBACK = 4   /* no title in info.smdh: the name is the caller's fallback */
};

typedef struct tp_member {      /* where a zip member's data is; private to the core */
    uint64_t offset;
    uint32_t csize, size, crc;
    uint16_t method;
    uint8_t present;
} tp_member;

typedef struct tp_theme {
    char name[TP_NAME_BYTES];          /* UTF-8, NUL-terminated: the first non-empty short title of the SMDH */
    char description[TP_DESC_BYTES];   /* the first non-empty long title, or "" */
    uint8_t icon[TP_ICON_SIZE];        /* the SMDH large icon, as is */
    uint32_t body_size;                /* bytes of body_LZ.bin */
    uint32_t bgm_size;                 /* bytes of bgm.bcstm, if has_bgm */
    int has_bgm;
    int use_bgm;                       /* byte 0x5 of the decompressed body is non-zero */
    unsigned warnings;                 /* TP_WARN_* */
    /* private */
    tp_source src;
    tp_member body, bgm;
    uint8_t *l2;                       /* SHA-256 of every 4 KiB block of the theme's RomFS data */
    uint64_t l3_size;
    uint8_t *body_mem;                 /* after tp_theme_drop_bgm: the body with its use-BGM byte cleared */
    uint32_t body_mem_size;
} tp_theme;

/* Reads and checks one theme zip (Anemone / Theme Plaza format: body_LZ.bin, optional bgm.bcstm, info.smdh at
 * the root; other members are ignored): sizes and limits, CRC-32, the whole LZ11 stream, the SMDH. One pass
 * over the members. fallback_name (UTF-8, may be NULL) names a theme whose SMDH has no title, as the zip's
 * file name without ".zip" does in theme_pack.py. Keeps the source and about 40 KiB until tp_theme_close. */
int tp_theme_open(tp_theme *theme, const tp_source *zip, const char *fallback_name);
void tp_theme_close(tp_theme *theme);   /* safe on a theme that failed to open, and twice; close before
                                           reusing the struct, and do not copy an open theme */

/* The theme without its music, for "install without music": bgm.bcstm is left out of the content, and a body
 * whose use-BGM byte is set gets it cleared (decoded, byte 0x5 set to 0, encoded again, the new stream decoded once
 * more and compared), so the content has the shape of a theme that never had music: HOME plays bgm.bcstm only when
 * that byte is set (3dbrew, Home Menu/Themes). The open theme is hashed again; afterwards has_bgm, use_bgm and the
 * BGM warnings are 0 and body_size is the new body's. TP_E_ARG if the theme is not open or has no bgm.bcstm;
 * TP_E_TOO_BIG if the new body would not fit 0x150000 bytes. On an error the theme is as before. Holds the new
 * body (up to 0x150000 bytes) until tp_theme_close; about 4 times the decoded body's size while it runs. */
int tp_theme_drop_bgm(tp_theme *theme);

/* The cheap first half of tp_theme_open, for a list of zips: the members, their stored sizes against the limits
 * and the SMDH (name, description, icon, has_bgm, body_size, bgm_size, TP_WARN_NAME_FALLBACK). The body is not
 * decoded and nothing is hashed, so use_bgm, the BGM warnings and the LZ11 check come only with tp_theme_open;
 * a theme that peeks fine can still fail to open. Keeps nothing allocated; the struct is not open (no
 * tp_theme_content), and tp_theme_close on it is harmless. */
int tp_theme_peek(tp_theme *theme, const tp_source *zip, const char *fallback_name);

/* ---------------------------------------------------------------- a theme installer CIA
 * A theme can also come as a CIA: content 0 a plain (unencrypted) NCCH whose RomFS holds config.ini (lines such
 * as "destination=Themes/<name>.zip" and "location=theme.zip") and the theme zip that location names (default
 * theme.zip). The core never installs such a CIA; it reads the zip out of it for tp_theme_peek and tp_theme_open. */

enum { TP_KIND_NONE, TP_KIND_ZIP, TP_KIND_CIA };

/* What a file is, by its first bytes: a zip's local header "PK\3\4", or a CIA header (size 0x2020, type 0,
 * version 0). Its name and where it came from do not count. */
int tp_kind(const tp_source *src);

#define TP_CIA_NAME 64

typedef struct tp_package {
    uint64_t title_id;              /* from the CIA's TMD, for the log */
    char location[TP_CIA_NAME];     /* the zip's name at the RomFS root */
    char destination[0x100];        /* config.ini's destination if printable ASCII, else "" */
    uint64_t offset, size;          /* where the zip lies in the CIA */
    tp_source zip;                  /* a window on those bytes: valid while the CIA's source is; the window points
                                       at this struct, so do not copy or move it while the zip is in use */
    tp_source cia;                  /* private */
} tp_package;

/* Reads and checks a theme installer CIA: the header and its 0x40-aligned sections, the TMD (signature type, the
 * first record is content 0, not encrypted, within the content data), content 0 as an NCCH (magic, size, NoCrypto
 * set, a RomFS inside it), the RomFS (IVFC header, level 3 header and tables, the root's file entries, no loops),
 * config.ini (optional, at most 4 KiB; location must be a plain root file name) and the zip it names. With verify
 * the SHA-256 of content 0 must match its TMD record (reads the whole content). Keeps nothing allocated. */
int tp_cia_open(const tp_source *cia, int verify, tp_package *package);

/* The theme zip in a file of either kind: *zip is the file itself for a zip, the window into package for a theme
 * installer CIA (tp_cia_open with verify); *kind says which. TP_E_KIND if the file is neither. */
int tp_theme_in(const tp_source *file, int verify, tp_package *package, const tp_source **zip, int *kind);

/* ---------------------------------------------------------------- contents (NCCH CFA) */

typedef struct tp_content {     /* a TMD content record */
    uint32_t id;
    uint16_t index;
    uint16_t type;              /* 0x4000 (optional) for themes, 0 for content 0 */
    uint64_t size;
    uint8_t sha256[32];
} tp_content;

/* Content `index` (1..349, content ID = index): a RomFS with bgm.bcstm (if any) and body_LZ.bin, copied from the
 * zip. out == NULL measures only. One pass over the members; a block that hashes differently from
 * tp_theme_open's pass ends it with TP_E_CHANGED. */
int tp_theme_content(const tp_theme *theme, uint64_t title_id, uint16_t index, const tp_sink *out,
                     tp_content *record);

typedef struct tp_entry {
    uint16_t index;             /* 1..349 */
    const char *name;           /* UTF-8; cut to 63 bytes at a character boundary */
    const char *description;    /* UTF-8 or NULL; cut to 127 bytes */
} tp_entry;

typedef struct tp_catalogue {
    const tp_entry *entries;    /* the themes the pack holds, any order, at most TP_THEMES_MAX */
    size_t count;
    uint16_t top;               /* the highest index ever handed out (>= every entry, <= 349): an index up to
                                   top without an entry is listed as "(removed)" with a grey icon */
    /* Fills the 0x1200-byte icon of an entry's index, 0 on success. Called twice per entry and call. */
    int (*icon)(void *ctx, uint16_t index, uint8_t icon[TP_ICON_SIZE]);
    void *icon_ctx;
} tp_catalogue;

/* Content 0: ExeFS icon "Custom themes" (what Data Management shows) and a RomFS with
 * MetaDataContentHeader.bin, ContentInfoArchive_EUR_<lang>.bin (8 languages, identical) and icons/<i>.icn for
 * i = 1..top. content_id is 0 for a full install, the delta plan's content0_id for a delta. type 0. */
int tp_catalogue_content(const tp_catalogue *catalogue, uint64_t title_id, uint32_t content_id,
                         const tp_sink *out, tp_content *record);

/* ---------------------------------------------------------------- TMD and ticket */

size_t tp_tmd_size(size_t count);   /* 0xB04 + 0x30 * count */

/* A TMD for a full install: records sorted by index, content 0 first, unique IDs, indices <= 349. */
int tp_tmd(uint64_t title_id, uint16_t version, const tp_content *records, size_t count, const tp_sink *out);

/* A ticket that grants content indices 0..0x3FF (so a later theme needs no new ticket). The title key is not
 * used for these plain contents; makerom puts random bytes there and a random ticket ID 0x0004xxxxxxxxxxxx. */
int tp_ticket(uint64_t title_id, uint64_t ticket_id, const uint8_t title_key[16], const tp_sink *out);

/* ---------------------------------------------------------------- the installed pack */

/* Checks an installed TMD (RSA-2048 signature type, size, hash chain, content 0 first, indices ascending and
 * <= 349, unique IDs) and returns its title, version and record count. */
int tp_tmd_read(const uint8_t *tmd, size_t size, uint64_t *title_id, uint16_t *version, size_t *count);
int tp_tmd_record(const uint8_t *tmd, size_t size, size_t k, tp_content *record);

/* The index for a new theme: one above the larger of top (the highest index ever handed out, from the
 * caller's own record, 0 if none) and every index in used[] (the installed TMD's, for example). So an index is
 * never handed out twice, also not after its theme is gone. TP_E_INDEX once 349 is taken. */
int tp_next_index(const uint16_t *used, size_t count, uint16_t top, uint16_t *index);

typedef struct tp_delta {
    uint16_t version;           /* installed title version + 1 */
    uint32_t content0_id;       /* 0x10000 + version: an ID no content of the title had */
    size_t kept;                /* installed theme records carried over unchanged */
    size_t count;               /* themes to import, ascending */
    uint16_t add[TP_INDEX_MAX];
} tp_delta;

/* What to import to go from the installed TMD to the catalogue (the same rules as theme_pack.py delta): every
 * installed theme record stays as installed (also of themes HOME deleted), each must still be in the
 * catalogue (TP_E_GONE), the catalogue's other entries are imported (none: TP_E_NOTHING), content 0 gets a new
 * ID, the version goes up by one. title_id and the TMD's title must be TP_TITLE_ID. */
int tp_delta_plan(const uint8_t *installed_tmd, size_t size, uint64_t title_id, const tp_catalogue *catalogue,
                  tp_delta *plan);

size_t tp_delta_tmd_size(const tp_delta *plan, size_t installed_count);

/* The new TMD: the installed one with content 0 replaced, the new records added (added[i] for plan->add[i],
 * from tp_theme_content), the version raised and the hashes renewed; signature and header otherwise as
 * installed. */
int tp_delta_tmd(const uint8_t *installed_tmd, size_t size, const tp_delta *plan, const tp_content *content0,
                 const tp_content *added, const tp_sink *out);

/* ---------------------------------------------------------------- what is on the console
 * The application fetches the bytes (content 0's RomFS files, the AM content list); the core checks them. */

typedef struct tp_installed {
    uint16_t index;
    int enabled;                /* the entry's enabled word is not 0 (HOME skips an entry whose word is 0) */
    char name[0x41];            /* the 0x40-byte field up to its NUL, UTF-8 */
    char description[0x81];     /* the 0x80-byte field */
    int has_icon;
    uint8_t icon[TP_ICON_SIZE];
} tp_installed;

/* The highest index of an installed catalogue, from its ContentInfoArchive_EUR_<lang>.bin (all eight are the
 * same in this pack): version 1, count 1..349, size 8 + 0xC8 * (count + 1), else TP_E_CATALOGUE. */
int tp_installed_top(const tp_source *archive, uint16_t *top);

/* Entry `index` (0..top; 0 is the disabled dummy) of that archive, and with icon != NULL the content of
 * icons/<index>.icn (exactly 0x1200 bytes). The entry must carry its own index and UTF-8 text. An index the
 * builders reserved reads back as "(removed)" with a grey icon. */
int tp_installed_entry(const tp_source *archive, const tp_source *icon, uint16_t index, tp_installed *entry);

typedef struct tp_am_content {  /* what AM lists per content of the installed TMD */
    uint16_t index;
    uint32_t id;
    uint64_t size;
} tp_am_content;

enum { TP_DIFF_NONE, TP_DIFF_VERSION, TP_DIFF_MISSING, TP_DIFF_EXTRA, TP_DIFF_ID, TP_DIFF_SIZE };

typedef struct tp_difference {
    int what;                   /* TP_DIFF_*: the title version; a TMD record AM does not list; an index AM lists
                                   that the TMD has not (or twice); a record with another content ID or size */
    size_t record;              /* position in the TMD (MISSING, ID, SIZE) or in AM's list (EXTRA) */
    uint16_t index;             /* the content index */
} tp_difference;

/* Is the stored TMD what is installed? TP_OK if AM reports its title version and, per content in any order, its
 * index, content ID and size; else TP_E_STALE and the first difference (the version first, then the TMD's
 * records in order, then AM's extra entries). Build a delta only from a TMD that passes. */
int tp_tmd_check(const uint8_t *tmd, size_t size, uint16_t version, const tp_am_content *am, size_t count,
                 tp_difference *difference);

#ifdef __cplusplus
}
#endif

#endif
