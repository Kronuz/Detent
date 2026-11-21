#!/bin/sh
# Runs run_agent_sigterm.py in a fresh directory under .scratch; extra arguments go to the script.
# Usage: test_agent_sigterm.sh PYTHON AUTHORITY AGENT ORACLE REPOSITORY BUILD [ARG ...]
set -eu
python=$1 authority=$2 agent=$3 oracle=$4 repository=$5 build=$6
shift 6
mkdir -p "$repository/.scratch/stage6"
output=$(mktemp -d "$repository/.scratch/stage6/agent-sigterm-XXXXXX")
rmdir "$output"
"$python" "$repository/tests/detent_system/run_agent_sigterm.py" --binary "$authority" --agents "$agent" \
  --local-oracle "$oracle" --certificates "$build/tests/detent_authority/tls-fixture" \
  --output "$output" "$@"
rm -rf "$output" # a failed run keeps its directory, which CI uploads
