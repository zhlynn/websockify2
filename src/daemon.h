/* SPDX-License-Identifier: MIT */
#ifndef WS_DAEMON_H
#define WS_DAEMON_H

#include "platform.h"

/* Daemonize the process (fork, setsid, close fds) */
int ws_daemonize(void);

/* Write PID to file */
int ws_pid_write(const char *path);

/* Remove PID file */
void ws_pid_remove(const char *path);

/* Drop privileges to given user/group */
int ws_drop_privileges(const char *user, const char *group);

#endif /* WS_DAEMON_H */
