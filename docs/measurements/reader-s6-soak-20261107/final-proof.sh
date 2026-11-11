#!/bin/bash
set -euo pipefail
W=/Users/kronuz/code/Kronuz/Detent/.claude/worktrees/agent-a1fbf8344cbf48ecb
O=$W/.scratch/s6-final-proof
mkdir -p "$O"
cd "$W"
git diff --quiet 31ac047 -- src include tests benchmarks CMakeLists.txt || { echo "code changed; replan proof"; exit 90; }
printf 'head %s\n' "$(git rev-parse HEAD)"
git diff --quiet
run() {
  local label=$1; shift
  quiet check || true
  quiet light "s6-$label" 30 "Reader S6 final proof: $label" -- gtimeout -k 10 1750 nice -n 19 "$@" >"$O/$label.log" 2>&1
}
run release-build cmake --build .scratch/build --parallel 4 --target test_reader_core test_reader_switch test_reader_syscalls
run release-core .scratch/build/tests/detent_reader/test_reader_core
cp src/detent_local/prepared_reader.cc "$O/prepared-reader-original.cc"
trap 'cp "$O/prepared-reader-original.cc" "$W/src/detent_local/prepared_reader.cc"' EXIT
run fix2-revert /usr/local/bin/python3 .scratch/mutants.py .scratch/build .scratch/s6-final-fix2.json
cat "$O/fix2-revert.log"
grep -F 'KILLED rc=' "$O/fix2-revert.log"
run restored-core .scratch/build/tests/detent_reader/test_reader_core
run release-syscalls .scratch/build/tests/detent_reader/test_reader_syscalls
for round in $(seq 1 6); do
  run "forks-x4-$round" bash -c '
    W=$1; O=$2; round=$3
    pids=()
    for worker in 1 2 3 4; do
      (cd "$W/.scratch/build" && "$W/.scratch/build/tests/detent_reader/test_reader_switch" forks) >"$O/forks-$round-$worker.log" 2>&1 &
      pids+=("$!")
    done
    failed=0
    for pid in "${pids[@]}"; do wait "$pid" || failed=1; done
    exit "$failed"
  ' bash "$W" "$O" "$round"
done
for flavor in asan tsan; do
  run "$flavor-build" cmake --build ".scratch/build-$flavor" --parallel 4 --target test_reader_core test_reader_switch test_reader_syscalls
  run "$flavor-core" env ASAN_OPTIONS=detect_leaks=0:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 TSAN_OPTIONS='halt_on_error=1 second_deadlock_stack=1' ".scratch/build-$flavor/tests/detent_reader/test_reader_core"
done
touch "$O/preflight-passed"
printf 'release, revert, forks and sanitizer core complete\n'
