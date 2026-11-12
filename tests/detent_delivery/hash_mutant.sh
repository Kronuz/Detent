#!/bin/bash
# Applies one namespace S4 mutant patch from tests/detent_delivery/mutants, builds TARGETS in the checkout's
# .scratch/build and runs the tests matching REGEX. Exits 0 when a test fails (the mutant is killed), 1 when
# every test passes (it survives) and 3 when there is no verdict (the patch does not apply, the build fails,
# no test matches, or ctest fails other than by a test failing).
set -uo pipefail

patch=${1:?mutant patch}
regex=${2:?ctest regex}
shift 2
test "$#" -gt 0 || { echo "usage: $0 PATCH REGEX TARGET..."; exit 3; }
build=${HASH_MUTANT_BUILD:-.scratch/build}
no_verdict() {
    echo "NO VERDICT: $patch: $1"
    exit 3
}
git apply --check "$patch" && git apply "$patch" || no_verdict "patch does not apply"
trap 'git apply -R "$patch"' EXIT
git diff --stat
mkdir -p "$build"
log=$build/mutant-build.log
tests/detent_delivery/hash_build.sh "$build" "$@" > "$log" 2>&1 || { tail -30 "$log"; no_verdict "mutant does not build"; }
tests=$(ctest --test-dir "$build" -N -R "$regex" | sed -n 's/^Total Tests: //p')
test "${tests:-0}" -gt 0 || no_verdict "no test matches $regex"
ctest --test-dir "$build" --output-on-failure --timeout 300 -R "$regex"
status=$?
if test "$status" -eq 8; then
    echo "MUTANT KILLED: $patch by ctest ($tests tests matching $regex)"
    exit 0
fi
test "$status" -eq 0 || no_verdict "ctest exited $status"
echo "MUTANT SURVIVED: $patch"
exit 1
