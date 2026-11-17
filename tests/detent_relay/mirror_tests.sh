#!/usr/bin/env bash
# usage, from the checkout root, in a light job: mirror_tests.sh
# Builds and runs the relay mirror's own tests: the scripted upstream and the crash model.
set -eu
build=.scratch/mirror-tests-build
cmake -S . -B "$build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=/usr/bin/clang++ -DCMAKE_C_COMPILER=/usr/bin/clang \
	-DCMAKE_OSX_SYSROOT= -DDETENT_BUILD_MANAGED_AGENT=ON -DDETENT_BUILD_AUTHORITY=OFF -DDETENT_BUILD_RELAY=OFF \
	-DDETENT_BUILD_EXAMPLES=OFF -DDETENT_BUILD_BENCHMARKS=OFF -DDETENT_BUILD_TESTS=ON >/dev/null
cmake --build "$build" -j4 --target test_relay_mirror test_relay_mirror_crash >/dev/null
ctest --test-dir "$build" -R '^test_relay_mirror(_crash)?$' --output-on-failure
