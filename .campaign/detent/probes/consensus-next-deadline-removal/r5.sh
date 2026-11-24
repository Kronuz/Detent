#!/bin/bash
# R5 probe, in a consensus checkout: builds r5_pressure.cc against test/worker_test.cc (Debug).
set -u
src="$(pwd)"; mine="$(cd "$(dirname "$0")" && pwd)"; OUT="${JOB_OUT:-$src/.probe-out}"; B="$OUT/build-debug"; mkdir -p "$OUT"
[ -f "$B/build.ninja" ] || cmake -S "$src" -B "$B" -G Ninja -DCMAKE_BUILD_TYPE=Debug > "$OUT/configure.log" 2>&1 || { tail -20 "$OUT/configure.log"; exit 2; }
ninja -C "$B" consensus_worker_test > "$OUT/build.log" 2>&1 || { tail -20 "$OUT/build.log"; exit 2; }
compile=$(ninja -C "$B" -t commands consensus_worker_test | grep -- '-c .*worker_test.cc' | sed 's/ -MD .*//; s/ -o [^ ]*//; s/ -c [^ ]*//')
link=$(ninja -C "$B" -t commands consensus_worker_test | tail -1)
(cd "$B" && $compile -I"$src/test" -c "$mine/r5_pressure.cc" -o "$OUT/r5.o") || exit 2
link=${link/CMakeFiles\/consensus_worker_test.dir\/test\/worker_test.cc.o/$OUT/r5.o}
(cd "$B" && eval "${link/-o consensus_worker_test/-o $OUT/r5}") || exit 2
cd "$OUT" && ./r5
