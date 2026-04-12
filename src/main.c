/* SPDX-License-Identifier: MIT */
#include "platform.h"
#include "config.h"
#include "log.h"
#include "daemon.h"
#include "server.h"

int main(int argc, char **argv) {
    ws_config_t config;
    ws_config_init(&config);

    int ret = ws_config_parse(&config, argc, argv);
    if (ret != 0)
        return ret < 0 ? 1 : 0;

    if (ws_platform_init() < 0) {
        fprintf(stderr, "Platform initialization failed\n");
        return 1;
    }

    ws_log_init((ws_log_level_t)config.log_level,
                config.log_file[0] ? config.log_file : NULL);

    ws_log_info("websockify2 %s starting", WS_VERSION);
    ws_log_info("listening on %s:%d, target %s:%d",
               config.listen_host[0] ? config.listen_host : "*",
               config.listen_port,
               config.target_host[0] ? config.target_host : "(token)",
               config.target_port);
    ws_log_info("max connections: %d, buffer: %d",
               config.max_connections, config.buffer_size);

    if (config.daemon_mode) {
        if (ws_daemonize() < 0) {
            ws_log_fatal("daemonize failed");
            return 1;
        }
    }

    if (config.pid_file[0]) {
        ws_pid_write(config.pid_file);
    }

    ret = ws_server_run(&config);

    if (config.pid_file[0]) {
        ws_pid_remove(config.pid_file);
    }

    ws_log_info("websockify2 shutdown");
    ws_log_close();
    ws_platform_cleanup();

    return ret < 0 ? 1 : 0;
}
