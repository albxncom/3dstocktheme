#include "media.h"

#include "internal.h"

int tp_body_decode(const uint8_t *lz, size_t n, uint8_t *out, size_t cap, size_t *size)
{
    size_t declared = 0, start;
    int r = lz11_size(lz, n, &declared, &start);
    if (r)
        return r;
    if (declared < 6)
        return TP_E_LZ11;
    if (declared > TP_BODY_MAX || declared > cap)
        return TP_E_TOO_BIG;
    r = lz11_decode(lz, n, out, declared);
    if (!r)
        *size = declared;
    return r;
}

struct fill {
    uint8_t *buf;
    size_t have, size;
};

static int fill_fn(void *ctx, const uint8_t *data, size_t size)
{
    struct fill *f = ctx;
    if (size > f->size - f->have)
        return TP_E_ZIP;
    memcpy(f->buf + f->have, data, size);
    f->have += size;
    return TP_OK;
}

int tp_theme_body(const tp_theme *t, uint8_t *out, size_t cap, size_t *size)
{
    if (!t || !t->l2 || !out || !size)
        return TP_E_ARG;
    if (t->body_mem)
        return tp_body_decode(t->body_mem, t->body_mem_size, out, cap, size);
    uint8_t *packed = tp_alloc(t->body.size);
    if (!packed)
        return TP_E_NOMEM;
    struct fill f = {packed, 0, t->body.size};
    int r = zip_stream(&t->src, &t->body, fill_fn, &f);
    if (!r)
        r = tp_body_decode(packed, f.have, out, cap, size);
    tp_free(packed);
    return r;
}

int tp_theme_bgm(const tp_theme *t, uint8_t *out, size_t cap, size_t *size)
{
    if (!t || !t->l2 || !out || !size)
        return TP_E_ARG;
    if (!t->has_bgm || !t->bgm.present)
        return TP_E_MISSING;
    if (t->bgm.size > cap)
        return TP_E_TOO_BIG;
    struct fill f = {out, 0, t->bgm.size};
    int r = zip_stream(&t->src, &t->bgm, fill_fn, &f);
    if (!r)
        *size = f.have;
    return r;
}
