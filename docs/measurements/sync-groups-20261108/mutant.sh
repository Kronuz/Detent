#!/bin/bash
# usage: mutant.sh positive|MUTANT     (run from the checkout root, in a job)
# positive: Debug build and run of the power-cut and startup tests. MUTANT: apply one source edit,
# rebuild, run the test that must catch it, and exit with that test's status (a killed mutant fails
# the job; a survivor passes it and prints SURVIVED). Exit 125 means the build failed.
set -u
name=$1
build=.scratch/build-dbg
targets=(test_local_publication_power_cut test_local_paged_store_startup)
restore() { git checkout -q -- src tests; }
trap restore EXIT
restore
if [ ! -f "$build/CMakeCache.txt" ]; then
  cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_COMPILER=/usr/bin/clang++ -DCMAKE_C_COMPILER=/usr/bin/clang -DCMAKE_OSX_SYSROOT= \
    -DDETENT_BUILD_AUTHORITY=OFF -DDETENT_BUILD_RELAY=OFF -DDETENT_BUILD_MANAGED_AGENT=OFF \
    -DDETENT_BUILD_EXAMPLES=OFF -DDETENT_BUILD_BENCHMARKS=OFF -DDETENT_BUILD_TESTS=ON || exit 125
fi
if [ "$name" != positive ]; then
  /usr/local/bin/python3 - "$name" <<'PY' || exit 125
import sys
from pathlib import Path

mutant = sys.argv[1]
disk = Path("src/detent_local/disk_store.cc")
cut = Path("tests/detent_local/test_publication_power_cut.cc")
edits = {
    "closed-session": (
        disk,
        "core.session ? core.session->sync_counters() : core.closed_sync",
        "core.session->sync_counters()",
    ),
    "empty-payload-group": (
        disk,
        "request_.files = staged_;",
        "request_.files = group_end_ == child_count() ? decltype(request_.files){} : decltype(request_.files)(staged_);",
    ),
    "unsynced-descriptor": (
        disk,
        "request_.files = staged_;",
        "request_.files = index_ == child_count() ? std::span(staged_).first(staged_.size() - 1) : std::span(staged_);",
    ),
    "startup-before-barrier": (
        disk,
        "prepare(Phase::Barrier, c::Kind::DirectorySync);\n\t\t\treturn;",
        "ready();\n\t\t\treturn;",
    ),
}
if mutant == "phase-counts":
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
  git --no-pager diff --stat
  git --no-pager diff -U0 | grep '^[-+][^-+]'
fi
cmake --build "$build" -j4 --target "${targets[@]}" || exit 125
case "$name" in
  closed-session | startup-before-barrier) run=(test_local_paged_store_startup) ;;
  positive) run=("${targets[@]}") ;;
  *) run=(test_local_publication_power_cut) ;;
esac
status=0
for t in "${run[@]}"; do
  "$build/tests/detent_local/$t"
  rc=$?
  echo "$name: $t rc=$rc"
  [ "$rc" -ne 0 ] && status=$rc
done
if [ "$name" != positive ] && [ "$status" -eq 0 ]; then echo "MUTANT $name SURVIVED"; fi
exit "$status"
