/* SPDX-License-Identifier: MIT */
/*
 * Benchmark: concurrent WebSocket connection test
 * Creates many simultaneous connections to measure throughput and latency.
 * Usage: bench_connections [host] [port] [num_connections] [message_size]
 */
#include "platform.h"
#include "net.h"
#include "crypto.h"
#include "ws.h"
#include "log.h"

#define DEFAULT_HOST "127.0.0.1"
#define DEFAULT_PORT 8080
#define DEFAULT_CONNS 1000
#define DEFAULT_MSG_SIZE 1024
#define DEFAULT_DURATION_SEC 10

typedef struct {
    ws_socket_t fd;
    int         connected;
    int         upgraded;
    uint64_t    bytes_sent;
    uint64_t    bytes_recv;
    uint64_t    messages_sent;
    uint64_t    messages_recv;
    uint64_t    latency_sum_us;
} bench_conn_t;

static int do_ws_handshake(ws_socket_t fd, const char *host, int port) {
    char req[1024];
    int n = snprintf(req, sizeof(req),
        "GET / HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n",
        host, port);

    ssize_t sent = send(fd, req, (size_t)n, 0);
    if (sent != n) return -1;

    /* Read response */
    char resp[2048];
    ssize_t total = 0;
    for (int attempts = 0; attempts < 100; attempts++) {
        ssize_t r = recv(fd, resp + total, sizeof(resp) - (size_t)total - 1, 0);
        if (r < 0) {
            if (ws_errno() == WS_EAGAIN || ws_errno() == WS_EINTR) {
                usleep(1000);
                continue;
            }
            return -1;
        }
        if (r == 0) return -1;
        total += r;
        resp[total] = '\0';
        if (strstr(resp, "\r\n\r\n")) break;
    }

    return strstr(resp, "101") ? 0 : -1;
}

int main(int argc, char **argv) {
    const char *host = argc > 1 ? argv[1] : DEFAULT_HOST;
    int port         = argc > 2 ? atoi(argv[2]) : DEFAULT_PORT;
    int num_conns    = argc > 3 ? atoi(argv[3]) : DEFAULT_CONNS;
    int msg_size     = argc > 4 ? atoi(argv[4]) : DEFAULT_MSG_SIZE;
    int duration     = argc > 5 ? atoi(argv[5]) : DEFAULT_DURATION_SEC;

    ws_platform_init();
    ws_log_init(WS_LOG_INFO, NULL);

    printf("========================================\n");
    printf("  websockify2 benchmark\n");
    printf("========================================\n");
    printf("Target:      %s:%d\n", host, port);
    printf("Connections: %d\n", num_conns);
    printf("Message:     %d bytes\n", msg_size);
    printf("Duration:    %d seconds\n", duration);
    printf("========================================\n\n");

    bench_conn_t *conns = (bench_conn_t *)calloc((size_t)num_conns, sizeof(bench_conn_t));
    if (!conns) {
        fprintf(stderr, "Failed to allocate connection array\n");
        return 1;
    }

    /* Prepare message payload */
    uint8_t *msg = (uint8_t *)malloc((size_t)msg_size);
    for (int i = 0; i < msg_size; i++) msg[i] = (uint8_t)(i & 0xFF);

    /* Phase 1: Establish connections */
    printf("Phase 1: Establishing connections...\n");
    uint64_t t_start = ws_time_ms();
    int connected = 0;

    for (int i = 0; i < num_conns; i++) {
        conns[i].fd = ws_connect_tcp(host, port);
        if (conns[i].fd == WS_INVALID_SOCKET) {
            fprintf(stderr, "  Connection %d failed\n", i);
            continue;
        }

        /* Set blocking for handshake */
#ifdef WS_PLATFORM_POSIX
        int flags = fcntl(conns[i].fd, F_GETFL, 0);
        fcntl(conns[i].fd, F_SETFL, flags & ~O_NONBLOCK);
#endif

        /* Wait for connect */
        fd_set wfds;
        FD_ZERO(&wfds);
        FD_SET(conns[i].fd, &wfds);
        struct timeval tv = {2, 0};
        if (select((int)conns[i].fd + 1, NULL, &wfds, NULL, &tv) <= 0) {
            ws_close_socket(conns[i].fd);
            conns[i].fd = WS_INVALID_SOCKET;
            continue;
        }

        /* Check connect error */
        int err = 0;
        socklen_t len = sizeof(err);
        getsockopt(conns[i].fd, SOL_SOCKET, SO_ERROR, (char *)&err, &len);
        if (err != 0) {
            ws_close_socket(conns[i].fd);
            conns[i].fd = WS_INVALID_SOCKET;
            continue;
        }

        conns[i].connected = 1;
        connected++;

        if ((i + 1) % 100 == 0)
            printf("  %d/%d connected\n", connected, i + 1);
    }

    uint64_t t_conn = ws_time_ms() - t_start;
    printf("  %d/%d connected in %llu ms (%.0f conn/s)\n\n",
           connected, num_conns, (unsigned long long)t_conn,
           connected > 0 ? (double)connected / ((double)t_conn / 1000.0) : 0);

    /* Phase 2: WebSocket handshake */
    printf("Phase 2: WebSocket handshake...\n");
    t_start = ws_time_ms();
    int upgraded = 0;

    for (int i = 0; i < num_conns; i++) {
        if (!conns[i].connected) continue;
        if (do_ws_handshake(conns[i].fd, host, port) == 0) {
            conns[i].upgraded = 1;
            upgraded++;
        } else {
            ws_close_socket(conns[i].fd);
            conns[i].fd = WS_INVALID_SOCKET;
            conns[i].connected = 0;
        }
    }

    uint64_t t_upgrade = ws_time_ms() - t_start;
    printf("  %d/%d upgraded in %llu ms\n\n", upgraded, connected, (unsigned long long)t_upgrade);

    /* Phase 3: Send/receive messages */
    printf("Phase 3: Sending messages for %d seconds...\n", duration);

    /* Set all to non-blocking */
    for (int i = 0; i < num_conns; i++) {
        if (conns[i].upgraded)
            ws_set_nonblocking(conns[i].fd);
    }

    uint64_t total_sent = 0, total_recv = 0;
    uint64_t msg_sent = 0, msg_recv = 0;
    t_start = ws_time_ms();
    uint64_t deadline = t_start + (uint64_t)duration * 1000;

    /* Prepare WebSocket frame */
    uint8_t frame_hdr[WS_MAX_FRAME_HEADER];
    int hdr_len = ws_frame_encode_header(frame_hdr, WS_OP_BIN, (uint64_t)msg_size, 1);

    while (ws_time_ms() < deadline) {
        for (int i = 0; i < num_conns; i++) {
            if (!conns[i].upgraded) continue;

            /* Send */
            send(conns[i].fd, (const char *)frame_hdr, (size_t)hdr_len, 0);
            ssize_t s = send(conns[i].fd, (const char *)msg, (size_t)msg_size, 0);
            if (s > 0) {
                total_sent += (uint64_t)s;
                msg_sent++;
            }

            /* Receive */
            uint8_t rbuf[65536];
            ssize_t r = recv(conns[i].fd, rbuf, sizeof(rbuf), 0);
            if (r > 0) {
                total_recv += (uint64_t)r;
                msg_recv++;
            }
        }
    }

    uint64_t elapsed = ws_time_ms() - t_start;
    double elapsed_sec = (double)elapsed / 1000.0;

    /* Phase 4: Close connections */
    printf("Phase 4: Closing connections...\n");
    for (int i = 0; i < num_conns; i++) {
        if (conns[i].fd != WS_INVALID_SOCKET)
            ws_close_socket(conns[i].fd);
    }

    /* Results */
    printf("\n========================================\n");
    printf("  Results\n");
    printf("========================================\n");
    printf("Duration:        %.1f seconds\n", elapsed_sec);
    printf("Connections:     %d established, %d upgraded\n", connected, upgraded);
    printf("Conn rate:       %.0f conn/s\n",
           t_conn > 0 ? (double)connected / ((double)t_conn / 1000.0) : 0);
    printf("Messages sent:   %llu (%.0f msg/s)\n",
           (unsigned long long)msg_sent,
           elapsed_sec > 0 ? (double)msg_sent / elapsed_sec : 0);
    printf("Messages recv:   %llu (%.0f msg/s)\n",
           (unsigned long long)msg_recv,
           elapsed_sec > 0 ? (double)msg_recv / elapsed_sec : 0);
    printf("Throughput sent: %.2f MB/s\n",
           elapsed_sec > 0 ? (double)total_sent / elapsed_sec / 1048576.0 : 0);
    printf("Throughput recv: %.2f MB/s\n",
           elapsed_sec > 0 ? (double)total_recv / elapsed_sec / 1048576.0 : 0);
    printf("========================================\n");

    free(conns);
    free(msg);
    ws_platform_cleanup();
    return 0;
}
