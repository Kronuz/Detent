#!/bin/bash
# Builds and runs the relay-delta shutdown probe in the current checkout (any tree: main has no probe, so its
# files are taken from PROBE_COMMIT) and exits with the probe's status; 124 means the run did not drain.
set -euo pipefail
commit=${1:?commit that holds the probe}
for f in tests/detent_delivery/scripted_upstream.hh tests/detent_delivery/relay_delta_shutdown.cc; do
    git show "$commit:$f" > "$f"
done
if ! grep -q relay_delta_shutdown tests/detent_delivery/CMakeLists.txt; then
    cat >> tests/detent_delivery/CMakeLists.txt <<'CMAKE'
add_executable(test_delivery_relay_delta_shutdown relay_delta_shutdown.cc)
target_include_directories(test_delivery_relay_delta_shutdown PRIVATE
  "${PROJECT_SOURCE_DIR}/src/detent_local" "${PROJECT_SOURCE_DIR}/src/detent_delivery")
target_link_libraries(test_delivery_relay_delta_shutdown PRIVATE detent::delivery_coordinator detent::relay)
CMAKE
fi
cmake -S . -B .scratch/build-probe -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=/usr/bin/clang++ \
    -DCMAKE_C_COMPILER=/usr/bin/clang -DCMAKE_OSX_SYSROOT= -DDETENT_BUILD_AUTHORITY=ON -DDETENT_BUILD_RELAY=ON \
    -DDETENT_BUILD_MANAGED_AGENT=ON -DDETENT_BUILD_EXAMPLES=OFF -DDETENT_BUILD_BENCHMARKS=OFF -DDETENT_BUILD_TESTS=ON > /dev/null
cmake --build .scratch/build-probe -j4 --target test_delivery_relay_delta_shutdown > /dev/null
ctest --test-dir .scratch/build-probe -R delivery_tls_certificates > /dev/null
dir=.scratch/build-probe/tests/detent_delivery
echo "probe from $commit on $(git rev-parse --short HEAD)"
set +e
timeout 90 "$dir/test_delivery_relay_delta_shutdown" "$dir/tls-fixture" 10000
status=$?
echo "probe exit $status"
exit $status
