#!/bin/bash
# Runs `test_delivery_hash_once MODE` RUNS times, PARALLEL at once, and prints each failure's message.
# Exits 0 only when every run passes. usage: hash_once_stress.sh MODE RUNS PARALLEL [BUILD]
set -uo pipefail
mode=${1:?mode} runs=${2:?runs} parallel=${3:?parallel} build=${4:-.scratch/build}
tests/detent_delivery/hash_build.sh "$build" test_delivery_hash_once > /dev/null || exit 3
ctest --test-dir "$build" -R delivery_tls_certificates > /dev/null || exit 3
dir=$build/tests/detent_delivery
out=$(mktemp -d "$build/stress.XXXXXX")
seq "$runs" | xargs -P "$parallel" -I{} sh -c \
    "timeout 240 '$dir/test_delivery_hash_once' '$dir/tls-fixture' '$mode' > '$out/{}.log' 2>&1 || echo {} >> '$out/failed'"
failed=$(test -f "$out/failed" && wc -l < "$out/failed" | tr -d ' ' || echo 0)
echo "stress $mode: $failed of $runs runs failed ($parallel at once)"
test "$failed" -eq 0 && exit 0
for n in $(sort -n "$out/failed"); do grep -h 'test_delivery_hash_once:' "$out/$n.log" || tail -2 "$out/$n.log"; done | sort | uniq -c
exit 1
