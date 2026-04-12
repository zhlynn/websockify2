/* SPDX-License-Identifier: MIT */
#include "test_framework.h"
#include "buf.h"

/* ---- FIFO buffer tests ---- */

static int test_buf_init(void) {
    ws_buf_t b;
    ws_buf_init(&b);
    ASSERT(ws_buf_empty(&b));
    ASSERT_EQ(ws_buf_readable(&b), 0u);
    ws_buf_free(&b);
    return 0;
}

static int test_buf_write_read(void) {
    ws_buf_t b;
    ws_buf_init(&b);

    const char *data = "Hello, World!";
    ASSERT_EQ(ws_buf_write(&b, data, 13), 0);
    ASSERT_EQ(ws_buf_readable(&b), 13u);

    char out[32] = {0};
    uint32_t n = ws_buf_read(&b, out, sizeof(out));
    ASSERT_EQ(n, 13u);
    ASSERT(memcmp(out, data, 13) == 0);
    ASSERT(ws_buf_empty(&b));

    ws_buf_free(&b);
    return 0;
}

static int test_buf_append_drain_cycle(void) {
    ws_buf_t b;
    ws_buf_init(&b);

    /* Simulate many push/pop rounds — capacity should stay bounded
     * because drain-to-empty resets len/off to 0. */
    for (int i = 0; i < 1000; i++) {
        char data[32];
        memset(data, 'A' + (i % 26), sizeof(data));
        ASSERT_EQ(ws_buf_write(&b, data, sizeof(data)), 0);
        char out[32];
        uint32_t n = ws_buf_read(&b, out, sizeof(out));
        ASSERT_EQ(n, 32u);
    }
    ASSERT(ws_buf_empty(&b));
    ASSERT(b.cap <= 8192u);  /* no runaway growth */
    ws_buf_free(&b);
    return 0;
}

static int test_buf_partial_drain_then_compact(void) {
    ws_buf_t b;
    ws_buf_init(&b);

    char big[4096];
    memset(big, 'X', sizeof(big));
    ws_buf_write(&b, big, sizeof(big));
    ws_buf_drain(&b, 3000);
    ASSERT_EQ(ws_buf_readable(&b), 1096u);

    /* Next append should compact rather than grow unbounded */
    ws_buf_write(&b, big, sizeof(big));
    ASSERT_EQ(ws_buf_readable(&b), 5192u);

    ws_buf_free(&b);
    return 0;
}

static int test_buf_peek(void) {
    ws_buf_t b;
    ws_buf_init(&b);
    ws_buf_write(&b, "Hello", 5);

    char out[8] = {0};
    ASSERT_EQ(ws_buf_peek(&b, out, 5), 5u);
    ASSERT(memcmp(out, "Hello", 5) == 0);
    ASSERT_EQ(ws_buf_readable(&b), 5u);

    ws_buf_free(&b);
    return 0;
}

static int test_buf_drain(void) {
    ws_buf_t b;
    ws_buf_init(&b);
    ws_buf_write(&b, "Hello World", 11);
    ws_buf_drain(&b, 6);
    ASSERT_EQ(ws_buf_readable(&b), 5u);

    char out[8] = {0};
    ws_buf_read(&b, out, 5);
    ASSERT(memcmp(out, "World", 5) == 0);

    ws_buf_free(&b);
    return 0;
}

static int test_buf_reset(void) {
    ws_buf_t b;
    ws_buf_init(&b);
    ws_buf_write(&b, "data", 4);
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
    ASSERT(b.cap > 0);
    ws_dbuf_free(&b);
    return 0;
}

void test_buf_run(void) {
    TEST_SUITE("FIFO Buffer");
    TEST_RUN(test_buf_init);
    TEST_RUN(test_buf_write_read);
    TEST_RUN(test_buf_append_drain_cycle);
    TEST_RUN(test_buf_partial_drain_then_compact);
    TEST_RUN(test_buf_peek);
    TEST_RUN(test_buf_drain);
    TEST_RUN(test_buf_reset);

    TEST_SUITE("Dynamic Buffer");
    TEST_RUN(test_dbuf_basic);
    TEST_RUN(test_dbuf_grow);
    TEST_RUN(test_dbuf_reset);
}
