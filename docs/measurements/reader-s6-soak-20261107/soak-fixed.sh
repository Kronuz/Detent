#!/bin/bash
# test_reader_switch soak at reader/s6 Detent:47470ea or later (the unlinked-selected retry), alone and four at once.
B=/Users/kronuz/code/Kronuz/Detent/.claude/worktrees/agent-a1fbf8344cbf48ecb/.scratch/build
T=$B/tests/detent_reader/test_reader_switch
W=/Users/kronuz/code/Kronuz/Detent/.scratch/overnight/done/detent-s6-soak-fixed
mkdir -p $W
echo "head $(git -C $B/../.. rev-parse --short HEAD)"
quiet light s6fix-build 60 "S6 rebuild" -- nice -n 19 make -C $B -j4 test_reader_switch >$W/build.log 2>&1 || { echo "build failed"; exit 1; }
f=0
for i in $(seq 1 12); do
  quiet light s6fix-soak 20 "S6 reader_switch alone" -- nice -n 19 $T >$W/alone-$i.log 2>&1 || { f=$((f+1)); echo "alone-$i FAIL: $(grep -m1 'refresh status\|FAIL' $W/alone-$i.log)"; }
done
echo "alone failures: $f of 12"
g=0
for i in $(seq 1 6); do
  quiet light s6fix-soak4 40 "S6 reader_switch x4" -- bash -c "for k in 1 2 3 4; do (nice -n 19 $T >$W/x4-$i-\$k.log 2>&1; echo \$? >$W/x4-$i-\$k.rc) & done; wait"
  for k in 1 2 3 4; do [ "$(cat $W/x4-$i-$k.rc)" = 0 ] || g=$((g+1)); done
done
echo "x4 failures: $g of 24"
[ $f -eq 0 ] && [ $g -eq 0 ]
