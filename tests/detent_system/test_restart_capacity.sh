#!/bin/sh
# Runs test_restart_capacity.py in a fresh directory under .scratch; extra arguments go to the script.
# Usage: test_restart_capacity.sh PYTHON AUTHORITY AGENT ORACLE REPOSITORY BUILD RELAY [ARG ...]
set -eu
python=$1 authority=$2 agent=$3 oracle=$4 repository=$5 build=$6 relay=$7
shift 7
mkdir -p "$repository/.scratch/stage6"
output=$(mktemp -d "$repository/.scratch/stage6/restart-capacity-XXXXXX")
rmdir "$output"
"$python" "$repository/tests/detent_system/test_restart_capacity.py" --binary "$authority" --agents "$agent" \
  --relays "$relay" --local-oracle "$oracle" --certificates "$build/tests/detent_authority/tls-fixture" \
  --output "$output" "$@"
rm -rf "$output" # a failed run keeps its directory
