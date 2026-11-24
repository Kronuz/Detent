#!/bin/bash
# Sourced by the probes, in a consensus checkout ($JOB_SRC, the cwd). `setup asan|release`
# configures one build of consensus_liveness_test in $OUT/build-KIND (asan: CI's Debug
# ASan+UBSan; release: asserts-on -O2) and defines cxx/lnk for variant builds of the test.
# The old harness is main's test at $base; the new one is this checkout's.
set -u
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
src="$(pwd)"
OUT="${JOB_OUT:-$src/.probe-out}"; mkdir -p "$OUT/src"
base=512e3ef46c9d
san="-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer"
bounded() { perl -e 'alarm shift; exec @ARGV' "$@"; }
setup() {
	kind=$1; B="$OUT/build-$kind"
	if [ ! -f "$B/build.ninja" ]; then
		if [ "$kind" = asan ]; then
			cmake -S "$src" -B "$B" -G Ninja -DCMAKE_BUILD_TYPE=Debug "-DCMAKE_CXX_FLAGS=$san" "-DCMAKE_EXE_LINKER_FLAGS=$san" > "$OUT/configure-$kind.log" 2>&1
		else
			cmake -S "$src" -B "$B" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS_RELEASE=-O2 > "$OUT/configure-$kind.log" 2>&1
		fi || { cat "$OUT/configure-$kind.log"; return 1; }
	fi
	compile=$(ninja -C "$B" -t commands consensus_liveness_test | grep -- '-c .*liveness_test.cc' | sed 's/ -MD .*//; s/ -o [^ ]*//; s/ -c [^ ]*//')
	[ -n "$compile" ] || { echo "no compile command"; return 1; }
	linkflags=""; [ "$kind" = asan ] && linkflags="$san"
	git -C "$src" show "$base:test/liveness_test.cc" > "$OUT/src/old.cc"
	cp "$src/test/liveness_test.cc" "$OUT/src/new.cc"
}
# cxx INCLUDE_DIR SOURCE BINARY: compiles in build dir B (relative includes), INCLUDE_DIR first.
cxx() { (cd "$B" && ${compile/ -I/ -I$1 -I} -c "$2" -o "$3.o" && c++ $linkflags "$3.o" -o "$3"); }
