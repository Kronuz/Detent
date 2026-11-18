#!/bin/sh
# Build one test target (RelWithDebInfo, the native suite's flags) and run its ctest entry RUNS times,
# JOBS copies at once (load). Usage: loop.sh TARGET RUNS JOBS [CTEST_NAME]. Exits nonzero if any failed.
set -u
target=$1 runs=$2 par=$3 name=${4:-$1}
build=.scratch/build-loop
mkdir -p .scratch
[ -f "$build/CMakeCache.txt" ] || cmake -S . -B "$build" -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DDETENT_BUILD_AUTHORITY=ON -DDETENT_BUILD_RELAY=ON -DDETENT_BUILD_MANAGED_AGENT=ON \
  -DDETENT_BUILD_BENCHMARKS=OFF -DDETENT_BUILD_EXAMPLES=OFF -DDETENT_BUILD_TESTS=ON >"$build.configure.log" 2>&1 \
  || { tail -40 "$build.configure.log"; exit 1; }
cmake --build "$build" -j4 --target "$target" >"$build.build.log" 2>&1 || { tail -60 "$build.build.log"; exit 1; }
fail=0 done_=0
while [ "$done_" -lt "$runs" ]; do
  n=$par; [ $((runs - done_)) -lt "$n" ] && n=$((runs - done_))
  pids=""
  for k in $(seq 1 "$n"); do
    (ctest --test-dir "$build" -R "^$name\$" --output-on-failure --timeout 600 >"$build.run.$k.log" 2>&1) & pids="$pids $!"
  done
  k=0
  for p in $pids; do
    k=$((k+1))
    wait "$p" || { fail=$((fail+1)); [ "$fail" -le 3 ] && { echo "--- a run failed"; tail -40 "$build.run.$k.log"; }; }
  done
  done_=$((done_ + n))
done
echo "RESULT $name: $fail of $runs failed ($par at once)"
[ "$fail" -eq 0 ]
