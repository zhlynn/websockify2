/* SPDX-License-Identifier: MIT */
#ifndef WS_HTTP_H
#define WS_HTTP_H

#include "platform.h"
#include "buf.h"

#define WS_HTTP_MAX_HEADERS 32
#define WS_HTTP_MAX_URI     4096

typedef struct {
    const char *name;
    const char *value;
} ws_http_header_t;

typedef struct {
    /* Request line */
    char method[16];
    char uri[WS_HTTP_MAX_URI];
    char path[WS_HTTP_MAX_URI];   /* decoded path component */
    char query[1024];              /* query string (after ?) */

    /* Headers */
    ws_http_header_t headers[WS_HTTP_MAX_HEADERS];
    int              header_count;

    /* Parsed state */
    int  complete;
    int  upgrade_websocket;  /* true if Connection: Upgrade + Upgrade: websocket */

    /* WebSocket specific headers */
    const char *ws_key;
    const char *ws_version;
    const char *ws_protocol;
    const char *host;

    /* Raw buffer (owns the strings pointed to by headers) */
    char *raw;
    int   raw_len;
} ws_http_request_t;

/* Parse HTTP request from buffer. Returns:
 *  > 0: complete request, value is total bytes consumed
 *    0: need more data
 *   -1: parse error */
int ws_http_parse_request(ws_http_request_t *req, const char *data, int len);

/* Find header value (case-insensitive). Returns NULL if not found */
const char *ws_http_get_header(const ws_http_request_t *req, const char *name);

/* Check if header contains a token (case-insensitive, comma-separated) */
int ws_http_header_contains(const ws_http_request_t *req, const char *name, const char *token);

/* Free request resources */
void ws_http_request_free(ws_http_request_t *req);

/* Build HTTP response into buffer. Returns bytes written */
int ws_http_response(char *buf, int buf_size, int status, const char *status_text,
                     const ws_http_header_t *headers, int header_count,
                     const char *body, int body_len);

/* Build WebSocket upgrade response */
int ws_http_ws_upgrade_response(char *buf, int buf_size,
                                 const char *accept_key,
                                 const char *protocol);

/* Build simple error response */
int ws_http_error_response(char *buf, int buf_size, int status, const char *message);

#endif /* WS_HTTP_H */
