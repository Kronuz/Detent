#!/bin/sh
# Runs run_relay_pages.py in a fresh directory under .scratch; extra arguments go to the script, and
# RELAY_PAGES_EXPECT=refusal (forcing/relay_pages_forced.sh) adds --expect-refusal.
# Usage: test_relay_pages.sh PYTHON AUTHORITY AGENT ORACLE REPOSITORY BUILD RELAY [ARG ...]
set -eu
python=$1 authority=$2 agent=$3 oracle=$4 repository=$5 build=$6 relay=$7
shift 7
mkdir -p "$repository/.scratch/stage6"
output=$(mktemp -d "$repository/.scratch/stage6/relay-pages-XXXXXX")
rmdir "$output"
"$python" "$repository/tests/detent_system/run_relay_pages.py" --binary "$authority" --agents "$agent" \
  --relays "$relay" --local-oracle "$oracle" --certificates "$build/tests/detent_authority/tls-fixture" \
  --output "$output" ${RELAY_PAGES_EXPECT:+--expect-refusal} "$@"
rm -rf "$output" # a failed run keeps its directory, which CI uploads
