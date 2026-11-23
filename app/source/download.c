#define _POSIX_C_SOURCE 200809L
#include "download.h"

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include "files.h"
#include "tp.h"

#ifdef __3DS__
#include <3ds.h>
static uint64_t now_us(void) { return (uint64_t)((double)svcGetSystemTick() / CPU_TICKS_PER_USEC); }
#else
#include <time.h>
static uint64_t now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}
#endif

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

static int starts_with(const char *s, const char *prefix)
{
    for (; *prefix; s++, prefix++)
        if (lower((unsigned char)*s) != *prefix)
            return 0;
    return 1;
}

int download_url_ok(const char *text)
{
    size_t n = strlen(text);
    if (n >= DOWNLOAD_URL_MAX || !(starts_with(text, "http://") || starts_with(text, "https://")))
        return 0;
    size_t host = starts_with(text, "https://") ? 8 : 7;
    if (n == host || text[host] == '/')
        return 0;
    for (size_t i = 0; i < n; i++)
        if ((unsigned char)text[i] <= ' ' || text[i] == 0x7F)
            return 0;
    return 1;
}

static int keep(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '.' ||
           c == '_' || c == '-' || c == '(' || c == ')';
}

static int hex(char c)
{
    return c >= '0' && c <= '9' ? c - '0' : lower((unsigned char)c) >= 'a' && lower((unsigned char)c) <= 'f'
                                                 ? lower((unsigned char)c) - 'a' + 10 : -1;
}

/* The candidate from a header value or a URL path part: percent escapes decoded, cut at quotes or ';'. */
static void take(const char *s, size_t n, char *out, size_t size)
{
    size_t o = 0;
    for (size_t i = 0; i < n && o + 1 < size; i++) {
        char c = s[i];
        if (c == '"' || c == ';')
            break;
        if (c == '%' && i + 2 < n && hex(s[i + 1]) >= 0 && hex(s[i + 2]) >= 0) {
            c = (char)(hex(s[i + 1]) * 16 + hex(s[i + 2]));
            i += 2;
        }
        out[o++] = c;
    }
    out[o] = 0;
}

static void from_disposition(const char *d, char *out, size_t size)
{
    out[0] = 0;
    for (const char *p = d; p && *p; p++) {
        if (!starts_with(p, "filename"))
            continue;
        const char *v = p + 8;
        int extended = *v == '*';
        v += extended;
        while (*v == ' ')
            v++;
        if (*v != '=')
            continue;
        v++;
        while (*v == ' ')
            v++;
        if (extended) {            /* filename*=UTF-8''name: the part after the second quote */
            const char *q = strstr(v, "''");
            if (!q)
                continue;
            v = q + 2;
        }
        if (*v == '"')
            v++;
        take(v, strlen(v), out, size);
        if (extended)
            return;                /* the extended form wins */
    }
}

static void from_url(const char *url, char *out, size_t size)
{
    const char *start = strstr(url, "://");
    start = start ? start + 3 : url;
    const char *path = strchr(start, '/');
    out[0] = 0;
    if (!path)
        return;
    size_t end = strcspn(path, "?#");
    const char *last = path;
    for (size_t i = 0; i < end; i++)
        if (path[i] == '/')
            last = path + i + 1;
    take(last, (size_t)(path + end - last), out, size);
}

void download_name(const char *disposition, const char *url, int kind, const char *dir, char *out, size_t size)
{
    char raw[256] = "", base[DOWNLOAD_NAME];
    if (disposition)
        from_disposition(disposition, raw, sizeof raw);
    if (!raw[0] && url)
        from_url(url, raw, sizeof raw);
    size_t n = 0;
    for (const char *p = raw; *p && n < 100; p++)
        base[n++] = keep(*p) ? *p : '_';
    base[n] = 0;
    char *dot = strrchr(base, '.');
    if (dot)
        *dot = 0;                  /* the extension comes from the content */
    char *b = base;
    while (*b == '.' || *b == ' ')
        b++;
    n = strlen(b);
    while (n && b[n - 1] == ' ')
        b[--n] = 0;
    if (!*b)
        b = "theme";
    const char *ext = kind == TP_KIND_CIA ? ".cia" : ".zip";
    for (int k = 1; k < 1000; k++) {
        char suffix[16] = "";
        if (k > 1)
            snprintf(suffix, sizeof suffix, " (%d)", k);
        snprintf(out, size, "%s%s%s", b, suffix, ext);
        char path[640];
        struct stat st;
        snprintf(path, sizeof path, "%s/%s", dir, out);
        if (stat(path, &st) != 0)
            return;
    }
}

/* ---------------------------------------------------------------- the transfer */

typedef struct transfer {
    FILE *f;
    uint64_t bytes;
    int too_big, write_failed;
    char disposition[512];
    char location[64];          /* the start of the last redirect's Location */
    int redirect;               /* the response under way is a redirect: its body is no part of the file */
    const download_progress *progress;
    download_result *res;       /* its pieces, largest and rcvbuf_* are filled as the transfer goes */
    uint64_t first_us, last_us, file_us;    /* the first body byte, the end of the last piece, the time in the file */
} transfer;

static size_t on_data(char *p, size_t size, size_t count, void *ctx)
{
    transfer *t = ctx;
    size_t n = size * count;
    uint64_t began = now_us();
    if (!t->res->pieces++)
        t->first_us = began;
    if (n > t->res->largest)
        t->res->largest = (unsigned)n;
    t->last_us = began;
    if (t->bytes + n > DOWNLOAD_MAX) {
        t->too_big = 1;
        return 0;
    }
    size_t written = fwrite(p, 1, n, t->f);
    t->last_us = now_us();
    t->file_us += t->last_us - began;
    if (written != n) {
        t->write_failed = 1;
        return 0;
    }
    t->bytes += n;
    return n;
}

static int rcvbuf(curl_socket_t s)
{
    int v = 0;
    socklen_t n = sizeof v;
    return getsockopt(s, SOL_SOCKET, SO_RCVBUF, &v, &n) == 0 && n > 0 && n <= sizeof v ? v : -1;
}

/* The socket's receive buffer (SO_RCVBUF at SOL_SOCKET, libctru's sys/socket.h) is read for the log's line and left
 * as the system made it: 8192 bytes on the console (its log, 2026-11-21), which caps the TCP window and so the
 * speed. "G18" asked for 256 KiB: the console took it ("rcvbuf 8192 -> 262144") and then received nothing, not the
 * response's first byte ("Recv failure: Network interface is not configured", then "Operation too slow"). With the
 * default the console measured 778 KB/s (its log, 2026-11-22), so nothing asks for more; the line keeps both
 * readings, before and after, to show it. */

/* Called for each socket curl makes, before it connects: the last one is the connection the body came on. */
static int on_socket(void *ctx, curl_socket_t s, curlsocktype purpose)
{
    (void)purpose;
    transfer *t = ctx;
    t->res->rcvbuf_before = rcvbuf(s);
    t->res->rcvbuf_after = rcvbuf(s);
    return CURL_SOCKOPT_OK;
}

static size_t on_header(char *p, size_t size, size_t count, void *ctx)
{
    transfer *t = ctx;
    size_t n = size * count;
    if (n >= 5 && !memcmp(p, "HTTP/", 5)) {
        t->disposition[0] = 0;     /* a new response (after a redirect): its own headers count */
        const char *code = memchr(p, ' ', n);
        t->redirect = code && code + 1 < p + n && code[1] == '3';
    }
    else if (n > 9 && starts_with(p, "location:")) {
        size_t a = 9;
        while (a < n && p[a] == ' ')
            a++;
        size_t m = n - a < sizeof t->location - 1 ? n - a : sizeof t->location - 1;
        memcpy(t->location, p + a, m);
        t->location[m] = 0;
    } else if (n > 20 && starts_with(p, "content-disposition:")) {
        size_t m = n - 20 < sizeof t->disposition - 1 ? n - 20 : sizeof t->disposition - 1;
        memcpy(t->disposition, p + 20, m);
        t->disposition[m] = 0;
        t->disposition[strcspn(t->disposition, "\r\n")] = 0;
    }
    return n;
}

static int on_progress(void *ctx, curl_off_t total, curl_off_t now, curl_off_t up_total, curl_off_t up_now)
{
    (void)up_total, (void)up_now;
    transfer *t = ctx;
    if (!t->progress || !t->progress->tick)
        return 0;
    if (t->redirect)                /* a redirect's own bytes: no size to show yet */
        return t->progress->tick(t->progress->ctx, 0, 0);
    return t->progress->tick(t->progress->ctx, (uint64_t)(now > 0 ? now : 0), (uint64_t)(total > 0 ? total : 0));
}

static int fail(download_result *res, int status, const char *part)
{
    remove(part);
    res->status = status;
    return status;
}

/* Is the part file one of the two kinds, with a theme that reads? */
static int check(const char *part, download_result *res)
{
    tp_source src;
    if (files_open_source(part, &src, NULL, NULL))
        return DL_WRITE;
    static tp_package package;
    const tp_source *zip;
    int r = tp_theme_in(&src, 1, &package, &zip, &res->kind);
    if (r == TP_OK) {
        tp_theme t;
        r = tp_theme_peek(&t, zip, NULL);
        tp_theme_close(&t);
    }
    files_close_source(&src);
    res->detail = r;
    return r == TP_OK ? DL_OK : r == TP_E_KIND ? DL_NOT_THEME : r == TP_E_NOMEM ? DL_NOMEM : DL_BAD_THEME;
}

/* The part file's own buffer for the transfer's time (setvbuf, full buffering): newlib's default (BUFSIZ, 1024 in
 * devkitARM's stdio.h) makes every few pieces a write to the card. From the heap, freed after fclose; without the
 * memory the file keeps the default buffer, and the log line says "file buffer 0". */
#define FILE_BUFFER 0x40000     /* 256 KiB */

int download_fetch(const char *url, const char *part_path, const char *ca_file, const char *user_agent,
                   const download_progress *progress, download_result *res, char *disposition,
                   size_t disposition_size)
{
    memset(res, 0, sizeof *res);
    res->rcvbuf_before = res->rcvbuf_after = -1;
    disposition[0] = 0;
    remove(part_path);
    if (!download_url_ok(url))
        return res->status = DL_BAD_URL;
    struct stat st;
    int have_ca = ca_file && stat(ca_file, &st) == 0;
    if (starts_with(url, "https://") && !have_ca)
        return res->status = DL_NO_CA;
    transfer t = {0};
    t.progress = progress;
    t.res = res;
    if (!(t.f = fopen(part_path, "wb")))
        return res->status = DL_WRITE;
    char *file_buffer = malloc(FILE_BUFFER);
    if (file_buffer && setvbuf(t.f, file_buffer, _IOFBF, FILE_BUFFER)) {
        free(file_buffer);
        file_buffer = NULL;
    }
    res->file_buffer = file_buffer ? FILE_BUFFER : 0;
    CURL *c = curl_easy_init();
    if (!c) {
        fclose(t.f);
        free(file_buffer);
        return fail(res, DL_NOMEM, part_path);
    }
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_PROTOCOLS_STR, "http,https");
    /* Without the card's list no https at all, also not after a redirect from http. */
    curl_easy_setopt(c, CURLOPT_REDIR_PROTOCOLS_STR, have_ca ? "http,https" : "http");
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(c, CURLOPT_USERAGENT, user_agent);
    curl_easy_setopt(c, CURLOPT_MAXFILESIZE_LARGE, (curl_off_t)DOWNLOAD_MAX);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 20L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 100L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 30L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 2L);
    /* Always the card's list, also when it is missing: TLS then fails instead of falling back to the library's
     * built-in path (sdmc:/config/ssl/cacert.pem for the 3DS portlib) or a certificate folder. */
    if (ca_file)
        curl_easy_setopt(c, CURLOPT_CAINFO, ca_file);
    curl_easy_setopt(c, CURLOPT_CAPATH, NULL);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, on_data);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &t);
    curl_easy_setopt(c, CURLOPT_HEADERFUNCTION, on_header);
    curl_easy_setopt(c, CURLOPT_HEADERDATA, &t);
    curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, on_progress);
    curl_easy_setopt(c, CURLOPT_XFERINFODATA, &t);
    curl_easy_setopt(c, CURLOPT_ERRORBUFFER, res->error);
    curl_easy_setopt(c, CURLOPT_SOCKOPTFUNCTION, on_socket);
    curl_easy_setopt(c, CURLOPT_SOCKOPTDATA, &t);
    uint64_t start = now_us();
    CURLcode r = curl_easy_perform(c);
    uint64_t end = now_us();
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &res->http);
    curl_easy_cleanup(c);
    uint64_t closing = now_us();
    int closed = fclose(t.f);
    t.file_us += now_us() - closing;
    free(file_buffer);
    res->bytes = t.bytes;
    res->first_ms = (unsigned)(((res->pieces ? t.first_us : end) - start) / 1000);
    res->body_ms = res->pieces ? (unsigned)((t.last_us - t.first_us) / 1000) : 0;
    res->file_ms = (unsigned)(t.file_us / 1000);
    if (r != CURLE_OK) {
        if (!res->error[0])
            snprintf(res->error, sizeof res->error, "%s", curl_easy_strerror(r));
        if (t.too_big || r == CURLE_FILESIZE_EXCEEDED)
            return fail(res, DL_TOO_BIG, part_path);
        if (r == CURLE_ABORTED_BY_CALLBACK)
            return fail(res, DL_CANCELLED, part_path);
        if (t.write_failed || r == CURLE_WRITE_ERROR)
            return fail(res, DL_WRITE, part_path);
        if (r == CURLE_UNSUPPORTED_PROTOCOL && !have_ca && starts_with(t.location, "https://"))
            return fail(res, DL_NO_CA, part_path);  /* redirected to https without the list */
        if (r == CURLE_PEER_FAILED_VERIFICATION || r == CURLE_SSL_CACERT_BADFILE || r == CURLE_SSL_CERTPROBLEM)
            return fail(res, DL_CERT, part_path);
        return fail(res, DL_NETWORK, part_path);
    }
    if (closed)
        return fail(res, DL_WRITE, part_path);
    if (res->http < 200 || res->http > 299)
        return fail(res, DL_HTTP, part_path);
    snprintf(disposition, disposition_size, "%s", t.disposition);
    return res->status = DL_OK;
}

int download(const char *url, const char *part_path, const char *dir, const char *ca_file, const char *user_agent,
             const download_progress *progress, download_result *res)
{
    char disposition[sizeof ((transfer *)0)->disposition];
    int status = download_fetch(url, part_path, ca_file, user_agent, progress, res, disposition, sizeof disposition);
    if (status != DL_OK)
        return status;
    status = check(part_path, res);
    if (status != DL_OK)
        return fail(res, status, part_path);
    mkdir(dir, 0777);
    download_name(disposition, url, res->kind, dir, res->file, sizeof res->file);
    char final[640];
    snprintf(final, sizeof final, "%s/%s", dir, res->file);
    if (rename(part_path, final))
        return fail(res, DL_WRITE, part_path);
    return res->status = DL_OK;
}
