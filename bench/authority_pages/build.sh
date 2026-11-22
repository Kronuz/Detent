#!/bin/bash
# Release build of the harness at the checkout's commit, into .scratch/build-ap. Run from the root.
set -eu
b=.scratch/build-ap
mkdir -p .scratch
cmake -S bench/authority_pages -B "$b" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=/usr/bin/clang++ \
  -DCMAKE_C_COMPILER=/usr/bin/clang -DCMAKE_OSX_SYSROOT= >"$b.configure.log" 2>&1 || { tail -40 "$b.configure.log"; exit 1; }
cmake --build "$b" -j4 --target authority_pages_bench
file "$b/authority_pages_bench"
git rev-parse HEAD >"$b/BUILT_COMMIT"
