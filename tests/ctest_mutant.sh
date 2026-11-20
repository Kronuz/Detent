#!/bin/bash
# Applies one mutant patch, builds TARGET and runs it through ctest (so its fixtures, such as the
# delivery TLS certificates, run first). Exits the run's own nonzero status when it fails (the mutant
# is killed), 0 when it passes (it survives) and 126 when there is no verdict (the patch does not
# apply or the mutant does not build): `campaign job submit --fails-without` passes on a nonzero
# status but 124, 126 and 127.
#   tests/ctest_mutant.sh PATCH TARGET
set -uo pipefail

patch=${1:?mutant patch}
target=${2:?test target}
build=${CTEST_MUTANT_BUILD:-.scratch/build-ctest-mutant}
no_verdict() {
    echo "NO VERDICT: $patch: $1"
    git apply -R "$patch" 2>/dev/null
    exit 126
}
git apply --check "$patch" && git apply "$patch" || { echo "NO VERDICT: $patch: patch does not apply"; exit 126; }
git diff --stat
cmake -S . -B "$build" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_COMPILER=/usr/bin/clang++ -DCMAKE_C_COMPILER=/usr/bin/clang \
    -DCMAKE_OSX_SYSROOT= -DDETENT_BUILD_MANAGED_AGENT=ON -DDETENT_BUILD_AUTHORITY=ON \
    -DDETENT_BUILD_RELAY=ON -DDETENT_BUILD_BENCHMARKS=OFF -DDETENT_BUILD_EXAMPLES=OFF \
    -DDETENT_BUILD_TESTS=ON > /dev/null || no_verdict "configure failed"
cmake --build "$build" -j4 --target "$target" > /dev/null || no_verdict "mutant does not build"
ctest --test-dir "$build" -R "^$target\$" --output-on-failure --timeout 600
status=$?
git apply -R "$patch"
if test "$status" -ne 0; then
    echo "MUTANT KILLED: $patch by $target (status $status)"
else
    echo "MUTANT SURVIVED: $patch"
fi
exit "$status"
