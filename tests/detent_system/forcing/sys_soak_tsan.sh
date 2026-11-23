#!/bin/sh
# sys_soak.sh on a ThreadSanitizer build (CI's linux-tsan flags). Usage: sys_soak_tsan.sh RUNS JOBS TEST
set -u
here=$(dirname "$0")
build=.scratch/build-tsan
mkdir -p .scratch
[ -f "$build/CMakeCache.txt" ] || sh "$here/tsan_configure.sh" "$build" || exit 1
export TSAN_OPTIONS="halt_on_error=1 second_deadlock_stack=1"
exec sh "$here/sys_soak.sh" "$build" "$@"
