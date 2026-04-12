/* SPDX-License-Identifier: MIT */
#include "platform.h"

#if defined(__APPLE__) || defined(__FreeBSD__)
#include <sys/sysctl.h>
#endif

#if defined(__APPLE__)
#include <mach/mach.h>
#include <mach/mach_time.h>
#endif

int ws_platform_init(void) {
#ifdef WS_PLATFORM_WINDOWS
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
        return -1;
#else
    /* Ignore SIGPIPE globally */
    signal(SIGPIPE, SIG_IGN);
#endif
    return 0;
}

void ws_platform_cleanup(void) {
#ifdef WS_PLATFORM_WINDOWS
    WSACleanup();
#endif
}

int ws_cpu_count(void) {
#ifdef WS_PLATFORM_WINDOWS
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return (int)si.dwNumberOfProcessors;
#elif defined(__APPLE__) || defined(__FreeBSD__)
    int n = 0;
    size_t len = sizeof(n);
    if (sysctlbyname("hw.ncpu", &n, &len, NULL, 0) == 0 && n > 0)
        return n;
    return 1;
#else
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (int)n : 1;
#endif
}

uint64_t ws_time_ms(void) {
#ifdef WS_PLATFORM_WINDOWS
    return (uint64_t)GetTickCount64();
#elif defined(__APPLE__)
    static mach_timebase_info_data_t tb;
    if (tb.denom == 0)
        mach_timebase_info(&tb);
    uint64_t ns = mach_absolute_time() * tb.numer / tb.denom;
    return ns / 1000000ULL;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
#endif
}

int ws_set_nonblocking(ws_socket_t fd) {
#ifdef WS_PLATFORM_WINDOWS
    u_long mode = 1;
    return ioctlsocket(fd, FIONBIO, &mode) == 0 ? 0 : -1;
#else
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#endif
}

int ws_set_cloexec(ws_socket_t fd) {
#ifdef WS_PLATFORM_POSIX
    int flags = fcntl(fd, F_GETFD, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
#else
    (void)fd;
    return 0;
#endif
}
