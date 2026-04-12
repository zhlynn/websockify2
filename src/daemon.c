/* SPDX-License-Identifier: MIT */
#include "daemon.h"
#include "log.h"

#ifdef WS_PLATFORM_POSIX

int ws_daemonize(void) {
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid > 0) _exit(0);  /* parent exits */

    setsid();

    /* Fork again to prevent terminal acquisition */
    pid = fork();
    if (pid < 0) return -1;
    if (pid > 0) _exit(0);

    /* Redirect stdio to /dev/null */
    int devnull = open("/dev/null", O_RDWR);
    if (devnull >= 0) {
        dup2(devnull, STDIN_FILENO);
        dup2(devnull, STDOUT_FILENO);
        dup2(devnull, STDERR_FILENO);
        if (devnull > 2) close(devnull);
    }

    /* Change working directory */
    if (chdir("/") < 0) {
        /* non-fatal */
    }

    umask(0027);
    return 0;
}

int ws_pid_write(const char *path) {
    if (!path || !path[0]) return 0;
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "%d\n", (int)getpid());
    fclose(f);
    return 0;
}

void ws_pid_remove(const char *path) {
    if (path && path[0])
        unlink(path);
}

int ws_drop_privileges(const char *user, const char *group) {
    if (!user && !group) return 0;

    if (group) {
        struct group *gr = getgrnam(group);
        if (!gr) {
            ws_log_error("group not found: %s", group);
            return -1;
        }
        if (setgid(gr->gr_gid) < 0) return -1;
    }

    if (user) {
        struct passwd *pw = getpwnam(user);
        if (!pw) {
            ws_log_error("user not found: %s", user);
            return -1;
        }
        if (setuid(pw->pw_uid) < 0) return -1;
    }

    return 0;
}

#else /* Windows */

int ws_daemonize(void) {
    ws_log_warn("daemonize not supported on Windows, use SCM services");
    return 0;
}

int ws_pid_write(const char *path) {
    if (!path || !path[0]) return 0;
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "%lu\n", (unsigned long)GetCurrentProcessId());
    fclose(f);
    return 0;
}

void ws_pid_remove(const char *path) {
    if (path && path[0])
        DeleteFileA(path);
}

int ws_drop_privileges(const char *user, const char *group) {
    (void)user; (void)group;
    return 0;
}

#endif
