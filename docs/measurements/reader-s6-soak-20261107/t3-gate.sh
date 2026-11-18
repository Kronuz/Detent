#!/bin/bash
set -euo pipefail
W=/Users/kronuz/code/Kronuz/Detent/.claude/worktrees/agent-a1fbf8344cbf48ecb
O=$W/.scratch/s6-final-proof
cd "$W"
[ -e "$O/preflight-passed" ] || { echo 'proof preflight did not finish'; exit 90; }
git diff --quiet 31ac047 -- src include tests benchmarks CMakeLists.txt || { echo "code changed; replan gate"; exit 90; }
for flavor in asan tsan; do
  for iteration in $(seq 1 20); do
    quiet check || true
    quiet light "s6-t3-$flavor-$iteration" 30 "S6 T3 default sanitizer gate: $flavor $iteration/20" -- gtimeout -k 10 1750 nice -n 19 env ASAN_OPTIONS=detect_leaks=0:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 TSAN_OPTIONS='halt_on_error=1 second_deadlock_stack=1' ctest --test-dir ".scratch/build-$flavor" -R '^test_reader_switch$' --output-on-failure >"$O/t3-$flavor-$iteration.log" 2>&1
    printf '%s T3 %s/20 passed\n' "$flavor" "$iteration"
  done
done
