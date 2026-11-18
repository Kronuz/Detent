#!/bin/bash
# Compile every source with the same sanitizer flavor; no prebuilt libraries are linked.
set -euo pipefail
here="$(cd "$(dirname "$0")/../.." && pwd)"
build="${1:?configured build directory with its own dependency sources}"
out="${2:?output binary}"
clang="${CLANG:-/usr/local/opt/llvm/bin/clang++}"
includes=("-I$here/src/detent_local" "-I$here/include")
for dir in "$build"/_deps/*-src; do
  includes+=("-I$dir")
  [ ! -d "$dir/include" ] || includes+=("-I$dir/include")
done
"$clang" -std=c++20 -O1 -g -DSHA256_NO_HARDWARE -fsanitize=fuzzer,address,undefined \
  -fno-sanitize-recover=all -fno-omit-frame-pointer "${includes[@]}" \
  "$here/tests/detent_local/paged_codec_fuzz.cc" "$here/src/detent_local/paged_layout.cc" \
  "$here/src/detent_local/layout.cc" "$build/_deps/sha256-src/sha256.cc" -o "$out"
