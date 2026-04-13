/* SPDX-License-Identifier: MIT */
#ifndef WS_CONFIG_H
#define WS_CONFIG_H

#include "platform.h"
#include "net.h"

#define WS_VERSION "1.0.0"
#define WS_DEFAULT_BACKLOG    512
#define WS_DEFAULT_BUFSIZE    (1024u * 1024u)
#define WS_DEFAULT_MAX_CONN   10000
#define WS_MAX_HEADER_SIZE    8192
#define WS_MAX_PATH           4096

typedef struct {
    /* Listen */
    char        listen_host[256];
    int         listen_port;
    char        unix_listen[WS_MAX_PATH];

    /* Target */
    char        target_host[256];
    int         target_port;
    char        unix_target[WS_MAX_PATH];

    /* SSL */
    char        cert_file[WS_MAX_PATH];
    char        key_file[WS_MAX_PATH];
    char        ca_file[WS_MAX_PATH];
    char        ssl_ciphers[1024];
    int         ssl_only;
    int         verify_client;

    /* Web server */
    char        web_dir[WS_MAX_PATH];

    /* Token routing */
    char        token_plugin[WS_MAX_PATH]; /* file or directory path */
    int         host_token;

    /* Session recording */
    char        record_file[WS_MAX_PATH];

    /* Daemon */
    int         daemon_mode;
    char        pid_file[WS_MAX_PATH];
    char        log_file[WS_MAX_PATH];
    int         log_level;  /* ws_log_level_t */

    /* Tuning */
    int         max_connections;
    int         buffer_size;
    int         idle_timeout;  /* seconds, 0 = disabled */
    int         timeout;       /* total runtime limit */

    /* TCP keepalive */
    ws_keepalive_t keepalive;

    /* Misc */
    int         verbose;
} ws_config_t;

/* Initialize with defaults */
void ws_config_init(ws_config_t *cfg);

/* Parse command line. Returns 0 on success, -1 on error, 1 if help/version printed */
int ws_config_parse(ws_config_t *cfg, int argc, char **argv);

/* Print usage */
void ws_config_usage(const char *progname);

#endif /* WS_CONFIG_H */
