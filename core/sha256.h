/* SHA-256 (FIPS 180-4). */
#ifndef TP_SHA256_H
#define TP_SHA256_H

#include <stddef.h>
#include <stdint.h>

typedef struct sha256_ctx {
    uint32_t state[8];
    uint64_t length;            /* bytes so far */
    uint8_t buffer[64];
    size_t fill;
} sha256_ctx;

void sha256_init(sha256_ctx *c);
void sha256_update(sha256_ctx *c, const void *data, size_t size);
void sha256_final(sha256_ctx *c, uint8_t digest[32]);
void sha256(const void *data, size_t size, uint8_t digest[32]);

#endif
