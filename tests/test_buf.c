/* SPDX-License-Identifier: MIT */
#include "test_framework.h"
#include "buf.h"

/* ---- Ring buffer tests ---- */

static int test_buf_init(void) {
    ws_buf_t b;
    ASSERT_EQ(ws_buf_init(&b, 1024), 0);
    ASSERT_EQ(b.size, 1024u);
    ASSERT_EQ(b.mask, 1023u);
    ASSERT(ws_buf_empty(&b));
    ASSERT(!ws_buf_full(&b));
    ASSERT_EQ(ws_buf_readable(&b), 0u);
    ASSERT_EQ(ws_buf_writable(&b), 1024u);
    ws_buf_free(&b);
    return 0;
}

static int test_buf_init_roundup(void) {
    ws_buf_t b;
    ws_buf_init(&b, 1000);
    ASSERT_EQ(b.size, 1024u);  /* rounded up to power of 2 */
    ws_buf_free(&b);
    return 0;
}

static int test_buf_init_small(void) {
    ws_buf_t b;
    ws_buf_init(&b, 1);
    ASSERT(b.size >= 16);  /* minimum size */
    ws_buf_free(&b);
    return 0;
}

static int test_buf_write_read(void) {
    ws_buf_t b;
    ws_buf_init(&b, 256);

    const char *data = "Hello, World!";
    uint32_t n = ws_buf_write(&b, data, 13);
    ASSERT_EQ(n, 13u);
    ASSERT_EQ(ws_buf_readable(&b), 13u);

    char out[32] = {0};
    n = ws_buf_read(&b, out, sizeof(out));
    ASSERT_EQ(n, 13u);
    ASSERT(memcmp(out, data, 13) == 0);
    ASSERT(ws_buf_empty(&b));

    ws_buf_free(&b);
    return 0;
}

static int test_buf_wraparound(void) {
    ws_buf_t b;
    ws_buf_init(&b, 32);

    /* Fill most of the buffer */
    char fill[24];
    memset(fill, 'A', sizeof(fill));
    ws_buf_write(&b, fill, 24);

    /* Read some to advance tail */
    char tmp[16];
    ws_buf_read(&b, tmp, 16);

    /* Now write across the wrap boundary */
    char data[16];
    memset(data, 'B', sizeof(data));
    uint32_t n = ws_buf_write(&b, data, 16);
    ASSERT_EQ(n, 16u);

    /* Read all: should be 8 A's + 16 B's */
    char out[32];
    n = ws_buf_read(&b, out, sizeof(out));
    ASSERT_EQ(n, 24u);
    for (int i = 0; i < 8; i++) ASSERT(out[i] == 'A');
    for (int i = 8; i < 24; i++) ASSERT(out[i] == 'B');

    ws_buf_free(&b);
    return 0;
}

static int test_buf_full(void) {
    ws_buf_t b;
    ws_buf_init(&b, 16);

    char data[16];
    memset(data, 'X', sizeof(data));
    uint32_t n = ws_buf_write(&b, data, 16);
    ASSERT_EQ(n, 16u);
    ASSERT(ws_buf_full(&b));
    ASSERT_EQ(ws_buf_writable(&b), 0u);

    /* Extra write should return 0 */
    n = ws_buf_write(&b, data, 1);
    ASSERT_EQ(n, 0u);

    ws_buf_free(&b);
    return 0;
}

static int test_buf_peek(void) {
    ws_buf_t b;
    ws_buf_init(&b, 64);

    ws_buf_write(&b, "Hello", 5);

    char out[8] = {0};
    uint32_t n = ws_buf_peek(&b, out, 5);
    ASSERT_EQ(n, 5u);
    ASSERT(memcmp(out, "Hello", 5) == 0);

    /* Data should still be there */
    ASSERT_EQ(ws_buf_readable(&b), 5u);

    ws_buf_free(&b);
    return 0;
}

static int test_buf_drain(void) {
    ws_buf_t b;
    ws_buf_init(&b, 64);

    ws_buf_write(&b, "Hello World", 11);
    ws_buf_drain(&b, 6);
    ASSERT_EQ(ws_buf_readable(&b), 5u);

    char out[8] = {0};
    ws_buf_read(&b, out, 5);
    ASSERT(memcmp(out, "World", 5) == 0);

    ws_buf_free(&b);
    return 0;
}

static int test_buf_produce_consume(void) {
    ws_buf_t b;
    ws_buf_init(&b, 64);

    /* Simulate direct write */
    memcpy(b.data, "Direct", 6);
    ws_buf_produce(&b, 6);
    ASSERT_EQ(ws_buf_readable(&b), 6u);

    char out[8] = {0};
    ws_buf_peek(&b, out, 6);
    ASSERT(memcmp(out, "Direct", 6) == 0);

    ws_buf_consume(&b, 3);
    ASSERT_EQ(ws_buf_readable(&b), 3u);

    ws_buf_free(&b);
    return 0;
}

static int test_buf_reset(void) {
    ws_buf_t b;
    ws_buf_init(&b, 64);
    ws_buf_write(&b, "data", 4);
    ws_buf_reset(&b);
    ASSERT(ws_buf_empty(&b));
    ASSERT_EQ(ws_buf_readable(&b), 0u);
    ws_buf_free(&b);
    return 0;
}

static int test_buf_stress(void) {
    ws_buf_t b;
    ws_buf_init(&b, 256);

    /* Many small writes and reads */
    for (int round = 0; round < 1000; round++) {
        char w = (char)('A' + (round % 26));
        ws_buf_write(&b, &w, 1);

        if (ws_buf_readable(&b) >= 10) {
            char out[10];
            ws_buf_read(&b, out, 10);
        }
    }

    /* Drain remaining */
    ws_buf_reset(&b);
    ASSERT(ws_buf_empty(&b));
    ws_buf_free(&b);
    return 0;
}

/* ---- Dynamic buffer tests ---- */

static int test_dbuf_basic(void) {
    ws_dbuf_t b;
    ws_dbuf_init(&b);

    ASSERT_EQ(ws_dbuf_append(&b, "Hello", 5), 0);
    ASSERT_EQ(b.len, 5u);
    ASSERT(memcmp(b.data, "Hello", 5) == 0);

    ASSERT_EQ(ws_dbuf_append(&b, " World", 6), 0);
    ASSERT_EQ(b.len, 11u);
    ASSERT(memcmp(b.data, "Hello World", 11) == 0);

    ws_dbuf_free(&b);
    return 0;
}

static int test_dbuf_grow(void) {
    ws_dbuf_t b;
    ws_dbuf_init(&b);

    /* Write enough to force multiple grows */
    char data[100];
    memset(data, 'X', sizeof(data));
    for (int i = 0; i < 100; i++) {
        ASSERT_EQ(ws_dbuf_append(&b, data, sizeof(data)), 0);
    }
    ASSERT_EQ(b.len, 10000u);
    ASSERT(b.cap >= 10000u);

    ws_dbuf_free(&b);
    return 0;
}

static int test_dbuf_reset(void) {
    ws_dbuf_t b;
    ws_dbuf_init(&b);
    ws_dbuf_append(&b, "data", 4);
    ws_dbuf_reset(&b);
    ASSERT_EQ(b.len, 0u);
    ASSERT(b.cap > 0);  /* capacity preserved */
    ws_dbuf_free(&b);
    return 0;
}

void test_buf_run(void) {
    TEST_SUITE("Ring Buffer");
    TEST_RUN(test_buf_init);
    TEST_RUN(test_buf_init_roundup);
    TEST_RUN(test_buf_init_small);
    TEST_RUN(test_buf_write_read);
    TEST_RUN(test_buf_wraparound);
    TEST_RUN(test_buf_full);
    TEST_RUN(test_buf_peek);
    TEST_RUN(test_buf_drain);
    TEST_RUN(test_buf_produce_consume);
    TEST_RUN(test_buf_reset);
    TEST_RUN(test_buf_stress);

    TEST_SUITE("Dynamic Buffer");
    TEST_RUN(test_dbuf_basic);
    TEST_RUN(test_dbuf_grow);
    TEST_RUN(test_dbuf_reset);
}
