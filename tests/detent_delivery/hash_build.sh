#!/bin/bash
# Configures BUILD (as the native check does) when it has no cache yet and builds TARGETS in it.
# usage: hash_build.sh BUILD TARGET...
set -euo pipefail
build=${1:?build directory}
shift
test "$#" -gt 0 || { echo "usage: $0 BUILD TARGET..."; exit 2; }
if ! test -f "$build/CMakeCache.txt"; then
    cmake -S . -B "$build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=/usr/bin/clang++ \
        -DCMAKE_C_COMPILER=/usr/bin/clang -DCMAKE_OSX_SYSROOT= -DDETENT_BUILD_AUTHORITY=ON -DDETENT_BUILD_RELAY=ON \
        -DDETENT_BUILD_MANAGED_AGENT=ON -DDETENT_BUILD_EXAMPLES=OFF -DDETENT_BUILD_BENCHMARKS=OFF \
        -DDETENT_BUILD_TESTS=ON > /dev/null
fi
cmake --build "$build" -j4 --target "$@"
