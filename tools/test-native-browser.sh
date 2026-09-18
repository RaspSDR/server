#!/bin/sh

set -eu

build_dir=${WEBSDR_HARNESS_BUILD_DIR:-build-native}
port=${WEBSDR_HARNESS_PORT:-8073}
log_file=$(mktemp)
server_pid=

cleanup()
{
    if [ -n "$server_pid" ] && kill -0 "$server_pid" 2>/dev/null; then
        kill "$server_pid"
        wait "$server_pid" 2>/dev/null || true
    fi
    rm -f "$log_file"
}

trap cleanup EXIT INT TERM

cmake -S . -B "$build_dir" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DNATIVE_HARNESS=ON \
    -DENABLE_HDFL=OFF
cmake --build "$build_dir" --parallel "${BUILD_JOBS:-$(nproc)}"
ctest --test-dir "$build_dir" --output-on-failure

WEBSDR_HARNESS_PORT=$port "$build_dir/websdr.bin" >"$log_file" 2>&1 &
server_pid=$!

attempt=0
while ! curl --fail --silent --max-time 2 "http://127.0.0.1:$port/status" >/dev/null; do
    if ! kill -0 "$server_pid" 2>/dev/null; then
        cat "$log_file"
        exit 1
    fi
    attempt=$((attempt + 1))
    if [ "$attempt" -ge 30 ]; then
        cat "$log_file"
        exit 1
    fi
    sleep 1
done

WEBSDR_HARNESS_URL="http://127.0.0.1:$port/" node tests/native_browser_smoke.js
