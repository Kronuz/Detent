#!/bin/bash
# Unattended CPU runs owed by S6 and S1/S2; every step is held by quiet and logged under logs/.
# Summary lines go to SUMMARY; each step runs even when an earlier one fails.
set -u
D=/Users/kronuz/code/Kronuz/Detent
S2=$D/.claude/worktrees/agent-af2c08dd24001ec58
S6=$D/.claude/worktrees/agent-a1fbf8344cbf48ecb
BEFORE=/Users/kronuz/code/Kronuz/Detent-wt/s2-before
TRIALS=$D/.scratch/capacity-trials
PY=/usr/local/bin/python3
O=$D/.scratch/overnight
L=$O/logs
mkdir -p "$L" "$O/coldboot"
S=$O/SUMMARY
note() { echo "$(date '+%F %T') $*" | tee -a "$S"; }
step() { # step NAME LOGFILE CMD... ; records rc
  local name=$1 log=$2; shift 2
  "$@" >"$L/$log" 2>&1; local rc=$?
  note "$name rc=$rc log=logs/$log"; return $rc
}

note "start S2=$(git -C $S2 rev-parse --short HEAD) S6=$(git -C $S6 rev-parse --short HEAD) before=$(git -C $BEFORE rev-parse --short HEAD)"

# 1. S6 flake: test_reader_switch alone, then four at once (the -j4 load that failed once).
step s6-build s6-build.log quiet light s6-build 60 "S6 rebuild" -- nice -n 19 ninja -C $S6/.scratch/build -j4
for i in $(seq 1 12); do
  step "s6-alone-$i" "s6-alone-$i.log" quiet light s6-soak 20 "S6 reader_switch alone" -- \
    nice -n 19 $S6/.scratch/build/tests/detent_reader/test_reader_switch
done
for i in $(seq 1 8); do
  quiet light s6-soak4 40 "S6 reader_switch x4" -- bash -c "
    for k in 1 2 3 4; do (cd $L && nice -n 19 $S6/.scratch/build/tests/detent_reader/test_reader_switch >s6-x4-$i-\$k.log 2>&1; echo \$? >s6-x4-$i-\$k.rc) & done; wait"
  note "s6-x4-$i rcs=$(cat $L/s6-x4-$i-*.rc | tr '\n' ' ')"
done

# 2. S2 build and tests (ledger Next 1 and 2).
step s2-build s2-build.log quiet light s2-build 90 "S2 full build" -- nice -n 19 ninja -C $S2/build-rel -j4
for t in test_local_paged_publication_artifacts test_local_paged_store_startup test_local_disk_store \
         test_local_paged_publication_faults test_local_publication test_local_paged_disk_store test_local_publication_power_cut; do
  step "s2-$t" "s2-$t.log" quiet light s2-test 60 "S2 $t" -- nice -n 19 $S2/build-rel/tests/detent_local/$t
done
step s2-ctest s2-ctest.log quiet light s2-ctest 180 "S2 ctest" -- nice -n 19 ctest --test-dir $S2/build-rel -j4 --output-on-failure

# 3. S2 sanitizers (ledger Next 4), separate builds.
for san in asan tsan; do
  case $san in asan) f="-fsanitize=address,undefined -fno-omit-frame-pointer" ;; tsan) f="-fsanitize=thread" ;; esac
  b=$S2/.scratch/build-$san
  step "s2-$san-configure" "s2-$san-configure.log" quiet light s2-$san 30 "S2 $san configure" -- \
    cmake -S $S2 -B $b -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_FLAGS="$f" \
      -DCMAKE_EXE_LINKER_FLAGS="$f" -DCMAKE_SHARED_LINKER_FLAGS="$f"
  step "s2-$san-build" "s2-$san-build.log" quiet light s2-$san 120 "S2 $san build" -- nice -n 19 ninja -C $b -j4 \
    test_local_publication_power_cut test_local_disk_store test_local_paged_publication_artifacts
  for t in test_local_publication_power_cut test_local_disk_store test_local_paged_publication_artifacts; do
    step "s2-$san-$t" "s2-$san-$t.log" quiet light s2-$san 90 "S2 $san $t" -- nice -n 19 $b/tests/detent_local/$t
  done
done

# 4. Cold-bootstrap timings (ledger Next 6): before = main plus counters, after = S2.
for b in $S2/build-rel $BEFORE/build-rel; do cp -R $D/build/tests/detent_authority/tls-fixture $b/tests/detent_authority/ 2>/dev/null; done
for b in $S2/build-rel $BEFORE/build-rel; do
  step "cb-targets-$(basename $(dirname $b))" "cb-targets-$(basename $(dirname $b)).log" quiet light s2-cbbuild 60 "coldboot targets" -- \
    nice -n 19 ninja -C $b -j4 detentd detent-agent detent-relay detent_system_mixed_oracle
done
cb() { # cb LABEL RECORDS SOURCE BUILD TREE MODE
  local out=$O/coldboot/$1
  rm -rf "$out"
  step "cb-$1" "cb-$1.log" quiet $6 cb-$1 60 "coldboot $1" -- $PY $S2/.scratch/cold-bootstrap-5m/coldboot.py \
    $3 "$out" $2 $4 $5 restart
}
for r in 1 2 3; do
  cb 100k-before-$r 100000 $TRIALS/100000-r3 $BEFORE/build-rel $BEFORE strict
  cb 100k-after-$r 100000 $TRIALS/100000-r3 $S2/build-rel $S2 strict
done
for r in 1 2; do
  cb 5m-before-$r 5000000 $TRIALS/5000000-flat-r1 $BEFORE/build-rel $BEFORE exclusive
  cb 5m-after-$r 5000000 $TRIALS/5000000-flat-r1 $S2/build-rel $S2 exclusive
done
grep -h "publication synced" $L/cb-*.log $O/coldboot/*/*.log 2>/dev/null | sort | uniq -c | tee -a "$S"

# 5. ext4 VM probe at 1445 files (ledger Next 7).
(cd $S2 && .scratch/vm.sh push .scratch/vm_probe.sh) >"$L/vm-push.log" 2>&1
(cd $S2 && QUIET_VM=65515 quiet strict s2-vm-ext4 40 "S2 ext4 grouped sync probe" -- .scratch/vm.sh run vm_probe.sh) >"$L/vm-probe.log" 2>&1
note "vm-probe rc=$? log=logs/vm-probe.log"

note "done"
