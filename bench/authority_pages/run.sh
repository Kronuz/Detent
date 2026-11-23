#!/bin/bash
# One row of docs/design/authority-pages.md, from the checkout root after build.sh:
#   run.sh smoke | seals SPACES KEYS SEALS CHANGES [write] | commit SPACES KEYS GROUPS ARM...
# Prints the harness's JSON lines and checks them (check.py); `commit` runs the arms in the order
# named on one authority. A commit row is one job per rotation of the arms, combined by
# `check.py combine` (docs/design/authority-pages.md).
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
	/usr/local/bin/python3 -I "$here/check.py" commit none thread inline write <.scratch/ap-smoke-commit.jsonl ;;
seals)
	"$bin" seals "$out" "$@" | tee .scratch/ap-seals.jsonl
	/usr/local/bin/python3 -I "$here/check.py" seals "$1" "$3" <.scratch/ap-seals.jsonl ;;
commit)
	"$bin" commit "$out" "$@" | tee .scratch/ap-commit.jsonl
	shift 3
	/usr/local/bin/python3 -I "$here/check.py" commit "$@" <.scratch/ap-commit.jsonl ;;
*) echo "mode: smoke, seals or commit" >&2; exit 2 ;;
esac
