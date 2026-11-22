#!/bin/bash
# One row of docs/design/authority-pages.md, from the checkout root after build.sh:
#   run.sh smoke | seals SPACES KEYS SEALS CHANGES [write] | commit SPACES KEYS GROUPS ROUNDS
# Prints the harness's JSON lines and checks them (check.py); `commit` interleaves the arms
# none, thread, inline, write ROUNDS times on one authority.
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
	/usr/local/bin/python3 -I "$here/check.py" commit 1 <.scratch/ap-smoke-commit.jsonl ;;
seals)
	"$bin" seals "$out" "$@" | tee .scratch/ap-seals.jsonl
	/usr/local/bin/python3 -I "$here/check.py" seals "$1" "$3" <.scratch/ap-seals.jsonl ;;
commit)
	arms=()
	for _ in $(seq "$4"); do arms+=(none thread inline write); done
	"$bin" commit "$out" "$1" "$2" "$3" "${arms[@]}" | tee .scratch/ap-commit.jsonl
	/usr/local/bin/python3 -I "$here/check.py" commit "$4" <.scratch/ap-commit.jsonl ;;
*) echo "mode: smoke, seals or commit" >&2; exit 2 ;;
esac
