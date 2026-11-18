#!/bin/bash
set -euo pipefail
W=/Users/kronuz/code/Kronuz/Detent/.claude/worktrees/agent-af2c08dd24001ec58/.scratch/recovery-s2
T=$W/tree
B=$W/build-debug
Q=/Users/kronuz/.local/bin/quiet
TIMEOUT=/usr/local/bin/gtimeout
mkdir -p "$W/logs"
source "$W/identity.sh"
identity=$(qualification_identity)
expected=$(code_identity /Users/kronuz/code/Kronuz/Detent/.claude/worktrees/agent-af2c08dd24001ec58 522452a)
test "$(code_identity /Users/kronuz/code/Kronuz/Detent/.claude/worktrees/agent-af2c08dd24001ec58)" = "$expected"
cd "$T"
echo 'Snapshot: Detent:522452a; baseline instrumentation fix Detent:0cd1527'
"$Q" check || true
run() {
  local name=$1 minutes=$2; shift 2
  "$Q" light "s2proof-$name" "$minutes" "S2 proof $name" -- nice -n 19 \
    "$TIMEOUT" --kill-after=15s "$((minutes - 1))m" "$@" >"$W/logs/$name.log" 2>&1
}
build() { run "$1-build" 30 cmake --build "$B" -j4 --target "${@:2}"; }
expect_failure() {
  local name=$1 binary=$2 rc=0
  run "$name" 20 "$B/tests/detent_local/$binary" || rc=$?
  echo "$name rc=$rc"
  if [ "$rc" -eq 0 ] || [ "$rc" -eq 124 ] || [ "$rc" -eq 137 ]; then
    echo "Expected an invariant failure, got pass or timeout"; exit 1
  fi
}
run configure 30 cmake -S "$T" -B "$B" -G Ninja -DCMAKE_BUILD_TYPE=Debug
build baseline test_local_publication_power_cut test_local_paged_store_startup
run startup-positive 20 "$B/tests/detent_local/test_local_paged_store_startup"
run power-cut-positive 20 "$B/tests/detent_local/test_local_publication_power_cut"
cp src/detent_local/disk_store.cc "$W/disk_store.good.cc"
cp tests/detent_local/test_publication_power_cut.cc "$W/power_cut.good.cc"
restore() {
  cp "$W/disk_store.good.cc" src/detent_local/disk_store.cc
  cp "$W/power_cut.good.cc" tests/detent_local/test_publication_power_cut.cc
}
trap restore EXIT
for mutant in closed-session phase-counts empty-payload-group unsynced-descriptor startup-before-barrier; do
  restore
  /usr/local/bin/python3 - "$mutant" <<'PY'
import sys
from pathlib import Path

mutant = sys.argv[1]
path = Path("src/detent_local/disk_store.cc")
edits: dict[str, tuple[str, str]] = {
    "closed-session": (
        "core.session ? core.session->sync_counters() : core.closed_sync",
        "core.session->sync_counters()",
    ),
    "empty-payload-group": (
        "request_.files = staged_;",
        "request_.files = group_end_ == child_count() ? decltype(request_.files){} : decltype(request_.files)(staged_);",
    ),
    "unsynced-descriptor": (
        "request_.files = staged_;",
        "request_.files = index_ == child_count() ? std::span(staged_).first(staged_.size() - 1) : std::span(staged_);",
    ),
    "startup-before-barrier": (
        "prepare(Phase::Barrier, c::Kind::DirectorySync);\n\t\t\treturn;",
        "ready();\n\t\t\treturn;",
    ),
}
if mutant == "phase-counts":
    path = Path("tests/detent_local/test_publication_power_cut.cc")
    source = path.read_text()
    for suffix in ("files", "directories", "flushes"):
        old = f"g_seam_{suffix} - startup_{suffix}"
        assert source.count(old) == 1
        source = source.replace(old, f"g_seam_{suffix}")
else:
    source = path.read_text()
    old, new = edits[mutant]
    assert source.count(old) == 1, (mutant, source.count(old))
    source = source.replace(old, new)
path.write_text(source)
PY
  if [ "$mutant" = closed-session ] || [ "$mutant" = startup-before-barrier ]; then target=test_local_paged_store_startup; else target=test_local_publication_power_cut; fi
  build "$mutant" "$target"
  expect_failure "$mutant" "$target"
done
restore
build restored test_local_publication_power_cut test_local_paged_store_startup
run startup-restored 20 "$B/tests/detent_local/test_local_paged_store_startup"
run power-cut-restored 20 "$B/tests/detent_local/test_local_publication_power_cut"
echo 'S2 positives, lifecycle/accounting reverts, three T7 mutants, and restored positives passed'

test "$(qualification_identity)" = "$identity"
printf "%s\n" "$identity" > "$W/proof.passed"
