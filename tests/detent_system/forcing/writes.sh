#!/bin/sh
# Soak the snapshot catch-up test with catchup-writes-probe.patch applied, then print, per run, the
# degraded phase's write count and its time split: leader discovery, TLS connect, request.
# Usage: writes.sh RUNS JOBS. Run through patched.sh.
set -u
here=$(dirname "$0")
export CATCHUP_PHASES="$PWD/.scratch/catchup-phases.txt"
mkdir -p .scratch && rm -f "$CATCHUP_PHASES"
python3 "$here/soak.py" .scratch/build-loop detentd "$1" "$2" test_authority_snapshot_catchup
code=$?
grep -v ' write ' "$CATCHUP_PHASES" | sort -s -k1,1n
awk '$2 == "write" {
  split($3, l, "="); split($4, c, "="); split($5, r, "=");
  n[$1]++; lead[$1] += l[2]; conn[$1] += c[2]; req[$1] += r[2]
  if (r[2] > top[$1]) top[$1] = r[2]
  if ($6 != "status=200") bad[$1]++
} END {
  for (p in n) printf "%s writes=%d leader=%.1f connect=%.1f request=%.1f max_request=%.2f non200=%d\n",
    p, n[p], lead[p], conn[p], req[p], top[p], bad[p]
}' "$CATCHUP_PHASES"
exit $code
