#!/bin/sh
# Build test_delivery_repair (Release, the failing job's flags) and run the scrub-worker entry RUNS times,
# printing each run's scrub-carry line and probe lines. Usage: scrub_probe.sh RUNS. Exits nonzero if any failed.
set -u
runs=$1
build=.scratch/scrub-build
mkdir -p .scratch
[ -f "$build/CMakeCache.txt" ] || cmake -S . -B "$build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=/usr/bin/clang++ \
  -DCMAKE_C_COMPILER=/usr/bin/clang -DCMAKE_OSX_SYSROOT= -DDETENT_BUILD_AUTHORITY=OFF -DDETENT_BUILD_RELAY=OFF \
  -DDETENT_BUILD_MANAGED_AGENT=ON -DDETENT_BUILD_EXAMPLES=OFF -DDETENT_BUILD_BENCHMARKS=OFF -DDETENT_BUILD_TESTS=ON \
  >"$build.configure.log" 2>&1 || { tail -40 "$build.configure.log"; exit 1; }
cmake --build "$build" -j4 --target test_delivery_repair >"$build.build.log" 2>&1 || { tail -60 "$build.build.log"; exit 1; }
fail=0
for i in $(seq 1 "$runs"); do
  if ctest --test-dir "$build" -R '^test_delivery_repair_scrub-worker$' -V --timeout 300 >"$build.run.log" 2>&1; then r=pass; else r=FAIL; fail=$((fail+1)); fi
  echo "--- run $i $r"; grep -E 'probe-|scrub-carry|invariant' "$build.run.log" | sed 's/^[0-9]*: //'
done
echo "RESULT scrub-worker: $fail of $runs failed"
[ "$fail" -eq 0 ]
