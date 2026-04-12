/* SPDX-License-Identifier: MIT */
#ifndef WS_PROXY_H
#define WS_PROXY_H

#include "platform.h"
#include "event.h"
#include "buf.h"
#include "http.h"
#include "ws.h"
#include "ssl_conn.h"
#include "config.h"
#include "token.h"
#include "record.h"

typedef enum {
    CONN_STATE_HANDSHAKE,      /* Reading HTTP upgrade request / SSL handshake */
    CONN_STATE_SSL_HANDSHAKE,  /* SSL handshake in progress */
    CONN_STATE_HTTP_REQUEST,   /* Reading HTTP request */
    CONN_STATE_CONNECTING,     /* Connecting to target */
    CONN_STATE_PROXYING,       /* Bidirectional proxy active */
    CONN_STATE_WEB_SERVING,    /* Serving static file */
    CONN_STATE_CLOSING,        /* Graceful close in progress */
    CONN_STATE_CLOSED
} ws_conn_state_t;

typedef struct ws_conn {
    ws_socket_t      client_fd;
    ws_socket_t      target_fd;

    ws_conn_state_t  state;

    /* SSL */
    SSL             *client_ssl;

    /* Buffers: client side */
    ws_buf_t         client_recv;   /* raw data from client */
    ws_buf_t         client_send;   /* data to send to client */

    /* Buffers: target side */
    ws_buf_t         target_recv;   /* raw data from target */
    ws_buf_t         target_send;   /* data to send to target */

    /* HTTP request (used during handshake) */
    ws_dbuf_t        http_buf;

    /* WebSocket frame parsing state */
    ws_frame_header_t ws_hdr;
    int               ws_hdr_parsed;
    uint64_t          ws_payload_read;

    /* Static file serving */
    int              file_fd;
    size_t           file_size;
    size_t           file_sent;

    /* Target info */
    ws_target_t      target;

    /* Recording */
    ws_record_t     *record;

    /* Timestamps */
    uint64_t         created_at;
    uint64_t         last_activity;

    /* Intrusive links for the active-connection list */
    struct ws_conn  *next;
    struct ws_conn  *prev;

    /* Client address for logging */
    char             client_addr[64];
    int              client_port;

    /* Back-reference to event loop */
    ws_event_loop_t *loop;
} ws_conn_t;

typedef struct {
    ws_config_t     *config;
    ws_event_loop_t *loop;
    ws_socket_t      listen_fd;
    ws_ssl_ctx_t    *ssl_ctx;
    ws_token_ctx_t  *token_ctx;

    /* Active connections (doubly-linked for O(1) removal) */
    ws_conn_t       *active_list;
    int              conn_count;
} ws_proxy_ctx_t;

/* Initialize proxy context */
int  ws_proxy_init(ws_proxy_ctx_t *ctx, ws_config_t *config,
                   ws_event_loop_t *loop, ws_socket_t listen_fd);
void ws_proxy_cleanup(ws_proxy_ctx_t *ctx);

/* Called by event loop when listen socket is readable */
void ws_proxy_on_accept(ws_event_loop_t *loop, ws_socket_t fd, int events, void *data);

#endif /* WS_PROXY_H */
