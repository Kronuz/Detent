#!/bin/sh
# Run the service test RUNS times and print the probe line (admitted requests after the readers loop).
set -u
runs=$1 build=.scratch/build-loop
[ -f "$build/CMakeCache.txt" ] || cmake -S . -B "$build" -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DDETENT_BUILD_AUTHORITY=ON -DDETENT_BUILD_RELAY=ON -DDETENT_BUILD_MANAGED_AGENT=ON \
  -DDETENT_BUILD_BENCHMARKS=OFF -DDETENT_BUILD_EXAMPLES=OFF -DDETENT_BUILD_TESTS=ON >"$build.configure.log" 2>&1
cmake --build "$build" -j4 --target test_authority_service >"$build.build.log" 2>&1 || { tail -40 "$build.build.log"; exit 1; }
for i in $(seq 1 "$runs"); do
  ctest --test-dir "$build" -R '^test_authority_service$' -V 2>&1 | grep -o 'probe: .*' || echo "run $i: no probe line"
done | sort | uniq -c
