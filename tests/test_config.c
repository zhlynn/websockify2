/* SPDX-License-Identifier: MIT */
#include "test_framework.h"
#include "config.h"

/* Save/restore optind since getopt is stateful across tests */
extern int optind;

static void reset_getopt(void) {
    optind = 1;
#ifdef __APPLE__
    extern int optreset;
    optreset = 1;
#endif
}

static int test_config_defaults(void) {
    ws_config_t cfg;
    ws_config_init(&cfg);
    ASSERT_EQ(cfg.keepalive.enabled, 1);
    ASSERT_EQ(cfg.keepalive.idle, 60);
    ASSERT_EQ(cfg.keepalive.interval, 10);
    ASSERT_EQ(cfg.keepalive.count, 5);
    ASSERT_EQ(cfg.ssl_only, 0);
    ASSERT_EQ(cfg.daemon_mode, 0);
    ASSERT_EQ(cfg.verbose, 0);
    return 0;
}

static int test_config_parse_basic(void) {
    ws_config_t cfg;
    ws_config_init(&cfg);
    char *argv[] = {"ws", "8080", "localhost:5900", NULL};
    reset_getopt();
    int ret = ws_config_parse(&cfg, 3, argv);
    ASSERT_EQ(ret, 0);
    ASSERT_EQ(cfg.listen_port, 8080);
    ASSERT_STR_EQ(cfg.target_host, "localhost");
    ASSERT_EQ(cfg.target_port, 5900);
    return 0;
}

static int test_config_parse_listen_host(void) {
    ws_config_t cfg;
    ws_config_init(&cfg);
    char *argv[] = {"ws", "0.0.0.0:8080", "localhost:5900", NULL};
    reset_getopt();
    ASSERT_EQ(ws_config_parse(&cfg, 3, argv), 0);
    ASSERT_STR_EQ(cfg.listen_host, "0.0.0.0");
    ASSERT_EQ(cfg.listen_port, 8080);
    return 0;
}

static int test_config_parse_ipv6(void) {
    ws_config_t cfg;
    ws_config_init(&cfg);
    char *argv[] = {"ws", "[::1]:8080", "localhost:5900", NULL};
    reset_getopt();
    ASSERT_EQ(ws_config_parse(&cfg, 3, argv), 0);
    ASSERT_STR_EQ(cfg.listen_host, "::1");
    ASSERT_EQ(cfg.listen_port, 8080);
    return 0;
}

static int test_config_parse_web(void) {
    ws_config_t cfg;
    ws_config_init(&cfg);
    char *argv[] = {"ws", "--web", "/var/www", "8080", "localhost:5900", NULL};
    reset_getopt();
    ASSERT_EQ(ws_config_parse(&cfg, 5, argv), 0);
    ASSERT_STR_EQ(cfg.web_dir, "/var/www");
    return 0;
}

static int test_config_parse_ssl(void) {
    ws_config_t cfg;
    ws_config_init(&cfg);
    char *argv[] = {"ws", "--cert", "/tmp/cert.pem", "--key", "/tmp/key.pem",
                    "--ssl-only", "8080", "localhost:5900", NULL};
    reset_getopt();
    ASSERT_EQ(ws_config_parse(&cfg, 8, argv), 0);
    ASSERT_STR_EQ(cfg.cert_file, "/tmp/cert.pem");
    ASSERT_STR_EQ(cfg.key_file, "/tmp/key.pem");
    ASSERT_EQ(cfg.ssl_only, 1);
    return 0;
}

static int test_config_key_defaults_to_cert(void) {
    ws_config_t cfg;
    ws_config_init(&cfg);
    char *argv[] = {"ws", "--cert", "/tmp/bundle.pem", "8080", "localhost:5900", NULL};
    reset_getopt();
    ASSERT_EQ(ws_config_parse(&cfg, 5, argv), 0);
    ASSERT_STR_EQ(cfg.cert_file, "/tmp/bundle.pem");
    ASSERT_STR_EQ(cfg.key_file, "/tmp/bundle.pem");
    return 0;
}

static int test_config_parse_daemon(void) {
    ws_config_t cfg;
    ws_config_init(&cfg);
    char *argv[] = {"ws", "-D", "--pid-file", "/run/x.pid",
                    "--log-file", "/var/log/x.log", "8080", "localhost:5900", NULL};
    reset_getopt();
    ASSERT_EQ(ws_config_parse(&cfg, 8, argv), 0);
    ASSERT_EQ(cfg.daemon_mode, 1);
    ASSERT_STR_EQ(cfg.pid_file, "/run/x.pid");
    ASSERT_STR_EQ(cfg.log_file, "/var/log/x.log");
    return 0;
}

static int test_config_parse_log_level(void) {
    ws_config_t cfg;
    ws_config_init(&cfg);
    char *argv[] = {"ws", "--log-level", "debug", "8080", "localhost:5900", NULL};
    reset_getopt();
    ASSERT_EQ(ws_config_parse(&cfg, 5, argv), 0);
    ASSERT_EQ(cfg.log_level, 0);

    ws_config_init(&cfg);
    char *argv2[] = {"ws", "--log-level", "error", "8080", "localhost:5900", NULL};
    reset_getopt();
    ASSERT_EQ(ws_config_parse(&cfg, 5, argv2), 0);
    ASSERT_EQ(cfg.log_level, 3);
    return 0;
}

static int test_config_parse_tuning(void) {
    ws_config_t cfg;
    ws_config_init(&cfg);
    char *argv[] = {"ws", "--idle-timeout", "300",
                    "8080", "localhost:5900", NULL};
    reset_getopt();
    ASSERT_EQ(ws_config_parse(&cfg, 5, argv), 0);
    ASSERT_EQ(cfg.idle_timeout, 300);
    return 0;
}

static int test_config_parse_keepalive(void) {
    ws_config_t cfg;
    ws_config_init(&cfg);
    char *argv[] = {"ws", "--keepalive-idle", "120",
                    "--keepalive-intvl", "20",
                    "--keepalive-cnt", "9",
                    "8080", "localhost:5900", NULL};
    reset_getopt();
    ASSERT_EQ(ws_config_parse(&cfg, 9, argv), 0);
    ASSERT_EQ(cfg.keepalive.idle, 120);
    ASSERT_EQ(cfg.keepalive.interval, 20);
    ASSERT_EQ(cfg.keepalive.count, 9);
    return 0;
}

static int test_config_parse_token(void) {
    ws_config_t cfg;
    ws_config_init(&cfg);
    char *argv[] = {"ws", "--token-plugin", "/etc/tokens.cfg",
                    "--host-token", "8080", NULL};
    reset_getopt();
    ASSERT_EQ(ws_config_parse(&cfg, 5, argv), 0);
    ASSERT_STR_EQ(cfg.token_plugin, "/etc/tokens.cfg");
    ASSERT_EQ(cfg.host_token, 1);
    return 0;
}

static int test_config_parse_unix(void) {
    ws_config_t cfg;
    ws_config_init(&cfg);
    char *argv[] = {"ws", "--unix-listen", "/tmp/ws.sock",
                    "--unix-target", "/tmp/target.sock", NULL};
    reset_getopt();
    ASSERT_EQ(ws_config_parse(&cfg, 5, argv), 0);
    ASSERT_STR_EQ(cfg.unix_listen, "/tmp/ws.sock");
    ASSERT_STR_EQ(cfg.unix_target, "/tmp/target.sock");
    return 0;
}

static int test_config_parse_record(void) {
    ws_config_t cfg;
    ws_config_init(&cfg);
    char *argv[] = {"ws", "--record", "/tmp/session.log", "8080", "localhost:5900", NULL};
    reset_getopt();
    ASSERT_EQ(ws_config_parse(&cfg, 5, argv), 0);
    ASSERT_STR_EQ(cfg.record_file, "/tmp/session.log");
    return 0;
}

static int test_config_parse_verbose(void) {
    ws_config_t cfg;
    ws_config_init(&cfg);
    char *argv[] = {"ws", "-v", "8080", "localhost:5900", NULL};
    reset_getopt();
    ASSERT_EQ(ws_config_parse(&cfg, 4, argv), 0);
    ASSERT_EQ(cfg.verbose, 1);
    ASSERT_EQ(cfg.log_level, 0);  /* verbose sets debug */
    return 0;
}

static int test_config_help(void) {
    ws_config_t cfg;
    ws_config_init(&cfg);
    char *argv[] = {"ws", "--help", NULL};
    reset_getopt();
    /* --help returns 1, printed to stderr */
    FILE *old = stderr;
    stderr = fopen("/dev/null", "w");
    int ret = ws_config_parse(&cfg, 2, argv);
    fclose(stderr);
    stderr = old;
    ASSERT_EQ(ret, 1);
    return 0;
}

static int test_config_missing_listen(void) {
    ws_config_t cfg;
    ws_config_init(&cfg);
    char *argv[] = {"ws", NULL};
    reset_getopt();
    FILE *old = stderr;
    stderr = fopen("/dev/null", "w");
    int ret = ws_config_parse(&cfg, 1, argv);
    fclose(stderr);
    stderr = old;
    ASSERT_EQ(ret, 1);  /* missing args → usage, exit clean */
    return 0;
}

static int test_config_missing_target(void) {
    ws_config_t cfg;
    ws_config_init(&cfg);
    char *argv[] = {"ws", "8080", NULL};
    reset_getopt();
    FILE *old = stderr;
    stderr = fopen("/dev/null", "w");
    int ret = ws_config_parse(&cfg, 2, argv);
    fclose(stderr);
    stderr = old;
    ASSERT_EQ(ret, 1);  /* no target and no token-plugin → usage */
    return 0;
}

static int test_config_token_without_target(void) {
    /* --token-plugin makes target optional */
    ws_config_t cfg;
    ws_config_init(&cfg);
    char *argv[] = {"ws", "--token-plugin", "/etc/tokens", "8080", NULL};
    reset_getopt();
    ASSERT_EQ(ws_config_parse(&cfg, 4, argv), 0);
    return 0;
}

void test_config_run(void) {
    TEST_SUITE("Config Parsing");
    TEST_RUN(test_config_defaults);
    TEST_RUN(test_config_parse_basic);
    TEST_RUN(test_config_parse_listen_host);
    TEST_RUN(test_config_parse_ipv6);
    TEST_RUN(test_config_parse_web);
    TEST_RUN(test_config_parse_ssl);
    TEST_RUN(test_config_key_defaults_to_cert);
    TEST_RUN(test_config_parse_daemon);
    TEST_RUN(test_config_parse_log_level);
    TEST_RUN(test_config_parse_tuning);
    TEST_RUN(test_config_parse_keepalive);
    TEST_RUN(test_config_parse_token);
    TEST_RUN(test_config_parse_unix);
    TEST_RUN(test_config_parse_record);
    TEST_RUN(test_config_parse_verbose);
    TEST_RUN(test_config_help);
    TEST_RUN(test_config_missing_listen);
    TEST_RUN(test_config_missing_target);
    TEST_RUN(test_config_token_without_target);
}
