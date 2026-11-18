#!/bin/sh
# Build one test target under TSan (CI's linux-tsan flags) and run its ctest entry RUNS times.
# Usage: tsan_loop.sh TARGET RUNS [CTEST_NAME]. Exits nonzero if any run failed.
set -u
here=$(dirname "$0")
target=$1 runs=$2 name=${3:-$1}
build=.scratch/build-tsan
mkdir -p .scratch
[ -f "$build/CMakeCache.txt" ] || sh "$here/tsan_configure.sh" "$build" || exit 1
cmake --build "$build" -j4 --target "$target" >"$build.build.log" 2>&1 || { tail -60 "$build.build.log"; exit 1; }
export TSAN_OPTIONS="halt_on_error=1 second_deadlock_stack=1"
fail=0
for i in $(seq 1 "$runs"); do
  ctest --test-dir "$build" -R "^$name\$" --output-on-failure --timeout 600 >"$build.run.log" 2>&1 \
    || { fail=$((fail+1)); [ "$fail" -le 3 ] && { echo "--- run $i failed"; tail -30 "$build.run.log"; }; }
done
echo "RESULT $name: $fail of $runs failed (TSan)"
[ "$fail" -eq 0 ]
