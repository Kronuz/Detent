#!/bin/sh
# Soak the snapshot catch-up test with catchup-phases-probe.patch applied, then print each run's phase
# times (seconds since start). Usage: phases.sh RUNS JOBS. Run through patched.sh.
set -u
here=$(dirname "$0")
export CATCHUP_PHASES="$PWD/.scratch/catchup-phases.txt"
mkdir -p .scratch && rm -f "$CATCHUP_PHASES"
python3 "$here/soak.py" .scratch/build-loop detentd "$1" "$2" test_authority_snapshot_catchup
code=$?
sort -k1,1n -k3,3n "$CATCHUP_PHASES"
exit $code
