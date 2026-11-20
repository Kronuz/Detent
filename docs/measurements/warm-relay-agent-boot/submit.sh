#!/bin/bash
# usage: submit.sh smoke | seed | rows SMOKE_JOB SEED_JOB
# Queues this quest's jobs at its branch: the 10K smoke (strict), the 1M seed (light), and the
# timed rows (exclusive, at most 10 minutes each): 100K as one job of three interleaved rounds,
# 1M as one job per round, 5M as one job per arm and round, in round order.
set -eu
B='mkdir -p .scratch && cmake -S . -B .scratch/build-rel -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=/usr/bin/clang++ -DCMAKE_C_COMPILER=/usr/bin/clang -DCMAKE_OSX_SYSROOT= -DDETENT_BUILD_AUTHORITY=ON -DDETENT_BUILD_RELAY=ON -DDETENT_BUILD_MANAGED_AGENT=ON -DDETENT_BUILD_EXAMPLES=OFF -DDETENT_BUILD_BENCHMARKS=OFF -DDETENT_BUILD_TESTS=ON && cmake --build .scratch/build-rel -j4 --target detent-authority detent-agent detent-relay detent_system_mixed_oracle && git rev-parse HEAD >.scratch/build-rel/BUILT_COMMIT'
d=docs/measurements/warm-relay-agent-boot
q=(--quest warm-relay-agent-boot --build "$B")
row() { # SIZE ROUND AFTER FIXTURE MODE...
  local size=$1 round=$2 after=$3 fixture=$4; shift 4
  campaign job submit "${q[@]}" --for A1 --level exclusive --minutes 10 --after "$after" \
    --env SIZE="$size" --env ROUND="$round" --what "A1 $size round $round: $*" -- bash $d/row.sh "$fixture" "$@"
}
case $1 in
  smoke) campaign job submit "${q[@]}" --level strict --minutes 10 --what "smoke 10K warm, cold" -- \
    bash $d/row.sh 10000-main warm cold ;;
  seed) campaign job submit "${q[@]}" --level light --minutes 30 --what "seed 1M fixture at main" -- \
    bash $d/seed.sh 1000000 1000000-main ;;
  rows)
    smoke=$2 seed=$3
    row 100K 1-3 "$smoke" 100000-main warm cold cold warm warm cold
    row 1M 1 "$smoke,$seed" 1000000-main warm cold
    row 5M 1 "$smoke" 5000000-flat-main warm
    row 5M 1 "$smoke" 5000000-flat-main cold
    row 1M 2 "$smoke,$seed" 1000000-main cold warm
    row 5M 2 "$smoke" 5000000-flat-main cold
    row 5M 2 "$smoke" 5000000-flat-main warm
    row 1M 3 "$smoke,$seed" 1000000-main warm cold
    row 5M 3 "$smoke" 5000000-flat-main warm
    row 5M 3 "$smoke" 5000000-flat-main cold ;;
esac
