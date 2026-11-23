#!/bin/bash
# Runs the hash-once bootstrap and delta at 10K and 100K knobs for the agent and the relay tier and prints the
# counters each test read (one "hash-once" line per run). BUILD is the build directory holding the tests.
set -euo pipefail
build=${1:-.scratch/build}
dir=$build/tests/detent_delivery
tests/detent_delivery/hash_build.sh "$build" test_delivery_hash_once > /dev/null
ctest --test-dir "$build" -R delivery_tls_certificates > /dev/null
for knobs in 10000 100000; do
    for mode in agent relay agent-delta; do
        timeout 200 "$dir/test_delivery_hash_once" "$dir/tls-fixture" "$mode" "$knobs" | grep -v '^ok$'
    done
done
# relay-delta's shutdown does not drain (pre-existing, quest relay-delta-shutdown-hang), so its exit 124 is that
# hang; its line is checked here: page and pack bytes hashed equal those built. Any other exit fails.
for knobs in 10000 100000; do
    set +e
    out=$(timeout 30 "$dir/test_delivery_hash_once" "$dir/tls-fixture" relay-delta "$knobs" 2>&1)
    status=$?
    set -e
    line=$(grep '^hash-once relay-delta' <<<"$out" || true)
    echo "$line"
    case $status in
    0) ;;
    124) echo "relay-delta knobs=$knobs: exit 124, the known shutdown hang (relay-delta-shutdown-hang)" ;;
    *) echo "$out"; echo "relay-delta knobs=$knobs: FAILED, exit $status"; exit 1 ;;
    esac
    field() { sed -n "s/.* $1=\([0-9]*\).*/\1/p" <<<"$line"; }
    if test -z "$line" || test "$(field hashed_page_bytes)" != "$(field built_page_bytes)" ||
        test "$(field hashed_pack_bytes)" != "$(field built_pack_bytes)"; then
        echo "relay-delta knobs=$knobs: FAILED, hashed page or pack bytes differ from built"
        exit 1
    fi
done
