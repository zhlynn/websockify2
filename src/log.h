/* SPDX-License-Identifier: MIT */
#ifndef WS_LOG_H
#define WS_LOG_H

#include "platform.h"

typedef enum {
    WS_LOG_DEBUG = 0,
    WS_LOG_INFO,
    WS_LOG_WARN,
    WS_LOG_ERROR,
    WS_LOG_FATAL
} ws_log_level_t;

/* Initialize logging. file_path=NULL for stderr, "syslog" for syslog */
int  ws_log_init(ws_log_level_t level, const char *file_path);
void ws_log_close(void);
void ws_log_set_level(ws_log_level_t level);
void ws_log_set_prefix(const char *prefix);

/* Log functions */
void ws_log(ws_log_level_t level, const char *fmt, ...) WS_PRINTF(2, 3);

/* Convenience macros */
#define ws_log_debug(...) ws_log(WS_LOG_DEBUG, __VA_ARGS__)
#define ws_log_info(...)  ws_log(WS_LOG_INFO,  __VA_ARGS__)
#define ws_log_warn(...)  ws_log(WS_LOG_WARN,  __VA_ARGS__)
#define ws_log_error(...) ws_log(WS_LOG_ERROR, __VA_ARGS__)
#define ws_log_fatal(...) ws_log(WS_LOG_FATAL, __VA_ARGS__)

#endif /* WS_LOG_H */
