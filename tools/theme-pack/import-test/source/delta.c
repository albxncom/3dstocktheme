#include "delta.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------- SHA-256 (FIPS 180-4)

static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

static uint32_t ror(uint32_t x, int n) { return x >> n | x << (32 - n); }

static void sha256_block(Sha256 *s, const uint8_t *p) {
    uint32_t w[64], v[8];
    for (int i = 0; i < 16; i++)
        w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = ror(w[i - 15], 7) ^ ror(w[i - 15], 18) ^ w[i - 15] >> 3;
        uint32_t s1 = ror(w[i - 2], 17) ^ ror(w[i - 2], 19) ^ w[i - 2] >> 10;
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    memcpy(v, s->state, sizeof v);
    for (int i = 0; i < 64; i++) {
        uint32_t t1 = v[7] + (ror(v[4], 6) ^ ror(v[4], 11) ^ ror(v[4], 25)) + ((v[4] & v[5]) ^ (~v[4] & v[6])) + K[i] + w[i];
        uint32_t t2 = (ror(v[0], 2) ^ ror(v[0], 13) ^ ror(v[0], 22)) + ((v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]));
        memmove(v + 1, v, 7 * sizeof *v);
        v[4] += t1;
        v[0] = t1 + t2;
    }
    for (int i = 0; i < 8; i++) s->state[i] += v[i];
}

void sha256_init(Sha256 *s) {
    static const uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                  0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    memcpy(s->state, h, sizeof h);
    s->length = 0;
    s->used = 0;
}

void sha256_update(Sha256 *s, const void *data, size_t len) {
    const uint8_t *p = data;
    s->length += len;
    while (len) {
        size_t n = 64 - s->used < len ? 64 - s->used : len;
        memcpy(s->block + s->used, p, n);
        s->used += n;
        p += n;
        len -= n;
        if (s->used == 64) {
            sha256_block(s, s->block);
            s->used = 0;
        }
    }
}

void sha256_final(Sha256 *s, uint8_t out[32]) {
    uint64_t bits = s->length * 8;
    uint8_t pad = 0x80, zero = 0, len[8];
    sha256_update(s, &pad, 1);
    while (s->used != 56) sha256_update(s, &zero, 1);
    for (int i = 0; i < 8; i++) len[i] = (uint8_t)(bits >> (56 - 8 * i));
    sha256_update(s, len, 8);
    for (int i = 0; i < 8; i++)
        for (int j = 0; j < 4; j++) out[4 * i + j] = (uint8_t)(s->state[i] >> (24 - 8 * j));
}

static void sha256_of(const void *data, size_t len, uint8_t out[32]) {
    Sha256 s;
    sha256_init(&s);
    sha256_update(&s, data, len);
    sha256_final(&s, out);
}

// ---------------------------------------------------------------- TMD

static uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static uint64_t be64(const uint8_t *p) { return (uint64_t)be32(p) << 32 | be32(p + 4); }

int tmd_parse(const uint8_t *raw, size_t len, Tmd *out, delta_say say) {
    if (len < 4) {
        say("TMD: too short");
        return -1;
    }
    size_t body;
    switch (be32(raw)) {
    case 0x10003: body = 0x240; break;
    case 0x10004: body = 0x140; break;
    case 0x10005: body = 0x80; break;
    default: say("TMD: unknown signature type %08" PRIX32, be32(raw)); return -1;
    }
    size_t info = body + 0xC4, chunks = info + 0x900;
    if (len < chunks) {
        say("TMD: %zu bytes, too short for its header", len);
        return -1;
    }
    out->title_id = be64(raw + body + 0x4C);
    out->title_type = be32(raw + body + 0x54);
    out->version = be16(raw + body + 0x9C);
    out->count = be16(raw + body + 0x9E);
    if (out->count == 0 || out->count > DELTA_MAX_CONTENTS || len != chunks + 0x30 * (size_t)out->count) {
        say("TMD: %u contents in %zu bytes do not fit (at most %d contents)", out->count, len, DELTA_MAX_CONTENTS);
        return -1;
    }
    uint8_t hash[32];
    sha256_of(raw + info, 0x900, hash);
    if (memcmp(hash, raw + body + 0xA4, 32)) {
        say("TMD: the info-record hash in the header is wrong");
        return -1;
    }
    unsigned covered = 0;
    for (int r = 0; r < 64; r++) {
        unsigned offset = be16(raw + info + 0x24 * r), n = be16(raw + info + 0x24 * r + 2);
        if (!n) continue;
        if (offset + n > out->count) {
            say("TMD: info record %d points past the content records", r);
            return -1;
        }
        sha256_of(raw + chunks + 0x30 * offset, 0x30 * n, hash);
        if (memcmp(hash, raw + info + 0x24 * r + 4, 32)) {
            say("TMD: info record %d does not hash its content records", r);
            return -1;
        }
        covered += n;
    }
    if (covered != out->count) {
        say("TMD: info records cover %u of %u content records", covered, out->count);
        return -1;
    }
    for (unsigned i = 0; i < out->count; i++) {
        const uint8_t *c = raw + chunks + 0x30 * i;
        TmdContent *t = &out->contents[i];
        t->id = be32(c);
        t->index = be16(c + 4);
        t->type = be16(c + 6);
        t->size = be64(c + 8);
        memcpy(t->sha256, c + 0x10, 32);
        if (t->index >= DELTA_MAX_CONTENTS || (i && t->index <= out->contents[i - 1].index) || (!i && t->index)) {
            say("TMD: content indices must start at 0, rise, and stay below %d (record %u has %u)", DELTA_MAX_CONTENTS,
                i, t->index);
            return -1;
        }
    }
    return 0;
}

int tmd_header_check(const uint8_t *raw, size_t len, delta_say say) {
    static const char issuer[] = "Root-CA00000003-CP0000000b";
    const size_t body = 0x140;
    uint8_t want[0xC4] = {0};
    if (len < body + sizeof want || be32(raw) != 0x10004) {
        say("REFUSED: the TMD does not have this pack's signature type (RSA-2048, 0x10004)");
        return -1;
    }
    for (size_t i = 0x104; i < body; i++)
        if (raw[i]) {
            say("REFUSED: the TMD's signature padding is not zero (byte %#zx)", i);
            return -1;
        }
    const uint8_t *h = raw + body;
    memcpy(want, issuer, sizeof issuer - 1);
    want[0x40] = 1;                                     // format version
    for (int i = 0; i < 8; i++) want[0x4C + i] = (uint8_t)(DELTA_TITLE_ID >> (56 - 8 * i));
    want[0x57] = 0x40;                                  // title type 0x00000040
    memcpy(want + 0x9C, h + 0x9C, 4);                   // title version, content count: may differ
    memcpy(want + 0xA4, h + 0xA4, 0x20);                // info-record hash: checked by tmd_parse
    for (size_t i = 0; i < sizeof want; i++)
        if (h[i] != want[i]) {
            say("REFUSED: TMD header byte %#zx is %02X, this pack's TMDs have %02X there", body + i, h[i], want[i]);
            return -1;
        }
    return 0;
}

// ---------------------------------------------------------------- delta files

static uint8_t buffer[0x10000];

int delta_load(const char *dir, uint8_t *raw, size_t *raw_len, Tmd *out, bool present[DELTA_MAX_CONTENTS],
               delta_say say) {
    char path[256];
    snprintf(path, sizeof path, "%s/tmd.bin", dir);
    FILE *f = fopen(path, "rb");
    if (!f) {
        say("Cannot open %s", path);
        return -1;
    }
    size_t n = fread(raw, 1, DELTA_TMD_MAX, f);
    bool longer = fgetc(f) != EOF;
    fclose(f);
    if (longer) {
        say("%s is larger than %d bytes", path, DELTA_TMD_MAX);
        return -1;
    }
    *raw_len = n;
    if (tmd_parse(raw, n, out, say)) return -1;
    if (out->title_id != DELTA_TITLE_ID) {
        say("REFUSED: the TMD is for title %016" PRIX64 ", this application handles only %016" PRIX64, out->title_id,
            (uint64_t)DELTA_TITLE_ID);
        return -1;
    }
    if (tmd_header_check(raw, n, say)) return -1;
    say("Delta TMD: title %016" PRIX64 ", version %u, %u contents", out->title_id, out->version, out->count);
    int bad = 0;
    for (unsigned i = 0; i < out->count; i++) {
        const TmdContent *c = &out->contents[i];
        snprintf(path, sizeof path, "%s/%u.app", dir, c->index);
        present[c->index] = false;
        f = fopen(path, "rb");
        if (!f) {
            say(" %3u  %08" PRIX32 "  no file, not imported", c->index, c->id);
            if (c->index == 0) {
                say("REFUSED: content 0 (0.app) is missing");
                bad = 1;
            }
            continue;
        }
        Sha256 s;
        sha256_init(&s);
        uint64_t size = 0;
        while ((n = fread(buffer, 1, sizeof buffer, f)) > 0) {
            sha256_update(&s, buffer, n);
            size += n;
        }
        bool error = ferror(f);
        fclose(f);
        uint8_t hash[32];
        sha256_final(&s, hash);
        bool ok = !error && size == c->size && !memcmp(hash, c->sha256, 32);
        say(" %3u  %08" PRIX32 "  %8" PRIu64 " bytes  %s", c->index, c->id, size,
            ok ? "file OK" : error ? "READ ERROR" : size != c->size ? "WRONG SIZE" : "WRONG SHA-256");
        bad |= !ok;
        present[c->index] = true;
    }
    if (bad) say("REFUSED: the delta files do not match its TMD");
    return bad ? -1 : 0;
}

// ---------------------------------------------------------------- plan

static const InstalledContent *installed_content(const Installed *inst, uint16_t index) {
    for (unsigned i = 0; i < inst->count; i++)
        if (inst->contents[i].index == index) return &inst->contents[i];
    return NULL;
}

static const TmdContent *tmd_content(const Tmd *tmd, uint16_t index) {
    for (unsigned i = 0; i < tmd->count; i++)
        if (tmd->contents[i].index == index) return &tmd->contents[i];
    return NULL;
}

static bool same_record(const InstalledContent *a, const TmdContent *b) {
    return b && a->id == b->id && a->size == b->size && a->type == b->type;
}

int delta_plan(const Tmd *delta, const bool present[DELTA_MAX_CONTENTS], const Installed *inst, Plan *plan,
               delta_say say) {
    memset(plan, 0, sizeof *plan);
    if (!inst->exists) {
        say("REFUSED: title %016" PRIX64 " is not installed. Install the full pack with FBI first.",
            (uint64_t)DELTA_TITLE_ID);
        return -1;
    }
    const TmdContent *c0 = tmd_content(delta, 0);
    bool ok = true;
    if (inst->title_type != delta->title_type)
        say("Note: AM reports title type %08" PRIX32 " for the installed title, the delta TMD has %08" PRIX32,
            inst->title_type, delta->title_type);

    if (inst->version + 1 == delta->version) {
        // The delta was made from this installed TMD: everything installed stays, content 0 and the new ones come.
        for (unsigned i = 0; i < inst->count; i++) {
            const InstalledContent *ic = &inst->contents[i];
            if (ic->id == c0->id) {
                say("REFUSED: the new content 0 has content ID %08" PRIX32 ", which installed content %u has", c0->id,
                    ic->index);
                ok = false;
            }
            if (ic->index && !same_record(ic, tmd_content(delta, ic->index))) {
                say("REFUSED: installed content %u (ID %08" PRIX32 ", %" PRIu64 " bytes) is not in the delta TMD "
                    "unchanged: the delta was made from another state",
                    ic->index, ic->id, ic->size);
                ok = false;
            }
        }
        for (unsigned i = 1; i < delta->count; i++) {
            uint16_t index = delta->contents[i].index;
            bool known = installed_content(inst, index) != NULL;
            if (known && present[index]) {
                say("REFUSED: %u.app would replace content %u of the installed TMD", index, index);
                ok = false;
            } else if (!known && !present[index]) {
                say("REFUSED: content %u is new in the TMD, but %u.app is missing", index, index);
                ok = false;
            } else if (!known) {
                plan->indices[plan->count++] = index;
            }
        }
        if (ok && plan->count == 0) {
            say("REFUSED: the delta adds no theme content");
            ok = false;
        }
        plan->phase1 = ok;
        return ok ? 0 : -1;
    }

    if (inst->version == delta->version) {
        // Phase 1 ran before (the new TMD is installed); only contents with a file that are not installed are left.
        bool same = inst->count == delta->count;
        for (unsigned i = 0; same && i < inst->count; i++)
            same = same_record(&inst->contents[i], tmd_content(delta, inst->contents[i].index));
        if (!same) {
            say("REFUSED: the installed TMD has the delta's version %u, but other content records", delta->version);
            return -1;
        }
        const InstalledContent *ic0 = installed_content(inst, 0);
        if (!ic0 || !ic0->installed) {
            say("REFUSED: the new TMD is installed but content 0 is not; reinstall the full pack with FBI");
            return -1;
        }
        for (unsigned i = 1; i < delta->count; i++) {
            uint16_t index = delta->contents[i].index;
            const InstalledContent *ic = installed_content(inst, index);
            if (present[index] && !ic->installed) plan->indices[plan->count++] = index;
        }
        if (plan->count == 0) {
            say("Nothing to do: the delta is already imported.");
            return 1;
        }
        say("The new TMD (version %u) is already installed: only phase 2 is left.", delta->version);
        return 0;
    }

    say("REFUSED: installed title version %u, the delta is version %u (made for version %d)", inst->version,
        delta->version, delta->version - 1);
    return -1;
}
