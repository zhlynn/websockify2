/* SPDX-License-Identifier: MIT */
#include "http.h"
#include "log.h"
#include <ctype.h>

static int strcasecmp_n(const char *a, const char *b) {
#ifdef WS_PLATFORM_WINDOWS
    return _stricmp(a, b);
#else
    return strcasecmp(a, b);
#endif
}

static int strncasecmp_n(const char *a, const char *b, size_t n) {
#ifdef WS_PLATFORM_WINDOWS
    return _strnicmp(a, b, n);
#else
    return strncasecmp(a, b, n);
#endif
}

/* URL decode in-place */
static void url_decode(char *dst, const char *src) {
    while (*src) {
        if (*src == '%' && isxdigit((unsigned char)src[1]) && isxdigit((unsigned char)src[2])) {
            char hex[3] = { src[1], src[2], '\0' };
            *dst++ = (char)strtol(hex, NULL, 16);
            src += 3;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

int ws_http_parse_request(ws_http_request_t *req, const char *data, int len) {
    /* Find end of headers (\r\n\r\n) */
    const char *end = NULL;
    for (int i = 0; i < len - 3; i++) {
        if (data[i] == '\r' && data[i+1] == '\n' && data[i+2] == '\r' && data[i+3] == '\n') {
            end = data + i + 4;
            break;
        }
    }
    if (!end) return 0;  /* need more data */

    int total = (int)(end - data);

    /* Make a mutable copy */
    req->raw = (char *)malloc((size_t)total + 1);
    if (!req->raw) return -1;
    memcpy(req->raw, data, (size_t)total);
    req->raw[total] = '\0';
    req->raw_len = total;

    char *p = req->raw;

    /* Parse request line: METHOD URI HTTP/x.x\r\n */
    char *line_end = strstr(p, "\r\n");
    if (!line_end) return -1;
    *line_end = '\0';

    /* Method */
    char *sp = strchr(p, ' ');
    if (!sp) return -1;
    size_t mlen = (size_t)(sp - p);
    if (mlen >= sizeof(req->method)) return -1;
    memcpy(req->method, p, mlen);
    req->method[mlen] = '\0';
    p = sp + 1;

    /* URI */
    sp = strchr(p, ' ');
    if (!sp) return -1;
    size_t ulen = (size_t)(sp - p);
    if (ulen >= sizeof(req->uri)) return -1;
    memcpy(req->uri, p, ulen);
    req->uri[ulen] = '\0';

    /* Split URI into path and query */
    char *qmark = strchr(req->uri, '?');
    if (qmark) {
        *qmark = '\0';
        url_decode(req->path, req->uri);
        snprintf(req->query, sizeof(req->query), "%s", qmark + 1);
        *qmark = '?';  /* restore */
    } else {
        url_decode(req->path, req->uri);
        req->query[0] = '\0';
    }

    /* Parse headers */
    p = line_end + 2;
    req->header_count = 0;

    while (p < req->raw + total - 2) {
        line_end = strstr(p, "\r\n");
        if (!line_end) break;
        /* RFC 7230 recommends 8000 bytes max per header line */
        if ((size_t)(line_end - p) > 8000) return -1;
        *line_end = '\0';

        if (p == line_end) break;  /* empty line */

        char *colon = strchr(p, ':');
        if (!colon) { p = line_end + 2; continue; }

        *colon = '\0';
        char *value = colon + 1;
        while (*value == ' ') value++;

        /* Trim trailing whitespace from header name */
        char *hend = colon - 1;
        while (hend > p && *hend == ' ') { *hend = '\0'; hend--; }

        if (req->header_count < WS_HTTP_MAX_HEADERS) {
            req->headers[req->header_count].name = p;
            req->headers[req->header_count].value = value;
            req->header_count++;
        }

        p = line_end + 2;
    }

    /* Detect WebSocket upgrade */
    req->host = ws_http_get_header(req, "Host");
    req->ws_key = ws_http_get_header(req, "Sec-WebSocket-Key");
    req->ws_version = ws_http_get_header(req, "Sec-WebSocket-Version");
    req->ws_protocol = ws_http_get_header(req, "Sec-WebSocket-Protocol");

    req->upgrade_websocket =
        ws_http_header_contains(req, "Connection", "Upgrade") &&
        ws_http_header_contains(req, "Upgrade", "websocket");

    req->complete = 1;
    return total;
}

const char *ws_http_get_header(const ws_http_request_t *req, const char *name) {
    for (int i = 0; i < req->header_count; i++) {
        if (strcasecmp_n(req->headers[i].name, name) == 0)
            return req->headers[i].value;
    }
    return NULL;
}

int ws_http_header_contains(const ws_http_request_t *req, const char *name, const char *token) {
    const char *value = ws_http_get_header(req, name);
    if (!value) return 0;

    size_t tlen = strlen(token);
    const char *p = value;
    while (*p) {
        while (*p == ' ' || *p == ',') p++;
        if (strncasecmp_n(p, token, tlen) == 0) {
            char c = p[tlen];
            if (c == '\0' || c == ',' || c == ' ' || c == ';')
                return 1;
        }
        while (*p && *p != ',') p++;
    }
    return 0;
}

void ws_http_request_free(ws_http_request_t *req) {
    free(req->raw);
    req->raw = NULL;
    req->raw_len = 0;
    req->header_count = 0;
    req->complete = 0;
}

int ws_http_response(char *buf, int buf_size, int status, const char *status_text,
                     const ws_http_header_t *headers, int header_count,
                     const char *body, int body_len) {
    int n = snprintf(buf, (size_t)buf_size, "HTTP/1.1 %d %s\r\n", status, status_text);

    for (int i = 0; i < header_count; i++) {
        n += snprintf(buf + n, (size_t)(buf_size - n), "%s: %s\r\n",
                     headers[i].name, headers[i].value);
    }

    if (body && body_len > 0) {
        n += snprintf(buf + n, (size_t)(buf_size - n), "Content-Length: %d\r\n", body_len);
    }

    n += snprintf(buf + n, (size_t)(buf_size - n), "\r\n");

    if (body && body_len > 0 && n + body_len < buf_size) {
        memcpy(buf + n, body, (size_t)body_len);
        n += body_len;
    }

    return n;
}

int ws_http_ws_upgrade_response(char *buf, int buf_size,
                                 const char *accept_key,
                                 const char *protocol) {
    int n = snprintf(buf, (size_t)buf_size,
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: %s\r\n",
        accept_key);

    if (protocol && protocol[0])
        n += snprintf(buf + n, (size_t)(buf_size - n),
                     "Sec-WebSocket-Protocol: %s\r\n", protocol);

    n += snprintf(buf + n, (size_t)(buf_size - n), "\r\n");
    return n;
}

int ws_http_error_response(char *buf, int buf_size, int status, const char *message) {
    const char *status_text = "Error";
    switch (status) {
    case 400: status_text = "Bad Request"; break;
    case 403: status_text = "Forbidden"; break;
    case 404: status_text = "Not Found"; break;
    case 405: status_text = "Method Not Allowed"; break;
    case 500: status_text = "Internal Server Error"; break;
    case 503: status_text = "Service Unavailable"; break;
    }

    char body[512];
    int body_len = snprintf(body, sizeof(body),
        "<html><body><h1>%d %s</h1><p>%s</p></body></html>",
        status, status_text, message ? message : status_text);

    ws_http_header_t headers[] = {
        {"Content-Type", "text/html"},
        {"Connection", "close"}
    };

    return ws_http_response(buf, buf_size, status, status_text,
                           headers, 2, body, body_len);
}
