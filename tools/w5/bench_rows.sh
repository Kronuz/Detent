#!/usr/bin/env bash
# Prints one benchmark of one build as unit rows (`name value unit`) for ab_compare.py.
#   tools/w5/bench_rows.sh BUILD_DIR KIND
# KIND: authority-update (detent_authority_bench),
#       authority-durable (detent_authority_durable_bench), authority-read (detent_authority_read_response_bench).
set -uo pipefail
build=${1:?build dir}
kind=${2:?kind}
here=$(cd "$(dirname "$0")" && pwd)
scratch=$(mktemp -d "${W5_SCRATCH:-${TMPDIR:-/tmp}}/w5-bench.XXXXXX")
trap 'rm -rf "$scratch"' EXIT
case "$kind" in
authority-update)
	"$build/benchmarks/detent_authority_bench" 100000 hot | python3 -I "$here/json_rows.py"
	;;
authority-durable)
	"$build/benchmarks/detent_authority_durable_bench" "$scratch/durable" 20000 4 | python3 -I "$here/json_rows.py" --keep 'ack|checkpoint_ms|load_ms'
	;;
authority-read)
	"$build/benchmarks/detent_authority_read_response_bench" | python3 -I "$here/json_rows.py"
	;;
*)
	echo "unknown kind $kind" >&2
	exit 2
	;;
esac
