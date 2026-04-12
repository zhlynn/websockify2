/* SPDX-License-Identifier: MIT */
#include "net.h"
#include "log.h"

int ws_resolve_host(const char *host, int port, struct sockaddr_storage *addr, socklen_t *addrlen) {
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);

    int err = getaddrinfo(host, port_str, &hints, &res);
    if (err != 0) {
        ws_log_error("getaddrinfo(%s:%d): %s", host ? host : "*", port, gai_strerror(err));
        return -1;
    }

    memcpy(addr, res->ai_addr, res->ai_addrlen);
    *addrlen = (socklen_t)res->ai_addrlen;
    freeaddrinfo(res);
    return 0;
}

ws_socket_t ws_listen_tcp(const char *host, int port, int backlog, int reuseport) {
    struct sockaddr_storage addr;
    socklen_t addrlen;

    if (ws_resolve_host(host, port, &addr, &addrlen) < 0)
        return WS_INVALID_SOCKET;

    ws_socket_t fd = socket(addr.ss_family, SOCK_STREAM, 0);
    if (fd == WS_INVALID_SOCKET) {
        ws_log_error("socket(): %s", strerror(ws_errno()));
        return WS_INVALID_SOCKET;
    }

    ws_set_reuseaddr(fd, 1);
    ws_set_cloexec(fd);

#ifdef WS_HAVE_REUSEPORT
    if (reuseport) {
        int on = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on));
    }
#else
    (void)reuseport;
#endif

    if (bind(fd, (struct sockaddr *)&addr, addrlen) < 0) {
        ws_log_error("bind(%s:%d): %s", host ? host : "*", port, strerror(ws_errno()));
        ws_close_socket(fd);
        return WS_INVALID_SOCKET;
    }

    if (listen(fd, backlog) < 0) {
        ws_log_error("listen(): %s", strerror(ws_errno()));
        ws_close_socket(fd);
        return WS_INVALID_SOCKET;
    }

    ws_set_nonblocking(fd);
    return fd;
}

#ifdef WS_PLATFORM_POSIX
ws_socket_t ws_listen_unix(const char *path, int backlog, int mode) {
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);

    /* Remove existing socket file */
    unlink(path);

    ws_socket_t fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd == WS_INVALID_SOCKET) return WS_INVALID_SOCKET;

    ws_set_cloexec(fd);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ws_log_error("bind(unix:%s): %s", path, strerror(errno));
        ws_close_socket(fd);
        return WS_INVALID_SOCKET;
    }

    if (mode > 0) chmod(path, (mode_t)mode);

    if (listen(fd, backlog) < 0) {
        ws_close_socket(fd);
        return WS_INVALID_SOCKET;
    }

    ws_set_nonblocking(fd);
    return fd;
}

ws_socket_t ws_connect_unix(const char *path) {
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);

    ws_socket_t fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd == WS_INVALID_SOCKET) return WS_INVALID_SOCKET;

    ws_set_nonblocking(fd);
    ws_set_cloexec(fd);

    int ret = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
    if (ret < 0 && errno != EINPROGRESS) {
        ws_close_socket(fd);
        return WS_INVALID_SOCKET;
    }
    return fd;
}
#else
ws_socket_t ws_listen_unix(const char *path, int backlog, int mode) {
    (void)path; (void)backlog; (void)mode;
    ws_log_error("Unix sockets not supported on Windows");
    return WS_INVALID_SOCKET;
}
ws_socket_t ws_connect_unix(const char *path) {
    (void)path;
    ws_log_error("Unix sockets not supported on Windows");
    return WS_INVALID_SOCKET;
}
#endif

ws_socket_t ws_connect_tcp(const char *host, int port) {
    struct sockaddr_storage addr;
    socklen_t addrlen;

    if (ws_resolve_host(host, port, &addr, &addrlen) < 0)
        return WS_INVALID_SOCKET;

    ws_socket_t fd = socket(addr.ss_family, SOCK_STREAM, 0);
    if (fd == WS_INVALID_SOCKET) return WS_INVALID_SOCKET;

    ws_set_nonblocking(fd);
    ws_set_cloexec(fd);
    ws_set_tcp_nodelay(fd, 1);

    int ret = connect(fd, (struct sockaddr *)&addr, addrlen);
    if (ret < 0) {
        int err = ws_errno();
        if (err != WS_EINPROGRESS && err != WS_EAGAIN) {
            ws_close_socket(fd);
            return WS_INVALID_SOCKET;
        }
    }
    return fd;
}

ws_socket_t ws_accept(ws_socket_t listen_fd, char *addr_out, size_t addr_size, int *port_out) {
    struct sockaddr_storage sa;
    socklen_t sa_len = sizeof(sa);

    ws_socket_t fd = accept(listen_fd, (struct sockaddr *)&sa, &sa_len);
    if (fd == WS_INVALID_SOCKET) return WS_INVALID_SOCKET;

    ws_set_nonblocking(fd);
    ws_set_cloexec(fd);

    if (addr_out) {
        if (sa.ss_family == AF_INET) {
            struct sockaddr_in *s4 = (struct sockaddr_in *)&sa;
            inet_ntop(AF_INET, &s4->sin_addr, addr_out, (socklen_t)addr_size);
            if (port_out) *port_out = ntohs(s4->sin_port);
        } else if (sa.ss_family == AF_INET6) {
            struct sockaddr_in6 *s6 = (struct sockaddr_in6 *)&sa;
            inet_ntop(AF_INET6, &s6->sin6_addr, addr_out, (socklen_t)addr_size);
            if (port_out) *port_out = ntohs(s6->sin6_port);
        }
#ifdef WS_PLATFORM_POSIX
        else if (sa.ss_family == AF_UNIX) {
            snprintf(addr_out, addr_size, "unix");
            if (port_out) *port_out = 0;
        }
#endif
    }
    return fd;
}

int ws_set_tcp_nodelay(ws_socket_t fd, int on) {
    return setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const char *)&on, sizeof(on));
}

int ws_set_reuseaddr(ws_socket_t fd, int on) {
    return setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&on, sizeof(on));
}

int ws_set_tcp_keepalive(ws_socket_t fd, const ws_keepalive_t *ka) {
    if (!ka || !ka->enabled) return 0;

    int on = 1;
    setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, (const char *)&on, sizeof(on));

#ifdef __linux__
    if (ka->idle > 0)
        setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &ka->idle, sizeof(ka->idle));
    if (ka->interval > 0)
        setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &ka->interval, sizeof(ka->interval));
    if (ka->count > 0)
        setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &ka->count, sizeof(ka->count));
#elif defined(__APPLE__) || defined(__FreeBSD__)
    if (ka->idle > 0) {
        int val = ka->idle;
        setsockopt(fd, IPPROTO_TCP, TCP_KEEPALIVE, &val, sizeof(val));
    }
#endif
    return 0;
}
