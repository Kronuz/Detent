#!/usr/bin/env bash
# A1 of rename-detentd: `git grep -n -i detentd` finds only dated records. Exits 1 and prints each
# other hit. The journal (rename-detentd.md, "Rulings I made") says why each allowed path stays.
set -u
root=$(git -C "$(dirname "$0")" rev-parse --show-toplevel) || exit 2
cd "$root" || exit 2

# Whole files that are dated records: recorded results, a dated audit, design and review, a
# HISTORY handoff, and mutants that restore a file at a commit before the rename.
records=(
  benchmarks/distributor-publication-preliminary-20261020.json
  benchmarks/fleet-persistent-transport-qualification.json
  benchmarks/library-adoption-closure-20261019.json
  benchmarks/library-adoption-progress-20261017.json
  benchmarks/logging-footprint.json
  benchmarks/managed-failover-qualification.json
  benchmarks/raft-apply-retry-qualification-20261020.json
  benchmarks/xapiand-logging-preliminary-20261020.json
  docs/perf/m1-w5-evidence/classic-daemon-log-diff.txt
  docs/handoffs/detent-codex-2026-11.md
  docs/dev/docs-site-audit.md
  docs/design/http-coroutines.md
  docs/VALIDATED_ARCHITECTURE.md
  tests/detent_authority/revert_ready_check.sh
  tests/detent_authority/revert_followup_check.sh
)
# Lines of CHANGELOG.md that name the old binary or files removed under the old name.
changelog=(
  '- The authority daemon `detentd` is renamed `detent-authority`, so the daemons are `detent-authority`,'
  '- Removed the classic distributor and classic Raft: `detentd.cc`, `raft_delegate`,'
  '  `CLUSTERING.md`, `tests/detentd`, `detentd_fanout_bench`, the'
)

allowed() {
  local path=$1 text=$2 entry
  case $path in .campaign/* | docs/measurements/* | docs/reviews/*) return 0 ;; esac
  if [[ $path == docs/status/* ]] && grep -q '^> Status: HISTORY' "$path"; then return 0; fi
  for entry in "${records[@]}"; do [[ $path == "$entry" ]] && return 0; done
  if [[ $path == CHANGELOG.md ]]; then
    for entry in "${changelog[@]}"; do [[ $text == "$entry" ]] && return 0; done
  fi
  return 1
}

hits=0
while IFS= read -r line; do
  path=${line%%:*}
  rest=${line#*:}
  text=${rest#*:}
  allowed "$path" "$text" && continue
  printf '%s\n' "$line"
  hits=$((hits + 1))
done < <(git grep -n -i detentd)

if ((hits)); then
  echo "rename-detentd A1: $hits mention(s) outside the dated records" >&2
  exit 1
fi
echo "rename-detentd A1: every detentd mention is in a dated record"
