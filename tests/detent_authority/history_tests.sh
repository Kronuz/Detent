#!/usr/bin/env bash
# usage, from the checkout root, in a light job: history_tests.sh TEST...
# Builds the named history test targets and runs each; exits nonzero when any fails.
set -eu
build=.scratch/history-tests-build
cmake -S . -B "$build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=/usr/bin/clang++ -DCMAKE_C_COMPILER=/usr/bin/clang \
	-DCMAKE_OSX_SYSROOT= -DDETENT_BUILD_AUTHORITY=ON -DDETENT_BUILD_RELAY=ON -DDETENT_BUILD_MANAGED_AGENT=ON \
	-DDETENT_BUILD_EXAMPLES=OFF -DDETENT_BUILD_BENCHMARKS=OFF -DDETENT_BUILD_TESTS=ON >/dev/null
cmake --build "$build" -j4 --target "$@" >/dev/null
for test in "$@"; do
	binary=$(find "$build" -name "$test" -type f -perm -u+x | head -n 1)
	echo "== $test"
	if [ "$test" = test_authority_history_transport ]; then
		ctest --test-dir "$build" -R '^authority_tls_certificates$' >/dev/null
		"$binary" "$(dirname "$binary")/tls-fixture"
	else
		"$binary"
	fi
done
