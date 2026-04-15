#!/usr/bin/env bash
# Runs baseline + optimized bench inside a Linux container.
# Assumes the working tree currently holds the OPTIMIZED version of every
# file listed in OPT_FILES. Stashes them to produce the baseline build, then
# restores for the optimized build. configure.sh is rerun for each build so
# the baseline picks up the committed (conservative) CFLAGS and the optimized
# build picks up the aggressive CFLAGS.
set -eu
cd /src

apt-get update -qq >/dev/null 2>&1 || true
apt-get install -y -qq libssl-dev git >/dev/null 2>&1 || true
apk add --quiet build-base openssl-dev git 2>/dev/null || true

OPT_FILES=(
    configure.sh
    src/proxy.c
    src/buf.h
    src/buf.c
    src/net.h
    src/net.c
)

# --- stash optimized versions ---
mkdir -p /tmp/wsopt
for f in "${OPT_FILES[@]}"; do
    mkdir -p /tmp/wsopt/$(dirname "$f")
    cp "$f" "/tmp/wsopt/$f"
done

# --- baseline: revert all optimized files to committed HEAD ---
git checkout -- "${OPT_FILES[@]}"
sh configure.sh >/dev/null
make clean >/dev/null
make -j"$(nproc)" >/dev/null 2>&1
make bench-build -j"$(nproc)" >/dev/null 2>&1
scripts/run_bench.sh /tmp/linux_baseline.txt 3

# --- optimized: restore opt sources ---
for f in "${OPT_FILES[@]}"; do
    cp "/tmp/wsopt/$f" "$f"
done
sh configure.sh >/dev/null
make clean >/dev/null
make -j"$(nproc)" >/dev/null 2>&1
make bench-build -j"$(nproc)" >/dev/null 2>&1
scripts/run_bench.sh /tmp/linux_optimized.txt 3

mkdir -p docs/bench
cp /tmp/linux_baseline.txt  docs/bench/linux_baseline.txt
cp /tmp/linux_optimized.txt docs/bench/linux_optimized.txt
echo "=== baseline ==="
grep RESULT docs/bench/linux_baseline.txt
echo "=== optimized ==="
grep RESULT docs/bench/linux_optimized.txt
