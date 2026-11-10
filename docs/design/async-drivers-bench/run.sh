#!/usr/bin/env bash
# Builds the async-drivers measurements against the library commits a job pins and runs them.
#   run.sh idle_cost [groups]       per-idle-group bytes, descriptors and threads
#   run.sh driver_cost [section]    frames, resumes, wheels, steps, idle CPU
#   run.sh channel_layout           where a follower's SnapshotChannel bytes go
# A campaign job pins the libraries with --ref consensus=..., journal=..., completion=...,
# task=... (checkouts at $JOB_REF_<NAME>); without them, the commits CMakeLists.txt declares.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
build=${BUILD_DIR:-$here/build}
args=()
for name in consensus journal completion task; do
	upper=$(printf '%s' "$name" | tr '[:lower:]' '[:upper:]')
	var=JOB_REF_$upper
	if [ -n "${!var:-}" ]; then
		args+=("-DFETCHCONTENT_SOURCE_DIR_$upper=${!var}")
		echo "pin $name=$(git -C "${!var}" rev-parse HEAD)"
	fi
done
cmake -S "$here" -B "$build" -DCMAKE_BUILD_TYPE=Release ${args[@]+"${args[@]}"} >/dev/null
cmake --build "$build" -j4 >/dev/null
program=${1:?program}
shift
echo "host=$(uname -sm) compiler=$(c++ --version | head -1)"
file "$build/$program"
export BENCH_DIR="$build/data"
mkdir -p "$BENCH_DIR"
echo "start=$(date -u +%FT%TZ)"
"$build/$program" "$@"
echo "end=$(date -u +%FT%TZ)"
rm -rf "$BENCH_DIR"
