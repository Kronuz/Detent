#!/bin/bash
# usage: build.sh BUILD_DIR   (run from the checkout root, in a light job on a named slot)
# Release build of the binaries the cold-bootstrap harness runs, and a stamp naming the code built.
set -eu
build=$1
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=/usr/bin/clang++ -DCMAKE_C_COMPILER=/usr/bin/clang -DCMAKE_OSX_SYSROOT= \
  -DDETENT_BUILD_AUTHORITY=ON -DDETENT_BUILD_RELAY=ON -DDETENT_BUILD_MANAGED_AGENT=ON \
  -DDETENT_BUILD_EXAMPLES=OFF -DDETENT_BUILD_BENCHMARKS=OFF -DDETENT_BUILD_TESTS=ON
cmake --build "$build" -j4 --target detentd detent-agent detent-relay detent_system_mixed_oracle
git rev-parse HEAD >"$build/BUILT_COMMIT"
file "$build/src/detentd" "$build/src/detent-agent" "$build/src/detent-relay" \
  "$build/tests/detent_system/detent_system_mixed_oracle"
