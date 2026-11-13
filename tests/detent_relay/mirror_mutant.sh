#!/bin/bash
# Applies one mutant patch from tests/detent_relay/mutants, builds TARGET and runs it. Exits 0 when the
# run fails (the mutant is killed), 1 when it passes (it survives) and 3 when there is no verdict: the
# patch does not apply or the mutant does not build.
set -uo pipefail

patch=${1:?mutant patch}
target=${2:?test target}
# test_relay_chain runs real voters, so it builds the authority; the mirror's own tests do not.
authority=OFF
test "$target" = test_relay_chain && authority=ON
build=${MIRROR_MUTANT_BUILD:-.scratch/build-mirror-mutant-$authority}
no_verdict() {
    echo "NO VERDICT: $patch: $1"
    exit 3
}
git apply --check "$patch" && git apply "$patch" || no_verdict "patch does not apply"
git diff --stat
cmake -S . -B "$build" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_COMPILER=/usr/bin/clang++ -DCMAKE_C_COMPILER=/usr/bin/clang \
    -DCMAKE_OSX_SYSROOT= -DDETENT_BUILD_MANAGED_AGENT=ON -DDETENT_BUILD_AUTHORITY=$authority \
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
    exit 0
fi
echo "MUTANT SURVIVED: $patch"
exit 1
