/* SPDX-License-Identifier: MIT */
#include "server.h"
#include "event.h"
#include "net.h"
#include "proxy.h"
#include "log.h"

/* Global proxy context (single-process). Referenced by proxy.c */
ws_proxy_ctx_t *g_proxy_ctx = NULL;

static volatile sig_atomic_t g_running = 1;
static ws_event_loop_t *g_loop = NULL;

#ifdef WS_PLATFORM_POSIX
#include <signal.h>
/* Async-signal-safe: only writes to volatile sig_atomic_t. The pending signal
 * also interrupts epoll_wait/kevent with EINTR, so the loop wakes promptly. */
static void signal_handler(int sig) {
    (void)sig;
    g_running = 0;
    if (g_loop)
        g_loop->running = 0;
}
#else
static BOOL WINAPI console_handler(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_CLOSE_EVENT) {
        g_running = 0;
        if (g_loop) g_loop->running = 0;
        return TRUE;
    }
    return FALSE;
}
#endif

static void idle_timeout_cb(ws_event_loop_t *loop, void *data) {
    (void)loop;
    ws_proxy_ctx_t *ctx = (ws_proxy_ctx_t *)data;
    uint64_t now = ws_time_ms();
    uint64_t timeout = (uint64_t)ctx->config->idle_timeout * 1000ULL;

    ws_conn_t *conn = ctx->active_list;
    while (conn) {
        ws_conn_t *next = conn->next;
        if (conn->state != CONN_STATE_CLOSED &&
            (now - conn->last_activity) > timeout) {
            ws_log_debug("idle timeout for %s:%d", conn->client_addr, conn->client_port);

            if (conn->loop) {
                if (conn->client_fd != WS_INVALID_SOCKET)
                    ws_event_del(conn->loop, conn->client_fd);
                if (conn->target_fd != WS_INVALID_SOCKET)
                    ws_event_del(conn->loop, conn->target_fd);
            }
            if (conn->client_ssl) { ws_ssl_free(conn->client_ssl); conn->client_ssl = NULL; }
            if (conn->client_fd != WS_INVALID_SOCKET) { ws_close_socket(conn->client_fd); conn->client_fd = WS_INVALID_SOCKET; }
            if (conn->target_fd != WS_INVALID_SOCKET) { ws_close_socket(conn->target_fd); conn->target_fd = WS_INVALID_SOCKET; }
            if (conn->file_fd >= 0) { close(conn->file_fd); conn->file_fd = -1; }
            if (conn->record) { ws_record_close(conn->record); free(conn->record); conn->record = NULL; }
            ws_buf_free(&conn->client_recv); ws_buf_free(&conn->client_send);
            ws_buf_free(&conn->target_recv); ws_buf_free(&conn->target_send);
            ws_dbuf_free(&conn->http_buf);
            conn->state = CONN_STATE_CLOSED;

            if (conn->prev) conn->prev->next = conn->next;
            else ctx->active_list = conn->next;
            if (conn->next) conn->next->prev = conn->prev;
            ctx->conn_count--;
            free(conn);
        }
        conn = next;
    }
}

int ws_server_run(ws_config_t *config) {
#ifdef WS_PLATFORM_POSIX
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
#else
    SetConsoleCtrlHandler(console_handler, TRUE);
#endif

    /* Create listen socket. SO_REUSEPORT enabled so users can run multiple
     * instances on the same port for multi-core scaling if needed. */
    ws_socket_t listen_fd;
    if (config->unix_listen[0]) {
        listen_fd = ws_listen_unix(config->unix_listen, WS_DEFAULT_BACKLOG, 0);
    } else {
        listen_fd = ws_listen_tcp(
            config->listen_host[0] ? config->listen_host : NULL,
            config->listen_port,
            WS_DEFAULT_BACKLOG,
            1  /* reuseport — allows running multiple instances for multi-core */
        );
    }
    if (listen_fd == WS_INVALID_SOCKET) {
        ws_log_fatal("failed to create listen socket");
        return -1;
    }

    ws_event_loop_t *loop = ws_event_create(1024);
    g_loop = loop;
    if (!loop) {
        ws_close_socket(listen_fd);
        return -1;
    }

    ws_proxy_ctx_t proxy_ctx;
    if (ws_proxy_init(&proxy_ctx, config, loop, listen_fd) < 0) {
        ws_event_destroy(loop);
        ws_close_socket(listen_fd);
        return -1;
    }
    g_proxy_ctx = &proxy_ctx;

    if (config->idle_timeout > 0) {
        ws_timer_add(loop, 5000, 1, idle_timeout_cb, &proxy_ctx);
    }

    ws_log_info("server started, listening on %s:%d",
               config->listen_host[0] ? config->listen_host : "*",
               config->listen_port);

    /* ws_event_run loops internally until loop->running is cleared by the
     * signal handler (SIGINT/SIGTERM); it returns 0 for a graceful stop and
     * -1 on unrecoverable error. Either way we fall through to cleanup. */
    ws_event_run(loop, 1000);

    ws_log_info("server shutting down");
    ws_proxy_cleanup(&proxy_ctx);
    ws_event_destroy(loop);
    ws_close_socket(listen_fd);
    g_proxy_ctx = NULL;
    g_loop = NULL;

    return 0;
}
