/* SPDX-License-Identifier: MIT */
/*
 * Simple TCP echo server for integration testing.
 * Echoes back all received data.
 * Usage: echo_server <port>
 */
#include "platform.h"
#include "net.h"
#include "log.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        return 1;
    }

    ws_platform_init();
    ws_log_init(WS_LOG_INFO, NULL);

    int port = atoi(argv[1]);
    ws_socket_t listen_fd = ws_listen_tcp(NULL, port, 16, 0);
    if (listen_fd == WS_INVALID_SOCKET) {
        fprintf(stderr, "Failed to listen on port %d\n", port);
        return 1;
    }

    /* Set blocking for simplicity */
#ifdef WS_PLATFORM_POSIX
    int flags = fcntl(listen_fd, F_GETFL, 0);
    fcntl(listen_fd, F_SETFL, flags & ~O_NONBLOCK);
#endif

    printf("Echo server listening on port %d\n", port);
    fflush(stdout);

    /* Accept and echo in a loop (single connection at a time for testing) */
    for (;;) {
        char addr[64];
        int cport;
        ws_socket_t fd = ws_accept(listen_fd, addr, sizeof(addr), &cport);
        if (fd == WS_INVALID_SOCKET) continue;

        /* Set blocking */
#ifdef WS_PLATFORM_POSIX
        flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
#endif

        printf("Connection from %s:%d\n", addr, cport);
        fflush(stdout);

        char buf[4096];
        for (;;) {
            ssize_t n = recv(fd, buf, sizeof(buf), 0);
            if (n <= 0) break;

            /* Echo back */
            ssize_t sent = 0;
            while (sent < n) {
                ssize_t s = send(fd, buf + sent, (size_t)(n - sent), 0);
                if (s <= 0) break;
                sent += s;
            }
            if (sent < n) break;
        }

        ws_close_socket(fd);
        printf("Connection closed\n");
        fflush(stdout);
    }

    ws_close_socket(listen_fd);
    ws_platform_cleanup();
    return 0;
}
