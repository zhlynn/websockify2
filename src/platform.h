/* SPDX-License-Identifier: MIT */
#ifndef WS_PLATFORM_H
#define WS_PLATFORM_H

/*
 * Platform abstraction layer
 * Normalizes differences between Linux, macOS, FreeBSD, and Windows
 */

#ifdef _WIN32
  #define WS_PLATFORM_WINDOWS 1
  #ifndef _WIN32_WINNT
    #define _WIN32_WINNT 0x0602  /* Windows 8+ for IOCP improvements */
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <windows.h>
  #include <io.h>
  #include <process.h>
  #pragma comment(lib, "ws2_32.lib")

  typedef SOCKET ws_socket_t;
  typedef HANDLE ws_pid_t;
  typedef DWORD  ws_err_t;
  #define WS_INVALID_SOCKET INVALID_SOCKET
  #define WS_SOCKET_ERROR   SOCKET_ERROR

  #define ws_close_socket(s) closesocket(s)
  #define ws_errno()         WSAGetLastError()
  #define WS_EAGAIN          WSAEWOULDBLOCK
  #define WS_EINTR           WSAEINTR
  #define WS_EINPROGRESS     WSAEWOULDBLOCK
  #define WS_ECONNRESET      WSAECONNRESET

  #define WS_PATH_SEP '\\'
  #define WS_PATH_SEP_STR "\\"

  /* ssize_t not defined on Windows */
  #include <basetsd.h>
  typedef SSIZE_T ssize_t;

#else
  /* POSIX (Linux, macOS, FreeBSD) */
  #define WS_PLATFORM_POSIX 1
  #include <unistd.h>
  #include <sys/types.h>
  #include <sys/socket.h>
  #include <sys/un.h>
  #include <netinet/in.h>
  #include <netinet/tcp.h>
  #include <arpa/inet.h>
  #include <netdb.h>
  #include <signal.h>
  #include <sys/wait.h>
  #include <sys/stat.h>
  #include <fcntl.h>
  #include <errno.h>
  #include <pwd.h>
  #include <grp.h>

  typedef int     ws_socket_t;
  typedef pid_t   ws_pid_t;
  typedef int     ws_err_t;
  #define WS_INVALID_SOCKET (-1)
  #define WS_SOCKET_ERROR   (-1)

  #define ws_close_socket(s) close(s)
  #define ws_errno()         errno
  #define WS_EAGAIN          EAGAIN
  #define WS_EINTR           EINTR
  #define WS_EINPROGRESS     EINPROGRESS
  #define WS_ECONNRESET      ECONNRESET

  #define WS_PATH_SEP '/'
  #define WS_PATH_SEP_STR "/"
#endif

/* Platform detection for event backends */
#if defined(__linux__)
  #define WS_HAVE_EPOLL 1
  #include <sys/epoll.h>
#endif

#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
  #define WS_HAVE_KQUEUE 1
  #include <sys/event.h>
#endif

/* Windows event backend: uses poll/WSAPoll for now (IOCP not yet implemented) */

/* sendfile variants */
#if defined(__linux__)
  #define WS_HAVE_SENDFILE_LINUX 1
  #include <sys/sendfile.h>
#elif defined(__APPLE__)
  #define WS_HAVE_SENDFILE_DARWIN 1
#elif defined(__FreeBSD__)
  #define WS_HAVE_SENDFILE_FREEBSD 1
#endif

/* SO_REUSEPORT */
#if defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__)
  #define WS_HAVE_REUSEPORT 1
#endif

/* Common includes */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <time.h>
#include <limits.h>

/* Compiler hints */
#ifdef __GNUC__
  #define WS_LIKELY(x)   __builtin_expect(!!(x), 1)
  #define WS_UNLIKELY(x) __builtin_expect(!!(x), 0)
  #define WS_UNUSED       __attribute__((unused))
  #define WS_PRINTF(f,a)  __attribute__((format(printf, f, a)))
#else
  #define WS_LIKELY(x)   (x)
  #define WS_UNLIKELY(x) (x)
  #define WS_UNUSED
  #define WS_PRINTF(f,a)
#endif

/* Min/Max */
#define WS_MIN(a, b) ((a) < (b) ? (a) : (b))
#define WS_MAX(a, b) ((a) > (b) ? (a) : (b))

/* Initialize platform (Winsock, etc.) */
int  ws_platform_init(void);
void ws_platform_cleanup(void);

/* Get number of CPU cores */
int ws_cpu_count(void);

/* Get monotonic time in milliseconds */
uint64_t ws_time_ms(void);

/* Set socket non-blocking */
int ws_set_nonblocking(ws_socket_t fd);

/* Set close-on-exec */
int ws_set_cloexec(ws_socket_t fd);

#endif /* WS_PLATFORM_H */
