#!/bin/bash
# Counts how often the scrub loop skips a report for a root the store no longer selects (the `continue` that
# posts the next slice about every millisecond) over the repair tests. Applies the counting patch, builds
# test_delivery_repair, runs each mode and prints the skip count and the span of each burst; reverts the patch.
set -uo pipefail
patch=tests/detent_testing/probes/s3b-n5-obsolete-count.patch
build=${N5_BUILD:-.scratch/build-n5}
git apply --check "$patch" && git apply "$patch" || { echo "NO VERDICT: patch does not apply"; exit 3; }
trap 'git apply -R "$patch"' EXIT
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_COMPILER=/usr/bin/clang++ -DCMAKE_C_COMPILER=/usr/bin/clang \
    -DCMAKE_OSX_SYSROOT= -DDETENT_BUILD_MANAGED_AGENT=ON -DDETENT_BUILD_AUTHORITY=ON \
    -DDETENT_BUILD_RELAY=ON -DDETENT_BUILD_BENCHMARKS=OFF -DDETENT_BUILD_EXAMPLES=OFF \
    -DDETENT_BUILD_TESTS=ON > /dev/null || { echo "NO VERDICT: configure failed"; exit 3; }
cmake --build "$build" -j4 --target test_delivery_repair > "$build/n5-build.log" 2>&1 ||
    { tail -40 "$build/n5-build.log"; echo "NO VERDICT: build failed"; exit 3; }
fixture="$build/tests/detent_delivery/tls-fixture"
test -d "$fixture" || ctest --test-dir "$build" -R '^delivery_tls_certificates$' > /dev/null
test -d "$fixture" || { echo "NO VERDICT: no tls fixture"; exit 3; }
status=0
for mode in heal lease-close delta unchanged relay scrub-worker reset delta-clears forced-again; do
    log="$build/n5-$mode.log"
    "$build/tests/detent_delivery/test_delivery_repair" "$fixture" "$mode" > "$log" 2>&1 || { echo "$mode: test failed"; status=1; }
    count=$(grep -c N5-OBSOLETE-SKIP "$log")
    span=$(grep N5-OBSOLETE-SKIP "$log" | awk 'NR==1{first=$2} {last=$2} END{if (NR) print (last-first)/1000 " ms first to last"; else print "none"}')
    echo "$mode: $count obsolete skips ($span)"
done
exit $status
