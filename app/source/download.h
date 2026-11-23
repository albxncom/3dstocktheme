/* Downloading the file a QR code points at, with libcurl (the 3ds-curl portlib on the console, the system's on the
 * Mac for the tests). The address comes only from the code; nothing about any site is built in.
 *
 * Rules: http and https only, also after redirects (at most 5); https only with the certificate list on the card
 * (ca_file): libcurl is always pointed at that file and nothing else, and without it neither an https address nor a
 * redirect to one is followed; the application names itself in User-Agent; at most DOWNLOAD_MAX bytes; the file goes
 * to a part file first and is kept only if its content is a theme zip or a theme installer CIA whose theme reads
 * (tp_theme_in, tp_theme_peek); then it is renamed into the themes folder under a cleaned name with the extension
 * of its kind. On any failure or cancel the part file is removed, so nothing partial stays on the card. */
#ifndef DOWNLOAD_H
#define DOWNLOAD_H

#include <stddef.h>
#include <stdint.h>

#define DOWNLOAD_MAX (16u << 20)   /* a theme's own data is at most 0x150000 + 0x337000 bytes; zips from theme
                                       sites also carry previews and a second copy of the music */
#define DOWNLOAD_URL_MAX 2048
#define DOWNLOAD_NAME 128

enum download_status {
    DL_OK,
    DL_BAD_URL,         /* not an http or https address */
    DL_NO_CA,           /* https (asked for, or redirected to), but no certificate list on the card */
    DL_NETWORK,         /* no connection, DNS, TLS, too many redirects, timeout: detail in error */
    DL_CERT,            /* the site's certificate did not check out against the list */
    DL_HTTP,            /* an HTTP status other than 2xx */
    DL_TOO_BIG,
    DL_NOT_THEME,       /* neither a theme zip nor a theme installer CIA (a web page, for example) */
    DL_BAD_THEME,       /* one of the two kinds, but damaged (detail: tp_result) */
    DL_CANCELLED,
    DL_WRITE,           /* the card could not be written */
    DL_NOMEM
};

typedef struct download_result {
    int status;
    long http;                  /* the last HTTP status */
    int detail;                 /* tp_result for DL_BAD_THEME */
    int kind;                   /* TP_KIND_ZIP or TP_KIND_CIA for DL_OK */
    uint64_t bytes;
    char error[256];            /* libcurl's words for DL_NETWORK and DL_CERT */
    char file[DOWNLOAD_NAME];   /* the name in the themes folder for DL_OK */
    /* What the transfer saw, for the log's "download:" line (main.c, download_done); filled on a failure or a cancel
     * too, with what there was. */
    unsigned first_ms;          /* from the start to the first body byte (name lookup, connect, TLS, redirects);
                                   with no body byte the whole transfer's time */
    unsigned body_ms;           /* from the first body byte to the last */
    unsigned pieces, largest;   /* how many pieces curl handed to the write callback, and the largest in bytes */
    unsigned file_ms;           /* in the part file's fwrite and fclose */
    int rcvbuf_before, rcvbuf_after;    /* the socket's receive buffer as getsockopt reads it when the socket is
                                           made and after a change; -1 where it cannot be read */
    unsigned file_buffer;       /* the part file's own buffer in bytes; 0: none (no memory), newlib's default */
} download_result;

typedef struct download_progress {
    void *ctx;
    /* Told how far it is (total 0 if unknown); returning nonzero cancels. */
    int (*tick)(void *ctx, uint64_t done, uint64_t total);
} download_progress;

/* Is text an address this application downloads from (http or https, no spaces or control characters)? */
int download_url_ok(const char *text);

/* The file name to save under: the Content-Disposition file name if any, else the address's last path part,
 * else "theme"; only letters, digits, space and ". _ - ( )" kept, at most 100 bytes, the extension set by kind,
 * and " (2)", " (3)" ... added while the name exists in dir. */
void download_name(const char *disposition, const char *url, int kind, const char *dir, char *out, size_t size);

/* The transfer of download() alone, with the same rules: url to part_path, no look at the content. DL_OK leaves the
 * part file in place; on any other status it is removed. disposition gets the last response's Content-Disposition
 * value ("" if none), for download_name. */
int download_fetch(const char *url, const char *part_path, const char *ca_file, const char *user_agent,
                   const download_progress *progress, download_result *res, char *disposition,
                   size_t disposition_size);

/* Downloads url to part_path, checks it, and renames it into dir. ca_file is the certificate list for https, the
 * only one libcurl is given (NULL or missing: https and redirects to https are refused). curl_global_init must
 * have run; on the console the socket service too. */
int download(const char *url, const char *part_path, const char *dir, const char *ca_file, const char *user_agent,
             const download_progress *progress, download_result *res);

#endif
