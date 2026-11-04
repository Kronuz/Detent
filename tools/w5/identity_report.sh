#!/usr/bin/env bash
# Writes the per-class comparison of two identity runs (tools/w5/run_both.sh output) to OUT_DIR.
#   tools/w5/identity_report.sh RUN_DIR OUT_DIR
set -uo pipefail
here=$(cd "$(dirname "$0")" && pwd)
run=${1:?run dir}
out=${2:?output dir}
mkdir -p "$out"
cmp_class() {  # name, extra compare.py args...
	local name=$1
	shift
	python3 -I "$here/compare.py" "$run/old/$name.txt" "$run/new/$name.txt" "$@" > "$out/identity-$name.txt" 2>&1
	echo "$name: exit $? ($(tail -n 1 "$out/identity-$name.txt"))"
}
cmp_class w5_compressors --show 2
cmp_class w5_http_parser --show 2 --signature 'errno=(\w+)'
cmp_class w5_stox --show 2
cmp_class w5_enum --show 3 --strip ' try=\S+'
cmp_class w5_random --show 2 --ignore seq
cmp_class w5_detent --show 2
{
	diff "$run/old/logger_transcript.txt" "$run/new/logger_transcript.txt"
	echo "exit $?"
} > "$out/identity-logger_transcript.txt" 2>&1
if diff -r "$run/old/store" "$run/new/store" > /dev/null; then
	echo "store: $(find "$run/new/store" -type f | wc -l | tr -d ' ') files byte-identical" | tee "$out/identity-store.txt"
else
	diff -rq "$run/old/store" "$run/new/store" | tee "$out/identity-store.txt"
fi
