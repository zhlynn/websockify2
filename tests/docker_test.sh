#!/bin/sh
# Run cross-platform Docker tests
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

echo "=========================================="
echo "  Cross-platform Docker tests"
echo "=========================================="

run_test() {
    local name="$1"
    local dockerfile="$2"
    echo ""
    echo "--- Testing: $name ---"
    if docker build -f "$dockerfile" -t "websockify-test-$name" "$PROJECT_DIR"; then
        echo "  $name: PASS"
    else
        echo "  $name: FAIL"
        FAILED=1
    fi
}

FAILED=0

run_test "linux-gcc"    "$SCRIPT_DIR/Dockerfile.linux"
run_test "alpine-musl"  "$SCRIPT_DIR/Dockerfile.alpine"
run_test "freebsd-compat" "$SCRIPT_DIR/Dockerfile.freebsd-compat"

echo ""
echo "=========================================="
if [ "$FAILED" -eq 0 ]; then
    echo "  All Docker tests passed"
else
    echo "  Some Docker tests failed"
fi
echo "=========================================="

# Cleanup
docker rmi websockify-test-linux-gcc websockify-test-alpine-musl websockify-test-freebsd-compat 2>/dev/null || true

exit $FAILED
