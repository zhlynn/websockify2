/* SPDX-License-Identifier: MIT */
/*
 * Event-driven WebSocket throughput benchmark.
 *
 * Spawns N non-blocking client connections, performs a WebSocket handshake
 * against `host:port`, then for `duration` seconds pumps fixed-size binary
 * frames in a pipelined echo loop (WINDOW_SIZE outstanding). Reports
 * msg/s and MB/s across all connections.
 *
 *   Usage: bench_throughput <host> <port> <msg_size> <num_conns> <duration_s>
 */
#include "platform.h"
#include "net.h"
#include "event.h"
#include "ws.h"
#include "log.h"

#define WINDOW_SIZE   8
#define RECV_BUF_SIZE 262144
#define BENCH_MASK    0xA1B2C3D4u

typedef enum {
    ST_CONNECTING,
    ST_HS_SEND,
    ST_HS_RECV,
    ST_STEADY,
    ST_DEAD
} bench_state_t;

typedef struct {
    ws_socket_t fd;
    bench_state_t state;

    /* Handshake */
    const char *hs_req;
    int hs_req_len;
    int hs_req_sent;
    char hs_resp[1024];
    int hs_resp_len;

    /* Steady */
    uint8_t *send_template;     /* [hdr | masked payload] shared across conns */
    int send_template_len;
    int send_off;               /* progress through template for current msg */
    int outstanding;

    /* Recv parse state */
    uint64_t rx_payload_remaining;
    int rx_hdr_pos;             /* bytes of header accumulated */
    uint8_t rx_hdr_buf[WS_MAX_FRAME_HEADER];
    int rx_hdr_expected;        /* resolved once we know payload-len field */

    /* Stats */
    uint64_t msgs_sent;
    uint64_t msgs_recv;
    uint64_t bytes_recv;
} bench_conn_t;

static int          g_msg_size;
static int          g_num_conns;
static uint64_t     g_deadline;
static uint64_t     g_hs_deadline;
static uint64_t     g_steady_start;
static bench_conn_t *g_conns;
static uint8_t     *g_send_template;
static int          g_send_template_len;
static ws_event_loop_t *g_loop;
static int          g_attempted = 0;
static int          g_phase = 0;  /* 0=handshake, 1=steady */

static int g_handshaked = 0;
static int g_steady_count = 0;

static void on_conn_event(ws_event_loop_t *loop, ws_socket_t fd, int events, void *data);

/* Build the shared send template: frame header + masked payload (fixed). */
static void build_send_template(void) {
    int payload_hdr_bytes = (g_msg_size < 126) ? 2 : (g_msg_size <= 0xFFFF ? 4 : 10);
    g_send_template_len = payload_hdr_bytes + 4 /*mask*/ + g_msg_size;
    g_send_template = (uint8_t *)malloc((size_t)g_send_template_len);

    uint8_t *p = g_send_template;
    int hdr_len = ws_frame_encode_header(p, WS_OP_BIN, (uint64_t)g_msg_size, 1);
    p[1] |= 0x80;  /* MASK bit set */
    p += hdr_len;

    uint8_t mask[4] = {
        (uint8_t)(BENCH_MASK >> 24), (uint8_t)(BENCH_MASK >> 16),
        (uint8_t)(BENCH_MASK >> 8),  (uint8_t)(BENCH_MASK),
    };
    memcpy(p, mask, 4);
    p += 4;

    for (int i = 0; i < g_msg_size; i++)
        p[i] = (uint8_t)(i & 0xFF) ^ mask[i & 3];
}

static void conn_close(bench_conn_t *c) {
    if (c->state == ST_DEAD) return;
    c->state = ST_DEAD;
    if (c->fd != WS_INVALID_SOCKET) {
        ws_event_del(g_loop, c->fd);
        ws_close_socket(c->fd);
        c->fd = WS_INVALID_SOCKET;
    }
}

static int try_send_frames(bench_conn_t *c) {
    while (c->outstanding < WINDOW_SIZE) {
        if (c->send_off == 0 && ws_time_ms() >= g_deadline) return 0;

        int remain = c->send_template_len - c->send_off;
        ssize_t s = send(c->fd, c->send_template + c->send_off, (size_t)remain, 0);
        if (s < 0) {
            if (ws_errno() == WS_EAGAIN || ws_errno() == WS_EINTR) return 0;
            return -1;
        }
        if (s == 0) return 0;
        c->send_off += (int)s;
        if (c->send_off == c->send_template_len) {
            c->send_off = 0;
            c->outstanding++;
            c->msgs_sent++;
        } else {
            return 0;  /* partial, try later on WRITE */
        }
    }
    return 0;
}

static int try_recv_frames(bench_conn_t *c) {
    uint8_t buf[RECV_BUF_SIZE];
    for (;;) {
        ssize_t n = recv(c->fd, buf, sizeof(buf), 0);
        if (n == 0) return -1;
        if (n < 0) {
            if (ws_errno() == WS_EAGAIN || ws_errno() == WS_EINTR) return 0;
            return -1;
        }

        uint8_t *p = buf;
        ssize_t left = n;
        while (left > 0) {
            if (c->rx_payload_remaining > 0) {
                uint64_t take = (uint64_t)left < c->rx_payload_remaining ? (uint64_t)left : c->rx_payload_remaining;
                c->rx_payload_remaining -= take;
                p += take; left -= (ssize_t)take;
                c->bytes_recv += take;
                /* Count one logical reply each time bytes_recv crosses msg_size */
                while (c->bytes_recv >= (uint64_t)g_msg_size) {
                    c->bytes_recv -= (uint64_t)g_msg_size;
                    c->outstanding--;
                    c->msgs_recv++;
                }
                continue;
            }

            /* Need header bytes */
            if (c->rx_hdr_expected == 0) {
                /* Copy first 2 bytes at least */
                while (c->rx_hdr_pos < 2 && left > 0) {
                    c->rx_hdr_buf[c->rx_hdr_pos++] = *p++; left--;
                }
                if (c->rx_hdr_pos < 2) break;
                uint8_t l = c->rx_hdr_buf[1] & 0x7F;
                c->rx_hdr_expected = (l < 126) ? 2 : (l == 126 ? 4 : 10);
            }
            while (c->rx_hdr_pos < c->rx_hdr_expected && left > 0) {
                c->rx_hdr_buf[c->rx_hdr_pos++] = *p++; left--;
            }
            if (c->rx_hdr_pos < c->rx_hdr_expected) break;

            ws_frame_header_t hdr;
            int hl = ws_frame_parse_header(c->rx_hdr_buf, c->rx_hdr_pos, &hdr);
            if (hl <= 0) return -1;
            c->rx_payload_remaining = hdr.payload_len;
            c->rx_hdr_pos = 0;
            c->rx_hdr_expected = 0;
        }
    }
}

static void on_steady(bench_conn_t *c, int events) {
    if (events & WS_EV_READ) {
        if (try_recv_frames(c) < 0) { conn_close(c); return; }
    }
    if (try_send_frames(c) < 0) { conn_close(c); return; }
}

static void start_handshake(bench_conn_t *c) {
    static char req_template[512];
    static int req_len = 0;
    if (req_len == 0) {
        req_len = snprintf(req_template, sizeof(req_template),
            "GET / HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
            "Sec-WebSocket-Version: 13\r\n"
            "\r\n");
    }
    c->hs_req = req_template;
    c->hs_req_len = req_len;
    c->hs_req_sent = 0;
    c->state = ST_HS_SEND;
}

static void on_conn_event(ws_event_loop_t *loop, ws_socket_t fd, int events, void *data) {
    (void)loop; (void)fd;
    bench_conn_t *c = (bench_conn_t *)data;
    if (c->state == ST_DEAD) return;

    if (events & (WS_EV_ERROR | WS_EV_HUP)) {
        /* For STEADY state this is terminal; for CONNECTING check SO_ERROR */
        if (c->state != ST_CONNECTING) { conn_close(c); return; }
    }

    switch (c->state) {
    case ST_CONNECTING: {
        int err = 0; socklen_t el = sizeof(err);
        getsockopt(c->fd, SOL_SOCKET, SO_ERROR, (char *)&err, &el);
        if (err) { conn_close(c); return; }
        ws_set_tcp_nodelay(c->fd, 1);
        start_handshake(c);
        /* fall through to send */
    } /* fall-through */
    case ST_HS_SEND: {
        while (c->hs_req_sent < c->hs_req_len) {
            ssize_t s = send(c->fd, c->hs_req + c->hs_req_sent,
                             (size_t)(c->hs_req_len - c->hs_req_sent), 0);
            if (s <= 0) {
                if (s < 0 && (ws_errno() == WS_EAGAIN || ws_errno() == WS_EINTR)) return;
                conn_close(c); return;
            }
            c->hs_req_sent += (int)s;
        }
        c->state = ST_HS_RECV;
        ws_event_mod(g_loop, c->fd, WS_EV_READ, on_conn_event, c);
        /* fall through to recv attempt in case data already there */
    } /* fall-through */
    case ST_HS_RECV: {
        for (;;) {
            ssize_t n = recv(c->fd, c->hs_resp + c->hs_resp_len,
                             sizeof(c->hs_resp) - (size_t)c->hs_resp_len - 1, 0);
            if (n == 0) { conn_close(c); return; }
            if (n < 0) {
                if (ws_errno() == WS_EAGAIN || ws_errno() == WS_EINTR) return;
                conn_close(c); return;
            }
            c->hs_resp_len += (int)n;
            c->hs_resp[c->hs_resp_len] = 0;
            if (strstr(c->hs_resp, "\r\n\r\n")) break;
            if (c->hs_resp_len >= (int)sizeof(c->hs_resp) - 1) { conn_close(c); return; }
        }
        if (!strstr(c->hs_resp, "101")) { conn_close(c); return; }
        c->state = ST_STEADY;
        c->send_template = g_send_template;
        c->send_template_len = g_send_template_len;
        g_handshaked++;
        g_steady_count++;
        ws_event_mod(g_loop, c->fd, WS_EV_READ | WS_EV_WRITE, on_conn_event, c);
        on_steady(c, WS_EV_READ | WS_EV_WRITE);
        return;
    }
    case ST_STEADY:
        on_steady(c, events);
        return;
    case ST_DEAD:
        return;
    }
}

static void on_tick(ws_event_loop_t *loop, void *data) {
    (void)data;
    uint64_t now = ws_time_ms();
    if (g_phase == 0) {
        if (g_handshaked >= g_attempted || now >= g_hs_deadline) {
            fprintf(stderr, "handshaked %d/%d\n", g_handshaked, g_attempted);
            g_phase = 1;
            g_steady_start = ws_time_ms();
            g_deadline = g_steady_start + (g_deadline - g_hs_deadline);  /* duration preserved */
            /* reset per-conn counters so we measure only steady-state phase */
            for (int i = 0; i < g_num_conns; i++) {
                g_conns[i].msgs_sent = 0;
                g_conns[i].msgs_recv = 0;
                g_conns[i].bytes_recv = 0;
            }
        }
    } else {
        if (now >= g_deadline) ws_event_stop(loop);
    }
}

int main(int argc, char **argv) {
    if (argc < 6) {
        fprintf(stderr, "Usage: %s <host> <port> <msg_size> <num_conns> <duration_s>\n", argv[0]);
        return 1;
    }
    const char *host = argv[1];
    int port         = atoi(argv[2]);
    g_msg_size       = atoi(argv[3]);
    g_num_conns      = atoi(argv[4]);
    int duration     = atoi(argv[5]);

    ws_platform_init();
    ws_log_init(WS_LOG_WARN, NULL);

    build_send_template();

    g_loop = ws_event_create(g_num_conns * 2 + 16);
    if (!g_loop) { fprintf(stderr, "event loop failed\n"); return 1; }

    g_conns = (bench_conn_t *)calloc((size_t)g_num_conns, sizeof(bench_conn_t));

    /* Connect all */
    for (int i = 0; i < g_num_conns; i++) {
        ws_socket_t fd = ws_connect_tcp(host, port);
        if (fd == WS_INVALID_SOCKET) { g_conns[i].state = ST_DEAD; g_conns[i].fd = WS_INVALID_SOCKET; continue; }
        g_conns[i].fd = fd;
        g_conns[i].state = ST_CONNECTING;
        ws_event_add(g_loop, fd, WS_EV_WRITE, on_conn_event, &g_conns[i]);
        g_attempted++;
    }

    /* Run handshake + steady-state via single timer-driven loop */
    uint64_t now = ws_time_ms();
    g_hs_deadline = now + 5000;
    g_deadline = g_hs_deadline + (uint64_t)duration * 1000;
    g_phase = 0;
    ws_timer_add(g_loop, 50, 1, on_tick, NULL);
    ws_event_run(g_loop, 1000);

    double secs = (double)(ws_time_ms() - g_steady_start) / 1000.0;

    uint64_t tot_sent = 0, tot_recv = 0;
    int live = 0;
    for (int i = 0; i < g_num_conns; i++) {
        tot_sent += g_conns[i].msgs_sent;
        tot_recv += g_conns[i].msgs_recv;
        if (g_conns[i].state == ST_STEADY) live++;
    }

    double msg_rate = secs > 0 ? (double)tot_recv / secs : 0;
    double mb_rate  = secs > 0 ? (double)tot_recv * (double)g_msg_size / secs / 1048576.0 : 0;

    printf("RESULT msg_size=%d conns=%d(live=%d) duration=%.2fs msgs_sent=%llu msgs_recv=%llu msg_per_s=%.0f MB_per_s=%.2f\n",
           g_msg_size, g_num_conns, live, secs,
           (unsigned long long)tot_sent, (unsigned long long)tot_recv,
           msg_rate, mb_rate);

    for (int i = 0; i < g_num_conns; i++) conn_close(&g_conns[i]);
    ws_event_destroy(g_loop);
    free(g_send_template);
    free(g_conns);
    ws_platform_cleanup();
    return 0;
}
