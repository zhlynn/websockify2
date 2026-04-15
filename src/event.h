/* SPDX-License-Identifier: MIT */
#ifndef WS_EVENT_H
#define WS_EVENT_H

#include "platform.h"
#include <signal.h>

#define WS_EV_READ   0x01
#define WS_EV_WRITE  0x02
#define WS_EV_ERROR  0x04
#define WS_EV_HUP    0x08

typedef struct ws_event_loop ws_event_loop_t;

typedef void (*ws_event_cb)(ws_event_loop_t *loop, ws_socket_t fd, int events, void *data);

/* Timer callback */
typedef void (*ws_timer_cb)(ws_event_loop_t *loop, void *data);

typedef struct {
    uint64_t    fire_at;   /* absolute time in ms */
    uint64_t    interval;  /* 0 = one-shot */
    ws_timer_cb cb;
    void       *data;
    int         active;
} ws_timer_t;

#define WS_MAX_TIMERS 64

struct ws_event_loop {
    int         fd;        /* epoll/kqueue fd, -1 for poll/IOCP */
    volatile sig_atomic_t running;  /* written from signal handler */
    int         max_events;

#if defined(WS_HAVE_EPOLL)
    struct epoll_event *events;
#elif defined(WS_HAVE_KQUEUE)
    struct kevent *events;
    struct kevent *changes;
    int            nchanges;
    int            changes_cap;
#else
    /* poll fallback (opaque) */
    void          *pollfds;
    void         **polldata;
    ws_event_cb   *pollcbs;
    int            poll_count;
    int            poll_cap;
#endif

    /* Timers */
    ws_timer_t   timers[WS_MAX_TIMERS];
    int          timer_count;
};

/* Create/destroy event loop */
ws_event_loop_t *ws_event_create(int max_events);
void             ws_event_destroy(ws_event_loop_t *loop);

/* Add/modify/remove fd from event loop */
int  ws_event_add(ws_event_loop_t *loop, ws_socket_t fd, int events, ws_event_cb cb, void *data);
int  ws_event_mod(ws_event_loop_t *loop, ws_socket_t fd, int events, ws_event_cb cb, void *data);
int  ws_event_del(ws_event_loop_t *loop, ws_socket_t fd);

/* Run event loop. Returns when loop->running = 0 or error */
int  ws_event_run(ws_event_loop_t *loop, int timeout_ms);

/* Stop event loop */
void ws_event_stop(ws_event_loop_t *loop);

/* Timer management */
int  ws_timer_add(ws_event_loop_t *loop, uint64_t interval_ms, int repeat, ws_timer_cb cb, void *data);
void ws_timer_del(ws_event_loop_t *loop, int timer_id);

#endif /* WS_EVENT_H */
