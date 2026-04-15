/* SPDX-License-Identifier: MIT */
/*
 * Multi-connection TCP echo upstream for benchmarking.
 * Event-driven (epoll / kqueue / poll) so it does not become the bottleneck.
 *
 *   Usage: echo_upstream <port>
 */
#include "platform.h"
#include "net.h"
#include "event.h"
#include "log.h"

typedef struct echo_conn {
    ws_socket_t fd;
    uint8_t    *pending;      /* data we couldn't echo back immediately */
    size_t      pending_len;
    size_t      pending_cap;
    struct echo_conn *next;   /* bookkeeping so we can free on shutdown */
} echo_conn_t;

static echo_conn_t *g_head = NULL;
static ws_event_loop_t *g_loop = NULL;

static void echo_conn_close(echo_conn_t *c) {
    if (c->fd != WS_INVALID_SOCKET) {
        ws_event_del(g_loop, c->fd);
        ws_close_socket(c->fd);
        c->fd = WS_INVALID_SOCKET;
    }
    /* Unlink */
    echo_conn_t **pp = &g_head;
    while (*pp) {
        if (*pp == c) { *pp = c->next; break; }
        pp = &(*pp)->next;
    }
    free(c->pending);
    free(c);
}

static int echo_conn_buffer(echo_conn_t *c, const uint8_t *data, size_t len) {
    size_t need = c->pending_len + len;
    if (need > c->pending_cap) {
        size_t cap = c->pending_cap ? c->pending_cap : 8192;
        while (cap < need) cap *= 2;
        uint8_t *p = (uint8_t *)realloc(c->pending, cap);
        if (!p) return -1;
        c->pending = p;
        c->pending_cap = cap;
    }
    memcpy(c->pending + c->pending_len, data, len);
    c->pending_len += len;
    return 0;
}

static void on_conn_event(ws_event_loop_t *loop, ws_socket_t fd, int events, void *data) {
    (void)loop; (void)fd;
    echo_conn_t *c = (echo_conn_t *)data;

    if (events & (WS_EV_ERROR | WS_EV_HUP)) { echo_conn_close(c); return; }

    if (events & WS_EV_WRITE) {
        while (c->pending_len > 0) {
            ssize_t s = send(c->fd, c->pending, c->pending_len, 0);
            if (s <= 0) {
                if (s < 0 && (ws_errno() == WS_EAGAIN || ws_errno() == WS_EINTR)) break;
                echo_conn_close(c); return;
            }
            memmove(c->pending, c->pending + s, c->pending_len - (size_t)s);
            c->pending_len -= (size_t)s;
        }
        if (c->pending_len == 0)
            ws_event_mod(g_loop, c->fd, WS_EV_READ, on_conn_event, c);
    }

    if (events & WS_EV_READ) {
        uint8_t buf[65536];
        for (;;) {
            ssize_t n = recv(c->fd, buf, sizeof(buf), 0);
            if (n == 0) { echo_conn_close(c); return; }
            if (n < 0) {
                if (ws_errno() == WS_EAGAIN || ws_errno() == WS_EINTR) break;
                echo_conn_close(c); return;
            }
            /* Try to echo synchronously. */
            ssize_t off = 0;
            if (c->pending_len == 0) {
                while (off < n) {
                    ssize_t s = send(c->fd, buf + off, (size_t)(n - off), 0);
                    if (s <= 0) {
                        if (s < 0 && (ws_errno() == WS_EAGAIN || ws_errno() == WS_EINTR)) break;
                        echo_conn_close(c); return;
                    }
                    off += s;
                }
            }
            if (off < n) {
                if (echo_conn_buffer(c, buf + off, (size_t)(n - off)) < 0) {
                    echo_conn_close(c); return;
                }
                ws_event_mod(g_loop, c->fd, WS_EV_READ | WS_EV_WRITE, on_conn_event, c);
            }
        }
    }
}

static void on_accept(ws_event_loop_t *loop, ws_socket_t listen_fd, int events, void *data) {
    (void)events; (void)data;
    for (;;) {
        char addr[64]; int port;
        ws_socket_t fd = ws_accept(listen_fd, addr, sizeof(addr), &port);
        if (fd == WS_INVALID_SOCKET) break;
        ws_set_tcp_nodelay(fd, 1);
        echo_conn_t *c = (echo_conn_t *)calloc(1, sizeof(*c));
        if (!c) { ws_close_socket(fd); continue; }
        c->fd = fd;
        c->next = g_head;
        g_head = c;
        ws_event_add(loop, fd, WS_EV_READ, on_conn_event, c);
    }
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "Usage: %s <port>\n", argv[0]); return 1; }
    int port = atoi(argv[1]);

    ws_platform_init();
    ws_log_init(WS_LOG_WARN, NULL);

    ws_socket_t lfd = ws_listen_tcp(NULL, port, 1024, 0);
    if (lfd == WS_INVALID_SOCKET) {
        fprintf(stderr, "listen failed on port %d\n", port);
        return 1;
    }

    g_loop = ws_event_create(8192);
    if (!g_loop) { fprintf(stderr, "event loop create failed\n"); return 1; }

    ws_event_add(g_loop, lfd, WS_EV_READ, on_accept, NULL);
    printf("echo_upstream listening on %d\n", port);
    fflush(stdout);

    while (ws_event_run(g_loop, 1000) >= 0) { /* loop */ }

    ws_event_destroy(g_loop);
    ws_close_socket(lfd);
    ws_platform_cleanup();
    return 0;
}
