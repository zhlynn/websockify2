/* SPDX-License-Identifier: MIT */
#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Global counters — defined in test_main.c */
extern int g_tests_run;
extern int g_tests_passed;
extern int g_tests_failed;

#define TEST_SUITE(name) \
    do { printf("\n--- %s ---\n", name); } while(0)

#define TEST_RUN(fn) \
    do { \
        g_tests_run++; \
        printf("  %-50s ", #fn); \
        fflush(stdout); \
        int _result = fn(); \
        if (_result == 0) { \
            g_tests_passed++; \
            printf("PASS\n"); \
        } else { \
            g_tests_failed++; \
            printf("FAIL (line %d)\n", _result); \
        } \
    } while(0)

#define ASSERT(cond) \
    do { if (!(cond)) { fprintf(stderr, "    ASSERT FAILED: %s\n", #cond); return __LINE__; } } while(0)

#define ASSERT_EQ(a, b) \
    do { if ((a) != (b)) { fprintf(stderr, "    ASSERT_EQ FAILED: %s != %s (%ld != %ld)\n", \
        #a, #b, (long)(a), (long)(b)); return __LINE__; } } while(0)

#define ASSERT_STR_EQ(a, b) \
    do { if (strcmp((a), (b)) != 0) { fprintf(stderr, "    ASSERT_STR_EQ FAILED: \"%s\" != \"%s\"\n", \
        (a), (b)); return __LINE__; } } while(0)

#define ASSERT_MEM_EQ(a, b, n) \
    do { if (memcmp((a), (b), (n)) != 0) { fprintf(stderr, "    ASSERT_MEM_EQ FAILED at line %d\n", \
        __LINE__); return __LINE__; } } while(0)

#define ASSERT_NULL(a) \
    do { if ((a) != NULL) { fprintf(stderr, "    ASSERT_NULL FAILED: %s\n", #a); return __LINE__; } } while(0)

#define ASSERT_NOT_NULL(a) \
    do { if ((a) == NULL) { fprintf(stderr, "    ASSERT_NOT_NULL FAILED: %s\n", #a); return __LINE__; } } while(0)

#define TEST_SUMMARY() \
    do { \
        printf("\n========================================\n"); \
        printf("Tests: %d run, %d passed, %d failed\n", g_tests_run, g_tests_passed, g_tests_failed); \
        printf("========================================\n"); \
        return g_tests_failed > 0 ? 1 : 0; \
    } while(0)

#endif /* TEST_FRAMEWORK_H */
