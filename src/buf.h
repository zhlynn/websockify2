/* SPDX-License-Identifier: MIT */
#ifndef WS_BUF_H
#define WS_BUF_H

#include "platform.h"

/*
 * Ring buffer — lock-free single-producer single-consumer.
 * Used for per-connection read/write buffering.
 * Power-of-two size for fast modulo via bitmask.
 */
typedef struct {
    uint8_t  *data;
    uint32_t  size;     /* always power of 2 */
    uint32_t  mask;     /* size - 1 */
    uint32_t  head;     /* write position */
    uint32_t  tail;     /* read position */
} ws_buf_t;

/* Initialize buffer. size will be rounded up to next power of 2 */
int  ws_buf_init(ws_buf_t *b, uint32_t size);
void ws_buf_free(ws_buf_t *b);
void ws_buf_reset(ws_buf_t *b);

/* Available bytes to read / space to write */
uint32_t ws_buf_readable(const ws_buf_t *b);
uint32_t ws_buf_writable(const ws_buf_t *b);
int      ws_buf_empty(const ws_buf_t *b);
int      ws_buf_full(const ws_buf_t *b);

/* Write data into buffer. Returns bytes written */
uint32_t ws_buf_write(ws_buf_t *b, const void *data, uint32_t len);

/* Read data from buffer. Returns bytes read */
uint32_t ws_buf_read(ws_buf_t *b, void *out, uint32_t len);

/* Peek at data without consuming. Returns bytes peeked */
uint32_t ws_buf_peek(const ws_buf_t *b, void *out, uint32_t len);

/* Discard n bytes from the read side */
uint32_t ws_buf_drain(ws_buf_t *b, uint32_t len);

#ifdef WS_PLATFORM_POSIX
#include <sys/uio.h>
/* Get pointers for direct I/O (zero-copy readv/writev).
 * Returns up to 2 iovec segments. Returns segment count (0, 1, or 2). */
int ws_buf_read_iovec(const ws_buf_t *b, struct iovec *iov, uint32_t max_len);
int ws_buf_write_iovec(const ws_buf_t *b, struct iovec *iov, uint32_t max_len);
#endif

/* Advance head after direct write into buffer */
void ws_buf_produce(ws_buf_t *b, uint32_t len);
/* Advance tail after direct read from buffer */
void ws_buf_consume(ws_buf_t *b, uint32_t len);

/*
 * Dynamic byte buffer (simple, growable, for HTTP parsing etc.)
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
