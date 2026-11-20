#!/bin/sh
# usage: fleet_dial_fault.sh PYTHON BUILD RUNS PARALLEL
# Runs fleet_dial_fault.py (test_system_fleet_raw with a [::1] echo on each relay port) RUNS
# times, PARALLEL at once; exits nonzero when any run fails and keeps the failed runs' logs.
set -eu
python=$1
build=$2
runs=$3
parallel=$4
here=$(cd "$(dirname "$0")" && pwd)
repository=$(cd "$here/../.." && pwd)
logs="$repository/.scratch/fleet-dial-fault"
mkdir -p "$logs"
seq "$runs" | xargs -P "$parallel" -I{} sh -c '
  output=$(mktemp -d "$5/run-$6-XXXXXX") && rmdir "$output"
  if "$1" "$2/fleet_dial_fault.py" \
    --scenario "$2/presets/fleet-smoke.json" \
    --binary "$3/src/detent-authority" --agents "$3/src/detent-agent" --relays "$3/src/detent-relay" \
    --local-oracle "$3/tests/detent_system/detent_system_local_oracle" \
    --fleet "$3/benchmarks/detent_fleet_load" --build "$3" \
    --certificates "$3/tests/detent_authority/tls-fixture" \
    --output "$output" --active 16 --workers 2 --compression raw > "$output.log" 2>&1
  then echo "run $6: passed, $(grep "echo accepted" "$output.log")"; rm -rf "$output" "$output.log"
  else echo "run $6: FAILED, log $output.log"; grep -E "UNEXPECTED_MESSAGE|did not drain|echo accepted|Error:" "$output.log" | sort | uniq -c; exit 1
  fi' sh "$python" "$here" "$build" "$repository" "$logs" {}
