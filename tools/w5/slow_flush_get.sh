#!/bin/bash
# Runs the get bench (detent_authority_read_response_bench) with every flush slowed by MS
# milliseconds (slow_flush.c), as a loaded disk slows it: the bench must still validate.
#   slow_flush_get.sh BENCH [MS]   exits with the bench's status; macOS only.
set -uo pipefail
bench=${1:?bench binary}
ms=${2:-400}
here=$(cd "$(dirname "$0")" && pwd)
work=$(mktemp -d "${JOB_OUT:-${TMPDIR:-/tmp}}/slow-flush.XXXXXX")
trap 'rm -rf "$work"' EXIT
/usr/bin/clang -dynamiclib -O2 -o "$work/slow_flush.dylib" "$here/slow_flush.c" || exit 126
cd "$work" || exit 126
start=$(date +%s)
DYLD_INSERT_LIBRARIES=$work/slow_flush.dylib SLOW_FLUSH_MS=$ms "$bench" > bench.out 2> bench.err
status=$?
echo "flush delay ${ms} ms: exit $status after $(($(date +%s) - start)) s"
tail -3 bench.err
grep -c '"trial"' bench.out | sed 's/^/trials: /'
exit $status
