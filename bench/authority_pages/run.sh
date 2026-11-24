#!/bin/bash
# One row of docs/design/authority-pages.md, from the checkout root after build.sh:
#   run.sh smoke | seals SPACES KEYS SEALS CHANGES [write] | commit SPACES KEYS GROUPS ARM...
#        | paced SPACES KEYS SEAL_RATE ROUNDS GROUPS | lag RUN...
# Prints the harness's JSON lines and checks them (check.py); `commit` runs the arms in the order
# named on one authority, `paced` interleaves them beside a builder at a fixed seal rate (pairs:
# `check.py pairs`),
# and `lag` runs each RUN, "SPACES KEYS THREADS START INTERVAL RATE SECONDS BURST AT SPAN", in turn.
set -eu
bin=.scratch/build-ap/authority_pages_bench
here=bench/authority_pages
test "$(cat .scratch/build-ap/BUILT_COMMIT)" = "$(git rev-parse HEAD)"
git diff --quiet HEAD -- src include CMakeLists.txt "$here"
out=.scratch/ap-run
rm -rf "$out"
mode=${1:?smoke, seals or commit}
shift
uname -a
sysctl -n machdep.cpu.brand_string
echo "commit $(git rev-parse HEAD)"
case $mode in
smoke)
	"$bin" seals "$out" 2 5000 3 64 write | tee .scratch/ap-smoke-seals.jsonl
	/usr/local/bin/python3 -I "$here/check.py" seals 2 3 <.scratch/ap-smoke-seals.jsonl
	"$bin" commit "$out" 2 5000 40 none thread inline write | tee .scratch/ap-smoke-commit.jsonl
	/usr/local/bin/python3 -I "$here/check.py" commit none thread inline write <.scratch/ap-smoke-commit.jsonl
	"$bin" paced "$out" 2 5000 0.98 1 8 | tee .scratch/ap-smoke-paced.jsonl
	/usr/local/bin/python3 -I "$here/check.py" paced 0.98 <.scratch/ap-smoke-paced.jsonl
	{ "$bin" lag "$out" 2 5000 2 cold 250 2000 3 6000 1 1; "$bin" lag "$out" 2 5000 1 warm 250 2000 3 0 0 0; } \
		| tee .scratch/ap-smoke-lag.jsonl
	/usr/local/bin/python3 -I "$here/check.py" lag <.scratch/ap-smoke-lag.jsonl ;;
seals)
	"$bin" seals "$out" "$@" | tee .scratch/ap-seals.jsonl
	/usr/local/bin/python3 -I "$here/check.py" seals "$1" "$3" <.scratch/ap-seals.jsonl ;;
commit)
	"$bin" commit "$out" "$@" | tee .scratch/ap-commit.jsonl
	shift 3
	/usr/local/bin/python3 -I "$here/check.py" commit "$@" <.scratch/ap-commit.jsonl ;;
paced)
	"$bin" paced "$out" "$@" | tee .scratch/ap-paced.jsonl
	/usr/local/bin/python3 -I "$here/check.py" paced "$3" <.scratch/ap-paced.jsonl ;;
lag)
	for run in "$@"; do
		# shellcheck disable=SC2086 # each RUN is its words
		"$bin" lag "$out" $run
	done | tee .scratch/ap-lag.jsonl
	/usr/local/bin/python3 -I "$here/check.py" lag <.scratch/ap-lag.jsonl ;;
*) echo "mode: smoke, seals, commit, paced or lag" >&2; exit 2 ;;
esac
