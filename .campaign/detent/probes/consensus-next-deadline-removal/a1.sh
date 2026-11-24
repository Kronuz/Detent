#!/bin/bash
# A1, A2: core_deadline_test (ASan+UBSan Debug, CI's flags) in a consensus checkout (the cwd).
set -u
src="$(pwd)"; OUT="${JOB_OUT:-$src/.probe-out}"; B="$OUT/build-asan"; mkdir -p "$OUT"
san="-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer"
[ -f "$B/build.ninja" ] || cmake -S "$src" -B "$B" -G Ninja -DCMAKE_BUILD_TYPE=Debug "-DCMAKE_CXX_FLAGS=$san" "-DCMAKE_EXE_LINKER_FLAGS=$san" > "$OUT/configure.log" 2>&1 || { tail -20 "$OUT/configure.log"; exit 2; }
ninja -C "$B" consensus_core_deadline_test > "$OUT/build.log" 2>&1 || { tail -30 "$OUT/build.log"; exit 2; }
"$B/consensus_core_deadline_test"
