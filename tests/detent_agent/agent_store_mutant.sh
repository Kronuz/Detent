#!/bin/bash
# Applies one mutant patch from tests/detent_agent/mutants, builds TARGET and runs it with ARGS.
# Exits the run's own nonzero status when it fails (the mutant is killed), 0 when it passes (it
# survives) and 126 when there is no verdict (the patch does not apply or the mutant does not build):
# `campaign job submit --fails-without` passes on a nonzero status but 124, 126, 127.
set -uo pipefail

patch=${1:?mutant patch}
target=${2:?test target}
build=${AGENT_MUTANT_BUILD:-.scratch/build-mutant}
no_verdict() {
    echo "NO VERDICT: $patch: $1"
    exit 126
}
git apply --check "$patch" && git apply "$patch" || no_verdict "patch does not apply"
git diff --stat
cmake -S . -B "$build" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_COMPILER=/usr/bin/clang++ -DCMAKE_C_COMPILER=/usr/bin/clang \
    -DCMAKE_OSX_SYSROOT= -DDETENT_BUILD_MANAGED_AGENT=ON -DDETENT_BUILD_AUTHORITY=OFF \
    -DDETENT_BUILD_RELAY=OFF -DDETENT_BUILD_BENCHMARKS=OFF -DDETENT_BUILD_EXAMPLES=OFF \
    -DDETENT_BUILD_TESTS=ON > /dev/null || no_verdict "configure failed"
cmake --build "$build" -j4 --target "$target" > /dev/null || no_verdict "mutant does not build"
binary=$(find "$build/tests" -type f -name "$target" -perm -u+x | head -1)
test -n "$binary" || no_verdict "no $target binary"
"$binary" "$build/mutant-scratch"
status=$?
git apply -R "$patch"
if test "$status" -ne 0; then
    echo "MUTANT KILLED: $patch by $target (status $status)"
else
    echo "MUTANT SURVIVED: $patch"
fi
exit "$status"
