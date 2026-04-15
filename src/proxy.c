/* SPDX-License-Identifier: MIT */
#include "proxy.h"
#include "http.h"
#include "crypto.h"
#include "net.h"
#include "web.h"
#include "log.h"

#ifdef WS_PLATFORM_POSIX
#include <sys/uio.h>
#endif

/* Forward declarations */
static void conn_close(ws_conn_t *conn);
static void on_client_event(ws_event_loop_t *loop, ws_socket_t fd, int events, void *data);
static void on_target_event(ws_event_loop_t *loop, ws_socket_t fd, int events, void *data);
static void on_target_connect(ws_event_loop_t *loop, ws_socket_t fd, int events, void *data);
static int  process_ws_frame(ws_conn_t *conn);
static int  flush_client_send(ws_conn_t *conn);
static int  flush_target_send(ws_conn_t *conn);

/* ---- Connection lifecycle ---- */

static ws_conn_t *conn_alloc(ws_proxy_ctx_t *ctx) {
    ws_conn_t *conn = (ws_conn_t *)calloc(1, sizeof(*conn));
    if (!conn) return NULL;

    conn->client_fd = WS_INVALID_SOCKET;
    conn->target_fd = WS_INVALID_SOCKET;

    /* Insert at head of active list */
    conn->next = ctx->active_list;
    conn->prev = NULL;
    if (ctx->active_list) ctx->active_list->prev = conn;
    ctx->active_list = conn;
    ctx->conn_count++;
    return conn;
}

static void conn_release(ws_proxy_ctx_t *ctx, ws_conn_t *conn) {
    /* Remove from active list */
    if (conn->prev) conn->prev->next = conn->next;
    else ctx->active_list = conn->next;
    if (conn->next) conn->next->prev = conn->prev;

    ctx->conn_count--;
    free(conn);
}

int ws_proxy_init(ws_proxy_ctx_t *ctx, ws_config_t *config,
                  ws_event_loop_t *loop, ws_socket_t listen_fd) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->config = config;
    ctx->loop = loop;
    ctx->listen_fd = listen_fd;

    /* SSL context */
    if (config->cert_file[0]) {
        ws_ssl_init_library();
        ctx->ssl_ctx = ws_ssl_ctx_create(config->cert_file, config->key_file,
                                          config->ca_file, config->ssl_ciphers,
                                          config->verify_client, config->ssl_only);
        if (!ctx->ssl_ctx) {
            ws_log_error("Failed to initialize SSL context");
            return -1;
        }
    }

    /* Token context */
    if (config->token_plugin[0]) {
        ctx->token_ctx = (ws_token_ctx_t *)malloc(sizeof(ws_token_ctx_t));
        if (!ctx->token_ctx) {
            ws_log_error("out of memory allocating token context");
            return -1;
        }
        if (ws_token_init(ctx->token_ctx, config->token_plugin) < 0) {
            free(ctx->token_ctx);
            ctx->token_ctx = NULL;
            return -1;
        }
    }

    /* Register listen socket */
    ws_event_add(loop, listen_fd, WS_EV_READ, ws_proxy_on_accept, ctx);

    return 0;
}

void ws_proxy_cleanup(ws_proxy_ctx_t *ctx) {
    /* Close and free all active connections */
    ws_conn_t *conn = ctx->active_list;
    while (conn) {
        ws_conn_t *next = conn->next;
        conn_close(conn);
        free(conn);
        conn = next;
    }
    ctx->active_list = NULL;
    ctx->conn_count = 0;

    if (ctx->ssl_ctx) ws_ssl_ctx_destroy(ctx->ssl_ctx);
    if (ctx->token_ctx) {
        ws_token_free(ctx->token_ctx);
        free(ctx->token_ctx);
    }
}

/* ---- Accept new connection ---- */

void ws_proxy_on_accept(ws_event_loop_t *loop, ws_socket_t fd, int events, void *data) {
    (void)events;
    (void)fd;
    ws_proxy_ctx_t *ctx = (ws_proxy_ctx_t *)data;

    /* Accept multiple connections per event (edge-triggered) */
    for (;;) {
        char addr[64] = {0};
        int port = 0;
        ws_socket_t client_fd = ws_accept(ctx->listen_fd, addr, sizeof(addr), &port);
        if (client_fd == WS_INVALID_SOCKET) break;

        ws_conn_t *conn = conn_alloc(ctx);
        if (!conn) {
            ws_log_warn("out of memory allocating connection, rejecting %s:%d", addr, port);
            ws_close_socket(client_fd);
            break;
        }

        conn->client_fd = client_fd;
        conn->file_fd = -1;
        conn->loop = loop;
        conn->created_at = ws_time_ms();
        conn->last_activity = conn->created_at;
        snprintf(conn->client_addr, sizeof(conn->client_addr), "%s", addr);
        conn->client_port = port;

        ws_buf_init(&conn->client_recv);
        ws_buf_init(&conn->client_send);
        ws_buf_init(&conn->target_recv);
        ws_buf_init(&conn->target_send);
        ws_dbuf_init(&conn->http_buf);

        ws_set_tcp_nodelay(client_fd, 1);
        ws_set_tcp_keepalive(client_fd, &ctx->config->keepalive);

        /* Store proxy context in the connection for later use */
        /* We use the active_list to find context back from connection */

        if (ctx->ssl_ctx) {
            /* Detect SSL */
            if (ws_ssl_detect(client_fd)) {
                conn->client_ssl = ws_ssl_new(ctx->ssl_ctx, client_fd);
                if (!conn->client_ssl) {
                    conn_close(conn);
                    conn_release(ctx, conn);
                    continue;
                }
                conn->state = CONN_STATE_SSL_HANDSHAKE;
            } else if (ctx->ssl_ctx->ssl_only) {
                ws_log_debug("rejecting non-SSL connection from %s:%d", addr, port);
                conn_close(conn);
                conn_release(ctx, conn);
                continue;
            } else {
                conn->state = CONN_STATE_HTTP_REQUEST;
            }
        } else {
            conn->state = CONN_STATE_HTTP_REQUEST;
        }

        ws_event_add(loop, client_fd, WS_EV_READ, on_client_event, conn);
        ws_log_debug("accepted connection from %s:%d (total: %d)", addr, port, ctx->conn_count);
    }
}

/* ---- Find proxy context from connection ---- */

static ws_proxy_ctx_t *conn_get_ctx(ws_conn_t *conn) {
    /* Single-process design — the one proxy ctx lives in server.c. */
    extern ws_proxy_ctx_t *g_proxy_ctx;
    (void)conn;
    return g_proxy_ctx;
}

/* ---- Close connection ---- */

static void conn_close(ws_conn_t *conn) {
    if (conn->state == CONN_STATE_CLOSED) return;
    conn->state = CONN_STATE_CLOSED;

    if (conn->loop) {
        if (conn->client_fd != WS_INVALID_SOCKET)
            ws_event_del(conn->loop, conn->client_fd);
        if (conn->target_fd != WS_INVALID_SOCKET)
            ws_event_del(conn->loop, conn->target_fd);
    }

    if (conn->client_ssl) {
        /* Best-effort graceful TLS close; never block the close path */
        ws_ssl_shutdown(conn->client_ssl);
        ws_ssl_free(conn->client_ssl);
        conn->client_ssl = NULL;
    }

    if (conn->client_fd != WS_INVALID_SOCKET) {
        ws_close_socket(conn->client_fd);
        conn->client_fd = WS_INVALID_SOCKET;
    }
    if (conn->target_fd != WS_INVALID_SOCKET) {
        ws_close_socket(conn->target_fd);
        conn->target_fd = WS_INVALID_SOCKET;
    }
    if (conn->file_fd >= 0) {
        close(conn->file_fd);
        conn->file_fd = -1;
    }
    if (conn->record) {
        ws_record_close(conn->record);
        free(conn->record);
        conn->record = NULL;
    }

    ws_buf_free(&conn->client_recv);
    ws_buf_free(&conn->client_send);
    ws_buf_free(&conn->target_recv);
    ws_buf_free(&conn->target_send);
    ws_dbuf_free(&conn->http_buf);

    ws_log_debug("closed connection from %s:%d", conn->client_addr, conn->client_port);
}

/* ---- SSL handshake ---- */

static void handle_ssl_handshake(ws_conn_t *conn) {
    ws_ssl_status_t st = ws_ssl_handshake(conn->client_ssl);
    switch (st) {
    case WS_SSL_OK:
        conn->state = CONN_STATE_HTTP_REQUEST;
        break;
    case WS_SSL_WANT_READ:
        ws_event_mod(conn->loop, conn->client_fd, WS_EV_READ, NULL, NULL);
        break;
    case WS_SSL_WANT_WRITE:
        ws_event_mod(conn->loop, conn->client_fd, WS_EV_WRITE, NULL, NULL);
        break;
    default:
        ws_log_debug("SSL handshake failed for %s:%d", conn->client_addr, conn->client_port);
        conn_close(conn);
        conn_release(conn_get_ctx(conn), conn);
        break;
    }
}

/* ---- Read from client (raw or SSL) ---- */

static int client_recv(ws_conn_t *conn, void *buf, int len) {
    if (conn->client_ssl) {
        ws_ssl_status_t st;
        int n = ws_ssl_read(conn->client_ssl, buf, len, &st);
        if (n > 0) return n;
        if (st == WS_SSL_WANT_READ || st == WS_SSL_WANT_WRITE) return 0;
        return -1;
    }
    ssize_t n = recv(conn->client_fd, buf, (size_t)len, 0);
    if (n > 0) return (int)n;
    if (n == 0) return -1;  /* closed */
    if (ws_errno() == WS_EAGAIN || ws_errno() == WS_EINTR) return 0;
    return -1;
}

static int client_send_raw(ws_conn_t *conn, const void *buf, int len) {
    if (conn->client_ssl) {
        ws_ssl_status_t st;
        int n = ws_ssl_write(conn->client_ssl, buf, len, &st);
        if (n > 0) return n;
        if (st == WS_SSL_WANT_READ || st == WS_SSL_WANT_WRITE) return 0;
        return -1;
    }
    ssize_t n = send(conn->client_fd, buf, (size_t)len, 0);
    if (n > 0) return (int)n;
    if (n == 0) return 0;
    if (ws_errno() == WS_EAGAIN || ws_errno() == WS_EINTR) return 0;
    return -1;
}

/* ---- Handle HTTP request (upgrade or static file) ---- */

static int handle_http_request(ws_conn_t *conn) {
    ws_proxy_ctx_t *ctx = conn_get_ctx(conn);
    ws_http_request_t req;
    memset(&req, 0, sizeof(req));

    int consumed = ws_http_parse_request(&req, (const char *)conn->http_buf.data,
                                          (int)conn->http_buf.len);
    if (consumed == 0) return 0;  /* need more data */
    if (consumed < 0) {
        ws_http_request_free(&req);
        return -1;
    }

    conn->last_activity = ws_time_ms();

    if (req.upgrade_websocket) {
        /* WebSocket upgrade */
        if (!req.ws_key || !req.ws_version) {
            ws_http_request_free(&req);
            return -1;
        }

        /* Determine target */
        if (ctx->token_ctx) {
            const char *token = NULL;
            if (ctx->config->host_token && req.host) {
                /* Extract token from Host header (before first .) */
                static char token_buf[256];
                snprintf(token_buf, sizeof(token_buf), "%s", req.host);
                char *dot = strchr(token_buf, '.');
                if (dot) *dot = '\0';
                char *colon = strchr(token_buf, ':');
                if (colon) *colon = '\0';
                token = token_buf;
            } else if (req.query[0]) {
                /* Extract token from query string: ?token=xxx */
                const char *tp = strstr(req.query, "token=");
                if (tp) {
                    static char token_buf2[256];
                    snprintf(token_buf2, sizeof(token_buf2), "%s", tp + 6);
                    char *amp = strchr(token_buf2, '&');
                    if (amp) *amp = '\0';
                    token = token_buf2;
                }
            }

            if (!token || ws_token_lookup(ctx->token_ctx, token, &conn->target) < 0) {
                ws_log_warn("token lookup failed for %s", token ? token : "(null)");
                char resp[1024];
                int n = ws_http_error_response(resp, sizeof(resp), 403, "Invalid token");
                ws_buf_write(&conn->client_send, resp, (uint32_t)n);
                flush_client_send(conn);
                ws_http_request_free(&req);
                return -1;
            }
        } else {
            /* Use static target */
            snprintf(conn->target.host, sizeof(conn->target.host), "%s",
                    ctx->config->unix_target[0] ? ctx->config->unix_target : ctx->config->target_host);
            conn->target.port = ctx->config->target_port;
        }

        /* Generate accept key */
        char accept_key[64];
        if (ws_websocket_accept_key(req.ws_key, accept_key, sizeof(accept_key)) < 0) {
            ws_http_request_free(&req);
            return -1;
        }

        /* Send upgrade response */
        char resp[1024];
        int n = ws_http_ws_upgrade_response(resp, sizeof(resp), accept_key, req.ws_protocol);
        ws_buf_write(&conn->client_send, resp, (uint32_t)n);
        flush_client_send(conn);

        /* Connect to target */
        ws_socket_t target_fd;
        if (ctx->config->unix_target[0]) {
            target_fd = ws_connect_unix(ctx->config->unix_target);
        } else {
            target_fd = ws_connect_tcp(conn->target.host, conn->target.port);
        }

        if (target_fd == WS_INVALID_SOCKET) {
            ws_log_error("failed to connect to target %s:%d",
                        conn->target.host, conn->target.port);
            ws_http_request_free(&req);
            return -1;
        }

        conn->target_fd = target_fd;
        ws_set_tcp_nodelay(target_fd, 1);

        /* Setup recording if configured */
        if (ctx->config->record_file[0]) {
            conn->record = (ws_record_t *)malloc(sizeof(ws_record_t));
            if (conn->record) {
                char rec_path[4096];
                snprintf(rec_path, sizeof(rec_path), "%s.%llu",
                        ctx->config->record_file,
                        (unsigned long long)conn->created_at);
                if (ws_record_open(conn->record, rec_path) < 0) {
                    free(conn->record);
                    conn->record = NULL;
                }
            }
        }

        /* Wait for target connection */
        conn->state = CONN_STATE_CONNECTING;
        ws_event_add(conn->loop, target_fd, WS_EV_WRITE, on_target_connect, conn);

        ws_log_info("WebSocket connection from %s:%d -> %s:%d",
                   conn->client_addr, conn->client_port,
                   conn->target.host, conn->target.port);

    } else if (ctx->config->web_dir[0]) {
        /* Serve static file */
        char resp[4096];
        int file_fd = -1;
        size_t file_size = 0;
        int n = ws_web_serve_file(ctx->config->web_dir, &req, resp, sizeof(resp),
                                   &file_fd, &file_size);
        if (n > 0) {
            ws_buf_write(&conn->client_send, resp, (uint32_t)n);
            flush_client_send(conn);
        }

        if (file_fd >= 0 && file_size > 0) {
            conn->file_fd = file_fd;
            conn->file_size = file_size;
            conn->file_sent = 0;
            conn->state = CONN_STATE_WEB_SERVING;
            ws_event_mod(conn->loop, conn->client_fd, WS_EV_WRITE, NULL, NULL);
        } else {
            ws_http_request_free(&req);
            return -1;  /* close after error response */
        }
    } else {
        /* No web dir, reject */
        char resp[1024];
        int n = ws_http_error_response(resp, sizeof(resp), 400,
                                       "WebSocket upgrade required");
        ws_buf_write(&conn->client_send, resp, (uint32_t)n);
        flush_client_send(conn);
        ws_http_request_free(&req);
        return -1;
    }

    ws_http_request_free(&req);
    return 0;
}

/* ---- Flush send buffers ---- */

static int flush_client_send(ws_conn_t *conn) {
    while (!ws_buf_empty(&conn->client_send)) {
        const uint8_t *p; uint32_t avail;
        ws_buf_peek_ptr(&conn->client_send, &p, &avail);
        int sent = client_send_raw(conn, p, (int)avail);
        if (sent <= 0) {
            if (sent == 0) {
                ws_event_mod(conn->loop, conn->client_fd,
                            WS_EV_READ | WS_EV_WRITE, NULL, NULL);
                return 0;
            }
            return -1;
        }
        ws_buf_drain(&conn->client_send, (uint32_t)sent);
    }
    return 0;
}

static int flush_target_send(ws_conn_t *conn) {
    while (!ws_buf_empty(&conn->target_send)) {
        const uint8_t *p; uint32_t avail;
        ws_buf_peek_ptr(&conn->target_send, &p, &avail);
        ssize_t sent = send(conn->target_fd, p, avail, 0);
        if (sent <= 0) {
            if (sent < 0 && (ws_errno() == WS_EAGAIN || ws_errno() == WS_EINTR)) {
                ws_event_mod(conn->loop, conn->target_fd,
                            WS_EV_READ | WS_EV_WRITE, NULL, NULL);
                return 0;
            }
            return -1;
        }
        ws_buf_drain(&conn->target_send, (uint32_t)sent);
    }
    return 0;
}

/* ---- WebSocket frame processing ---- */

#ifdef WS_PLATFORM_POSIX
/* Flush a batched iovec of unmasked payloads to the target socket. Any bytes
 * not sent by writev() (EAGAIN or short-write) are buffered into target_send
 * so the caller can drain them later via flush_target_send(). */
static int flush_batch_iov(ws_conn_t *conn, struct iovec *iov, int n, uint32_t total) {
    ssize_t sent = writev(conn->target_fd, iov, n);
    if (sent < 0) {
        if (!(ws_errno() == WS_EAGAIN || ws_errno() == WS_EINTR))
            return -1;
        sent = 0;
    }
    if ((uint32_t)sent < total) {
        size_t skip = (size_t)sent;
        for (int i = 0; i < n; i++) {
            size_t ilen = iov[i].iov_len;
            if (skip >= ilen) { skip -= ilen; continue; }
            if (ws_buf_write(&conn->target_send,
                             (uint8_t *)iov[i].iov_base + skip,
                             (uint32_t)(ilen - skip)) < 0) return -1;
            skip = 0;
        }
    }
    return 0;
}
#endif

static int process_ws_frame(ws_conn_t *conn) {
#ifdef WS_PLATFORM_POSIX
    /* Batched writev path: collect unmasked data-frame payloads straight out
     * of client_recv and send them to target_fd in one syscall at the end of
     * the loop. Safe because ws_buf_drain only advances cursors — it never
     * memmoves bytes — so iov pointers stay valid; and we never write to
     * client_recv inside this function. */
    enum { BATCH_IOV_MAX = 128 };
    struct iovec batch[BATCH_IOV_MAX];
    int batch_n = 0;
    uint32_t batch_bytes = 0;
#endif

    for (;;) {
        uint32_t avail = ws_buf_readable(&conn->client_recv);
        if (avail == 0) break;

        if (!conn->ws_hdr_parsed) {
            const uint8_t *hp; uint32_t hlen;
            ws_buf_peek_ptr(&conn->client_recv, &hp, &hlen);
            int need = (int)WS_MIN(hlen, (uint32_t)WS_MAX_FRAME_HEADER);
            int hdr_len = ws_frame_parse_header(hp, need, &conn->ws_hdr);
            if (hdr_len == 0) break;  /* need more data */
            if (hdr_len < 0) return -1;

            ws_buf_drain(&conn->client_recv, (uint32_t)hdr_len);
            conn->ws_hdr_parsed = 1;
            conn->ws_payload_read = 0;
        }

        uint64_t remaining = conn->ws_hdr.payload_len - conn->ws_payload_read;
        if (remaining == 0) {
            conn->ws_hdr_parsed = 0;
            continue;
        }

        avail = ws_buf_readable(&conn->client_recv);
        if (avail == 0) break;

        uint32_t chunk = (uint32_t)WS_MIN((uint64_t)avail, remaining);

        int is_data = (conn->ws_hdr.opcode == WS_OP_BIN ||
                       conn->ws_hdr.opcode == WS_OP_TEXT ||
                       conn->ws_hdr.opcode == WS_OP_CONT);

        if (is_data) {
            const uint8_t *cp; uint32_t clen;
            ws_buf_peek_ptr(&conn->client_recv, &cp, &clen);
            uint32_t take = WS_MIN(chunk, clen);
            uint8_t *mp = (uint8_t *)cp;  /* unmask in place */

            if (conn->ws_hdr.masked) {
                uint8_t rot[4];
                uint32_t off = (uint32_t)(conn->ws_payload_read & 3);
                rot[0] = conn->ws_hdr.mask[(0 + off) & 3];
                rot[1] = conn->ws_hdr.mask[(1 + off) & 3];
                rot[2] = conn->ws_hdr.mask[(2 + off) & 3];
                rot[3] = conn->ws_hdr.mask[(3 + off) & 3];
                ws_frame_apply_mask(mp, take, rot);
            }
            if (conn->record)
                ws_record_frame(conn->record, '<', mp, (int)take);

#ifdef WS_PLATFORM_POSIX
            /* Batch path: only valid while target_send is empty (ordering)
             * AND the payload is large enough that the iov-entry overhead
             * is amortized. For sub-KB messages the kernel's per-iov cost
             * beats what we save on the memcpy. Drain updates cursors only,
             * so iov pointers stay live until we writev below. */
            if (take >= 512 && ws_buf_empty(&conn->target_send)) {
                if (batch_n == BATCH_IOV_MAX) {
                    if (flush_batch_iov(conn, batch, batch_n, batch_bytes) < 0)
                        return -1;
                    batch_n = 0;
                    batch_bytes = 0;
                }
                /* target_send may have filled from a partial writev above.
                 * Re-check to stay ordered. */
                if (ws_buf_empty(&conn->target_send)) {
                    batch[batch_n].iov_base = mp;
                    batch[batch_n].iov_len = take;
                    batch_n++;
                    batch_bytes += take;
                    ws_buf_drain(&conn->client_recv, take);
                    conn->ws_payload_read += take;
                    if (conn->ws_payload_read == conn->ws_hdr.payload_len)
                        conn->ws_hdr_parsed = 0;
                    continue;
                }
            }
#endif

            /* Fallback: target_send already has buffered data, so ordering
             * forces us to append here too. */
            if (ws_buf_write(&conn->target_send, mp, take) < 0) return -1;
            ws_buf_drain(&conn->client_recv, take);
            conn->ws_payload_read += take;
            if (conn->ws_payload_read == conn->ws_hdr.payload_len)
                conn->ws_hdr_parsed = 0;
        } else {
#ifdef WS_PLATFORM_POSIX
            /* Control frames have side effects on the client-side response
             * buffer, so flush any pending data-frame batch first to keep
             * target-side writes in submission order. */
            if (batch_n > 0) {
                if (flush_batch_iov(conn, batch, batch_n, batch_bytes) < 0)
                    return -1;
                batch_n = 0;
                batch_bytes = 0;
            }
#endif
            /* Control frames: small, use tmp */
            uint8_t tmp[128 + WS_MAX_FRAME_HEADER];
            uint32_t rd = ws_buf_read(&conn->client_recv, tmp,
                                       WS_MIN(chunk, (uint32_t)WS_MAX_CONTROL_PAYLOAD + 4));
            if (rd == 0) break;
            if (conn->ws_hdr.masked) {
                uint8_t rot[4];
                uint32_t off = (uint32_t)(conn->ws_payload_read & 3);
                rot[0] = conn->ws_hdr.mask[(0 + off) & 3];
                rot[1] = conn->ws_hdr.mask[(1 + off) & 3];
                rot[2] = conn->ws_hdr.mask[(2 + off) & 3];
                rot[3] = conn->ws_hdr.mask[(3 + off) & 3];
                ws_frame_apply_mask(tmp, rd, rot);
            }
            switch (conn->ws_hdr.opcode) {
            case WS_OP_PING: {
                uint8_t pong[128 + WS_MAX_FRAME_HEADER];
                int pong_len = ws_frame_encode_pong(pong, tmp, (int)rd);
                ws_buf_write(&conn->client_send, pong, (uint32_t)pong_len);
                break;
            }
            case WS_OP_PONG: break;
            case WS_OP_CLOSE: return -1;
            default: break;
            }
            conn->ws_payload_read += rd;
            if (conn->ws_payload_read == conn->ws_hdr.payload_len)
                conn->ws_hdr_parsed = 0;
        }
    }

#ifdef WS_PLATFORM_POSIX
    if (batch_n > 0) {
        if (flush_batch_iov(conn, batch, batch_n, batch_bytes) < 0)
            return -1;
    }
#endif

    if (!ws_buf_empty(&conn->target_send))
        flush_target_send(conn);
    if (!ws_buf_empty(&conn->client_send))
        flush_client_send(conn);

    return 0;
}

/* ---- Forward target data as WebSocket frames ---- */

static int forward_target_to_client(ws_conn_t *conn) {
    while (!ws_buf_empty(&conn->target_recv)) {
        const uint8_t *p; uint32_t avail;
        ws_buf_peek_ptr(&conn->target_recv, &p, &avail);
        uint32_t chunk = avail;  /* send everything as one frame */

        if (conn->record)
            ws_record_frame(conn->record, '>', p, (int)chunk);

        uint8_t hdr[WS_MAX_FRAME_HEADER];
        int hdr_len = ws_frame_encode_header(hdr, WS_OP_BIN, chunk, 1);

#ifdef WS_PLATFORM_POSIX
        /* Zero-copy fast path: writev header + payload. Since target_recv
         * coalesces multiple upstream replies into one big chunk per event,
         * even nominally "small" messages end up >1 KiB here and benefit
         * from the single-syscall writev. */
        if (!conn->client_ssl && ws_buf_empty(&conn->client_send)) {
            struct iovec iov[2];
            iov[0].iov_base = hdr;          iov[0].iov_len = (size_t)hdr_len;
            iov[1].iov_base = (void *)p;    iov[1].iov_len = chunk;
            ssize_t sent = writev(conn->client_fd, iov, 2);
            if (sent < 0) {
                if (ws_errno() == WS_EAGAIN || ws_errno() == WS_EINTR) {
                    if (ws_buf_write(&conn->client_send, hdr, (uint32_t)hdr_len) < 0) return -1;
                    if (ws_buf_write(&conn->client_send, p, chunk) < 0) return -1;
                    ws_buf_drain(&conn->target_recv, chunk);
                    ws_event_mod(conn->loop, conn->client_fd,
                                 WS_EV_READ | WS_EV_WRITE, NULL, NULL);
                    return 0;
                }
                return -1;
            }
            size_t total = (size_t)hdr_len + chunk;
            if ((size_t)sent < total) {
                /* Partial write: buffer the unsent remainder */
                size_t remaining = total - (size_t)sent;
                if ((size_t)sent < (size_t)hdr_len) {
                    size_t hdr_left = (size_t)hdr_len - (size_t)sent;
                    if (ws_buf_write(&conn->client_send,
                                     hdr + sent, (uint32_t)hdr_left) < 0) return -1;
                    if (ws_buf_write(&conn->client_send, p, chunk) < 0) return -1;
                } else {
                    size_t payload_sent = (size_t)sent - (size_t)hdr_len;
                    if (ws_buf_write(&conn->client_send,
                                     p + payload_sent,
                                     (uint32_t)(chunk - payload_sent)) < 0) return -1;
                }
                (void)remaining;
                ws_buf_drain(&conn->target_recv, chunk);
                ws_event_mod(conn->loop, conn->client_fd,
                             WS_EV_READ | WS_EV_WRITE, NULL, NULL);
                return 0;
            }
            /* Fully sent */
            ws_buf_drain(&conn->target_recv, chunk);
            continue;
        }
#endif
        if (ws_buf_write(&conn->client_send, hdr, (uint32_t)hdr_len) < 0) return -1;
        if (ws_buf_write(&conn->client_send, p, chunk) < 0) return -1;
        ws_buf_drain(&conn->target_recv, chunk);
    }

    return flush_client_send(conn);
}

/* ---- Event handlers ---- */

static void on_target_connect(ws_event_loop_t *loop, ws_socket_t fd, int events, void *data) {
    (void)loop;
    ws_conn_t *conn = (ws_conn_t *)data;

    if (events & (WS_EV_ERROR | WS_EV_HUP)) {
        ws_log_error("target connect failed for %s:%d", conn->target.host, conn->target.port);
        conn_close(conn);
        conn_release(conn_get_ctx(conn), conn);
        return;
    }

    /* Check if connection succeeded */
    int err = 0;
    socklen_t len = sizeof(err);
    getsockopt(fd, SOL_SOCKET, SO_ERROR, (char *)&err, &len);
    if (err != 0) {
        ws_log_error("target connect error: %s", strerror(err));
        conn_close(conn);
        conn_release(conn_get_ctx(conn), conn);
        return;
    }

    /* Connection established — start proxying */
    conn->state = CONN_STATE_PROXYING;
    ws_event_mod(loop, conn->client_fd, WS_EV_READ, on_client_event, conn);
    ws_event_mod(loop, conn->target_fd, WS_EV_READ, on_target_event, conn);

    ws_log_debug("target connected: %s:%d", conn->target.host, conn->target.port);

    /* Process any client data buffered while we were connecting. With edge-
     * triggered events no further read event fires until new data arrives. */
    if (!ws_buf_empty(&conn->client_recv)) {
        if (process_ws_frame(conn) < 0) {
            conn_close(conn);
            conn_release(conn_get_ctx(conn), conn);
            return;
        }
    }
}

static void on_client_event(ws_event_loop_t *loop, ws_socket_t fd, int events, void *data) {
    (void)loop; (void)fd;
    ws_conn_t *conn = (ws_conn_t *)data;

    if (events & (WS_EV_ERROR | WS_EV_HUP)) {
        goto close_conn;
    }

    conn->last_activity = ws_time_ms();

    if (events & WS_EV_READ) {
        if (conn->state == CONN_STATE_SSL_HANDSHAKE) {
            handle_ssl_handshake(conn);
            return;
        }

        if (conn->state == CONN_STATE_HTTP_REQUEST) {
            /* HTTP requests are small — stage through a tmp buffer so the
             * header-size cap check runs before touching http_buf. */
            uint8_t tmp[16384];
            for (;;) {
                int n = client_recv(conn, tmp, sizeof(tmp));
                if (n <= 0) {
                    if (n < 0) goto close_conn;
                    break;
                }
                if (conn->http_buf.len + (uint32_t)n > WS_MAX_HEADER_SIZE)
                    goto close_conn;
                if (ws_dbuf_append(&conn->http_buf, tmp, (uint32_t)n) < 0)
                    goto close_conn;
                if (handle_http_request(conn) < 0) goto close_conn;
                /* State may have flipped to CONNECTING — stop the HTTP loop
                 * so the zero-copy path below picks up any trailing bytes. */
                if (conn->state != CONN_STATE_HTTP_REQUEST) break;
            }
        }

        if (conn->state == CONN_STATE_PROXYING ||
            conn->state == CONN_STATE_CONNECTING) {
            /* Zero-copy recv path: read straight into client_recv's writable
             * tail, skipping the tmp-buffer memcpy. Reserve sizes recv blocks
             * at ~64 KiB so kernel reads stay large. */
            for (;;) {
                if (ws_buf_reserve(&conn->client_recv, 65536) < 0) goto close_conn;
                uint8_t *tp; uint32_t tcap;
                ws_buf_tail_ptr(&conn->client_recv, &tp, &tcap);
                int n = client_recv(conn, tp, (int)tcap);
                if (n <= 0) {
                    if (n < 0) goto close_conn;
                    break;
                }
                ws_buf_commit(&conn->client_recv, (uint32_t)n);
            }
        }

        if (conn->state == CONN_STATE_PROXYING) {
            if (process_ws_frame(conn) < 0) goto close_conn;
        }
    }

    if (events & WS_EV_WRITE) {
        if (conn->state == CONN_STATE_SSL_HANDSHAKE) {
            handle_ssl_handshake(conn);
            return;
        }

        if (conn->state == CONN_STATE_WEB_SERVING) {
            /* Send file content. pread() keeps our own offset authoritative
             * so a partial write (sent < nr) doesn't lose the unsent tail. */
            if (conn->file_sent < conn->file_size) {
                uint8_t fbuf[16384];
                size_t toread = WS_MIN(sizeof(fbuf), conn->file_size - conn->file_sent);
                ssize_t nr = ws_pread(conn->file_fd, fbuf, toread, (long long)conn->file_sent);
                if (nr <= 0) {
                    if (nr < 0 && (ws_errno() == WS_EAGAIN || ws_errno() == WS_EINTR))
                        return;
                    goto close_conn;
                }
                int sent = client_send_raw(conn, fbuf, (int)nr);
                if (sent < 0) goto close_conn;
                if (sent == 0) return;  /* would block, retry on next WRITE */
                conn->file_sent += (size_t)sent;
                if (conn->file_sent >= conn->file_size) goto close_conn;
            }
            return;
        }

        /* Flush pending send data */
        if (flush_client_send(conn) < 0) goto close_conn;

        if (ws_buf_empty(&conn->client_send)) {
            ws_event_mod(conn->loop, conn->client_fd, WS_EV_READ, NULL, NULL);
        }
    }
    return;

close_conn:
    conn_close(conn);
    conn_release(conn_get_ctx(conn), conn);
}

static void on_target_event(ws_event_loop_t *loop, ws_socket_t fd, int events, void *data) {
    (void)loop; (void)fd;
    ws_conn_t *conn = (ws_conn_t *)data;

    if (events & (WS_EV_ERROR | WS_EV_HUP)) {
        goto close_conn;
    }

    conn->last_activity = ws_time_ms();

    if (events & WS_EV_READ) {
        /* Zero-copy recv: drain the target socket straight into
         * target_recv's writable tail to avoid a tmp-buffer memcpy. */
        for (;;) {
            if (ws_buf_reserve(&conn->target_recv, 65536) < 0) goto close_conn;
            uint8_t *tp; uint32_t tcap;
            ws_buf_tail_ptr(&conn->target_recv, &tp, &tcap);
            ssize_t n = recv(conn->target_fd, tp, (size_t)tcap, 0);
            if (n <= 0) {
                if (n == 0) goto close_conn;
                if (ws_errno() == WS_EAGAIN || ws_errno() == WS_EINTR) break;
                goto close_conn;
            }
            ws_buf_commit(&conn->target_recv, (uint32_t)n);
        }

        if (forward_target_to_client(conn) < 0) goto close_conn;
    }

    if (events & WS_EV_WRITE) {
        if (flush_target_send(conn) < 0) goto close_conn;
        if (ws_buf_empty(&conn->target_send)) {
            ws_event_mod(conn->loop, conn->target_fd, WS_EV_READ, NULL, NULL);
        }
    }
    return;

close_conn:
    conn_close(conn);
    conn_release(conn_get_ctx(conn), conn);
}
