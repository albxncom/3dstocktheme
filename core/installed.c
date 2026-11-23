/* Reading back what is on the console: the catalogue of an installed content 0, and whether a stored TMD is
 * still what AM reports. The bytes come from the application; they are checked like any other input. */
#include "internal.h"

#define ENTRY_SIZE 0xC8u

static int read_at(const tp_source *src, uint64_t offset, void *buf, size_t size)
{
    if (offset > src->size || size > src->size - offset)
        return TP_E_CATALOGUE;
    return src->read(src->ctx, offset, buf, size) ? TP_E_READ : TP_OK;
}

int tp_installed_top(const tp_source *archive, uint16_t *top)
{
    uint8_t h[8];
    if (!archive || !archive->read || !top)
        return TP_E_ARG;
    int r = read_at(archive, 0, h, sizeof h);
    if (r)
        return r;
    uint32_t version = get_le32(h), count = get_le32(h + 4);
    if (version != 1 || count < 1 || count > TP_INDEX_MAX || archive->size != 8 + ENTRY_SIZE * ((uint64_t)count + 1))
        return TP_E_CATALOGUE;
    *top = (uint16_t)count;
    return TP_OK;
}

/* A NUL-terminated copy of a text field (at most its size, the copy has room for one more byte), UTF-8. */
static int text_field(char *out, const uint8_t *field, size_t size)
{
    size_t n = 0;
    while (n < size && field[n])
        n++;
    memcpy(out, field, n);
    out[n] = 0;
    return utf8_valid(out) ? TP_OK : TP_E_CATALOGUE;
}

int tp_installed_entry(const tp_source *archive, const tp_source *icon, uint16_t index, tp_installed *entry)
{
    uint16_t top;
    uint8_t e[ENTRY_SIZE];
    if (!entry || (icon && !icon->read))
        return TP_E_ARG;
    memset(entry, 0, sizeof *entry);
    int r = tp_installed_top(archive, &top);
    if (r)
        return r;
    if (index > top)
        return TP_E_INDEX;
    if ((r = read_at(archive, 8 + (uint64_t)ENTRY_SIZE * index, e, sizeof e)) != TP_OK)
        return r;
    if (get_le32(e + 0xC0) != index)
        return TP_E_CATALOGUE;      /* HOME takes entry i by position; it must say i */
    if ((r = text_field(entry->name, e, 0x40)) != TP_OK || (r = text_field(entry->description, e + 0x40, 0x80)))
        return r;
    entry->index = index;
    entry->enabled = get_le32(e + 0xC4) != 0;
    if (icon) {
        if (icon->size != TP_ICON_SIZE)
            return TP_E_CATALOGUE;
        if ((r = read_at(icon, 0, entry->icon, TP_ICON_SIZE)) != TP_OK)
            return r;
        entry->has_icon = 1;
    }
    return TP_OK;
}

int tp_tmd_check(const uint8_t *tmd, size_t size, uint16_t version, const tp_am_content *am, size_t count,
                 tp_difference *d)
{
    uint16_t stored;
    size_t records;
    if (!d || (count && !am))
        return TP_E_ARG;
    memset(d, 0, sizeof *d);
    int r = tp_tmd_read(tmd, size, NULL, &stored, &records);
    if (r)
        return r;
    if (stored != version) {
        d->what = TP_DIFF_VERSION;
        return TP_E_STALE;
    }
    for (size_t k = 0; k < records; k++) {
        tp_content rec;
        tp_tmd_record(tmd, size, k, &rec);
        size_t j = 0;
        while (j < count && am[j].index != rec.index)
            j++;
        d->record = k;
        d->index = rec.index;
        if (j == count)
            d->what = TP_DIFF_MISSING;
        else if (am[j].id != rec.id)
            d->what = TP_DIFF_ID;
        else if (am[j].size != rec.size)
            d->what = TP_DIFF_SIZE;
        if (d->what)
            return TP_E_STALE;
    }
    /* Every TMD index is in AM's list; anything more there is an index the TMD lacks, or one listed twice. */
    uint8_t seen[TP_INDEX_MAX + 1] = {0};
    for (size_t j = 0; j < count; j++) {
        int extra = am[j].index > TP_INDEX_MAX || seen[am[j].index];
        if (!extra) {
            seen[am[j].index] = 1;
            extra = 1;
            for (size_t k = 0; k < records && extra; k++) {
                tp_content rec;
                tp_tmd_record(tmd, size, k, &rec);
                extra = rec.index != am[j].index;
            }
        }
        if (extra) {
            d->what = TP_DIFF_EXTRA;
            d->record = j;
            d->index = am[j].index;
            return TP_E_STALE;
        }
    }
    d->record = 0;
    d->index = 0;
    return TP_OK;
}
