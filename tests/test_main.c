/* SPDX-License-Identifier: MIT */
#include "test_framework.h"
#include "platform.h"

/* Global test counters */
int g_tests_run = 0;
int g_tests_passed = 0;
int g_tests_failed = 0;

/* External test suite functions */
extern void test_crypto_run(void);
extern void test_buf_run(void);
extern void test_ws_run(void);
extern void test_http_run(void);
extern void test_token_run(void);
extern void test_config_run(void);
extern void test_web_run(void);

int main(void) {
    ws_platform_init();

    printf("========================================\n");
    printf("  websockify2 unit tests\n");
    printf("========================================\n");

    test_crypto_run();
    test_buf_run();
    test_ws_run();
    test_http_run();
    test_token_run();
    test_config_run();
    test_web_run();

    TEST_SUMMARY();
}
