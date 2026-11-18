#!/bin/sh
# Configure a ThreadSanitizer build the way CI's linux-tsan job does, in $1.
set -eu
build=$1
cmake -S . -B "$build" -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  "-DCMAKE_CXX_FLAGS_RELWITHDEBINFO=-O1 -g -fsanitize=thread -fno-omit-frame-pointer" \
  "-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=thread" \
  -DCMAKE_CXX_COMPILER=/usr/bin/clang++ -DCMAKE_C_COMPILER=/usr/bin/clang -DCMAKE_OSX_SYSROOT= \
  -DDETENT_BUILD_AUTHORITY=ON -DDETENT_BUILD_RELAY=ON -DDETENT_BUILD_MANAGED_AGENT=ON \
  -DDETENT_BUILD_BENCHMARKS=OFF -DDETENT_BUILD_EXAMPLES=OFF -DDETENT_BUILD_TESTS=ON >"$build.configure.log" 2>&1 \
  || { tail -40 "$build.configure.log"; exit 1; }
