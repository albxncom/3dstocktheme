/* LZ11 on whole buffers: decoding a body and encoding it again, for a theme installed without its music
 * (tp_theme_drop_bgm clears the body's use-BGM byte). The streaming check tp_theme_open runs is lz11_feed, theme.c.
 *
 * Format (as lz11_feed reads it): 0x11 and a 24-bit decoded size (0: a 32-bit size follows), then a flag byte per 8
 * tokens, MSB first, 0 = one literal byte, 1 = a copy of 3 to 0x10110 bytes from 1 to 0x1000 bytes back, in 2, 3 or
 * 4 bytes by its length. */
#include "internal.h"

#define WINDOW 0x1000u
#define MAX_COPY 0x10110u
#define HASH_BITS 13
#define CHAIN 64                 /* candidates looked at per position: enough for theme bodies, bounded time */

int lz11_size(const uint8_t *in, size_t n, size_t *size, size_t *start)
{
    if (n < 4 || in[0] != 0x11)
        return TP_E_LZ11;
    *size = (size_t)in[1] | (size_t)in[2] << 8 | (size_t)in[3] << 16;
    *start = 4;
    if (*size == 0) {
        if (n < 8)
            return TP_E_LZ11;
        *size = get_le32(in + 4);
        *start = 8;
    }
    return *size ? TP_OK : TP_E_LZ11;
}

int lz11_decode(const uint8_t *in, size_t n, uint8_t *out, size_t cap)
{
    size_t size, p, o = 0;
    int r = lz11_size(in, n, &size, &p);
    if (r)
        return r;
    if (size > cap)
        return TP_E_TOO_BIG;
    while (o < size) {
        if (p >= n)
            return TP_E_LZ11;
        uint8_t flags = in[p++];
        for (int bit = 0; bit < 8 && o < size; bit++) {
            if (!(flags & 0x80 >> bit)) {
                if (p >= n)
                    return TP_E_LZ11;
                out[o++] = in[p++];
                continue;
            }
            if (p >= n)
                return TP_E_LZ11;
            unsigned kind = in[p] >> 4, need = kind == 0 ? 3 : kind == 1 ? 4 : 2;
            if (n - p < need)
                return TP_E_LZ11;
            const uint8_t *t = in + p;
            size_t length, distance;
            if (need == 2) {
                length = (size_t)(t[0] >> 4) + 1;
                distance = (size_t)((t[0] & 0xF) << 8 | t[1]) + 1;
            } else if (need == 3) {
                length = (size_t)((t[0] & 0xF) << 4 | t[1] >> 4) + 0x11;
                distance = (size_t)((t[1] & 0xF) << 8 | t[2]) + 1;
            } else {
                length = (size_t)((t[0] & 0xF) << 12 | t[1] << 4 | t[2] >> 4) + 0x111;
                distance = (size_t)((t[2] & 0xF) << 8 | t[3]) + 1;
            }
            p += need;
            if (distance > o || length > size - o)
                return TP_E_LZ11;
            for (size_t k = 0; k < length; k++, o++)
                out[o] = out[o - distance];
        }
    }
    return TP_OK;
}

static unsigned hash3(const uint8_t *p)
{
    return ((unsigned)p[0] << 16 ^ (unsigned)p[1] << 8 ^ p[2]) * 2654435761u >> (32 - HASH_BITS);
}

typedef struct encoder {
    uint8_t *out;
    size_t at, cap, flag_at;
    int tokens;                 /* tokens under the current flag byte; 8: the next token opens a group */
    int full;
} encoder;

static void put(encoder *e, uint8_t b)
{
    if (e->at < e->cap)
        e->out[e->at] = b;
    else
        e->full = 1;
    e->at++;
}

static void token(encoder *e, int copy)
{
    if (e->tokens == 8) {
        e->flag_at = e->at;
        put(e, 0);
        e->tokens = 0;
    }
    if (copy && e->flag_at < e->cap)
        e->out[e->flag_at] |= (uint8_t)(0x80 >> e->tokens);
    e->tokens++;
}

static void copy_token(encoder *e, size_t length, size_t distance)
{
    size_t d = distance - 1;
    token(e, 1);
    if (length <= 0x10) {
        put(e, (uint8_t)((length - 1) << 4 | d >> 8));
    } else if (length <= 0x110) {
        size_t v = length - 0x11;
        put(e, (uint8_t)(v >> 4));
        put(e, (uint8_t)((v & 0xF) << 4 | d >> 8));
    } else {
        size_t v = length - 0x111;
        put(e, (uint8_t)(0x10 | v >> 12));
        put(e, (uint8_t)(v >> 4 & 0xFF));
        put(e, (uint8_t)((v & 0xF) << 4 | d >> 8));
    }
    put(e, (uint8_t)(d & 0xFF));
}

int lz11_encode(const uint8_t *in, size_t n, uint8_t *out, size_t cap, size_t *size)
{
    if (n == 0 || n > 0xFFFFFF)
        return TP_E_ARG;
    int32_t *head = tp_alloc(sizeof *head << HASH_BITS), *prev = tp_alloc(sizeof *prev * WINDOW);
    if (!head || !prev) {
        tp_free(head);
        tp_free(prev);
        return TP_E_NOMEM;
    }
    for (size_t i = 0; i < (size_t)1 << HASH_BITS; i++)
        head[i] = -1;
    encoder e = {out, 0, cap, 0, 8, 0};
    put(&e, 0x11);
    put(&e, (uint8_t)n);
    put(&e, (uint8_t)(n >> 8));
    put(&e, (uint8_t)(n >> 16));
    size_t p = 0;
    while (p < n && !e.full) {
        size_t best = 0, best_d = 0;
        if (n - p >= 3) {
            size_t limit = n - p < MAX_COPY ? n - p : MAX_COPY;
            int32_t c = head[hash3(in + p)];
            for (int steps = 0; c >= 0 && p - (size_t)c <= WINDOW && steps < CHAIN; steps++) {
                size_t len = 0;
                while (len < limit && in[(size_t)c + len] == in[p + len])
                    len++;
                if (len > best) {
                    best = len;
                    best_d = p - (size_t)c;
                    if (len == limit)
                        break;
                }
                c = prev[(size_t)c % WINDOW];
            }
        }
        size_t step = best >= 3 ? best : 1;
        if (best >= 3)
            copy_token(&e, best, best_d);
        else {
            token(&e, 0);
            put(&e, in[p]);
        }
        /* Positions older than WINDOW drop out of reach before their slot in prev is reused. */
        for (size_t q = p; q < p + step; q++)
            if (n - q >= 3) {
                unsigned h = hash3(in + q);
                prev[q % WINDOW] = head[h];
                head[h] = (int32_t)q;
            }
        p += step;
    }
    while (e.at % 4)            /* zeros after the end, as the usual encoders pad; decoders stop at the size */
        put(&e, 0);
    tp_free(head);
    tp_free(prev);
    if (e.full)
        return TP_E_TOO_BIG;
    *size = e.at;
    return TP_OK;
}
