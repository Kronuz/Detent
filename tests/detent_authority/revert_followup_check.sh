#!/usr/bin/env bash
# usage, from the checkout root, in a light job: revert_followup_check.sh MODE BASE
#   MODE lost-quorum: health's freshness test (leader_alive) is put back to the bare leader_known
#                     (the review's F1 mutant); test_authority_lost_quorum must fail on its assertion.
#   MODE stop:        detentd's main.cc is restored from BASE, which ignores SIGTERM and a runtime
#                     failure during the leader wait; test_authority_daemon_stop must fail on STARTUP.
# Exits 0 iff the named test fails on its own assertion, 1 if it passes or fails otherwise, 125 on a
# revert or build failure.
set -u
mode=${1:?usage: revert_followup_check.sh MODE BASE}
base=${2:?usage: revert_followup_check.sh MODE BASE}
build=.scratch/revert-followup-build
case $mode in
    lost-quorum)
        test_name=test_authority_lost_quorum pattern='^FAIL: a node without a leader'
        sed -i.orig 's/node\.worker()\.ready() && leader_alive()/node.worker().ready() \&\& leader_known/' src/detent_authority/service.cc || exit 125
        cmp -s src/detent_authority/service.cc src/detent_authority/service.cc.orig && { echo "mutation did not apply"; exit 125; }
        rm -f src/detent_authority/service.cc.orig ;;
    stop)
        test_name=test_authority_daemon_stop pattern='^STARTUP:'
        git checkout "$base" -- src/detentd/main.cc || exit 125 ;;
    *) echo "unknown mode $mode"; exit 125 ;;
esac
git diff --stat
cmake -S . -B "$build" -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=/usr/bin/clang++ -DCMAKE_C_COMPILER=/usr/bin/clang \
    -DCMAKE_OSX_SYSROOT= -DDETENT_BUILD_AUTHORITY=ON -DDETENT_BUILD_RELAY=ON -DDETENT_BUILD_MANAGED_AGENT=ON \
    -DDETENT_BUILD_EXAMPLES=OFF -DDETENT_BUILD_BENCHMARKS=OFF -DDETENT_BUILD_TESTS=ON || exit 125
cmake --build "$build" -j4 --target detentd || exit 125
output=$(ctest --test-dir "$build" -R "^${test_name}\$" --output-on-failure 2>&1)
status=$?
printf '%s\n' "$output"
if [ "$status" -eq 0 ]; then echo "the reverted code survived $test_name"; exit 1; fi
if ! grep -Eq "$pattern" <<< "$output"; then echo "$test_name failed without its assertion"; exit 1; fi
echo "the reverted code was killed by $test_name"
