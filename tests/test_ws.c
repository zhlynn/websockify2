/* SPDX-License-Identifier: MIT */
#include "test_framework.h"
#include "ws.h"

static int test_frame_parse_text(void) {
    /* Text frame "Hello" unmasked: 0x81 0x05 H e l l o */
    uint8_t data[] = {0x81, 0x05, 'H', 'e', 'l', 'l', 'o'};
    ws_frame_header_t hdr;
    int n = ws_frame_parse_header(data, sizeof(data), &hdr);
    ASSERT(n > 0);
    ASSERT_EQ(hdr.fin, 1);
    ASSERT_EQ(hdr.opcode, WS_OP_TEXT);
    ASSERT_EQ(hdr.masked, 0);
    ASSERT_EQ(hdr.payload_len, 5ull);
    ASSERT_EQ(hdr.header_len, 2);
    return 0;
}

static int test_frame_parse_binary_masked(void) {
    /* Binary frame, masked, 4 bytes payload */
    uint8_t data[] = {
        0x82,       /* FIN + binary */
        0x84,       /* MASK + 4 bytes */
        0x37, 0xfa, 0x21, 0x3d,  /* mask key */
        0x7f, 0x9f, 0x4d, 0x51   /* masked payload */
    };
    ws_frame_header_t hdr;
    int n = ws_frame_parse_header(data, sizeof(data), &hdr);
    ASSERT(n > 0);
    ASSERT_EQ(hdr.fin, 1);
    ASSERT_EQ(hdr.opcode, WS_OP_BIN);
    ASSERT_EQ(hdr.masked, 1);
    ASSERT_EQ(hdr.payload_len, 4ull);
    ASSERT_EQ(hdr.header_len, 6);
    ASSERT_MEM_EQ(hdr.mask, data + 2, 4);
    return 0;
}

static int test_frame_parse_16bit_len(void) {
    /* 126 = extended 16-bit length */
    uint8_t data[4] = {0x82, 126, 0x01, 0x00};  /* 256 bytes */
    ws_frame_header_t hdr;
    int n = ws_frame_parse_header(data, sizeof(data), &hdr);
    ASSERT(n > 0);
    ASSERT_EQ(hdr.payload_len, 256ull);
    ASSERT_EQ(hdr.header_len, 4);
    return 0;
}

static int test_frame_parse_64bit_len(void) {
    /* 127 = extended 64-bit length */
    uint8_t data[10] = {0x82, 127, 0, 0, 0, 0, 0, 0x01, 0x00, 0x00};  /* 65536 */
    ws_frame_header_t hdr;
    int n = ws_frame_parse_header(data, sizeof(data), &hdr);
    ASSERT(n > 0);
    ASSERT_EQ(hdr.payload_len, 65536ull);
    ASSERT_EQ(hdr.header_len, 10);
    return 0;
}

static int test_frame_parse_need_more(void) {
    uint8_t data[] = {0x81};  /* only 1 byte */
    ws_frame_header_t hdr;
    ASSERT_EQ(ws_frame_parse_header(data, 1, &hdr), 0);
    return 0;
}

static int test_frame_parse_ping(void) {
    uint8_t data[] = {0x89, 0x00};  /* ping with no payload */
    ws_frame_header_t hdr;
    int n = ws_frame_parse_header(data, sizeof(data), &hdr);
    ASSERT(n > 0);
    ASSERT_EQ(hdr.opcode, WS_OP_PING);
    ASSERT_EQ(hdr.payload_len, 0ull);
    return 0;
}

static int test_frame_parse_close(void) {
    /* Close frame with code 1000 */
    uint8_t data[] = {0x88, 0x02, 0x03, 0xe8};
    ws_frame_header_t hdr;
    int n = ws_frame_parse_header(data, sizeof(data), &hdr);
    ASSERT(n > 0);
    ASSERT_EQ(hdr.opcode, WS_OP_CLOSE);
    ASSERT_EQ(hdr.payload_len, 2ull);
    return 0;
}

static int test_frame_parse_continuation(void) {
    /* Non-FIN continuation frame */
    uint8_t data[] = {0x00, 0x03, 'a', 'b', 'c'};
    ws_frame_header_t hdr;
    int n = ws_frame_parse_header(data, sizeof(data), &hdr);
    ASSERT(n > 0);
    ASSERT_EQ(hdr.fin, 0);
    ASSERT_EQ(hdr.opcode, WS_OP_CONT);
    return 0;
}

/* ---- Mask tests ---- */

static int test_mask_basic(void) {
    uint8_t data[] = {'H', 'e', 'l', 'l', 'o'};
    uint8_t mask[] = {0x37, 0xfa, 0x21, 0x3d};
    uint8_t original[5];
    memcpy(original, data, 5);

    ws_frame_apply_mask(data, 5, mask);

    /* After masking, data should be different */
    int differs = 0;
    for (int i = 0; i < 5; i++)
        if (data[i] != original[i]) differs = 1;
    ASSERT(differs);

    /* Unmask (apply mask again) should recover original */
    ws_frame_apply_mask(data, 5, mask);
    ASSERT_MEM_EQ(data, original, 5);
    return 0;
}

static int test_mask_large(void) {
    /* Test the optimized 8-byte path */
    uint8_t data[1024];
    uint8_t original[1024];
    uint8_t mask[] = {0xAA, 0xBB, 0xCC, 0xDD};

    for (int i = 0; i < 1024; i++) data[i] = (uint8_t)i;
    memcpy(original, data, 1024);

    ws_frame_apply_mask(data, 1024, mask);
    ws_frame_apply_mask(data, 1024, mask);
    ASSERT_MEM_EQ(data, original, 1024);
    return 0;
}

static int test_mask_zero(void) {
    uint8_t mask[] = {0, 0, 0, 0};
    uint8_t data[] = {1, 2, 3, 4, 5};
    uint8_t original[5];
    memcpy(original, data, 5);

    ws_frame_apply_mask(data, 5, mask);
    ASSERT_MEM_EQ(data, original, 5);
    return 0;
}

/* ---- Encode tests ---- */

static int test_encode_small(void) {
    uint8_t buf[WS_MAX_FRAME_HEADER];
    int n = ws_frame_encode_header(buf, WS_OP_BIN, 100, 1);
    ASSERT_EQ(n, 2);
    ASSERT_EQ(buf[0], 0x82u);  /* FIN + BIN */
    ASSERT_EQ(buf[1], 100u);
    return 0;
}

static int test_encode_medium(void) {
    uint8_t buf[WS_MAX_FRAME_HEADER];
    int n = ws_frame_encode_header(buf, WS_OP_TEXT, 300, 1);
    ASSERT_EQ(n, 4);
    ASSERT_EQ(buf[0], 0x81u);
    ASSERT_EQ(buf[1], 126u);
    ASSERT_EQ(((uint16_t)buf[2] << 8) | buf[3], 300u);
    return 0;
}

static int test_encode_large(void) {
    uint8_t buf[WS_MAX_FRAME_HEADER];
    int n = ws_frame_encode_header(buf, WS_OP_BIN, 70000, 1);
    ASSERT_EQ(n, 10);
    ASSERT_EQ(buf[1], 127u);
    return 0;
}

static int test_encode_close(void) {
    uint8_t buf[32];
    int n = ws_frame_encode_close(buf, WS_CLOSE_NORMAL, "bye");
    ASSERT(n > 0);
    ASSERT_EQ(buf[0], 0x88u);  /* FIN + CLOSE */
    /* Payload should contain code + "bye" */
    ASSERT_EQ(buf[2], 0x03u);
    ASSERT_EQ(buf[3], 0xe8u);
    ASSERT(memcmp(buf + 4, "bye", 3) == 0);
    return 0;
}

static int test_encode_pong(void) {
    uint8_t payload[] = {1, 2, 3};
    uint8_t buf[32];
    int n = ws_frame_encode_pong(buf, payload, 3);
    ASSERT(n > 0);
    ASSERT_EQ(buf[0], 0x8Au);  /* FIN + PONG */
    ASSERT_EQ(buf[1], 3u);
    ASSERT_MEM_EQ(buf + 2, payload, 3);
    return 0;
}

static int test_encode_nofin(void) {
    uint8_t buf[WS_MAX_FRAME_HEADER];
    int n = ws_frame_encode_header(buf, WS_OP_TEXT, 5, 0);
    ASSERT_EQ(n, 2);
    ASSERT_EQ(buf[0] & 0x80u, 0u);  /* no FIN */
    ASSERT_EQ(buf[0] & 0x0Fu, WS_OP_TEXT);
    return 0;
}

void test_ws_run(void) {
    TEST_SUITE("WebSocket Frame Parsing");
    TEST_RUN(test_frame_parse_text);
    TEST_RUN(test_frame_parse_binary_masked);
    TEST_RUN(test_frame_parse_16bit_len);
    TEST_RUN(test_frame_parse_64bit_len);
    TEST_RUN(test_frame_parse_need_more);
    TEST_RUN(test_frame_parse_ping);
    TEST_RUN(test_frame_parse_close);
    TEST_RUN(test_frame_parse_continuation);

    TEST_SUITE("WebSocket Masking");
    TEST_RUN(test_mask_basic);
    TEST_RUN(test_mask_large);
    TEST_RUN(test_mask_zero);

    TEST_SUITE("WebSocket Frame Encoding");
    TEST_RUN(test_encode_small);
    TEST_RUN(test_encode_medium);
    TEST_RUN(test_encode_large);
    TEST_RUN(test_encode_close);
    TEST_RUN(test_encode_pong);
    TEST_RUN(test_encode_nofin);
}
