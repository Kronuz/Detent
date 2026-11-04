#!/usr/bin/env bash
# Runs every M1 identity driver of one build tree and writes its outputs to OUT.
#   tools/w5/run_identity.sh BUILD_DIR OUT_DIR [PORT]
# Compare two outputs with: tools/w5/compare.py OLD/<name>.txt NEW/<name>.txt
set -uo pipefail
build=${1:?build dir}
out=${2:?output dir}
port=${3:-18317}
here=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$out"
for name in w5_compressors w5_http_parser w5_stox w5_enum w5_random w5_detent logger_transcript; do
	exe="$build/w5/$name"
	[ -x "$exe" ] || exe="$build/$name"
	echo "== $name"
	"$exe" > "$out/$name.txt" 2> "$out/$name.err"
	echo "   exit $? lines $(wc -l < "$out/$name.txt")"
done
rm -rf "$out/store"
"$build/w5_store" "$out/store" > "$out/w5_store.txt" 2> "$out/w5_store.err" || "$build/w5/w5_store" "$out/store" > "$out/w5_store.txt" 2> "$out/w5_store.err"
echo "== w5_store exit $? files $(find "$out/store" -type f | wc -l)"
if "$build/src/detentd" --help 2>&1 | grep -q -- "--persist-path"; then
	python3 -I "$here/http_replay.py" --detentd "$build/src/detentd" --out "$out/replay" --port "$port"
	echo "== http_replay exit $?"
else
	echo "== http_replay skipped: this detentd has no classic profile"
fi
