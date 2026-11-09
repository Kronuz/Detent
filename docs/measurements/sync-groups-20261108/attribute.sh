#!/bin/bash
# usage: attribute.sh a|b     (run from the checkout root, in a job on its own slot)
# Attributes a T7 failure under TSan. a: the head with src/detent_local/disk_store.cc as of
# Detent:b3fef50 (pre-D1 code, new tests). b: the head with the power-cut test as of b3fef50
# (new code, old test). Exits with the test's status.
set -u
variant=$1
old=b3fef5099a0512c31b1ccee1b56e45ccb8b9daf7
build=.scratch/build-tsan-ab
case "$variant" in
  a) file=src/detent_local/disk_store.cc ;;
  b) file=tests/detent_local/test_publication_power_cut.cc ;;
  *) exit 2 ;;
esac
trap 'git checkout -q -- src tests' EXIT
git show "$old:$file" >"$file" || exit 125
git --no-pager diff --stat
flags="-fsanitize=thread -O1 -g"
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_COMPILER=/usr/bin/clang++ -DCMAKE_C_COMPILER=/usr/bin/clang -DCMAKE_OSX_SYSROOT= \
  -DDETENT_BUILD_AUTHORITY=OFF -DDETENT_BUILD_RELAY=OFF -DDETENT_BUILD_MANAGED_AGENT=ON \
  -DDETENT_BUILD_EXAMPLES=OFF -DDETENT_BUILD_BENCHMARKS=OFF -DDETENT_BUILD_TESTS=ON \
  -DCMAKE_C_FLAGS="$flags" -DCMAKE_CXX_FLAGS="$flags" || exit 125
cmake --build "$build" -j4 --target test_local_publication_power_cut || exit 125
"$build/tests/detent_local/test_local_publication_power_cut"
rc=$?
echo "attribute $variant: test_local_publication_power_cut rc=$rc"
exit $rc
