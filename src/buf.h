/* SPDX-License-Identifier: MIT */
#ifndef WS_BUF_H
#define WS_BUF_H

#include "platform.h"

/*
 * FIFO byte buffer — grows on demand, compacts on drain.
 * Used for per-connection read/write queues. No fixed capacity,
 * no reservation semantics; writes only fail on realloc OOM.
 */
typedef struct {
    uint8_t  *data;
    uint32_t  len;   /* bytes appended so far (always >= off) */
    uint32_t  off;   /* read cursor */
    uint32_t  cap;
} ws_buf_t;

void     ws_buf_init(ws_buf_t *b);
void     ws_buf_free(ws_buf_t *b);
void     ws_buf_reset(ws_buf_t *b);

uint32_t ws_buf_readable(const ws_buf_t *b);
int      ws_buf_empty(const ws_buf_t *b);

/* Append bytes. Returns 0 on success, -1 on allocation failure. */
int      ws_buf_write(ws_buf_t *b, const void *data, uint32_t len);

/* Ensure at least `additional` bytes of writable tail space; compacts the
 * buffer first if doing so avoids a grow. Returns 0 on success, -1 on OOM. */
int      ws_buf_reserve(ws_buf_t *b, uint32_t additional);

/* Expose the writable tail region for zero-copy recv() / read() target. */
static inline void ws_buf_tail_ptr(ws_buf_t *b, uint8_t **ptr, uint32_t *avail_cap) {
    *ptr = b->data + b->len;
    *avail_cap = b->cap - b->len;
}

/* Commit N bytes previously written through the tail pointer. */
static inline void ws_buf_commit(ws_buf_t *b, uint32_t n) { b->len += n; }

/* Copy up to len bytes into out, advancing the read cursor. */
uint32_t ws_buf_read(ws_buf_t *b, void *out, uint32_t len);

/* Copy up to len bytes into out without consuming. */
uint32_t ws_buf_peek(const ws_buf_t *b, void *out, uint32_t len);

/* Zero-copy peek: expose the contiguous readable region directly. */
static inline void ws_buf_peek_ptr(const ws_buf_t *b, const uint8_t **ptr, uint32_t *len) {
    *ptr = b->data + b->off;
    *len = b->len - b->off;
}

/* Advance read cursor by n bytes (capped at readable). */
uint32_t ws_buf_drain(ws_buf_t *b, uint32_t len);

/*
 * Dynamic byte buffer — append-only, used for HTTP parsing etc.
 */
typedef struct {
    uint8_t  *data;
    uint32_t  len;
    uint32_t  cap;
} ws_dbuf_t;

void     ws_dbuf_init(ws_dbuf_t *b);
void     ws_dbuf_free(ws_dbuf_t *b);
int      ws_dbuf_reserve(ws_dbuf_t *b, uint32_t additional);
int      ws_dbuf_append(ws_dbuf_t *b, const void *data, uint32_t len);
void     ws_dbuf_reset(ws_dbuf_t *b);

#endif /* WS_BUF_H */
