#!/bin/bash
# usage: retirement-wakeup.sh [REF [ROUNDS]]     (run from the checkout root, in a light job)
# Builds retirement_wakeup.cc against REF's (default main) retirement.cc and headers, Debug and -O2, and
# runs each for ROUNDS rounds. Exits 1 when a drain stalls with no resident (the lost wakeup), 0 when
# every round of both builds drains, 125 on a build error.
set -u
ref=${1:-main} rounds=${2:-300000}
here=$(cd "$(dirname "$0")" && pwd)
out=$PWD/.scratch/retirement-wakeup
rm -rf "$out"; mkdir -p "$out/src"
git archive "$ref" include src/detent_distribution/retirement.cc | tar -x -C "$out/src" || exit 125
echo "retirement.cc at $ref: $(git rev-parse --short "$ref"), $(git rev-parse "$ref:src/detent_distribution/retirement.cc")"
status=0
for flags in "-O0 -g" "-O2 -g"; do
  bin=$out/probe${flags// /}
  /usr/bin/clang++ -std=c++20 $flags -pthread -I"$out/src/include" "$here/retirement_wakeup.cc" \
    "$out/src/src/detent_distribution/retirement.cc" -o "$bin" || exit 125
  start=$(date +%s)
  "$bin" "$rounds"
  rc=$?
  echo "$flags: rc=$rc seconds=$(($(date +%s) - start))"
  [ "$rc" -eq 0 ] || status=1
done
exit $status
