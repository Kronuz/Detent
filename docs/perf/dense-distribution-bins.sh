#!/bin/bash
# The build step of the dense-distribution rows, run from the job's checkout: a release build of
# the head (authority, relay, pages agent) and the records agent, the head with its opens' one
# `out.pages = true;` set to false (it never asks for pages). Leaves the tree clean.
set -eu
b=.scratch/build-dd
c=src/detent_delivery/coordinator.cc
git diff --quiet HEAD -- src include CMakeLists.txt
mkdir -p .scratch
cmake -S . -B "$b" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=/usr/bin/clang++ \
  -DCMAKE_C_COMPILER=/usr/bin/clang -DCMAKE_OSX_SYSROOT= -DDETENT_BUILD_AUTHORITY=ON -DDETENT_BUILD_RELAY=ON \
  -DDETENT_BUILD_MANAGED_AGENT=ON -DDETENT_BUILD_EXAMPLES=OFF -DDETENT_BUILD_BENCHMARKS=OFF \
  -DDETENT_BUILD_TESTS=ON >"$b.configure.log" 2>&1 || { tail -40 "$b.configure.log"; exit 1; }
cmake --build "$b" -j4 --target detent-authority detent-agent detent-relay
rm -rf "$b/dd" && mkdir "$b/dd"
cp "$b/src/detent-authority" "$b/src/detent-relay" "$b/dd/"
cp "$b/src/detent-agent" "$b/dd/detent-agent-pages"
test "$(grep -c $'^\t\tout.pages = true;$' "$c")" = 1
trap 'git checkout -- "$c"' EXIT
sed -i '' $'s/^\t\tout.pages = true;$/\t\tout.pages = false;/' "$c"
test "$(git diff --numstat -- "$c")" = "$(printf '1\t1\t%s' "$c")"
cmake --build "$b" -j4 --target detent-agent
cp "$b/src/detent-agent" "$b/dd/detent-agent-records"
git checkout -- "$c"
trap - EXIT
git diff --quiet HEAD -- src include CMakeLists.txt
! cmp -s "$b/dd/detent-agent-pages" "$b/dd/detent-agent-records"
git rev-parse HEAD >"$b/dd/BUILT_COMMIT"
