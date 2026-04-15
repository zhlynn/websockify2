/* SPDX-License-Identifier: MIT */
#include "buf.h"

/* ---- FIFO buffer ---- */

void ws_buf_init(ws_buf_t *b) {
    b->data = NULL;
    b->len = b->off = b->cap = 0;
}

void ws_buf_free(ws_buf_t *b) {
    free(b->data);
    b->data = NULL;
    b->len = b->off = b->cap = 0;
}

void ws_buf_reset(ws_buf_t *b) {
    b->len = b->off = 0;
}

uint32_t ws_buf_readable(const ws_buf_t *b) {
    return b->len - b->off;
}

int ws_buf_empty(const ws_buf_t *b) {
    return b->len == b->off;
}

int ws_buf_reserve(ws_buf_t *b, uint32_t additional) {
    /* Ensure at least `additional` bytes fit in the writable tail.
     * Compact first (memmove live data to offset 0) when doing so is enough
     * to avoid a realloc — keeps steady-state cap flat under FIFO traffic. */
    uint32_t tail_free = b->cap - b->len;
    if (additional <= tail_free) return 0;

    if (b->off > 0) {
        uint32_t live = b->len - b->off;
        if (live > 0) memmove(b->data, b->data + b->off, live);
        b->len = live;
        b->off = 0;
        tail_free = b->cap - b->len;
        if (additional <= tail_free) return 0;
    }

    uint32_t need = b->len + additional;
    if (need < b->len) return -1;  /* overflow */
    uint32_t newcap = b->cap ? b->cap : 4096;
    while (newcap < need) {
        uint32_t grown = newcap + (newcap >> 1);
        if (grown < newcap) return -1;
        newcap = grown;
    }
    uint8_t *p = (uint8_t *)realloc(b->data, newcap);
    if (!p) return -1;
    b->data = p;
    b->cap = newcap;
    return 0;
}

int ws_buf_write(ws_buf_t *b, const void *data, uint32_t len) {
    if (len == 0) return 0;
    if (ws_buf_reserve(b, len) < 0) return -1;
    memcpy(b->data + b->len, data, len);
    b->len += len;
    return 0;
}

uint32_t ws_buf_peek(const ws_buf_t *b, void *out, uint32_t len) {
    uint32_t avail = b->len - b->off;
    if (len > avail) len = avail;
    if (len == 0) return 0;
    memcpy(out, b->data + b->off, len);
    return len;
}

uint32_t ws_buf_drain(ws_buf_t *b, uint32_t len) {
    uint32_t avail = b->len - b->off;
    if (len > avail) len = avail;
    b->off += len;
    if (b->off == b->len) {
        /* fully drained — reset cursors so cap stays reusable */
        b->off = b->len = 0;
    }
    return len;
}

uint32_t ws_buf_read(ws_buf_t *b, void *out, uint32_t len) {
    uint32_t n = ws_buf_peek(b, out, len);
    ws_buf_drain(b, n);
    return n;
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
