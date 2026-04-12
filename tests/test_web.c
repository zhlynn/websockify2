/* SPDX-License-Identifier: MIT */
#include "test_framework.h"
#include "web.h"

static int test_mime_html(void) {
    ASSERT_STR_EQ(ws_mime_type("index.html"), "text/html");
    ASSERT_STR_EQ(ws_mime_type("/path/to/page.htm"), "text/html");
    return 0;
}

static int test_mime_css_js(void) {
    ASSERT_STR_EQ(ws_mime_type("style.css"), "text/css");
    ASSERT_STR_EQ(ws_mime_type("app.js"), "application/javascript");
    ASSERT_STR_EQ(ws_mime_type("data.json"), "application/json");
    return 0;
}

static int test_mime_images(void) {
    ASSERT_STR_EQ(ws_mime_type("icon.png"), "image/png");
    ASSERT_STR_EQ(ws_mime_type("photo.jpg"), "image/jpeg");
    ASSERT_STR_EQ(ws_mime_type("photo.jpeg"), "image/jpeg");
    ASSERT_STR_EQ(ws_mime_type("anim.gif"), "image/gif");
    ASSERT_STR_EQ(ws_mime_type("vec.svg"), "image/svg+xml");
    ASSERT_STR_EQ(ws_mime_type("favicon.ico"), "image/x-icon");
    return 0;
}

static int test_mime_fonts(void) {
    ASSERT_STR_EQ(ws_mime_type("font.woff"), "font/woff");
    ASSERT_STR_EQ(ws_mime_type("font.woff2"), "font/woff2");
    ASSERT_STR_EQ(ws_mime_type("font.ttf"), "font/ttf");
    return 0;
}

static int test_mime_wasm(void) {
    ASSERT_STR_EQ(ws_mime_type("module.wasm"), "application/wasm");
    return 0;
}

static int test_mime_case_insensitive(void) {
    ASSERT_STR_EQ(ws_mime_type("INDEX.HTML"), "text/html");
    ASSERT_STR_EQ(ws_mime_type("Photo.JPG"), "image/jpeg");
    return 0;
}

static int test_mime_unknown(void) {
    ASSERT_STR_EQ(ws_mime_type("file.xyz"), "application/octet-stream");
    ASSERT_STR_EQ(ws_mime_type("noext"), "application/octet-stream");
    return 0;
}

void test_web_run(void) {
    TEST_SUITE("MIME Type Detection");
    TEST_RUN(test_mime_html);
    TEST_RUN(test_mime_css_js);
    TEST_RUN(test_mime_images);
    TEST_RUN(test_mime_fonts);
    TEST_RUN(test_mime_wasm);
    TEST_RUN(test_mime_case_insensitive);
    TEST_RUN(test_mime_unknown);
}
