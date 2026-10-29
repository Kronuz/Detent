#!/bin/sh
set -eu
root=/Users/kronuz/code/Detent/.scratch/reader-capsule-20261029
exec /usr/local/opt/llvm/bin/clang++ -O3 -DNDEBUG -std=gnu++20 \
  -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk \
  -I"$root/source/src/detent_local" -I"$root/source/include" \
  -I"$root/deps/char_classify-src" -I"$root/deps/strict_stox-src" \
  -I"$root/deps/sha256-src" -I"$root/deps/endian-src" \
  -I"$root/deps/atomic_shared_ptr-src" -I"$root/deps/flat_map-src" \
  -I"$root/deps/random_lib-src" -isystem "$root/deps/rapidjson-src/include" \
  "$root/convert-copied-fixture.cc" -o "$root/convert-copied-fixture" \
  "$root/build/src/detent_local/libdetent_local_view.a" \
  "$root/build/src/libdetent.a" "$root/build/_deps/sha256-build/libsha256.a" \
  "$root/build/_deps/random_lib-build/librandom.a" \
  "$root/build/src/detent_distribution/libdetent_resources.a"
