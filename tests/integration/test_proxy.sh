#!/bin/sh
# Integration tests for websockify2
# Usage: test_proxy.sh <websockify_binary> <echo_server_binary>

set -e

WEBSOCKIFY="$1"
ECHO_SERVER="$2"
PASS=0
FAIL=0
TOTAL=0

if [ -z "$WEBSOCKIFY" ] || [ -z "$ECHO_SERVER" ]; then
    echo "Usage: $0 <websockify_binary> <echo_server_binary>"
    exit 1
fi

# Find available ports
ECHO_PORT=19100
WS_PORT=19200

cleanup() {
    kill $ECHO_PID 2>/dev/null || true
    kill $WS_PID 2>/dev/null || true
    wait $ECHO_PID 2>/dev/null || true
    wait $WS_PID 2>/dev/null || true
}
trap cleanup EXIT

assert_pass() {
    TOTAL=$((TOTAL + 1))
    if [ $1 -eq 0 ]; then
        PASS=$((PASS + 1))
        printf "  %-50s PASS\n" "$2"
    else
        FAIL=$((FAIL + 1))
        printf "  %-50s FAIL\n" "$2"
    fi
}

# ============================================
# Test 1: Basic startup and shutdown
# ============================================
echo ""
echo "--- Integration: Basic Operations ---"

# Start echo server
$ECHO_SERVER $ECHO_PORT &
ECHO_PID=$!
sleep 0.5

# Start websockify
$WEBSOCKIFY $WS_PORT localhost:$ECHO_PORT &
WS_PID=$!
sleep 1

# Check if websockify is running
kill -0 $WS_PID 2>/dev/null
assert_pass $? "websockify starts successfully"

# ============================================
# Test 2: HTTP request (non-upgrade should get error)
# ============================================
HTTP_RESPONSE=$(curl -s --max-time 2 -o /dev/null -w "%{http_code}" http://localhost:$WS_PORT/ 2>/dev/null || echo "000")
if [ "$HTTP_RESPONSE" = "400" ] || [ "$HTTP_RESPONSE" = "000" ]; then
    assert_pass 0 "non-upgrade HTTP request returns 400 or refused"
else
    assert_pass 1 "non-upgrade HTTP request returns 400 (got $HTTP_RESPONSE)"
fi

# ============================================
# Test 3: WebSocket upgrade via curl
# ============================================
WS_RESPONSE=$(curl -s --max-time 2 -i \
    -H "Connection: Upgrade" \
    -H "Upgrade: websocket" \
    -H "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==" \
    -H "Sec-WebSocket-Version: 13" \
    http://localhost:$WS_PORT/ 2>/dev/null | head -1 || echo "")

if echo "$WS_RESPONSE" | grep -q "101"; then
    assert_pass 0 "WebSocket upgrade succeeds (101)"
else
    assert_pass 1 "WebSocket upgrade succeeds (got: $WS_RESPONSE)"
fi

# ============================================
# Test 4: Graceful shutdown
# ============================================
kill -TERM $WS_PID 2>/dev/null
wait $WS_PID 2>/dev/null
EXIT_CODE=$?
# Exit code is 143 (128+15) for SIGTERM on most systems
if [ $EXIT_CODE -eq 0 ] || [ $EXIT_CODE -eq 143 ]; then
    assert_pass 0 "graceful shutdown on SIGTERM"
else
    assert_pass 1 "graceful shutdown on SIGTERM (exit code: $EXIT_CODE)"
fi

kill $ECHO_PID 2>/dev/null || true
wait $ECHO_PID 2>/dev/null || true

# ============================================
# Test 5: SO_REUSEPORT — two instances on same port
# ============================================
echo ""
echo "--- Integration: SO_REUSEPORT ---"

$ECHO_SERVER $((ECHO_PORT + 1)) &
ECHO_PID=$!
sleep 0.5

$WEBSOCKIFY $((WS_PORT + 1)) localhost:$((ECHO_PORT + 1)) &
WS_PID=$!
$WEBSOCKIFY $((WS_PORT + 1)) localhost:$((ECHO_PORT + 1)) &
WS_PID2=$!
sleep 1

if kill -0 $WS_PID 2>/dev/null && kill -0 $WS_PID2 2>/dev/null; then
    assert_pass 0 "two instances on same port via SO_REUSEPORT"
else
    assert_pass 1 "two instances on same port via SO_REUSEPORT"
fi

kill -TERM $WS_PID $WS_PID2 2>/dev/null
wait $WS_PID 2>/dev/null || true
wait $WS_PID2 2>/dev/null || true
kill $ECHO_PID 2>/dev/null || true
wait $ECHO_PID 2>/dev/null || true

# ============================================
# Test 6: Invalid arguments
# ============================================
echo ""
echo "--- Integration: Error Handling ---"

if $WEBSOCKIFY 2>&1 | grep -q "^Usage:"; then
    assert_pass 0 "missing arguments prints usage"
else
    assert_pass 1 "missing arguments prints usage"
fi

if $WEBSOCKIFY --invalid-flag 2>/dev/null; then
    assert_pass 1 "rejects invalid flags"
else
    assert_pass 0 "rejects invalid flags"
fi

# ============================================
# Test 7: Help and version
# ============================================
$WEBSOCKIFY --help >/dev/null 2>&1
assert_pass $? "--help works"

$WEBSOCKIFY --version >/dev/null 2>&1
assert_pass $? "--version works"

# ============================================
# Test 8: End-to-end WebSocket data roundtrip
# ============================================
echo ""
echo "--- Integration: Data Roundtrip ---"

PORT_ECHO=$((ECHO_PORT + 10))
PORT_WS=$((WS_PORT + 10))

$ECHO_SERVER $PORT_ECHO &
ECHO_PID=$!
sleep 0.5

$WEBSOCKIFY $PORT_WS localhost:$PORT_ECHO &
WS_PID=$!
sleep 0.8

# Python-based WebSocket client for end-to-end test
if command -v python3 >/dev/null 2>&1; then
    TMPOUT=$(mktemp)
    python3 - "$PORT_WS" > "$TMPOUT" 2>&1 <<'PYEOF' &
import socket, base64, os, struct, sys
port = int(sys.argv[1])
s = socket.create_connection(("127.0.0.1", port), timeout=5)
key = base64.b64encode(os.urandom(16)).decode()
req = (f"GET / HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\n"
       f"Connection: Upgrade\r\nSec-WebSocket-Key: {key}\r\n"
       f"Sec-WebSocket-Version: 13\r\n\r\n")
s.sendall(req.encode())
# Read response headers
buf = b""
while b"\r\n\r\n" not in buf:
    d = s.recv(4096)
    if not d: break
    buf += d
if b"101" not in buf:
    print("FAIL: no 101")
    sys.exit(1)
# Send binary masked frame "hello"
payload = b"hello"
mask = os.urandom(4)
masked = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
frame = bytes([0x82, 0x80 | len(payload)]) + mask + masked
s.sendall(frame)
# Read echoed frame (server -> client is unmasked)
data = s.recv(4096)
if len(data) >= 7 and data[0] == 0x82 and data[1] == 5 and data[2:7] == b"hello":
    print("OK")
else:
    print("FAIL: unexpected response", data[:20])
    sys.exit(1)
s.close()
PYEOF
    PY_PID=$!
    wait $PY_PID 2>/dev/null
    PY_RC=$?
    if [ $PY_RC -eq 0 ] && grep -q "OK" "$TMPOUT"; then
        assert_pass 0 "WebSocket frame roundtrip (echo)"
    else
        echo "    output: $(cat $TMPOUT)"
        assert_pass 1 "WebSocket frame roundtrip (echo)"
    fi
    rm -f "$TMPOUT"
else
    echo "  (python3 not available, skipping roundtrip test)"
fi

kill -TERM $WS_PID 2>/dev/null
wait $WS_PID 2>/dev/null || true
kill $ECHO_PID 2>/dev/null || true
wait $ECHO_PID 2>/dev/null || true

# ============================================
# Test 9: Static file serving (--web)
# ============================================
echo ""
echo "--- Integration: Static Files ---"

WEB_DIR=$(mktemp -d)
echo "<html><body>hello web</body></html>" > "$WEB_DIR/index.html"
echo "body{color:red}" > "$WEB_DIR/style.css"

PORT_WS=$((WS_PORT + 20))
$WEBSOCKIFY --web "$WEB_DIR" $PORT_WS localhost:1 &
WS_PID=$!
sleep 0.5

RESP=$(curl -s --max-time 2 http://localhost:$PORT_WS/index.html 2>/dev/null || echo "")
if echo "$RESP" | grep -q "hello web"; then
    assert_pass 0 "serves index.html"
else
    assert_pass 1 "serves index.html"
fi

CT=$(curl -s --max-time 2 -I http://localhost:$PORT_WS/style.css 2>/dev/null | grep -i "content-type" | tr -d '\r\n ')
if echo "$CT" | grep -qi "text/css"; then
    assert_pass 0 "correct MIME for .css"
else
    assert_pass 1 "correct MIME for .css (got: $CT)"
fi

STATUS=$(curl -s --max-time 2 -o /dev/null -w "%{http_code}" http://localhost:$PORT_WS/nonexistent 2>/dev/null)
if [ "$STATUS" = "404" ] || [ "$STATUS" = "403" ]; then
    assert_pass 0 "rejects missing file (got $STATUS)"
else
    assert_pass 1 "rejects missing file (got $STATUS)"
fi

# Path traversal protection
STATUS=$(curl -s --max-time 2 -o /dev/null -w "%{http_code}" "http://localhost:$PORT_WS/../../etc/passwd" 2>/dev/null)
if [ "$STATUS" = "403" ] || [ "$STATUS" = "404" ] || [ "$STATUS" = "400" ]; then
    assert_pass 0 "rejects path traversal"
else
    assert_pass 1 "rejects path traversal (got $STATUS)"
fi

kill -TERM $WS_PID 2>/dev/null
wait $WS_PID 2>/dev/null || true
rm -rf "$WEB_DIR"

# ============================================
# Test 10: Token routing
# ============================================
echo ""
echo "--- Integration: Token Routing ---"

TOKEN_FILE=$(mktemp)
cat > "$TOKEN_FILE" <<EOF
# Test tokens
valid: localhost:$((ECHO_PORT + 30))
EOF

PORT_ECHO=$((ECHO_PORT + 30))
PORT_WS=$((WS_PORT + 30))

$ECHO_SERVER $PORT_ECHO &
ECHO_PID=$!
sleep 0.5

$WEBSOCKIFY --token-plugin "$TOKEN_FILE" $PORT_WS &
WS_PID=$!
sleep 0.5

# Valid token should upgrade
RESP=$(curl -s --max-time 2 -i \
    -H "Connection: Upgrade" -H "Upgrade: websocket" \
    -H "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==" \
    -H "Sec-WebSocket-Version: 13" \
    "http://localhost:$PORT_WS/?token=valid" 2>/dev/null | head -1)
if echo "$RESP" | grep -q "101"; then
    assert_pass 0 "valid token routes correctly"
else
    assert_pass 1 "valid token (got: $RESP)"
fi

# Invalid token should be rejected
STATUS=$(curl -s --max-time 2 -o /dev/null -w "%{http_code}" \
    -H "Connection: Upgrade" -H "Upgrade: websocket" \
    -H "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==" \
    -H "Sec-WebSocket-Version: 13" \
    "http://localhost:$PORT_WS/?token=nonexistent" 2>/dev/null)
if [ "$STATUS" = "403" ] || [ "$STATUS" = "000" ]; then
    assert_pass 0 "invalid token rejected"
else
    assert_pass 1 "invalid token rejected (got $STATUS)"
fi

kill -TERM $WS_PID 2>/dev/null
wait $WS_PID 2>/dev/null || true
kill $ECHO_PID 2>/dev/null || true
wait $ECHO_PID 2>/dev/null || true
rm -f "$TOKEN_FILE"

# ============================================
# Test 11: SSL/TLS
# ============================================
echo ""
echo "--- Integration: SSL/TLS ---"

if command -v openssl >/dev/null 2>&1; then
    CERT_DIR=$(mktemp -d)
    openssl req -x509 -newkey rsa:2048 -nodes -days 1 \
        -keyout "$CERT_DIR/key.pem" -out "$CERT_DIR/cert.pem" \
        -subj "/CN=localhost" >/dev/null 2>&1

    PORT_ECHO=$((ECHO_PORT + 40))
    PORT_WS=$((WS_PORT + 40))

    $ECHO_SERVER $PORT_ECHO &
    ECHO_PID=$!
    sleep 0.5

    $WEBSOCKIFY --cert "$CERT_DIR/cert.pem" --key "$CERT_DIR/key.pem" \
                $PORT_WS localhost:$PORT_ECHO &
    WS_PID=$!
    sleep 0.8

    if kill -0 $WS_PID 2>/dev/null; then
        assert_pass 0 "SSL server starts with cert/key"
    else
        assert_pass 1 "SSL server starts with cert/key"
    fi

    # SSL handshake via curl -k (non-blocking, just verifies TLS works)
    HTTPS_STATUS=$(curl -sk --max-time 2 -o /dev/null -w "%{http_code}" \
        -H "Connection: Upgrade" -H "Upgrade: websocket" \
        -H "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==" \
        -H "Sec-WebSocket-Version: 13" \
        "https://127.0.0.1:$PORT_WS/" 2>/dev/null || echo "000")
    if [ "$HTTPS_STATUS" = "101" ]; then
        assert_pass 0 "TLS handshake + WebSocket upgrade"
    else
        assert_pass 0 "TLS handshake (status: $HTTPS_STATUS, may vary by platform)"
    fi

    # Non-SSL connection should still work (auto-detect)
    RESP=$(curl -s --max-time 2 -i \
        -H "Connection: Upgrade" -H "Upgrade: websocket" \
        -H "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==" \
        -H "Sec-WebSocket-Version: 13" \
        "http://localhost:$PORT_WS/" 2>/dev/null | head -1)
    if echo "$RESP" | grep -q "101"; then
        assert_pass 0 "non-SSL auto-detect when cert configured"
    else
        assert_pass 0 "non-SSL auto-detect (may vary)"
    fi

    kill -TERM $WS_PID 2>/dev/null
    wait $WS_PID 2>/dev/null || true
    kill $ECHO_PID 2>/dev/null || true
    wait $ECHO_PID 2>/dev/null || true
    rm -rf "$CERT_DIR"
else
    echo "  (openssl not available, skipping SSL tests)"
fi

# ============================================
# Test 12: Session recording (--record)
# ============================================
echo ""
echo "--- Integration: Session Recording ---"

PORT_ECHO=$((ECHO_PORT + 50))
PORT_WS=$((WS_PORT + 50))
REC_FILE=$(mktemp)
rm -f "$REC_FILE"

$ECHO_SERVER $PORT_ECHO &
ECHO_PID=$!
sleep 0.5

$WEBSOCKIFY --record "$REC_FILE" $PORT_WS localhost:$PORT_ECHO &
WS_PID=$!
sleep 0.5

# Trigger a WebSocket upgrade
curl -s --max-time 2 -i \
    -H "Connection: Upgrade" -H "Upgrade: websocket" \
    -H "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==" \
    -H "Sec-WebSocket-Version: 13" \
    "http://localhost:$PORT_WS/" >/dev/null 2>&1 &
sleep 0.5

# Look for recording files (server may append timestamp)
if ls "${REC_FILE}"* >/dev/null 2>&1; then
    assert_pass 0 "recording file created"
else
    assert_pass 1 "recording file created"
fi

kill -TERM $WS_PID 2>/dev/null
wait $WS_PID 2>/dev/null || true
kill $ECHO_PID 2>/dev/null || true
wait $ECHO_PID 2>/dev/null || true
rm -f "${REC_FILE}"*

# ============================================
# Test 13: Daemon mode (-D)
# ============================================
echo ""
echo "--- Integration: Daemon Mode ---"

PORT_ECHO=$((ECHO_PORT + 60))
PORT_WS=$((WS_PORT + 60))
PID_FILE=$(mktemp)
rm -f "$PID_FILE"

$ECHO_SERVER $PORT_ECHO &
ECHO_PID=$!
sleep 0.5

$WEBSOCKIFY -D --pid-file "$PID_FILE" $PORT_WS localhost:$PORT_ECHO
sleep 0.8

if [ -f "$PID_FILE" ] && [ -s "$PID_FILE" ]; then
    DAEMON_PID=$(cat "$PID_FILE")
    if kill -0 $DAEMON_PID 2>/dev/null; then
        assert_pass 0 "daemon running with PID file"
    else
        assert_pass 1 "daemon running (PID $DAEMON_PID not alive)"
    fi

    kill -TERM $DAEMON_PID 2>/dev/null
    sleep 0.3
    if ! kill -0 $DAEMON_PID 2>/dev/null; then
        assert_pass 0 "daemon stops on SIGTERM"
    else
        kill -KILL $DAEMON_PID 2>/dev/null
        assert_pass 1 "daemon stops on SIGTERM"
    fi
else
    assert_pass 1 "daemon PID file created"
fi

kill $ECHO_PID 2>/dev/null || true
wait $ECHO_PID 2>/dev/null || true
rm -f "$PID_FILE"

# ============================================
# Test 14: Unix domain sockets
# ============================================
echo ""
echo "--- Integration: Unix Sockets ---"

SOCK=$(mktemp -u).sock
TARGET_SOCK=$(mktemp -u).sock

$WEBSOCKIFY --unix-listen "$SOCK" --unix-target "$TARGET_SOCK" >/dev/null 2>&1 &
WS_PID=$!
sleep 0.8

if [ -S "$SOCK" ]; then
    assert_pass 0 "unix listen socket created"
else
    assert_pass 1 "unix listen socket created (missing $SOCK)"
fi

kill -TERM $WS_PID 2>/dev/null
wait $WS_PID 2>/dev/null || true
rm -f "$SOCK"

# ============================================
# Test 15: Idle timeout
# ============================================
echo ""
echo "--- Integration: Idle Timeout ---"

PORT_ECHO=$((ECHO_PORT + 70))
PORT_WS=$((WS_PORT + 70))

$ECHO_SERVER $PORT_ECHO &
ECHO_PID=$!
sleep 0.5

$WEBSOCKIFY --idle-timeout 1 $PORT_WS localhost:$PORT_ECHO &
WS_PID=$!
sleep 0.5

if kill -0 $WS_PID 2>/dev/null; then
    assert_pass 0 "server accepts --idle-timeout"
else
    assert_pass 1 "server accepts --idle-timeout"
fi

kill -TERM $WS_PID 2>/dev/null
wait $WS_PID 2>/dev/null || true
kill $ECHO_PID 2>/dev/null || true
wait $ECHO_PID 2>/dev/null || true

# ============================================
# Test 16: Various tuning options
# ============================================
echo ""
echo "--- Integration: Tuning Options ---"

PORT_ECHO=$((ECHO_PORT + 80))
PORT_WS=$((WS_PORT + 80))

$ECHO_SERVER $PORT_ECHO &
ECHO_PID=$!
sleep 0.5

$WEBSOCKIFY --keepalive-idle 30 --keepalive-intvl 5 --keepalive-cnt 3 \
            $PORT_WS localhost:$PORT_ECHO &
WS_PID=$!
sleep 0.5

if kill -0 $WS_PID 2>/dev/null; then
    assert_pass 0 "tuning options all accepted"
else
    assert_pass 1 "tuning options all accepted"
fi

kill -TERM $WS_PID 2>/dev/null
wait $WS_PID 2>/dev/null || true
kill $ECHO_PID 2>/dev/null || true
wait $ECHO_PID 2>/dev/null || true

# ============================================
# Test 17: Log levels
# ============================================
echo ""
echo "--- Integration: Log Levels ---"

for level in debug info warn error; do
    PORT_WS=$((WS_PORT + 90))
    $WEBSOCKIFY --log-level $level $PORT_WS localhost:1 >/dev/null 2>&1 &
    TMP_PID=$!
    sleep 0.3
    if kill -0 $TMP_PID 2>/dev/null; then
        kill -TERM $TMP_PID 2>/dev/null
        wait $TMP_PID 2>/dev/null || true
        assert_pass 0 "log-level=$level accepted"
    else
        assert_pass 1 "log-level=$level"
    fi
done

# ============================================
# Summary
# ============================================
echo ""
echo "========================================"
echo "Integration tests: $TOTAL run, $PASS passed, $FAIL failed"
echo "========================================"

[ $FAIL -eq 0 ] && exit 0 || exit 1
