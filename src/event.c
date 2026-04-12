/* SPDX-License-Identifier: MIT */
#include "event.h"
#include "log.h"

#ifndef WS_HAVE_EPOLL
#ifndef WS_HAVE_KQUEUE
  #ifdef WS_PLATFORM_WINDOWS
    /* winsock2.h already pulled in via platform.h; provides WSAPoll/WSAPOLLFD */
    typedef WSAPOLLFD     ws_pollfd_t;
    typedef unsigned long ws_nfds_t;
    #define ws_poll(fds, n, t) WSAPoll((fds), (ULONG)(n), (t))
  #else
    #include <poll.h>
    typedef struct pollfd ws_pollfd_t;
    typedef nfds_t        ws_nfds_t;
    #define ws_poll(fds, n, t) poll((fds), (n), (t))
  #endif
  #define WS_USE_POLL 1
#endif
#endif

/* ---- Timer helpers ---- */

static uint64_t timer_next_fire(ws_event_loop_t *loop) {
    uint64_t earliest = UINT64_MAX;
    for (int i = 0; i < loop->timer_count; i++) {
        if (loop->timers[i].active && loop->timers[i].fire_at < earliest)
            earliest = loop->timers[i].fire_at;
    }
    return earliest;
}

static void timer_process(ws_event_loop_t *loop) {
    uint64_t now = ws_time_ms();
    for (int i = 0; i < loop->timer_count; i++) {
        ws_timer_t *t = &loop->timers[i];
        if (!t->active || now < t->fire_at)
            continue;
        t->cb(loop, t->data);
        if (t->interval > 0) {
            t->fire_at = now + t->interval;
        } else {
            t->active = 0;
        }
    }
}

int ws_timer_add(ws_event_loop_t *loop, uint64_t interval_ms, int repeat, ws_timer_cb cb, void *data) {
    if (loop->timer_count >= WS_MAX_TIMERS) return -1;
    int id = loop->timer_count++;
    ws_timer_t *t = &loop->timers[id];
    t->fire_at = ws_time_ms() + interval_ms;
    t->interval = repeat ? interval_ms : 0;
    t->cb = cb;
    t->data = data;
    t->active = 1;
    return id;
}

void ws_timer_del(ws_event_loop_t *loop, int timer_id) {
    if (timer_id >= 0 && timer_id < loop->timer_count)
        loop->timers[timer_id].active = 0;
}

static int compute_timeout(ws_event_loop_t *loop, int max_ms) {
    uint64_t next = timer_next_fire(loop);
    if (next == UINT64_MAX) return max_ms;
    uint64_t now = ws_time_ms();
    if (next <= now) return 0;
    int diff = (int)(next - now);
    return (max_ms >= 0 && max_ms < diff) ? max_ms : diff;
}

/* ============================================================
 * EPOLL backend (Linux)
 * ============================================================ */
#if defined(WS_HAVE_EPOLL)

/* Store callback + data in a small struct, keyed by fd */
typedef struct {
    ws_event_cb cb;
    void       *data;
} ws_ev_slot_t;

/* Simple fd->slot table (allocated to max_events * 4 slots) */
static ws_ev_slot_t *g_slots = NULL;
static int g_slots_cap = 0;

static void slots_ensure(int fd) {
    if (fd < g_slots_cap) return;
    int newcap = fd + 1024;
    ws_ev_slot_t *p = (ws_ev_slot_t *)realloc(g_slots, sizeof(ws_ev_slot_t) * (size_t)newcap);
    if (!p) return;
    memset(p + g_slots_cap, 0, sizeof(ws_ev_slot_t) * (size_t)(newcap - g_slots_cap));
    g_slots = p;
    g_slots_cap = newcap;
}

ws_event_loop_t *ws_event_create(int max_events) {
    ws_event_loop_t *loop = (ws_event_loop_t *)calloc(1, sizeof(*loop));
    if (!loop) return NULL;

    loop->fd = epoll_create1(EPOLL_CLOEXEC);
    if (loop->fd < 0) { free(loop); return NULL; }

    loop->max_events = max_events;
    loop->events = (struct epoll_event *)calloc((size_t)max_events, sizeof(struct epoll_event));
    if (!loop->events) { close(loop->fd); free(loop); return NULL; }

    loop->running = 1;
    return loop;
}

void ws_event_destroy(ws_event_loop_t *loop) {
    if (!loop) return;
    close(loop->fd);
    free(loop->events);
    free(loop);
    free(g_slots);
    g_slots = NULL;
    g_slots_cap = 0;
}

static uint32_t to_epoll_events(int ev) {
    uint32_t e = EPOLLET;  /* edge-triggered */
    if (ev & WS_EV_READ)  e |= EPOLLIN;
    if (ev & WS_EV_WRITE) e |= EPOLLOUT;
    return e;
}

int ws_event_add(ws_event_loop_t *loop, ws_socket_t fd, int events, ws_event_cb cb, void *data) {
    slots_ensure(fd);
    g_slots[fd].cb = cb;
    g_slots[fd].data = data;

    struct epoll_event ev;
    ev.events = to_epoll_events(events);
    ev.data.fd = fd;
    return epoll_ctl(loop->fd, EPOLL_CTL_ADD, fd, &ev);
}

int ws_event_mod(ws_event_loop_t *loop, ws_socket_t fd, int events, ws_event_cb cb, void *data) {
    slots_ensure(fd);
    if (cb) g_slots[fd].cb = cb;
    if (data) g_slots[fd].data = data;

    struct epoll_event ev;
    ev.events = to_epoll_events(events);
    ev.data.fd = fd;
    return epoll_ctl(loop->fd, EPOLL_CTL_MOD, fd, &ev);
}

int ws_event_del(ws_event_loop_t *loop, ws_socket_t fd) {
    if (fd < g_slots_cap) {
        g_slots[fd].cb = NULL;
        g_slots[fd].data = NULL;
    }
    return epoll_ctl(loop->fd, EPOLL_CTL_DEL, fd, NULL);
}

int ws_event_run(ws_event_loop_t *loop, int timeout_ms) {
    while (loop->running) {
        int wait = compute_timeout(loop, timeout_ms);
        int n = epoll_wait(loop->fd, loop->events, loop->max_events, wait);

        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }

        for (int i = 0; i < n; i++) {
            int fd = loop->events[i].data.fd;
            uint32_t ev = loop->events[i].events;
            int flags = 0;
            if (ev & EPOLLIN)  flags |= WS_EV_READ;
            if (ev & EPOLLOUT) flags |= WS_EV_WRITE;
            if (ev & EPOLLERR) flags |= WS_EV_ERROR;
            if (ev & EPOLLHUP) flags |= WS_EV_HUP;

            if (fd < g_slots_cap && g_slots[fd].cb)
                g_slots[fd].cb(loop, fd, flags, g_slots[fd].data);
        }

        timer_process(loop);
    }
    return 0;
}

/* ============================================================
 * KQUEUE backend (macOS, FreeBSD)
 * ============================================================ */
#elif defined(WS_HAVE_KQUEUE)

typedef struct {
    ws_event_cb cb;
    void       *data;
} ws_kq_slot_t;

static ws_kq_slot_t *g_kq_slots = NULL;
static int g_kq_slots_cap = 0;

static void kq_slots_ensure(int fd) {
    if (fd < g_kq_slots_cap) return;
    int newcap = fd + 1024;
    ws_kq_slot_t *p = (ws_kq_slot_t *)realloc(g_kq_slots, sizeof(ws_kq_slot_t) * (size_t)newcap);
    if (!p) return;
    memset(p + g_kq_slots_cap, 0, sizeof(ws_kq_slot_t) * (size_t)(newcap - g_kq_slots_cap));
    g_kq_slots = p;
    g_kq_slots_cap = newcap;
}

ws_event_loop_t *ws_event_create(int max_events) {
    ws_event_loop_t *loop = (ws_event_loop_t *)calloc(1, sizeof(*loop));
    if (!loop) return NULL;

    loop->fd = kqueue();
    if (loop->fd < 0) { free(loop); return NULL; }
    ws_set_cloexec(loop->fd);

    loop->max_events = max_events;
    loop->events = (struct kevent *)calloc((size_t)max_events, sizeof(struct kevent));
    loop->changes_cap = 256;
    loop->changes = (struct kevent *)calloc((size_t)loop->changes_cap, sizeof(struct kevent));
    loop->nchanges = 0;

    if (!loop->events || !loop->changes) {
        close(loop->fd);
        free(loop->events);
        free(loop->changes);
        free(loop);
        return NULL;
    }

    loop->running = 1;
    return loop;
}

void ws_event_destroy(ws_event_loop_t *loop) {
    if (!loop) return;
    close(loop->fd);
    free(loop->events);
    free(loop->changes);
    free(loop);
    free(g_kq_slots);
    g_kq_slots = NULL;
    g_kq_slots_cap = 0;
}

static void kq_add_change(ws_event_loop_t *loop, uintptr_t ident, int16_t filter,
                           uint16_t flags, void *udata) {
    if (loop->nchanges >= loop->changes_cap) {
        int newcap = loop->changes_cap * 2;
        struct kevent *p = (struct kevent *)realloc(loop->changes,
                                                     sizeof(struct kevent) * (size_t)newcap);
        if (!p) return;
        loop->changes = p;
        loop->changes_cap = newcap;
    }
    struct kevent *ke = &loop->changes[loop->nchanges++];
    EV_SET(ke, ident, filter, flags, 0, 0, udata);
}

int ws_event_add(ws_event_loop_t *loop, ws_socket_t fd, int events, ws_event_cb cb, void *data) {
    kq_slots_ensure(fd);
    g_kq_slots[fd].cb = cb;
    g_kq_slots[fd].data = data;

    if (events & WS_EV_READ)
        kq_add_change(loop, (uintptr_t)fd, EVFILT_READ, EV_ADD | EV_CLEAR, NULL);
    if (events & WS_EV_WRITE)
        kq_add_change(loop, (uintptr_t)fd, EVFILT_WRITE, EV_ADD | EV_CLEAR, NULL);
    return 0;
}

int ws_event_mod(ws_event_loop_t *loop, ws_socket_t fd, int events, ws_event_cb cb, void *data) {
    kq_slots_ensure(fd);
    if (cb) g_kq_slots[fd].cb = cb;
    if (data) g_kq_slots[fd].data = data;

    /* Remove and re-add */
    kq_add_change(loop, (uintptr_t)fd, EVFILT_READ,
                  (events & WS_EV_READ) ? (EV_ADD | EV_CLEAR) : EV_DELETE, NULL);
    kq_add_change(loop, (uintptr_t)fd, EVFILT_WRITE,
                  (events & WS_EV_WRITE) ? (EV_ADD | EV_CLEAR) : EV_DELETE, NULL);
    return 0;
}

int ws_event_del(ws_event_loop_t *loop, ws_socket_t fd) {
    kq_add_change(loop, (uintptr_t)fd, EVFILT_READ, EV_DELETE, NULL);
    kq_add_change(loop, (uintptr_t)fd, EVFILT_WRITE, EV_DELETE, NULL);
    if (fd < g_kq_slots_cap) {
        g_kq_slots[fd].cb = NULL;
        g_kq_slots[fd].data = NULL;
    }
    return 0;
}

int ws_event_run(ws_event_loop_t *loop, int timeout_ms) {
    while (loop->running) {
        int wait = compute_timeout(loop, timeout_ms);
        struct timespec ts, *tsp = NULL;
        if (wait >= 0) {
            ts.tv_sec = wait / 1000;
            ts.tv_nsec = (wait % 1000) * 1000000L;
            tsp = &ts;
        }

        int n = kevent(loop->fd, loop->changes, loop->nchanges,
                       loop->events, loop->max_events, tsp);
        loop->nchanges = 0;

        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }

        for (int i = 0; i < n; i++) {
            int fd = (int)loop->events[i].ident;

            /* EV_ERROR here reports failure of a change-list entry, NOT a
             * socket error. Common benign case: EV_DELETE on a filter that
             * was never added (ENOENT). Skip these. */
            if (loop->events[i].flags & EV_ERROR) {
                continue;
            }

            int flags = 0;
            if (loop->events[i].filter == EVFILT_READ)  flags |= WS_EV_READ;
            if (loop->events[i].filter == EVFILT_WRITE) flags |= WS_EV_WRITE;
            if (loop->events[i].flags & EV_EOF)         flags |= WS_EV_HUP;

            if (fd < g_kq_slots_cap && g_kq_slots[fd].cb)
                g_kq_slots[fd].cb(loop, fd, flags, g_kq_slots[fd].data);
        }

        timer_process(loop);
    }
    return 0;
}

/* ============================================================
 * POLL fallback (portable)
 * ============================================================ */
#elif defined(WS_USE_POLL)

#define PFDS(loop) ((ws_pollfd_t *)(loop)->pollfds)

ws_event_loop_t *ws_event_create(int max_events) {
    ws_event_loop_t *loop = (ws_event_loop_t *)calloc(1, sizeof(*loop));
    if (!loop) return NULL;

    loop->fd = -1;
    loop->max_events = max_events;
    loop->poll_cap = max_events;
    loop->pollfds = calloc((size_t)max_events, sizeof(ws_pollfd_t));
    loop->polldata = (void **)calloc((size_t)max_events, sizeof(void *));
    loop->pollcbs = (ws_event_cb *)calloc((size_t)max_events, sizeof(ws_event_cb));
    loop->poll_count = 0;

    if (!loop->pollfds || !loop->polldata || !loop->pollcbs) {
        free(loop->pollfds); free(loop->polldata); free(loop->pollcbs);
        free(loop);
        return NULL;
    }
    loop->running = 1;
    return loop;
}

void ws_event_destroy(ws_event_loop_t *loop) {
    if (!loop) return;
    free(loop->pollfds);
    free(loop->polldata);
    free(loop->pollcbs);
    free(loop);
}

static int poll_find(ws_event_loop_t *loop, ws_socket_t fd) {
    ws_pollfd_t *pfds = PFDS(loop);
    for (int i = 0; i < loop->poll_count; i++)
        if (pfds[i].fd == fd) return i;
    return -1;
}

int ws_event_add(ws_event_loop_t *loop, ws_socket_t fd, int events, ws_event_cb cb, void *data) {
    if (loop->poll_count >= loop->poll_cap) {
        int newcap = loop->poll_cap * 2;
        loop->pollfds = realloc(loop->pollfds, sizeof(ws_pollfd_t) * (size_t)newcap);
        loop->polldata = (void **)realloc(loop->polldata, sizeof(void *) * (size_t)newcap);
        loop->pollcbs = (ws_event_cb *)realloc(loop->pollcbs, sizeof(ws_event_cb) * (size_t)newcap);
        loop->poll_cap = newcap;
    }
    ws_pollfd_t *pfds = PFDS(loop);
    int idx = loop->poll_count++;
    pfds[idx].fd = fd;
    pfds[idx].events = 0;
    if (events & WS_EV_READ)  pfds[idx].events |= POLLIN;
    if (events & WS_EV_WRITE) pfds[idx].events |= POLLOUT;
    pfds[idx].revents = 0;
    loop->polldata[idx] = data;
    loop->pollcbs[idx] = cb;
    return 0;
}

int ws_event_mod(ws_event_loop_t *loop, ws_socket_t fd, int events, ws_event_cb cb, void *data) {
    int idx = poll_find(loop, fd);
    if (idx < 0) return -1;
    ws_pollfd_t *pfds = PFDS(loop);
    pfds[idx].events = 0;
    if (events & WS_EV_READ)  pfds[idx].events |= POLLIN;
    if (events & WS_EV_WRITE) pfds[idx].events |= POLLOUT;
    if (cb)   loop->pollcbs[idx] = cb;
    if (data) loop->polldata[idx] = data;
    return 0;
}

int ws_event_del(ws_event_loop_t *loop, ws_socket_t fd) {
    int idx = poll_find(loop, fd);
    if (idx < 0) return -1;
    ws_pollfd_t *pfds = PFDS(loop);
    loop->poll_count--;
    if (idx < loop->poll_count) {
        pfds[idx] = pfds[loop->poll_count];
        loop->polldata[idx] = loop->polldata[loop->poll_count];
        loop->pollcbs[idx] = loop->pollcbs[loop->poll_count];
    }
    return 0;
}

int ws_event_run(ws_event_loop_t *loop, int timeout_ms) {
    while (loop->running) {
        int wait = compute_timeout(loop, timeout_ms);
        ws_pollfd_t *pfds = PFDS(loop);
        int n = ws_poll(pfds, (ws_nfds_t)loop->poll_count, wait);
        if (n < 0) {
            if (ws_errno() == WS_EINTR) continue;
            return -1;
        }

        for (int i = 0; i < loop->poll_count && n > 0; i++) {
            short rev = pfds[i].revents;
            if (!rev) continue;
            n--;

            int flags = 0;
            if (rev & POLLIN)   flags |= WS_EV_READ;
            if (rev & POLLOUT)  flags |= WS_EV_WRITE;
            if (rev & POLLERR)  flags |= WS_EV_ERROR;
            if (rev & POLLHUP)  flags |= WS_EV_HUP;

            loop->pollcbs[i](loop, pfds[i].fd, flags, loop->polldata[i]);
        }

        timer_process(loop);
    }
    return 0;
}

#endif /* backend selection */

void ws_event_stop(ws_event_loop_t *loop) {
    loop->running = 0;
}
