/* SPDX-License-Identifier: MIT */
#include "test_framework.h"
#include "crypto.h"

/* ---- SHA-1 Tests ---- */

static int test_sha1_empty(void) {
    uint8_t digest[20];
    ws_sha1("", 0, digest);
    /* SHA-1("") = da39a3ee5e6b4b0d3255bfef95601890afd80709 */
    uint8_t expected[] = {
        0xda, 0x39, 0xa3, 0xee, 0x5e, 0x6b, 0x4b, 0x0d, 0x32, 0x55,
        0xbf, 0xef, 0x95, 0x60, 0x18, 0x90, 0xaf, 0xd8, 0x07, 0x09
    };
    ASSERT_MEM_EQ(digest, expected, 20);
    return 0;
}

static int test_sha1_abc(void) {
    uint8_t digest[20];
    ws_sha1("abc", 3, digest);
    /* SHA-1("abc") = a9993e364706816aba3e25717850c26c9cd0d89d */
    uint8_t expected[] = {
        0xa9, 0x99, 0x3e, 0x36, 0x47, 0x06, 0x81, 0x6a, 0xba, 0x3e,
        0x25, 0x71, 0x78, 0x50, 0xc2, 0x6c, 0x9c, 0xd0, 0xd8, 0x9d
    };
    ASSERT_MEM_EQ(digest, expected, 20);
    return 0;
}

static int test_sha1_long(void) {
    /* SHA-1("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") */
    uint8_t digest[20];
    const char *input = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    ws_sha1(input, strlen(input), digest);
    uint8_t expected[] = {
        0x84, 0x98, 0x3e, 0x44, 0x1c, 0x3b, 0xd2, 0x6e, 0xba, 0xae,
        0x4a, 0xa1, 0xf9, 0x51, 0x29, 0xe5, 0xe5, 0x46, 0x70, 0xf1
    };
    ASSERT_MEM_EQ(digest, expected, 20);
    return 0;
}

static int test_sha1_incremental(void) {
    ws_sha1_ctx_t ctx;
    ws_sha1_init(&ctx);
    ws_sha1_update(&ctx, "a", 1);
    ws_sha1_update(&ctx, "b", 1);
    ws_sha1_update(&ctx, "c", 1);
    uint8_t digest[20];
    ws_sha1_final(&ctx, digest);

    uint8_t expected[20];
    ws_sha1("abc", 3, expected);
    ASSERT_MEM_EQ(digest, expected, 20);
    return 0;
}

static int test_sha1_million_a(void) {
    /* SHA-1 of 1,000,000 'a' characters */
    ws_sha1_ctx_t ctx;
    ws_sha1_init(&ctx);
    char buf[1000];
    memset(buf, 'a', sizeof(buf));
    for (int i = 0; i < 1000; i++)
        ws_sha1_update(&ctx, buf, sizeof(buf));
    uint8_t digest[20];
    ws_sha1_final(&ctx, digest);

    uint8_t expected[] = {
        0x34, 0xaa, 0x97, 0x3c, 0xd4, 0xc4, 0xda, 0xa4, 0xf6, 0x1e,
        0xeb, 0x2b, 0xdb, 0xad, 0x27, 0x31, 0x65, 0x34, 0x01, 0x6f
    };
    ASSERT_MEM_EQ(digest, expected, 20);
    return 0;
}

/* ---- Base64 Tests ---- */

static int test_base64_encode_empty(void) {
    char out[8];
    int n = ws_base64_encode("", 0, out, sizeof(out));
    ASSERT(n > 0);
    ASSERT_STR_EQ(out, "");
    return 0;
}

static int test_base64_encode_f(void) {
    char out[8];
    ws_base64_encode("f", 1, out, sizeof(out));
    ASSERT_STR_EQ(out, "Zg==");
    return 0;
}

static int test_base64_encode_fo(void) {
    char out[8];
    ws_base64_encode("fo", 2, out, sizeof(out));
    ASSERT_STR_EQ(out, "Zm8=");
    return 0;
}

static int test_base64_encode_foo(void) {
    char out[8];
    ws_base64_encode("foo", 3, out, sizeof(out));
    ASSERT_STR_EQ(out, "Zm9v");
    return 0;
}

static int test_base64_encode_foobar(void) {
    char out[16];
    ws_base64_encode("foobar", 6, out, sizeof(out));
    ASSERT_STR_EQ(out, "Zm9vYmFy");
    return 0;
}

static int test_base64_decode_basic(void) {
    uint8_t out[32];
    int n = ws_base64_decode("Zm9vYmFy", 8, out, sizeof(out));
    ASSERT_EQ(n, 6);
    ASSERT(memcmp(out, "foobar", 6) == 0);
    return 0;
}

static int test_base64_decode_padding1(void) {
    uint8_t out[32];
    int n = ws_base64_decode("Zm8=", 4, out, sizeof(out));
    ASSERT_EQ(n, 2);
    ASSERT(memcmp(out, "fo", 2) == 0);
    return 0;
}

static int test_base64_decode_padding2(void) {
    uint8_t out[32];
    int n = ws_base64_decode("Zg==", 4, out, sizeof(out));
    ASSERT_EQ(n, 1);
    ASSERT(out[0] == 'f');
    return 0;
}

static int test_base64_roundtrip(void) {
    const char *inputs[] = {
        "Hello, World!",
        "WebSocket proxy test",
        "\x00\x01\x02\xff\xfe\xfd",
        "The quick brown fox jumps over the lazy dog",
        NULL
    };
    size_t lens[] = {13, 20, 6, 43};

    for (int i = 0; inputs[i]; i++) {
        char encoded[256];
        ws_base64_encode(inputs[i], lens[i], encoded, sizeof(encoded));

        uint8_t decoded[256];
        int n = ws_base64_decode(encoded, strlen(encoded), decoded, sizeof(decoded));
        ASSERT_EQ((size_t)n, lens[i]);
        ASSERT(memcmp(decoded, inputs[i], lens[i]) == 0);
    }
    return 0;
}

static int test_base64_decode_invalid(void) {
    uint8_t out[32];
    /* Invalid length */
    int n = ws_base64_decode("abc", 3, out, sizeof(out));
    ASSERT_EQ(n, -1);
    return 0;
}

/* ---- WebSocket Accept Key ---- */

static int test_ws_accept_key(void) {
    /* RFC 6455 Section 4.2.2 example */
    char accept[64];
    int ret = ws_websocket_accept_key("dGhlIHNhbXBsZSBub25jZQ==", accept, sizeof(accept));
    ASSERT_EQ(ret, 0);
    ASSERT_STR_EQ(accept, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
    return 0;
}

static int test_ws_accept_key_other(void) {
    /* Another test vector */
    char accept[64];
    int ret = ws_websocket_accept_key("x3JJHMbDL1EzLkh9GBhXDw==", accept, sizeof(accept));
    ASSERT_EQ(ret, 0);
    ASSERT_STR_EQ(accept, "HSmrc0sMlYUkAGmm5OPpG2HaGWk=");
    return 0;
}

void test_crypto_run(void) {
    TEST_SUITE("SHA-1");
    TEST_RUN(test_sha1_empty);
    TEST_RUN(test_sha1_abc);
    TEST_RUN(test_sha1_long);
    TEST_RUN(test_sha1_incremental);
    TEST_RUN(test_sha1_million_a);

    TEST_SUITE("Base64");
    TEST_RUN(test_base64_encode_empty);
    TEST_RUN(test_base64_encode_f);
    TEST_RUN(test_base64_encode_fo);
    TEST_RUN(test_base64_encode_foo);
    TEST_RUN(test_base64_encode_foobar);
    TEST_RUN(test_base64_decode_basic);
    TEST_RUN(test_base64_decode_padding1);
    TEST_RUN(test_base64_decode_padding2);
    TEST_RUN(test_base64_roundtrip);
    TEST_RUN(test_base64_decode_invalid);

    TEST_SUITE("WebSocket Accept Key");
    TEST_RUN(test_ws_accept_key);
    TEST_RUN(test_ws_accept_key_other);
}
