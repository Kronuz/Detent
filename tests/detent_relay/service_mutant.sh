#!/bin/bash
# Applies one mutant patch from tests/detent_relay/mutants, builds the voters, the relay and the
# probe, and runs test_relay_service. Exits the run's own nonzero status when it fails (the mutant is
# killed), 0 when it passes (it survives) and 126 when there is no verdict (the patch does not apply or
# does not build): `campaign job submit --fails-without` passes on a nonzero status but 124, 126, 127.
set -uo pipefail

patch=${1:?mutant patch}
build=${SERVICE_MUTANT_BUILD:-.scratch/build-service-mutant}
no_verdict() {
    echo "NO VERDICT: $patch: $1"
    git apply -R "$patch" 2>/dev/null
    exit 126
}
git apply --check "$patch" && git apply "$patch" || { echo "NO VERDICT: $patch: patch does not apply"; exit 126; }
git diff --stat
cmake -S . -B "$build" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_COMPILER=/usr/bin/clang++ -DCMAKE_C_COMPILER=/usr/bin/clang \
    -DCMAKE_OSX_SYSROOT= -DDETENT_BUILD_AUTHORITY=ON -DDETENT_BUILD_RELAY=ON \
    -DDETENT_BUILD_BENCHMARKS=OFF -DDETENT_BUILD_EXAMPLES=OFF -DDETENT_BUILD_TESTS=ON > /dev/null ||
    no_verdict "configure failed"
cmake --build "$build" -j4 --target detent-authority detent-relay test_relay_probe > /dev/null || no_verdict "mutant does not build"
(cd "$build" && ctest -R '^(authority_tls_certificates|test_relay_service)$' --output-on-failure)
status=$?
git apply -R "$patch"
if test "$status" -ne 0; then
    echo "MUTANT KILLED: $patch by test_relay_service (status $status)"
else
    echo "MUTANT SURVIVED: $patch"
fi
exit "$status"
