/* SPDX-License-Identifier: MIT */
#include "web.h"
#include "log.h"
#include <sys/stat.h>
#include <fcntl.h>

typedef struct {
    const char *ext;
    const char *mime;
} ws_mime_entry_t;

static const ws_mime_entry_t mime_table[] = {
    {".html", "text/html"},
    {".htm",  "text/html"},
    {".css",  "text/css"},
    {".js",   "application/javascript"},
    {".json", "application/json"},
    {".png",  "image/png"},
    {".jpg",  "image/jpeg"},
    {".jpeg", "image/jpeg"},
    {".gif",  "image/gif"},
    {".svg",  "image/svg+xml"},
    {".ico",  "image/x-icon"},
    {".woff", "font/woff"},
    {".woff2","font/woff2"},
    {".ttf",  "font/ttf"},
    {".txt",  "text/plain"},
    {".xml",  "application/xml"},
    {".wasm", "application/wasm"},
    {NULL, NULL}
};

const char *ws_mime_type(const char *path) {
    const char *dot = strrchr(path, '.');
    if (!dot) return "application/octet-stream";

    for (const ws_mime_entry_t *m = mime_table; m->ext; m++) {
#ifdef WS_PLATFORM_WINDOWS
        if (_stricmp(dot, m->ext) == 0) return m->mime;
#else
        if (strcasecmp(dot, m->ext) == 0) return m->mime;  /* cppcheck-suppress strcaseCmpInstead */
#endif
    }
    return "application/octet-stream";
}

/* Sanitize path: prevent directory traversal */
static int sanitize_path(const char *web_dir, const char *uri_path,
                         char *full_path, size_t full_path_size) {
    /* Reject paths with .. */
    if (strstr(uri_path, "..") != NULL) return -1;

    snprintf(full_path, full_path_size, "%s%s", web_dir, uri_path);

    /* If path ends with /, append index.html */
    size_t plen = strlen(full_path);
    if (plen > 0 && full_path[plen - 1] == '/') {
        snprintf(full_path + plen, full_path_size - plen, "index.html");
    }

    /* Check that resolved path starts with web_dir */
    char resolved[4096];
#ifdef WS_PLATFORM_POSIX
    if (!realpath(full_path, resolved)) return -1;
    char resolved_dir[4096];
    if (!realpath(web_dir, resolved_dir)) return -1;

    if (strncmp(resolved, resolved_dir, strlen(resolved_dir)) != 0)
        return -1;  /* path traversal attempt */

    snprintf(full_path, full_path_size, "%s", resolved);
#else
    (void)resolved;
#endif
    return 0;
}

int ws_web_serve_file(const char *web_dir, const ws_http_request_t *req,
                      char *resp_buf, int resp_buf_size,
                      int *file_fd, size_t *file_size) {
    *file_fd = -1;
    *file_size = 0;

    if (!web_dir || !web_dir[0])
        return ws_http_error_response(resp_buf, resp_buf_size, 404, "No web directory configured");

    /* Only allow GET and HEAD */
    if (strcmp(req->method, "GET") != 0 && strcmp(req->method, "HEAD") != 0)
        return ws_http_error_response(resp_buf, resp_buf_size, 405, "Method Not Allowed");

    char full_path[4096];
    if (sanitize_path(web_dir, req->path, full_path, sizeof(full_path)) < 0)
        return ws_http_error_response(resp_buf, resp_buf_size, 403, "Forbidden");

    struct stat st;
    if (stat(full_path, &st) < 0)
        return ws_http_error_response(resp_buf, resp_buf_size, 404, "Not Found");

    /* If it's a directory, try index.html */
    if (S_ISDIR(st.st_mode)) {
        char idx_path[4096];
        snprintf(idx_path, sizeof(idx_path), "%s/index.html", full_path);
        if (stat(idx_path, &st) < 0)
            return ws_http_error_response(resp_buf, resp_buf_size, 403, "Directory listing denied");
        snprintf(full_path, sizeof(full_path), "%s", idx_path);
    }

    /* Open file */
    int fd = open(full_path, O_RDONLY);
    if (fd < 0)
        return ws_http_error_response(resp_buf, resp_buf_size, 500, "Cannot open file");

    const char *mime = ws_mime_type(full_path);

    char size_str[32];
    snprintf(size_str, sizeof(size_str), "%lld", (long long)st.st_size);

    ws_http_header_t headers[] = {
        {"Content-Type", mime},
        {"Content-Length", size_str},
        {"Cache-Control", "max-age=3600"},
        {"Connection", "close"}
    };

    int n = ws_http_response(resp_buf, resp_buf_size, 200, "OK", headers, 4, NULL, 0);

    *file_fd = fd;
    *file_size = (size_t)st.st_size;

    return n;
}
