#!/bin/sh
set -eu
python=$1
binary=$2
repository=$3
build=$4
mkdir -p "$repository/.scratch/stage6"
output=$(mktemp -d "$repository/.scratch/stage6/authority-writers-XXXXXX")
rmdir "$output"
exec "$python" "$repository/tests/detent_system/run_writers.py" \
  --scenario "$repository/tests/detent_system/presets/smoke.json" \
  --binary "$binary" --build "$build" \
  --certificates "$build/tests/detent_authority/tls-fixture" \
  --output "$output" --duration 5
