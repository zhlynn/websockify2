/* SPDX-License-Identifier: MIT */
#include "test_framework.h"
#include "token.h"
#include <sys/stat.h>

static const char *TEST_TOKEN_FILE = "/tmp/ws_test_tokens.cfg";
static const char *TEST_TOKEN_DIR  = "/tmp/ws_test_tokens_dir";

static void setup_token_file(void) {
    FILE *f = fopen(TEST_TOKEN_FILE, "w");
    if (f) {
        fprintf(f, "# Comment line\n");
        fprintf(f, "token1: localhost:5900\n");
        fprintf(f, "token2: 192.168.1.1:5901\n");
        fprintf(f, "myvm: 10.0.0.5:5902\n");
        fprintf(f, "  spaces  :  host.example.com:5903  \n");
        fclose(f);
    }
}

static void setup_token_dir(void) {
    mkdir(TEST_TOKEN_DIR, 0755);
    char path[256];

    snprintf(path, sizeof(path), "%s/file1.cfg", TEST_TOKEN_DIR);
    FILE *f = fopen(path, "w");
    if (f) {
        fprintf(f, "vm-a: host-a:5900\n");
        fprintf(f, "vm-b: host-b:5901\n");
        fclose(f);
    }

    snprintf(path, sizeof(path), "%s/file2.cfg", TEST_TOKEN_DIR);
    f = fopen(path, "w");
    if (f) {
        fprintf(f, "vm-c: host-c:5902\n");
        fclose(f);
    }
}

static void cleanup_tokens(void) {
    unlink(TEST_TOKEN_FILE);
    char path[256];
    snprintf(path, sizeof(path), "%s/file1.cfg", TEST_TOKEN_DIR);
    unlink(path);
    snprintf(path, sizeof(path), "%s/file2.cfg", TEST_TOKEN_DIR);
    unlink(path);
    rmdir(TEST_TOKEN_DIR);
}

static int test_token_file_basic(void) {
    setup_token_file();
    ws_token_ctx_t ctx;
    ASSERT_EQ(ws_token_init(&ctx, TEST_TOKEN_FILE), 0);
    ASSERT_EQ(ctx.is_dir, 0);

    ws_target_t target;
    ASSERT_EQ(ws_token_lookup(&ctx, "token1", &target), 0);
    ASSERT_STR_EQ(target.host, "localhost");
    ASSERT_EQ(target.port, 5900);

    ASSERT_EQ(ws_token_lookup(&ctx, "token2", &target), 0);
    ASSERT_STR_EQ(target.host, "192.168.1.1");
    ASSERT_EQ(target.port, 5901);

    ASSERT_EQ(ws_token_lookup(&ctx, "myvm", &target), 0);
    ASSERT_EQ(target.port, 5902);

    ws_token_free(&ctx);
    cleanup_tokens();
    return 0;
}

static int test_token_file_not_found(void) {
    setup_token_file();
    ws_token_ctx_t ctx;
    ws_token_init(&ctx, TEST_TOKEN_FILE);

    ws_target_t target;
    ASSERT_EQ(ws_token_lookup(&ctx, "nonexistent", &target), -1);

    ws_token_free(&ctx);
    cleanup_tokens();
    return 0;
}

static int test_token_dir_lookup(void) {
    setup_token_dir();
    ws_token_ctx_t ctx;
    ASSERT_EQ(ws_token_init(&ctx, TEST_TOKEN_DIR), 0);
    ASSERT_EQ(ctx.is_dir, 1);

    ws_target_t target;
    ASSERT_EQ(ws_token_lookup(&ctx, "vm-a", &target), 0);
    ASSERT_STR_EQ(target.host, "host-a");
    ASSERT_EQ(target.port, 5900);

    ASSERT_EQ(ws_token_lookup(&ctx, "vm-c", &target), 0);
    ASSERT_STR_EQ(target.host, "host-c");
    ASSERT_EQ(target.port, 5902);

    ASSERT_EQ(ws_token_lookup(&ctx, "missing", &target), -1);

    ws_token_free(&ctx);
    cleanup_tokens();
    return 0;
}

static int test_token_bad_path(void) {
    ws_token_ctx_t ctx;
    ASSERT_EQ(ws_token_init(&ctx, "/nonexistent/path/tokens"), -1);
    return 0;
}

static int test_token_empty_file(void) {
    const char *path = "/tmp/ws_test_empty.cfg";
    FILE *f = fopen(path, "w");
    if (f) fclose(f);

    ws_token_ctx_t ctx;
    ASSERT_EQ(ws_token_init(&ctx, path), 0);

    ws_target_t target;
    ASSERT_EQ(ws_token_lookup(&ctx, "anything", &target), -1);

    ws_token_free(&ctx);
    unlink(path);
    return 0;
}

static int test_token_comments(void) {
    const char *path = "/tmp/ws_test_comments.cfg";
    FILE *f = fopen(path, "w");
    if (f) {
        fprintf(f, "# This is a comment\n");
        fprintf(f, "  # Another comment\n");
        fprintf(f, "\n");
        fprintf(f, "valid: host:1234\n");
        fclose(f);
    }

    ws_token_ctx_t ctx;
    ws_token_init(&ctx, path);

    ws_target_t target;
    ASSERT_EQ(ws_token_lookup(&ctx, "valid", &target), 0);
    ASSERT_EQ(target.port, 1234);

    ws_token_free(&ctx);
    unlink(path);
    return 0;
}

void test_token_run(void) {
    TEST_SUITE("Token File Lookup");
    TEST_RUN(test_token_file_basic);
    TEST_RUN(test_token_file_not_found);
    TEST_RUN(test_token_empty_file);
    TEST_RUN(test_token_comments);

    TEST_SUITE("Token Directory Lookup");
    TEST_RUN(test_token_dir_lookup);
    TEST_RUN(test_token_bad_path);
}
