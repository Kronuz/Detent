#!/bin/sh
# Builds the transfer receiver's libFuzzer target under ASan and UBSan with brew LLVM and fuzzes it
# for FUZZ_SECONDS (default 1200) on FUZZ_WORKERS (default 3) forks: one CPU-hour. Exit 0: nothing
# found; a crash leaves its input in $JOB_OUT/artifacts.
#   tests/detent_testing/transfer_fuzz.sh
set -u
llvm=${LLVM_BIN:-/usr/local/opt/llvm/bin}
# The SDK the compiler itself picks: CMake drops a found include directory (zlib's SDK/usr/include)
# only when it equals the compiler's implicit one, and an -isystem SDK/usr/include ahead of libc++
# breaks libc++'s headers.
sdk=$("$llvm/clang++" -### -x c++ -c /dev/null 2>&1 | sed -n 's/.*"-isysroot" "\([^"]*\)".*/\1/p' | head -1)
test -n "$sdk" || sdk=$(xcrun --show-sdk-path)
build=${FUZZ_BUILD:-.scratch/build-transfer-fuzz}
out=${JOB_OUT:-.scratch/transfer-fuzz-out}
mkdir -p "$out/corpus" "$out/artifacts"
flags="-fsanitize=fuzzer-no-link,address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer"
cmake -S . -B "$build" -DCMAKE_BUILD_TYPE=RelWithDebInfo -DDETENT_BUILD_MANAGED_AGENT=ON \
    -DDETENT_BUILD_AUTHORITY=OFF -DDETENT_BUILD_RELAY=OFF -DDETENT_BUILD_BENCHMARKS=OFF \
    -DDETENT_BUILD_EXAMPLES=OFF -DCMAKE_C_COMPILER="$llvm/clang" -DCMAKE_CXX_COMPILER="$llvm/clang++" \
    -DCMAKE_OSX_SYSROOT="$sdk" -DDETENT_TRANSFER_FUZZ=ON "-DCMAKE_C_FLAGS=$flags" "-DCMAKE_CXX_FLAGS=$flags" \
    "-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined" > "$out/configure.log" || exit 126
cmake --build "$build" -j4 --target paged_transfer_fuzz > "$out/build.log" 2>&1 || {
    grep -m5 error "$out/build.log"
    exit 126
}
bin=$(find "$build" -type f -name paged_transfer_fuzz -perm -u+x | head -1)
echo "fuzzing $bin: ${FUZZ_WORKERS:-3} workers x ${FUZZ_SECONDS:-1200} s"
"$bin" -fork="${FUZZ_WORKERS:-3}" -max_total_time="${FUZZ_SECONDS:-1200}" -max_len=8192 -timeout=60 \
    -rss_limit_mb=4096 -artifact_prefix="$out/artifacts/" "$out/corpus" > "$out/fuzz.log" 2>&1
status=$?
tail -20 "$out/fuzz.log"
exit $status
