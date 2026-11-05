#!/bin/bash
# Builds the live record fuzzer with Homebrew LLVM's libFuzzer, ASan and UBSan, from a configured and built
# build directory (for the dependency sources and libsha256). Usage: build_live_fuzz.sh BUILD_DIR OUT
set -euo pipefail
here="$(cd "$(dirname "$0")/../.." && pwd)"
build="${1:?build directory}"
out="${2:?output binary}"
clang="${CLANG:-/usr/local/opt/llvm/bin/clang++}"
includes=("-I$here/src/detent_local" "-I$here/include" "-I$build/_deps/sha256-src")
for dir in "$build"/_deps/*-src; do
  includes+=("-I$dir")
  [ -d "$dir/include" ] && includes+=("-I$dir/include")
done
"$clang" -std=c++20 -O1 -g -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=undefined \
  "${includes[@]}" \
  "$here/tests/detent_testing/live_record_fuzz.cc" "$here/src/detent_local/live_record.cc" \
  "$here/src/detent_local/layout.cc" "$build/_deps/sha256-build/libsha256.a" -o "$out"
