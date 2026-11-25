#!/bin/bash
# test_reader_switch x4 soak at reader/s6 Detent:b1d281c (fork drain fix); the x4-1-4 fork-reply timeout at 3d8559a was never retested under load.
set -u
S6=/Users/kronuz/code/Kronuz/Detent/.claude/worktrees/agent-a1fbf8344cbf48ecb
B=$S6/.scratch/build
T=$B/tests/detent_reader/test_reader_switch
W=/Users/kronuz/code/Kronuz/Detent/.scratch/overnight/done/detent-s6-soak-x4-forkfix
mkdir -p $W
head=$(git -C $S6 rev-parse --short HEAD)
echo "head $head"
git -C $S6 merge-base --is-ancestor b1d281c HEAD || { echo "head lacks b1d281c"; exit 90; }
quiet light s6fork-build 30 "S6 rebuild test_reader_switch" -- nice -n 19 gtimeout -k 15 28m make -C $B -j4 test_reader_switch >$W/build.log 2>&1 || { echo "build failed"; exit 1; }
g=0
for i in $(seq 1 8); do
  quiet light s6fork-soak4 30 "S6 reader_switch x4 $i/8" -- bash -c "for k in 1 2 3 4; do (nice -n 19 gtimeout -k 15 27m $T >$W/x4-$i-\$k.log 2>&1; echo \$? >$W/x4-$i-\$k.rc) & done; wait"
  for k in 1 2 3 4; do
    rc=$(cat $W/x4-$i-$k.rc 2>/dev/null || echo missing)
    [ "$rc" = 0 ] || { g=$((g+1)); echo "x4-$i-$k rc=$rc: $(grep -m1 'invariant\|fork reply\|FAIL' $W/x4-$i-$k.log)"; }
  done
  echo "round $i done, failures so far $g"
done
echo "x4 failures: $g of 32"
[ $g -eq 0 ]
