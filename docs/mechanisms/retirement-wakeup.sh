#!/bin/bash
# usage, from the checkout root, in a light job (see retirement-wakeup.md):
#   retirement-wakeup.sh test [REF]       build test_retirement.cc against REF's retirement.cc and
#                                         headers (default this checkout's) and run it
#   retirement-wakeup.sh mutant NAME      the same with one ordering mutant; exits 0 iff the test fails
#   retirement-wakeup.sh probe [REF [N]]  N close/worker stress rounds (default 300000) against REF's
#                                         retirement.cc (default HEAD), at -O0 and -O2; exits 1 on a stall
# Exits 125 when a build fails or a mutant does not apply.
set -u
here=$(cd "$(dirname "$0")" && pwd)
out=$PWD/.scratch/retirement-wakeup
rm -rf "$out"; mkdir -p "$out"
cxx() { /usr/bin/clang++ -std=c++20 -g -Wall -Wextra -pthread "$@"; }
extract() {
  mkdir -p "$out/src"
  git archive "$1" include src/detent_distribution/retirement.cc | tar -x -C "$out/src" || exit 125
  echo "retirement.cc at $1: $(git rev-parse --short "$1"), $(git rev-parse "$1:src/detent_distribution/retirement.cc")"
}
run_test() {
  cxx -O2 -I"$2" tests/detent_distribution/test_retirement.cc "$1" -o "$out/test" || exit 125
  "$out/test"
}
case ${1:-} in
test)
  if [ -n "${2:-}" ]; then
    extract "$2"
    run_test "$out/src/src/detent_distribution/retirement.cc" "$out/src/include"
  else
    run_test src/detent_distribution/retirement.cc include
  fi ;;
mutant)
  cp src/detent_distribution/retirement.cc "$out/mutant.cc"
  /usr/bin/python3 -I - "${2:-}" "$out/mutant.cc" <<'PY' || exit 125
import sys
name, path = sys.argv[1], sys.argv[2]
source = open(path).read()
rollbacks = "\t\tconst auto rollbacks = rolling_back.load(std::memory_order_acquire);\n"
admitted = "\t\tconst auto admitted = admission.load(std::memory_order_acquire);\n"
closing = "(closing_seen && !(admitted & closed))"
edits = {
    "reread-admission": [(closing, "(closing_seen && !(admission.load(std::memory_order_acquire) & closed))")],
    "swap-rollbacks-admission": [(rollbacks, "@@"), (admitted, rollbacks), ("@@", admitted)],
    "drop-closing": [(" || " + closing, "")],
}.get(name)
if edits is None:
    sys.exit(f"unknown mutant {name!r}")
for before, after in edits:
    if source.count(before) != 1:
        sys.exit(f"mutant {name}: {before!r} not found once")
    source = source.replace(before, after)
open(path, "w").write(source)
PY
  diff -u src/detent_distribution/retirement.cc "$out/mutant.cc"
  if run_test "$out/mutant.cc" include; then echo "mutant ${2} survived"; exit 1; fi
  echo "mutant ${2} killed" ;;
probe)
  ref=${2:-HEAD} rounds=${3:-300000}
  extract "$ref"
  status=0
  for flags in "-O0" "-O2"; do
    cxx $flags -I"$out/src/include" "$here/retirement-wakeup-probe.cc" "$out/src/src/detent_distribution/retirement.cc" \
      -o "$out/probe$flags" || exit 125
    start=$(date +%s)
    "$out/probe$flags" "$rounds"
    rc=$?
    echo "$flags: rc=$rc seconds=$(($(date +%s) - start))"
    [ "$rc" -eq 0 ] || status=1
  done
  exit $status ;;
*) echo "usage: $0 test | mutant NAME | probe [REF [ROUNDS]]" >&2; exit 2 ;;
esac
