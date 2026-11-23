#!/bin/bash
# usage: submit.sh smoke | seed | probe | rows PROBE_100K PROBE_5M
# Queues this quest's jobs at its branch: the 10K smoke (strict), the 1M seed (light), the light
# phase probes (100K warm+cold, 5M warm), and the timed rows (exclusive, one mode per job, at most
# 10 minutes each): 100K and 1M as three interleaved rounds, 5M as one round (D85).
set -eu
B='mkdir -p .scratch && cmake -S . -B .scratch/build-rel -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=/usr/bin/clang++ -DCMAKE_C_COMPILER=/usr/bin/clang -DCMAKE_OSX_SYSROOT= -DDETENT_BUILD_AUTHORITY=ON -DDETENT_BUILD_RELAY=ON -DDETENT_BUILD_MANAGED_AGENT=ON -DDETENT_BUILD_EXAMPLES=OFF -DDETENT_BUILD_BENCHMARKS=OFF -DDETENT_BUILD_TESTS=ON && cmake --build .scratch/build-rel -j4 --target detent-authority detent-agent detent-relay detent_system_mixed_oracle && git rev-parse HEAD >.scratch/build-rel/BUILT_COMMIT'
d=docs/measurements/warm-relay-agent-boot
q=(--quest warm-relay-agent-boot --build "$B")
row() { # SIZE ROUND AFTER FIXTURE MODE
  local size=$1 round=$2 after=$3 fixture=$4 mode=$5
  campaign job submit "${q[@]}" --for A1 --level exclusive --minutes 10 --after "$after" \
    --env SIZE="$size" --env ROUND="$round" --what "A1 $size round $round: $mode" -- bash $d/row.sh "$fixture" "$mode"
}
case $1 in
  smoke) campaign job submit "${q[@]}" --level strict --minutes 10 --what "smoke 10K warm, cold" -- \
    bash $d/row.sh 10000-main warm cold ;;
  seed) campaign job submit "${q[@]}" --level light --minutes 30 --what "seed 1M fixture at main" -- \
    bash $d/seed.sh 1000000 1000000-main ;;
  probe)
    campaign job submit "${q[@]}" --level light --minutes 10 --what "probe 100K warm, cold (phase times)" -- \
      bash $d/row.sh 100000-main warm cold
    campaign job submit "${q[@]}" --level light --minutes 15 --what "probe 5M warm (phase times)" -- \
      bash $d/row.sh 5000000-flat-main warm ;;
  rows)
    small=$2 large=$3
    for round in 1 2 3; do
      case $round in 2) order="cold warm" ;; *) order="warm cold" ;; esac
      for mode in $order; do row 100K $round "$small" 100000-main $mode; done
      for mode in $order; do row 1M $round "$small" 1000000-main $mode; done
    done
    row 5M 1 "$large" 5000000-flat-main warm
    row 5M 1 "$large" 5000000-flat-main cold ;;
esac
