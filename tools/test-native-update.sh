#!/bin/sh
set -eu

build_dir=${WEBSDR_HARNESS_BUILD_DIR:-build-native}
cmake -S . -B "$build_dir" -DCMAKE_BUILD_TYPE=Debug -DNATIVE_HARNESS=ON -DENABLE_HDFL=OFF
cmake --build "$build_dir" --target websdr.bin --parallel "${BUILD_JOBS:-$(nproc)}"
ctest --test-dir "$build_dir" --output-on-failure -R '^native_update$'
