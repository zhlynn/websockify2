#!/usr/bin/env bash
# Usage: scripts/run_bench.sh <out_file> [rounds=3]
# Runs 3 scenarios × rounds, kills old procs between runs, saves RESULT lines.
set -eu
OUT=${1:-bench_result.txt}
ROUNDS=${2:-3}
HOST=127.0.0.1
WS_PORT=9000
UP_PORT=9001
DUR=5

ROOT=$(cd "$(dirname "$0")/.." && pwd)
WS=$ROOT/build/bin/websockify2
UP=$ROOT/build/bin/echo_upstream
BN=$ROOT/build/bin/bench_throughput

pkill -9 -f echo_upstream >/dev/null 2>&1 || true
pkill -9 -f websockify2   >/dev/null 2>&1 || true
sleep 1

# scenarios: msg_size conns
SCEN="4096:200 128:500 65536:100"

: > "$OUT"
echo "# bench run at $(date)" >> "$OUT"

for S in $SCEN; do
    SIZE=${S%%:*}; CONNS=${S##*:}
    for R in $(seq 1 "$ROUNDS"); do
        pkill -9 -f echo_upstream >/dev/null 2>&1 || true
        pkill -9 -f websockify2   >/dev/null 2>&1 || true
        sleep 1
        "$UP" $UP_PORT > /tmp/bench_echo.log 2>&1 &
        EPID=$!
        sleep 1
        "$WS" -L warn $WS_PORT $HOST:$UP_PORT > /tmp/bench_ws.log 2>&1 &
        WPID=$!
        sleep 1
        # Warmup 1s run at small load isn't needed; skip.
        echo "# scenario size=$SIZE conns=$CONNS round=$R" >> "$OUT"
        "$BN" $HOST $WS_PORT $SIZE $CONNS $DUR >> "$OUT" 2>&1
        kill $WPID $EPID 2>/dev/null || true
        wait 2>/dev/null || true
    done
done

pkill -9 -f echo_upstream >/dev/null 2>&1 || true
pkill -9 -f websockify2   >/dev/null 2>&1 || true
echo "done. results in $OUT"
