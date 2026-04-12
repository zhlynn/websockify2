/* SPDX-License-Identifier: MIT */
#include "config.h"
#include "log.h"
#include <getopt.h>

#ifdef WS_PLATFORM_WINDOWS
  #define ws_strcasecmp(a, b) _stricmp((a), (b))
#else
  #define ws_strcasecmp(a, b) strcasecmp((a), (b))
#endif

void ws_config_init(ws_config_t *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->listen_port = 0;
    cfg->target_port = 0;
    cfg->log_level = 1;  /* WS_LOG_INFO */
    cfg->max_connections = WS_DEFAULT_MAX_CONN;
    cfg->buffer_size = WS_DEFAULT_BUFSIZE;
    cfg->keepalive.enabled = 1;
    cfg->keepalive.idle = 60;
    cfg->keepalive.interval = 10;
    cfg->keepalive.count = 5;
}

void ws_config_usage(const char *progname) {
    fprintf(stderr,
        "websockify2 %s — WebSocket-to-TCP proxy\n\n"
        "Usage: %s [options] [listen_host:]listen_port [target_host:target_port]\n\n"
        "Options:\n"
        "  -w, --web DIR             Serve static files from DIR\n"
        "  -c, --cert FILE           SSL certificate file\n"
        "  -k, --key FILE            SSL key file (default: same as cert)\n"
        "  -C, --ca-cert FILE        CA certificate for client verification\n"
        "  -s, --ssl-only            Reject non-SSL connections\n"
        "      --ssl-ciphers LIST    SSL cipher list\n"
        "      --verify-client       Require client certificate\n"
        "  -t, --token-plugin PATH   Token file or directory for target routing\n"
        "  -H, --host-token          Extract token from Host header\n"
        "  -r, --record FILE         Record sessions to FILE\n"
        "  -D, --daemon              Run as daemon\n"
        "  -p, --pid-file FILE       PID file path\n"
        "  -l, --log-file FILE       Log to FILE (or \"syslog\")\n"
        "  -L, --log-level LEVEL     debug/info/warn/error (default: info)\n"
        "  -n, --max-connections N   Max concurrent connections (default: %d)\n"
        "  -b, --buffer-size N       Per-connection buffer size (default: %d)\n"
        "  -i, --idle-timeout N      Close idle connections after N seconds\n"
        "  -T, --timeout N           Shutdown after N seconds\n"
        "  -U, --unix-listen PATH    Listen on Unix socket\n"
        "  -u, --unix-target PATH    Connect to Unix socket target\n"
        "      --keepalive-idle N    TCP keepalive idle time (default: 60)\n"
        "      --keepalive-intvl N   TCP keepalive interval (default: 10)\n"
        "      --keepalive-cnt N     TCP keepalive count (default: 5)\n"
        "  -v, --verbose             Verbose output\n"
        "  -h, --help                Show this help\n"
        "  -V, --version             Show version\n",
        WS_VERSION, progname, WS_DEFAULT_MAX_CONN, WS_DEFAULT_BUFSIZE);
}

static int parse_host_port(const char *str, char *host, size_t host_size, int *port) {
    const char *colon = strrchr(str, ':');
    if (!colon) {
        /* port only */
        host[0] = '\0';
        *port = atoi(str);
        return (*port > 0) ? 0 : -1;
    }

    /* Handle IPv6 [host]:port */
    if (str[0] == '[') {
        const char *bracket = strchr(str, ']');
        if (!bracket) return -1;
        size_t len = (size_t)(bracket - str - 1);
        if (len >= host_size) return -1;
        memcpy(host, str + 1, len);
        host[len] = '\0';
        if (bracket[1] == ':')
            *port = atoi(bracket + 2);
        else
            return -1;
        return 0;
    }

    /* Check if there are multiple colons (IPv6 without port) */
    if (strchr(str, ':') != colon) {
        /* Looks like bare IPv6, no port */
        return -1;
    }

    size_t len = (size_t)(colon - str);
    if (len >= host_size) return -1;
    memcpy(host, str, len);
    host[len] = '\0';
    *port = atoi(colon + 1);
    return (*port > 0) ? 0 : -1;
}

static int parse_log_level(const char *s) {
    if (ws_strcasecmp(s, "debug") == 0) return 0;
    if (ws_strcasecmp(s, "info") == 0)  return 1;
    if (ws_strcasecmp(s, "warn") == 0)  return 2;
    if (ws_strcasecmp(s, "error") == 0) return 3;
    return 1;
}

enum {
    OPT_SSL_CIPHERS = 256, OPT_VERIFY_CLIENT,
    OPT_KA_IDLE, OPT_KA_INTVL, OPT_KA_CNT
};

int ws_config_parse(ws_config_t *cfg, int argc, char **argv) {
    static struct option long_opts[] = {
        {"web",             required_argument, 0, 'w'},
        {"cert",            required_argument, 0, 'c'},
        {"key",             required_argument, 0, 'k'},
        {"ca-cert",         required_argument, 0, 'C'},
        {"ssl-only",        no_argument,       0, 's'},
        {"ssl-ciphers",     required_argument, 0, OPT_SSL_CIPHERS},
        {"verify-client",   no_argument,       0, OPT_VERIFY_CLIENT},
        {"token-plugin",    required_argument, 0, 't'},
        {"host-token",      no_argument,       0, 'H'},
        {"record",          required_argument, 0, 'r'},
        {"daemon",          no_argument,       0, 'D'},
        {"pid-file",        required_argument, 0, 'p'},
        {"log-file",        required_argument, 0, 'l'},
        {"log-level",       required_argument, 0, 'L'},
        {"max-connections", required_argument, 0, 'n'},
        {"buffer-size",     required_argument, 0, 'b'},
        {"idle-timeout",    required_argument, 0, 'i'},
        {"timeout",         required_argument, 0, 'T'},
        {"unix-listen",     required_argument, 0, 'U'},
        {"unix-target",     required_argument, 0, 'u'},
        {"keepalive-idle",  required_argument, 0, OPT_KA_IDLE},
        {"keepalive-intvl", required_argument, 0, OPT_KA_INTVL},
        {"keepalive-cnt",   required_argument, 0, OPT_KA_CNT},
        {"verbose",         no_argument,       0, 'v'},
        {"help",            no_argument,       0, 'h'},
        {"version",         no_argument,       0, 'V'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv,
                              "w:c:k:C:st:Hr:Dp:l:L:n:b:i:T:U:u:vhV",
                              long_opts, NULL)) != -1) {
        switch (opt) {
        case 'w':              snprintf(cfg->web_dir, sizeof(cfg->web_dir), "%s", optarg); break;
        case 'c':              snprintf(cfg->cert_file, sizeof(cfg->cert_file), "%s", optarg); break;
        case 'k':              snprintf(cfg->key_file, sizeof(cfg->key_file), "%s", optarg); break;
        case 'C':              snprintf(cfg->ca_file, sizeof(cfg->ca_file), "%s", optarg); break;
        case 's':              cfg->ssl_only = 1; break;
        case OPT_SSL_CIPHERS:  snprintf(cfg->ssl_ciphers, sizeof(cfg->ssl_ciphers), "%s", optarg); break;
        case OPT_VERIFY_CLIENT:cfg->verify_client = 1; break;
        case 't':              snprintf(cfg->token_plugin, sizeof(cfg->token_plugin), "%s", optarg); break;
        case 'H':              cfg->host_token = 1; break;
        case 'r':              snprintf(cfg->record_file, sizeof(cfg->record_file), "%s", optarg); break;
        case 'D':              cfg->daemon_mode = 1; break;
        case 'p':              snprintf(cfg->pid_file, sizeof(cfg->pid_file), "%s", optarg); break;
        case 'l':              snprintf(cfg->log_file, sizeof(cfg->log_file), "%s", optarg); break;
        case 'L':              cfg->log_level = parse_log_level(optarg); break;
        case 'n':              cfg->max_connections = atoi(optarg); break;
        case 'b':              cfg->buffer_size = atoi(optarg); break;
        case 'i':              cfg->idle_timeout = atoi(optarg); break;
        case 'T':              cfg->timeout = atoi(optarg); break;
        case 'U':              snprintf(cfg->unix_listen, sizeof(cfg->unix_listen), "%s", optarg); break;
        case 'u':              snprintf(cfg->unix_target, sizeof(cfg->unix_target), "%s", optarg); break;
        case OPT_KA_IDLE:      cfg->keepalive.idle = atoi(optarg); break;
        case OPT_KA_INTVL:     cfg->keepalive.interval = atoi(optarg); break;
        case OPT_KA_CNT:       cfg->keepalive.count = atoi(optarg); break;
        case 'v':              cfg->verbose = 1; cfg->log_level = 0; break;
        case 'h':              ws_config_usage(argv[0]); return 1;
        case 'V':              printf("websockify2 %s\n", WS_VERSION); return 1;
        default:               return -1;
        }
    }

    /* Positional args: [listen_host:]listen_port [target_host:target_port] */
    int remaining = argc - optind;
    if (remaining >= 1) {
        if (parse_host_port(argv[optind], cfg->listen_host, sizeof(cfg->listen_host),
                           &cfg->listen_port) < 0) {
            fprintf(stderr, "Invalid listen address: %s\n", argv[optind]);
            return -1;
        }
    }
    if (remaining >= 2) {
        if (parse_host_port(argv[optind + 1], cfg->target_host, sizeof(cfg->target_host),
                           &cfg->target_port) < 0) {
            fprintf(stderr, "Invalid target address: %s\n", argv[optind + 1]);
            return -1;
        }
    }

    /* Validation */
    if (cfg->listen_port <= 0 && cfg->unix_listen[0] == '\0') {
        fprintf(stderr, "Error: listen port or --unix-listen required\n");
        return -1;
    }

    if (cfg->target_port <= 0 && cfg->unix_target[0] == '\0' &&
        cfg->token_plugin[0] == '\0') {
        fprintf(stderr, "Error: target address or --token-plugin required\n");
        return -1;
    }

    /* Key defaults to cert if not specified */
    if (cfg->cert_file[0] && !cfg->key_file[0])
        snprintf(cfg->key_file, sizeof(cfg->key_file), "%s", cfg->cert_file);

    return 0;
}
