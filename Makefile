# websockify2 Makefile

-include config.mk

# Defaults if configure wasn't run
CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra -Wpedantic
LDFLAGS ?=
LIBS    ?= -lssl -lcrypto
PREFIX  ?= /usr/local

SRCDIR  = src
TESTDIR = tests
BUILDDIR = build
BINDIR  = $(BUILDDIR)/bin

# Source files
SRCS = $(SRCDIR)/main.c \
       $(SRCDIR)/platform.c \
       $(SRCDIR)/config.c \
       $(SRCDIR)/log.c \
       $(SRCDIR)/buf.c \
       $(SRCDIR)/crypto.c \
       $(SRCDIR)/net.c \
       $(SRCDIR)/event.c \
       $(SRCDIR)/daemon.c \
       $(SRCDIR)/ssl_conn.c \
       $(SRCDIR)/http.c \
       $(SRCDIR)/ws.c \
       $(SRCDIR)/token.c \
       $(SRCDIR)/record.c \
       $(SRCDIR)/web.c \
       $(SRCDIR)/proxy.c \
       $(SRCDIR)/server.c

# Library sources (everything except main.c)
LIB_SRCS = $(filter-out $(SRCDIR)/main.c, $(SRCS))

OBJS = $(patsubst $(SRCDIR)/%.c, $(BUILDDIR)/%.o, $(SRCS))
LIB_OBJS = $(patsubst $(SRCDIR)/%.c, $(BUILDDIR)/%.o, $(LIB_SRCS))

TARGET = $(BINDIR)/websockify2

# Add include path
CFLAGS += -I$(SRCDIR)

.PHONY: all clean install test test-unit test-integration test-bench configure

all: $(TARGET)

configure:
	@sh configure.sh

$(BUILDDIR):
	@mkdir -p $(BUILDDIR)

$(BINDIR):
	@mkdir -p $(BINDIR)

$(BUILDDIR)/%.o: $(SRCDIR)/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJS) | $(BINDIR)
	$(CC) $(LDFLAGS) $(OBJS) -o $@ $(LIBS)
	@echo "Build complete: $@"

# ---- Tests ----

TEST_CFLAGS = $(CFLAGS) -I$(SRCDIR) -DTEST_BUILD

# Unit tests
TEST_UNIT_SRCS = $(TESTDIR)/test_main.c \
                 $(TESTDIR)/test_crypto.c \
                 $(TESTDIR)/test_ws.c \
                 $(TESTDIR)/test_http.c \
                 $(TESTDIR)/test_buf.c \
                 $(TESTDIR)/test_token.c \
                 $(TESTDIR)/test_config.c \
                 $(TESTDIR)/test_web.c

TEST_UNIT_OBJS = $(patsubst $(TESTDIR)/%.c, $(BUILDDIR)/test_%.o, $(TEST_UNIT_SRCS))
TEST_UNIT_BIN = $(BINDIR)/test_unit

$(BUILDDIR)/test_%.o: $(TESTDIR)/%.c | $(BUILDDIR)
	$(CC) $(TEST_CFLAGS) -c $< -o $@

$(TEST_UNIT_BIN): $(TEST_UNIT_OBJS) $(LIB_OBJS) | $(BINDIR)
	$(CC) $(LDFLAGS) $(TEST_UNIT_OBJS) $(LIB_OBJS) -o $@ $(LIBS)

test-unit: $(TEST_UNIT_BIN)
	@echo "=== Running unit tests ==="
	$(TEST_UNIT_BIN)

# Echo server for integration tests
ECHO_SERVER = $(BINDIR)/echo_server
$(ECHO_SERVER): $(TESTDIR)/integration/echo_server.c $(LIB_OBJS) | $(BINDIR)
	$(CC) $(TEST_CFLAGS) $< $(LIB_OBJS) -o $@ $(LIBS)

# Bench tool
BENCH_BIN = $(BINDIR)/bench_connections
$(BENCH_BIN): $(TESTDIR)/bench/bench_connections.c $(LIB_OBJS) | $(BINDIR)
	$(CC) $(TEST_CFLAGS) $< $(LIB_OBJS) -o $@ $(LIBS)

test-integration: $(TARGET) $(ECHO_SERVER)
	@echo "=== Running integration tests ==="
	@sh $(TESTDIR)/integration/test_proxy.sh $(TARGET) $(ECHO_SERVER)

test-bench: $(BENCH_BIN) $(TARGET) $(ECHO_SERVER)
	@echo "=== Running benchmarks ==="
	$(BENCH_BIN)

test: test-unit test-integration
	@echo "=== All tests passed ==="

# ---- Install ----

install: $(TARGET)
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/websockify2

# ---- Clean ----

clean:
	rm -rf $(BUILDDIR)

# ---- Docker cross-platform test ----

docker-test-linux:
	docker run --rm -v $(PWD):/src -w /src gcc:latest \
		sh -c "apt-get update && apt-get install -y libssl-dev && sh configure.sh && make clean all test-unit"

docker-test-alpine:
	docker run --rm -v $(PWD):/src -w /src alpine:latest \
		sh -c "apk add build-base openssl-dev && sh configure.sh && make clean all test-unit"

docker-test-freebsd:
	@echo "FreeBSD Docker test requires special setup (use VM or CI)"
	@echo "Build command: sh configure.sh && make clean all test-unit"

docker-test-all: docker-test-linux docker-test-alpine
	@echo "=== Cross-platform tests complete ==="
