#!/bin/bash
# usage: attribute.sh a|b|c     (run from the checkout root, in a job on its own slot)
# Attributes a T7 failure under TSan. a: the head with src/detent_local/disk_store.cc as of
# Detent:b3fef50 (pre-D1 code, new tests). b: the head with the power-cut test as of b3fef50
# (new code, old test). Both exit with the test's status.
# c: pre-D1 disk_store.cc and the head test without the cycle oracle's restart allowance; passes
# (exit 0) iff T7 fails at a cycle from a Replace completion, i.e. the oracle gap predates D1.
set -u
variant=$1
old=b3fef5099a0512c31b1ccee1b56e45ccb8b9daf7
build=.scratch/build-tsan-ab
case "$variant" in
  a|c) file=src/detent_local/disk_store.cc ;;
  b) file=tests/detent_local/test_publication_power_cut.cc ;;
  *) exit 2 ;;
esac
trap 'git checkout -q -- src tests' EXIT
git show "$old:$file" >"$file" || exit 125
if [ "$variant" = c ]; then
  cut=tests/detent_local/test_publication_power_cut.cc
  grep -q 'const bool earlier = input && index < run.ready && allowed(\*input, input_index, selected);' "$cut" || exit 125
  sed -i '' 's/const bool earlier = input && index < run.ready && allowed(\*input, input_index, selected);/const bool earlier = false;/' "$cut" || exit 125
fi
git --no-pager diff --stat
flags="-fsanitize=thread -O1 -g"
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_COMPILER=/usr/bin/clang++ -DCMAKE_C_COMPILER=/usr/bin/clang -DCMAKE_OSX_SYSROOT= \
  -DDETENT_BUILD_AUTHORITY=OFF -DDETENT_BUILD_RELAY=OFF -DDETENT_BUILD_MANAGED_AGENT=ON \
  -DDETENT_BUILD_EXAMPLES=OFF -DDETENT_BUILD_BENCHMARKS=OFF -DDETENT_BUILD_TESTS=ON \
  -DCMAKE_C_FLAGS="$flags" -DCMAKE_CXX_FLAGS="$flags" || exit 125
cmake --build "$build" -j4 --target test_local_publication_power_cut || exit 125
log=$build/attribute-$variant.log
"$build/tests/detent_local/test_local_publication_power_cut" 2>&1 | tee "$log"
rc=${PIPESTATUS[0]}
echo "attribute $variant: test_local_publication_power_cut rc=$rc"
if [ "$variant" = c ]; then
  [ "$rc" -ne 0 ] && grep -qE '^cycle from event [0-9]+ \(complete 3\) variant 2 at event [0-9]+ .* selected generation g, before n, after o$' "$log"
  exit $?
fi
exit $rc
