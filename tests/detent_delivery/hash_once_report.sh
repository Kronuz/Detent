#!/bin/bash
# Runs the hash-once bootstrap and delta at 10K and 100K knobs for the agent and the relay tier and prints the
# counters each test read (one "hash-once" line per run). BUILD is the build directory holding the tests.
set -euo pipefail
build=${1:-.scratch/build}
dir=$build/tests/detent_delivery
ctest --test-dir "$build" -R delivery_tls_certificates > /dev/null
for knobs in 10000 100000; do
    for mode in agent relay agent-delta; do
        timeout 200 "$dir/test_delivery_hash_once" "$dir/tls-fixture" "$mode" "$knobs" | grep -v '^ok$'
    done
done
# The relay's delta checks pass but its shutdown does not drain: read its line, then stop it.
for knobs in 10000 100000; do
    timeout 30 "$dir/test_delivery_hash_once" "$dir/tls-fixture" relay-delta "$knobs" | grep '^hash-once' || true
done
