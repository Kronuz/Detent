#!/bin/sh
set -eu
mode=$1
python=$2
binary=$3
agent=$4
relay=$5
oracle=$6
fleet=$7
repository=$8
build=$9
case "$mode" in
  raw) set -- --compression raw ;;
  zstd) set -- --compression zstd ;;
  warm) set -- --compression zstd --warm ;;
  mixed) set -- --compression zstd --mixed --hot-namespace ;;
  partial-launch|cancel) set -- --fault "$mode" ;;
  *) exit 2 ;;
esac
mkdir -p "$repository/.scratch/stage6"
output=$(mktemp -d "$repository/.scratch/stage6/fleet-$mode-XXXXXX")
rmdir "$output"
exec "$python" "$repository/tests/detent_system/run_fleet.py" \
  --scenario "$repository/tests/detent_system/presets/fleet-smoke.json" \
  --binary "$binary" --agents "$agent" --relays "$relay" \
  --local-oracle "$oracle" --fleet "$fleet" --build "$build" \
  --certificates "$build/tests/detent_authority/tls-fixture" \
  --output "$output" --active 16 --workers 2 "$@"
