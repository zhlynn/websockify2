/* SPDX-License-Identifier: MIT */
#ifndef WS_NET_H
#define WS_NET_H

#include "platform.h"

/* TCP keepalive config */
typedef struct {
    int enabled;    /* 0 = off */
    int idle;       /* seconds before first probe (TCP_KEEPIDLE) */
    int interval;   /* seconds between probes (TCP_KEEPINTVL) */
    int count;      /* probes before drop (TCP_KEEPCNT) */
} ws_keepalive_t;

/* Create listening socket. Returns socket fd or WS_INVALID_SOCKET */
ws_socket_t ws_listen_tcp(const char *host, int port, int backlog, int reuseport);

/* Create Unix domain socket listener */
ws_socket_t ws_listen_unix(const char *path, int backlog, int mode);

/* Connect to target host:port (non-blocking). Returns socket fd */
ws_socket_t ws_connect_tcp(const char *host, int port);

/* Connect to Unix socket target */
ws_socket_t ws_connect_unix(const char *path);

/* Accept connection. Returns new fd */
ws_socket_t ws_accept(ws_socket_t listen_fd, char *addr_out, size_t addr_size, int *port_out);

/* Socket options */
int ws_set_tcp_nodelay(ws_socket_t fd, int on);
int ws_set_tcp_keepalive(ws_socket_t fd, const ws_keepalive_t *ka);
int ws_set_reuseaddr(ws_socket_t fd, int on);

/* Resolve host to string for logging */
int ws_resolve_host(const char *host, int port, struct sockaddr_storage *addr, socklen_t *addrlen);

#endif /* WS_NET_H */
