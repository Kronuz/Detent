#!/usr/bin/env bash
# usage, from the checkout root, in a light job: revert_ready_check.sh BASE
# Restores detent-authority's readiness from BASE (the commit before the leader-ready change), builds it and
# runs test_daemon_ready.sh against it. Exits 0 iff the test fails on its READINESS assertion, 1 if it
# passes or fails otherwise, and 125 when the revert or the build fails.
set -u
base=${1:?usage: revert_ready_check.sh BASE}
build=.scratch/revert-ready-build
# BASE predates the rename, so its daemon main lives at src/detentd/main.cc.
git show "$base:src/detentd/main.cc" >src/detent_authority/main.cc || exit 125
git checkout "$base" -- src/detent_authority/service.cc src/detent_authority/service.hh || exit 125
git diff --stat -- src/detent_authority
cmake -S . -B "$build" -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=/usr/bin/clang++ -DCMAKE_C_COMPILER=/usr/bin/clang \
    -DCMAKE_OSX_SYSROOT= -DDETENT_BUILD_AUTHORITY=ON -DDETENT_BUILD_RELAY=ON -DDETENT_BUILD_MANAGED_AGENT=ON \
    -DDETENT_BUILD_EXAMPLES=OFF -DDETENT_BUILD_BENCHMARKS=OFF -DDETENT_BUILD_TESTS=ON || exit 125
cmake --build "$build" -j4 --target detent-authority || exit 125
binary=$(find "$build" -name detent-authority -type f -perm -u+x | head -n 1)
[ -n "$binary" ] || exit 125
output=$(bash tests/detent_authority/test_daemon_ready.sh "$PWD/$binary" 2>&1)
status=$?
printf '%s\n' "$output"
if [ "$status" -eq 0 ]; then echo "old readiness survived the new test"; exit 1; fi
if ! grep -q '^READINESS:' <<< "$output"; then echo "test failed without a READINESS assertion"; exit 1; fi
echo "old readiness killed by the new test"
