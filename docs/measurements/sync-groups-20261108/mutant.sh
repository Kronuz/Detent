#!/bin/bash
# usage: mutant.sh positive|MUTANT     (run from the checkout root, in a job)
# positive: Debug build and run of the power-cut and startup tests; the job passes when both pass.
# MUTANT: apply one source edit, rebuild, run the tests named for it, and report each test's status.
# The job passes (exit 0) only when the mutant is killed, i.e. every test named for it fails;
# a survivor prints SURVIVED and exits 1; a configure or build failure exits 125 (not evidence).
# A test that hangs (T7's watchdog exits 124; the per-test bound of $limit s raises SIGALRM, 142) is
# neither a kill nor a pass: the mutant job prints HUNG and exits 125, the positive job fails.
set -u
name=$1
limit=300
build=.scratch/build-dbg
targets=(test_local_publication_power_cut test_local_paged_store_startup test_local_disk_store)
restore() { git checkout -q -- src tests; }
trap restore EXIT
restore
# Always configure: a build directory kept on the slot must not keep an older option set.
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_COMPILER=/usr/bin/clang++ -DCMAKE_C_COMPILER=/usr/bin/clang -DCMAKE_OSX_SYSROOT= \
  -DDETENT_BUILD_AUTHORITY=OFF -DDETENT_BUILD_RELAY=OFF -DDETENT_BUILD_MANAGED_AGENT=ON \
  -DDETENT_BUILD_EXAMPLES=OFF -DDETENT_BUILD_BENCHMARKS=OFF -DDETENT_BUILD_TESTS=ON || exit 125
if [ "$name" = targets ]; then
  ninja -C "$build" -t targets all | grep -E '^test_local_(disk_store|paged_store_startup|publication_power_cut): phony$' | tee /dev/stderr | grep -c . | grep -qx 3
  exit $?
fi
if [ "$name" = d1-revert ]; then
  # Review D1: the census barrier as of Detent:b3fef50 (a bare DirectorySync; storage.meta unsynced).
  git show b3fef5099a0512c31b1ccee1b56e45ccb8b9daf7:src/detent_local/disk_store.cc >src/detent_local/disk_store.cc || exit 125
  git --no-pager diff --stat
elif [ "$name" != positive ]; then
  /usr/local/bin/python3 - "$name" <<'PY' || exit 125
import sys
from pathlib import Path

mutant = sys.argv[1]
disk = Path("src/detent_local/disk_store.cc")
cut = Path("tests/detent_local/test_publication_power_cut.cc")
edits = {
    # The lifecycle fix Detent:0cd1527 reverted: counters read through a released session.
    "closed-session": (
        disk,
        "core.session ? core.session->sync_counters() : core.closed_sync",
        "core.session->sync_counters()",
    ),
    # The second group (payloads) syncs nothing: the payload barrier is gone.
    "empty-payload-group": (
        disk,
        "request_.files = staged_;",
        "request_.files = group_end_ == child_count() ? decltype(request_.files){} : decltype(request_.files)(staged_);",
    ),
    # The descriptor is renamed over selected without its fsync (rename before fsync).
    "unsynced-descriptor": (
        disk,
        "request_.files = staged_;",
        "request_.files = index_ == child_count() ? std::span(staged_).first(staged_.size() - 1) : std::span(staged_);",
    ),
    # Replace is issued before the payload group is synced: the table is renamed first, then synced.
    "rename-before-sync": (
        disk,
        "if (index_ == group_end_ || staged_.size() == window_) {\n\t\t\t\t\tphase_ = Phase::SyncGroup;\n\t\t\t\t\tprepare(c::Kind::SyncFiles);",
        "if (index_ == child_count()) {\n\t\t\t\t\tphase_ = Phase::Replace;\n\t\t\t\t\tprepare(c::Kind::Replace);\n\t\t\t\t} else if (index_ == group_end_ || staged_.size() == window_) {\n\t\t\t\t\tphase_ = Phase::SyncGroup;\n\t\t\t\t\tprepare(c::Kind::SyncFiles);",
    ),
    # The startup barrier is gone: ready() follows the census directly.
    "startup-before-barrier": (
        disk,
        "prepare(Phase::SyncOpen, c::Kind::OpenInto);\n\t\t\treturn;",
        "ready();\n\t\t\treturn;",
    ),
}
if mutant == "cycle-oracle":
    # The cycle oracle without its restart allowance: a cut before the rerun's barrier must select the rerun's own.
    source = cut.read_text()
    old = "const bool earlier = input && index < run.ready && allowed(*input, input_index, selected);"
    assert source.count(old) == 1, old
    cut.write_text(source.replace(old, "const bool earlier = false;"))
elif mutant == "phase-counts":
    # The T7 accounting fix Detent:522452a reverted: startup syncs counted with the publication's.
    source = cut.read_text()
    for suffix in ("files", "directories", "flushes"):
        old = f"g_seam_{suffix} - startup_{suffix}"
        assert source.count(old) == 1, old
        source = source.replace(old, f"g_seam_{suffix}")
    cut.write_text(source)
else:
    path, old, new = edits[mutant]
    source = path.read_text()
    assert source.count(old) == 1, (mutant, source.count(old))
    path.write_text(source.replace(old, new))
PY
  git --no-pager diff -U0 | grep '^[-+][^-+]'
fi
cmake --build "$build" -j4 --target "${targets[@]}" || exit 125
# The tests that must each fail for the mutant to count as killed.
case "$name" in
  positive) run=("${targets[@]}") ;;
  closed-session) run=(test_local_paged_store_startup) ;;
  startup-before-barrier) run=(test_local_publication_power_cut test_local_paged_store_startup) ;;
  *) run=(test_local_publication_power_cut) ;;
esac
failed=0 passed=0 hung=0
for t in "${run[@]}"; do
  echo "$name: running $t, bound $limit s"
  start=$(date +%s)
  /usr/bin/perl -e 'alarm shift; exec @ARGV or die "exec: $!"' "$limit" "$build/tests/detent_local/$t"
  rc=$?
  echo "$name: $t rc=$rc seconds=$(($(date +%s) - start))"
  if [ "$rc" -eq 0 ]; then passed=$((passed + 1));
  elif [ "$rc" -eq 124 ] || [ "$rc" -eq 142 ]; then hung=$((hung + 1)); echo "$name: $t HUNG";
  else failed=$((failed + 1)); fi
done
if [ "$name" = positive ]; then
  [ "$failed" -eq 0 ] && [ "$hung" -eq 0 ]
  exit $?
fi
if [ "$hung" -ne 0 ]; then
  echo "MUTANT $name HUNG ($hung of ${#run[@]} named tests): not evidence"
  exit 125
fi
if [ "$passed" -ne 0 ]; then
  echo "MUTANT $name SURVIVED ($passed of ${#run[@]} named tests passed)"
  exit 1
fi
echo "MUTANT $name KILLED by every named test"
