#!/bin/sh
set -eu
mode=$1
python=$2
binary=$3
agent=$4
oracle=$5
repository=$6
build=$7
relay=${8-}
mkdir -p "$repository/.scratch/stage6"
output=$(mktemp -d "$repository/.scratch/stage6/$mode-XXXXXX")
rmdir "$output"
set -- --scenario "$repository/tests/detent_system/presets/smoke.json" \
  --binary "$binary" --build "$build" --agents "$agent" --local-oracle "$oracle" \
  --certificates "$build/tests/detent_authority/tls-fixture" --output "$output"
case "$mode" in
  partition) script=run_partition.py ;;
  all-down|no-quorum) script=run_outage.py; set -- "$@" --kind "$mode" ;;
  poll) script=run_writers.py; set -- "$@" --delivery-mode poll --duration 12 --kill-leader-at 3 ;;
  relay) script=run_writers.py; set -- "$@" --relays "$relay" --duration 12 --kill-leader-at 3 ;;
  trust) script=run_trust_negatives.py; set -- --agents "$agent" --certificates "$build/tests/detent_authority/tls-fixture" --output "$output" ;;
  *) exit 2 ;;
esac
exec "$python" "$repository/tests/detent_system/$script" "$@"
