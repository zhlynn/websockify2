/* SPDX-License-Identifier: MIT */
#include "test_framework.h"
#include "http.h"

static int test_parse_get(void) {
    const char *req =
        "GET / HTTP/1.1\r\n"
        "Host: localhost:8080\r\n"
        "Connection: keep-alive\r\n"
        "\r\n";
    ws_http_request_t r;
    memset(&r, 0, sizeof(r));
    int n = ws_http_parse_request(&r, req, (int)strlen(req));
    ASSERT(n > 0);
    ASSERT(r.complete);
    ASSERT_STR_EQ(r.method, "GET");
    ASSERT_STR_EQ(r.path, "/");
    ASSERT_NOT_NULL(r.host);
    ASSERT_STR_EQ(r.host, "localhost:8080");
    ws_http_request_free(&r);
    return 0;
}

static int test_parse_ws_upgrade(void) {
    const char *req =
        "GET /websockify?token=abc HTTP/1.1\r\n"
        "Host: example.com\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "Sec-WebSocket-Protocol: binary\r\n"
        "\r\n";
    ws_http_request_t r;
    memset(&r, 0, sizeof(r));
    int n = ws_http_parse_request(&r, req, (int)strlen(req));
    ASSERT(n > 0);
    ASSERT(r.upgrade_websocket);
    ASSERT_NOT_NULL(r.ws_key);
    ASSERT_STR_EQ(r.ws_key, "dGhlIHNhbXBsZSBub25jZQ==");
    ASSERT_NOT_NULL(r.ws_version);
    ASSERT_STR_EQ(r.ws_version, "13");
    ASSERT_NOT_NULL(r.ws_protocol);
    ASSERT_STR_EQ(r.ws_protocol, "binary");
    ASSERT_STR_EQ(r.query, "token=abc");
    ws_http_request_free(&r);
    return 0;
}

static int test_parse_path_with_query(void) {
    const char *req =
        "GET /path/to/file?key=value&other=123 HTTP/1.1\r\n"
        "Host: test.com\r\n"
        "\r\n";
    ws_http_request_t r;
    memset(&r, 0, sizeof(r));
    int n = ws_http_parse_request(&r, req, (int)strlen(req));
    ASSERT(n > 0);
    ASSERT_STR_EQ(r.path, "/path/to/file");
    ASSERT_STR_EQ(r.query, "key=value&other=123");
    ws_http_request_free(&r);
    return 0;
}

static int test_parse_incomplete(void) {
    const char *req = "GET / HTTP/1.1\r\nHost: localhost\r\n";
    ws_http_request_t r;
    memset(&r, 0, sizeof(r));
    int n = ws_http_parse_request(&r, req, (int)strlen(req));
    ASSERT_EQ(n, 0);  /* need more data */
    ws_http_request_free(&r);
    return 0;
}

static int test_parse_multiple_headers(void) {
    const char *req =
        "GET / HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Accept: text/html\r\n"
        "Accept-Language: en-US\r\n"
        "Cache-Control: no-cache\r\n"
        "User-Agent: test/1.0\r\n"
        "\r\n";
    ws_http_request_t r;
    memset(&r, 0, sizeof(r));
    int n = ws_http_parse_request(&r, req, (int)strlen(req));
    ASSERT(n > 0);
    ASSERT_EQ(r.header_count, 5);
    ASSERT_NOT_NULL(ws_http_get_header(&r, "Accept"));
    ASSERT_STR_EQ(ws_http_get_header(&r, "Accept"), "text/html");
    ASSERT_NOT_NULL(ws_http_get_header(&r, "user-agent"));  /* case insensitive */
    ws_http_request_free(&r);
    return 0;
}

static int test_parse_header_not_found(void) {
    const char *req = "GET / HTTP/1.1\r\nHost: x\r\n\r\n";
    ws_http_request_t r;
    memset(&r, 0, sizeof(r));
    ws_http_parse_request(&r, req, (int)strlen(req));
    ASSERT_NULL(ws_http_get_header(&r, "X-Nonexistent"));
    ws_http_request_free(&r);
    return 0;
}

static int test_header_contains(void) {
    const char *req =
        "GET / HTTP/1.1\r\n"
        "Host: x\r\n"
        "Connection: keep-alive, Upgrade\r\n"
        "Upgrade: websocket\r\n"
        "\r\n";
    ws_http_request_t r;
    memset(&r, 0, sizeof(r));
    ws_http_parse_request(&r, req, (int)strlen(req));
    ASSERT(ws_http_header_contains(&r, "Connection", "Upgrade"));
    ASSERT(ws_http_header_contains(&r, "Connection", "keep-alive"));
    ASSERT(!ws_http_header_contains(&r, "Connection", "close"));
    ASSERT(ws_http_header_contains(&r, "Upgrade", "websocket"));
    ws_http_request_free(&r);
    return 0;
}

static int test_parse_url_encoding(void) {
    const char *req =
        "GET /path%20with%20spaces HTTP/1.1\r\n"
        "Host: x\r\n"
        "\r\n";
    ws_http_request_t r;
    memset(&r, 0, sizeof(r));
    ws_http_parse_request(&r, req, (int)strlen(req));
    ASSERT_STR_EQ(r.path, "/path with spaces");
    ws_http_request_free(&r);
    return 0;
}

static int test_not_upgrade(void) {
    const char *req =
        "GET / HTTP/1.1\r\n"
        "Host: x\r\n"
        "Connection: keep-alive\r\n"
        "\r\n";
    ws_http_request_t r;
    memset(&r, 0, sizeof(r));
    ws_http_parse_request(&r, req, (int)strlen(req));
    ASSERT(!r.upgrade_websocket);
    ws_http_request_free(&r);
    return 0;
}

/* ---- Response building tests ---- */

static int test_error_response(void) {
    char buf[1024];
    int n = ws_http_error_response(buf, sizeof(buf), 404, "Not Found");
    ASSERT(n > 0);
    ASSERT(strstr(buf, "404") != NULL);
    ASSERT(strstr(buf, "Not Found") != NULL);
    ASSERT(strstr(buf, "Content-Type: text/html") != NULL);
    return 0;
}

static int test_ws_upgrade_response(void) {
    char buf[512];
    int n = ws_http_ws_upgrade_response(buf, sizeof(buf),
                                         "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=", "binary");
    ASSERT(n > 0);
    ASSERT(strstr(buf, "101 Switching Protocols") != NULL);
    ASSERT(strstr(buf, "Upgrade: websocket") != NULL);
    ASSERT(strstr(buf, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") != NULL);
    ASSERT(strstr(buf, "Sec-WebSocket-Protocol: binary") != NULL);
    return 0;
}

static int test_response_no_protocol(void) {
    char buf[512];
    int n = ws_http_ws_upgrade_response(buf, sizeof(buf),
                                         "key123=", NULL);
    ASSERT(n > 0);
    ASSERT(strstr(buf, "Sec-WebSocket-Protocol") == NULL);
    return 0;
}

void test_http_run(void) {
    TEST_SUITE("HTTP Request Parsing");
    TEST_RUN(test_parse_get);
    TEST_RUN(test_parse_ws_upgrade);
    TEST_RUN(test_parse_path_with_query);
    TEST_RUN(test_parse_incomplete);
    TEST_RUN(test_parse_multiple_headers);
    TEST_RUN(test_parse_header_not_found);
    TEST_RUN(test_header_contains);
    TEST_RUN(test_parse_url_encoding);
    TEST_RUN(test_not_upgrade);

    TEST_SUITE("HTTP Response Building");
    TEST_RUN(test_error_response);
    TEST_RUN(test_ws_upgrade_response);
    TEST_RUN(test_response_no_protocol);
}
