#!/bin/bash
# Applies one namespace S3b mutant patch from tests/detent_delivery/mutants, builds TARGETS and runs the
# tests matching REGEX. Exits 0 when a test fails (the mutant is killed), 1 when every test passes (it
# survives) and 3 when there is no verdict: the patch does not apply, the mutant does not build, no test
# matches, or ctest fails other than by a test failing.
set -uo pipefail

patch=${1:?mutant patch}
regex=${2:?ctest regex}
shift 2
test "$#" -gt 0 || { echo "usage: $0 PATCH REGEX TARGET..."; exit 3; }
build=${REPAIR_MUTANT_BUILD:-.scratch/build-mutant}
no_verdict() {
    echo "NO VERDICT: $patch: $1"
    exit 3
}
git apply --check "$patch" && git apply "$patch" || no_verdict "patch does not apply"
trap 'git apply -R "$patch"' EXIT
git diff --stat
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_COMPILER=/usr/bin/clang++ -DCMAKE_C_COMPILER=/usr/bin/clang \
    -DCMAKE_OSX_SYSROOT= -DDETENT_BUILD_MANAGED_AGENT=ON -DDETENT_BUILD_AUTHORITY=ON \
    -DDETENT_BUILD_RELAY=ON -DDETENT_BUILD_BENCHMARKS=OFF -DDETENT_BUILD_EXAMPLES=OFF \
    -DDETENT_BUILD_TESTS=ON > /dev/null || no_verdict "configure failed"
cmake --build "$build" -j4 --target "$@" > /dev/null || no_verdict "mutant does not build"
tests=$(ctest --test-dir "$build" -N -R "$regex" | sed -n 's/^Total Tests: //p')
test "${tests:-0}" -gt 0 || no_verdict "no test matches $regex"
ctest --test-dir "$build" --output-on-failure --timeout 600 -R "$regex"
status=$?
# ctest exits 8 when a test fails or times out; anything else is ctest's own failure.
if test "$status" -eq 8; then
    echo "MUTANT KILLED: $patch by ctest ($tests tests matching $regex)"
    exit 0
fi
test "$status" -eq 0 || no_verdict "ctest exited $status"
echo "MUTANT SURVIVED: $patch"
exit 1
