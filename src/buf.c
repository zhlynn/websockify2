/* SPDX-License-Identifier: MIT */
#include "buf.h"

#ifndef WS_PLATFORM_WINDOWS
#include <sys/uio.h>
#endif

static uint32_t next_pow2(uint32_t v) {
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v++;
    return v < 16 ? 16 : v;
}

/* ---- Ring buffer ---- */

int ws_buf_init(ws_buf_t *b, uint32_t size) {
    size = next_pow2(size);
    b->data = (uint8_t *)malloc(size);
    if (!b->data) return -1;
    b->size = size;
    b->mask = size - 1;
    b->head = 0;
    b->tail = 0;
    return 0;
}

void ws_buf_free(ws_buf_t *b) {
    free(b->data);
    b->data = NULL;
    b->size = b->mask = b->head = b->tail = 0;
}

void ws_buf_reset(ws_buf_t *b) {
    b->head = 0;
    b->tail = 0;
}

uint32_t ws_buf_readable(const ws_buf_t *b) {
    return b->head - b->tail;
}

uint32_t ws_buf_writable(const ws_buf_t *b) {
    return b->size - (b->head - b->tail);
}

int ws_buf_empty(const ws_buf_t *b) {
    return b->head == b->tail;
}

int ws_buf_full(const ws_buf_t *b) {
    return (b->head - b->tail) == b->size;
}

uint32_t ws_buf_write(ws_buf_t *b, const void *data, uint32_t len) {
    uint32_t avail = ws_buf_writable(b);
    if (len > avail) len = avail;
    if (len == 0) return 0;

    uint32_t pos = b->head & b->mask;
    uint32_t first = WS_MIN(len, b->size - pos);
    memcpy(b->data + pos, data, first);
    if (len > first)
        memcpy(b->data, (const uint8_t *)data + first, len - first);
    b->head += len;
    return len;
}

uint32_t ws_buf_read(ws_buf_t *b, void *out, uint32_t len) {
    uint32_t avail = ws_buf_readable(b);
    if (len > avail) len = avail;
    if (len == 0) return 0;

    uint32_t pos = b->tail & b->mask;
    uint32_t first = WS_MIN(len, b->size - pos);
    memcpy(out, b->data + pos, first);
    if (len > first)
        memcpy((uint8_t *)out + first, b->data, len - first);
    b->tail += len;
    return len;
}

uint32_t ws_buf_peek(const ws_buf_t *b, void *out, uint32_t len) {
    uint32_t avail = ws_buf_readable(b);
    if (len > avail) len = avail;
    if (len == 0) return 0;

    uint32_t pos = b->tail & b->mask;
    uint32_t first = WS_MIN(len, b->size - pos);
    memcpy(out, b->data + pos, first);
    if (len > first)
        memcpy((uint8_t *)out + first, b->data, len - first);
    return len;
}

uint32_t ws_buf_drain(ws_buf_t *b, uint32_t len) {
    uint32_t avail = ws_buf_readable(b);
    if (len > avail) len = avail;
    b->tail += len;
    return len;
}

#ifndef WS_PLATFORM_WINDOWS
int ws_buf_read_iovec(const ws_buf_t *b, struct iovec *iov, uint32_t max_len) {
    uint32_t avail = ws_buf_readable(b);
    if (avail == 0) return 0;
    if (max_len > avail) max_len = avail;

    uint32_t pos = b->tail & b->mask;
    uint32_t first = WS_MIN(max_len, b->size - pos);

    iov[0].iov_base = b->data + pos;
    iov[0].iov_len  = first;

    if (max_len > first) {
        iov[1].iov_base = b->data;
        iov[1].iov_len  = max_len - first;
        return 2;
    }
    return 1;
}

int ws_buf_write_iovec(const ws_buf_t *b, struct iovec *iov, uint32_t max_len) {
    uint32_t avail = ws_buf_writable(b);
    if (avail == 0) return 0;
    if (max_len > avail) max_len = avail;

    uint32_t pos = b->head & b->mask;
    uint32_t first = WS_MIN(max_len, b->size - pos);

    iov[0].iov_base = b->data + pos;
    iov[0].iov_len  = first;

    if (max_len > first) {
        iov[1].iov_base = b->data;
        iov[1].iov_len  = max_len - first;
        return 2;
    }
    return 1;
}
#endif

void ws_buf_produce(ws_buf_t *b, uint32_t len) {
    b->head += len;
}

void ws_buf_consume(ws_buf_t *b, uint32_t len) {
    b->tail += len;
}

/* ---- Dynamic buffer ---- */

void ws_dbuf_init(ws_dbuf_t *b) {
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

void ws_dbuf_free(ws_dbuf_t *b) {
    free(b->data);
    b->data = NULL;
    b->len = b->cap = 0;
}

/* Cap dynamic buffers at 16 MiB — way beyond any legitimate HTTP header. */
#define WS_DBUF_MAX_CAP (16u * 1024u * 1024u)

int ws_dbuf_reserve(ws_dbuf_t *b, uint32_t additional) {
    /* Guard against overflow */
    if (additional > WS_DBUF_MAX_CAP || b->len > WS_DBUF_MAX_CAP - additional)
        return -1;
    uint32_t need = b->len + additional;
    if (need <= b->cap) return 0;

    /* Grow by 1.5x (less wasteful than 2x for many small appends) */
    uint32_t newcap = b->cap ? b->cap : 256;
    while (newcap < need) {
        uint32_t grown = newcap + (newcap >> 1);
        if (grown < newcap || grown > WS_DBUF_MAX_CAP) {
            newcap = WS_DBUF_MAX_CAP;
            break;
        }
        newcap = grown;
    }
    if (newcap < need) return -1;

    uint8_t *p = (uint8_t *)realloc(b->data, newcap);
    if (!p) return -1;
    b->data = p;
    b->cap = newcap;
    return 0;
}

int ws_dbuf_append(ws_dbuf_t *b, const void *data, uint32_t len) {
    if (ws_dbuf_reserve(b, len) < 0) return -1;
    memcpy(b->data + b->len, data, len);
    b->len += len;
    return 0;
}

void ws_dbuf_reset(ws_dbuf_t *b) {
    b->len = 0;
}
