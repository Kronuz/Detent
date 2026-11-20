#!/bin/sh
# The forced root mismatch end to end: under relay-root-forced.patch every full copy the relay sends
# names a root its pages do not hash to, so the cold agent refuses it, asks that namespace as records
# and boots from them, to the records agent's root. Run from the repository root of a disposable checkout:
#   patched.sh tests/detent_system/forcing/relay-root-forced.patch sh tests/detent_system/forcing/relay_pages_forced.sh
# RELAY_PAGES_BUILD names a configured build to reuse (the slot's suite build): only what the patch
# touches rebuilds.
set -eu
build=${RELAY_PAGES_BUILD:-.scratch/build-relay-pages}
mkdir -p .scratch
[ -f "$build/CMakeCache.txt" ] || cmake -S . -B "$build" -DCMAKE_BUILD_TYPE=Release -DDETENT_BUILD_AUTHORITY=ON -DDETENT_BUILD_RELAY=ON \
  -DDETENT_BUILD_MANAGED_AGENT=ON -DDETENT_BUILD_BENCHMARKS=OFF -DDETENT_BUILD_EXAMPLES=OFF \
  -DDETENT_BUILD_TESTS=ON >"$build.configure.log" 2>&1 || { tail -40 "$build.configure.log"; exit 1; }
cmake --build "$build" -j4 --target detent-authority detent-agent detent-relay detent_system_local_oracle \
  >"$build.build.log" 2>&1 || { tail -60 "$build.build.log"; exit 1; }
RELAY_PAGES_EXPECT=refusal ctest --test-dir "$build" -R '^test_system_relay_pages$' --output-on-failure
