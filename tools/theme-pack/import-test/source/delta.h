// Checks a delta (sdmc:/theme-pack/delta/) against the installed title before anything is written, and works out
// what to import. Plain C with stdio only, so the same code also runs on the Mac (host/check.c, the host test).
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DELTA_TITLE_ID 0x0004008C00009809ULL  // the only title this application touches
#define DELTA_MAX_CONTENTS 350               // content indices 0 to 349 (the builder's limit)
#define DELTA_TMD_MAX (0x140 + 0xC4 + 0x900 + 0x30 * DELTA_MAX_CONTENTS)

typedef void (*delta_say)(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

typedef struct {
    uint32_t id;
    uint16_t index;
    uint16_t type;
    uint64_t size;
    uint8_t sha256[32];
} TmdContent;

typedef struct {
    uint64_t title_id;
    uint32_t title_type;
    uint16_t version;
    uint16_t count;
    TmdContent contents[DELTA_MAX_CONTENTS];
} Tmd;

// What AM reports for the installed title (AMAPP_GetDLCTitleInfos, AMAPP_ListDLCContentInfos).
typedef struct {
    uint16_t index;
    uint16_t type;
    uint32_t id;
    uint64_t size;
    bool installed;
    bool owned;
} InstalledContent;

typedef struct {
    bool exists;
    uint16_t version;
    uint32_t title_type;  // as AM reports it (AM_TitleInfo.titleType); compared, not trusted
    uint16_t count;
    InstalledContent contents[DELTA_MAX_CONTENTS];
} Installed;

// Phase 1: the new TMD and content 0, then commit. Phase 2: the listed contents, then commit.
typedef struct {
    bool phase1;
    uint16_t count;
    uint16_t indices[DELTA_MAX_CONTENTS];
} Plan;

typedef struct {
    uint32_t state[8];
    uint64_t length;
    uint8_t block[64];
    size_t used;
} Sha256;

void sha256_init(Sha256 *s);
void sha256_update(Sha256 *s, const void *data, size_t len);
void sha256_final(Sha256 *s, uint8_t out[32]);

// Parses a TMD (signature types 0x10003 to 0x10005) and checks its info-record hash chain. 0 on success.
int tmd_parse(const uint8_t *raw, size_t len, Tmd *out, delta_say say);

// The header every TMD of this pack has (what makerom writes for it, and what the installed TMDs hold): RSA-2048
// signature type with zero padding, issuer Root-CA00000003-CP0000000b, format version 1, title type 0x40, the
// title ID, and zero in every other field except the title version, the content count and the info-record hash.
// 0 when raw matches.
int tmd_header_check(const uint8_t *raw, size_t len, delta_say say);

// Reads <dir>/tmd.bin into raw (DELTA_TMD_MAX bytes) and out, checks the title ID and the header, then every
// <dir>/<index>.app that exists: its size and SHA-256 must match the TMD. present[i] is set for each content that
// has a file. Content 0 must have one. 0 when everything matches.
int delta_load(const char *dir, uint8_t *raw, size_t *raw_len, Tmd *out, bool present[DELTA_MAX_CONTENTS],
               delta_say say);

// Compares the delta with the installed title and fills plan. 0 when the import may go ahead; 1 when there is
// nothing to do; -1 when the delta does not fit the installed state (nothing may be written).
int delta_plan(const Tmd *delta, const bool present[DELTA_MAX_CONTENTS], const Installed *inst, Plan *plan,
               delta_say say);
