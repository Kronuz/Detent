#!/usr/bin/env bash
# usage, from the checkout root, in a light job: relay_health_check.sh
# The relay's health JSON has no disk_* field (D43): no source, test or doc names one, the relay builds
# and test_relay_service (which refuses a disk_* key in the ready health) passes.
set -eu
if grep -rnE 'disk_(queued|peak_queued|grants|maximum_wait_ns|native_submitted|fallback_submitted|maximum_outstanding)' \
	src include tests docs website --include='*.cc' --include='*.hh' --include='*.py' --include='*.md' --include='*.json' \
	--exclude=relay_health_check.sh --exclude=docs-site-audit.md; then
	echo "a disk_* health field is still named"
	exit 1
fi
build=.scratch/relay-health-build
cmake -S . -B "$build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=/usr/bin/clang++ -DCMAKE_C_COMPILER=/usr/bin/clang \
	-DCMAKE_OSX_SYSROOT= -DDETENT_BUILD_AUTHORITY=ON -DDETENT_BUILD_RELAY=ON -DDETENT_BUILD_MANAGED_AGENT=ON \
	-DDETENT_BUILD_EXAMPLES=OFF -DDETENT_BUILD_BENCHMARKS=OFF -DDETENT_BUILD_TESTS=ON >/dev/null
cmake --build "$build" -j4 --target detent-relay detentd test_relay_probe >/dev/null
ctest --test-dir "$build" -R '^test_relay_service$' --output-on-failure
