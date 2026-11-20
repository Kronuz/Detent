#!/bin/bash
# Applies one mutant patch, builds TARGET and runs it through ctest (so its fixtures, such as the
# delivery TLS certificates, run first). Exits the run's own nonzero status when it fails (the mutant
# is killed), 0 when it passes (it survives) and 126 when there is no verdict (the patch does not
# apply or the mutant does not build): `campaign job submit --fails-without` passes on a nonzero
# status but 124, 126 and 127. PATCH "-" runs the target unmutated. MUTANT_SANITIZE=address (or
# address,undefined) builds with those sanitizers, in its own build directory.
#   tests/ctest_mutant.sh PATCH TARGET
set -uo pipefail

test $# -eq 2 || { echo "NO VERDICT: usage: $0 PATCH TARGET"; exit 126; }
patch=$1
target=$2
sanitize=${MUTANT_SANITIZE:-}
build=${CTEST_MUTANT_BUILD:-.scratch/build-ctest-mutant${sanitize:+-${sanitize//,/-}}}
flags=${sanitize:+-fsanitize=$sanitize -fno-sanitize-recover=all -fno-omit-frame-pointer}
revert() { test "$patch" = - || git apply -R "$patch"; }
no_verdict() {
    echo "NO VERDICT: $patch: $1"
    revert 2>/dev/null
    exit 126
}
if test "$patch" != -; then
    git apply --check "$patch" && git apply "$patch" || { echo "NO VERDICT: $patch: patch does not apply"; exit 126; }
    git diff --stat
fi
cmake -S . -B "$build" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_COMPILER=/usr/bin/clang++ -DCMAKE_C_COMPILER=/usr/bin/clang \
    -DCMAKE_OSX_SYSROOT= -DDETENT_BUILD_MANAGED_AGENT=ON -DDETENT_BUILD_AUTHORITY=ON \
    -DDETENT_BUILD_RELAY=ON -DDETENT_BUILD_BENCHMARKS=OFF -DDETENT_BUILD_EXAMPLES=OFF \
    -DDETENT_BUILD_TESTS=ON "-DCMAKE_C_FLAGS=$flags" "-DCMAKE_CXX_FLAGS=$flags" \
    "-DCMAKE_EXE_LINKER_FLAGS=${sanitize:+-fsanitize=$sanitize}" > /dev/null || no_verdict "configure failed"
cmake --build "$build" -j4 --target "$target" > /dev/null || no_verdict "target does not build"
ctest --test-dir "$build" -R "^$target\$" --output-on-failure --timeout 600
status=$?
revert
if test "$patch" = -; then
    echo "$target unmutated${sanitize:+ under $sanitize}: status $status"
elif test "$status" -ne 0; then
    echo "MUTANT KILLED: $patch by $target (status $status)"
else
    echo "MUTANT SURVIVED: $patch"
fi
exit "$status"
