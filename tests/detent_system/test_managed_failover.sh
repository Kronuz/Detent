#!/bin/sh
set -eu
python=$1
binary=$2
agent=$3
oracle=$4
repository=$5
build=$6
mkdir -p "$repository/.scratch/stage6"
output=$(mktemp -d "$repository/.scratch/stage6/managed-failover-XXXXXX")
rmdir "$output"
exec "$python" "$repository/tests/detent_system/run_writers.py" \
  --scenario "$repository/tests/detent_system/presets/smoke.json" \
  --binary "$binary" --build "$build" --agents "$agent" --local-oracle "$oracle" \
  --certificates "$build/tests/detent_authority/tls-fixture" \
  --output "$output" --duration 12 --kill-leader-at 3
