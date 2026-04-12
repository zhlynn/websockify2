/* SPDX-License-Identifier: MIT */
#include "log.h"
#include <time.h>

#ifndef WS_PLATFORM_WINDOWS
#include <syslog.h>
#endif

static ws_log_level_t g_level = WS_LOG_INFO;
static FILE          *g_fp    = NULL;
static int            g_use_syslog = 0;
static char           g_prefix[64] = "";

static const char *level_names[] = {
    "DEBUG", "INFO", "WARN", "ERROR", "FATAL"
};

int ws_log_init(ws_log_level_t level, const char *file_path) {
    g_level = level;
    g_use_syslog = 0;

    if (file_path == NULL) {
        g_fp = stderr;
    } else if (strcmp(file_path, "syslog") == 0) {
#ifndef WS_PLATFORM_WINDOWS
        openlog("websockify", LOG_PID | LOG_NDELAY, LOG_DAEMON);
        g_use_syslog = 1;
#else
        g_fp = stderr;
#endif
    } else {
        g_fp = fopen(file_path, "a");
        if (!g_fp) {
            g_fp = stderr;
            return -1;
        }
        /* Line-buffered for log files */
        setvbuf(g_fp, NULL, _IOLBF, 0);
    }
    return 0;
}

void ws_log_close(void) {
    if (g_use_syslog) {
#ifndef WS_PLATFORM_WINDOWS
        closelog();
#endif
        g_use_syslog = 0;
    }
    if (g_fp && g_fp != stderr && g_fp != stdout) {
        fclose(g_fp);
    }
    g_fp = NULL;
}

void ws_log_set_level(ws_log_level_t level) {
    g_level = level;
}

void ws_log_set_prefix(const char *prefix) {
    if (prefix) {
        snprintf(g_prefix, sizeof(g_prefix), "%s", prefix);
    } else {
        g_prefix[0] = '\0';
    }
}

void ws_log(ws_log_level_t level, const char *fmt, ...) {
    if (level < g_level)
        return;

    va_list ap;
    va_start(ap, fmt);

#ifndef WS_PLATFORM_WINDOWS
    if (g_use_syslog) {
        static const int syslog_levels[] = {
            LOG_DEBUG, LOG_INFO, LOG_WARNING, LOG_ERR, LOG_CRIT
        };
        char buf[2048];
        vsnprintf(buf, sizeof(buf), fmt, ap);
        syslog(syslog_levels[level], "%s%s", g_prefix, buf);
        va_end(ap);
        return;
    }
#endif

    if (!g_fp) g_fp = stderr;

    /* Timestamp */
    time_t now = time(NULL);
    struct tm tm_buf;
#ifdef WS_PLATFORM_WINDOWS
    localtime_s(&tm_buf, &now);
#else
    localtime_r(&now, &tm_buf);
#endif
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm_buf);

    fprintf(g_fp, "%s [%s] %s", ts, level_names[level], g_prefix);
    vfprintf(g_fp, fmt, ap);
    fprintf(g_fp, "\n");

    if (level >= WS_LOG_ERROR)
        fflush(g_fp);

    va_end(ap);
}
