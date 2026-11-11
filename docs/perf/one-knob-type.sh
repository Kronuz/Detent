#!/bin/bash
# A3 of one-knob-type: page bytes, read bench and first read, main against the one-knob tree, on the
# 100K and 5M corpora (docs/perf/one-knob-type.md).
#   one-knob-type.sh corpora OUT                 light: fixed and varint corpora from each pinned corpus
#   one-knob-type.sh pages OUT SIZE              light, --ref BASELINE=<main>: page bytes from each tree's
#       full build and from the dense model, for main's, the fixed and the varint bodies
#   one-knob-type.sh prepare OUT                 light, --ref BASELINE=<main>: Release builds of both trees
#       and 1-namespace stores of both corpora, each from its own tree's exporter
#   one-knob-type.sh measure PREPARED OUT SIZE   exclusive: interleaved pairs (D51), docs/perf/one-knob-type.py
set -euo pipefail
llvm=/usr/local/opt/llvm/bin
sdk=$(sed -n 's/^-isysroot //p' "$("$llvm/clang++" --version | sed -n 's/^Configuration file: //p')")
dense=/Users/kronuz/code/Kronuz/Detent/.scratch/dense
python=/usr/local/bin/python3
here=$PWD

corpus() { # SIZE ARM: main's pinned corpus, or its converted copy under OUT/corpora
	if [ "$2" = main ]; then echo "$dense/corpus-$1.bin"; else echo "${CORPORA:?CORPORA}/corpus-$1-$2.bin"; fi
}

corpora() {
	local out=$1
	mkdir -p "$out"
	shasum -a 256 "$dense/corpus-100k.bin" "$dense/corpus-5m.bin" | tee "$out/pinned.sha256"
	for size in 100k 5m; do
		for format in fixed varint; do
			"$python" -I docs/perf/one-knob-type-corpus.py convert "$dense/corpus-$size.bin" "$out/corpus-$size-$format.bin" \
				$([ "$format" = varint ] && echo --varint) | tee "$out/census-$size-$format.json"
		done
	done
	shasum -a 256 "$out"/corpus-*.bin | tee "$out/converted.sha256"
}

build() { # SRC BUILD TARGET... with the first-read gate's toolchain checks
	local src=$1 build=$2
	shift 2
	cmake -S "$src" -B "$build" -DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_CXX_COMPILER="$llvm/clang++" -DCMAKE_C_COMPILER="$llvm/clang" -DCMAKE_OSX_SYSROOT="$sdk" \
		-DDETENT_BUILD_AUTHORITY=OFF -DDETENT_BUILD_RELAY=OFF -DDETENT_BUILD_EXAMPLES=OFF \
		-DDETENT_BUILD_BENCHMARKS=OFF -DDETENT_BUILD_MANAGED_AGENT=ON -DDETENT_BUILD_TESTS=ON \
		-DDETENT_READ_COUNTERS=OFF >/dev/null
	grep -q 'set(CMAKE_CXX_COMPILER_VERSION "23\.1\.2' "$build"/CMakeFiles/*/CMakeCXXCompiler.cmake
	cmake --build "$build" -j4 --target "$@"
}

pages() {
	local out=$1 size=$2
	mkdir -p "$out"
	for arm in main fixed; do
		local src=$here
		[ "$arm" = main ] && src=${JOB_REF_BASELINE:?--ref BASELINE}
		git -C "$src" rev-parse HEAD > "$out/$arm-commit.txt"
		build "$src" "$src/.scratch/okt-pages" test_local_dense_fullbuild
		"$src/.scratch/okt-pages/tests/detent_local/test_local_dense_fullbuild" "$(corpus "$size" "$arm")" "" |
			tee "$out/fullbuild-$size-$arm.log"
		rm -rf "$src/.scratch/okt-pages"
	done
	for arm in main fixed varint; do
		"$python" -I docs/design/dense-pages-model.py pack "$(corpus "$size" "$arm")" 4096 --rule=height |
			tee "$out/model-$size-$arm.log"
	done
}

prepare() {
	local out=$1
	mkdir -p "$out"
	"$llvm/clang++" --version | tee "$out/toolchain.txt"
	for arm in main fixed; do
		local src=$here define=
		if [ "$arm" = main ]; then src=${JOB_REF_BASELINE:?--ref BASELINE}; define=ONE_KNOB_MAIN; fi
		mkdir -p "$out/$arm"
		git -C "$src" rev-parse HEAD > "$out/$arm/commit.txt"
		cat >> "$src/CMakeLists.txt" <<EOF

add_executable(first_read_export "$src/tests/detent_system/first_read_export.cc")
target_include_directories(first_read_export PRIVATE "\${PROJECT_SOURCE_DIR}/src/detent_local" "\${PROJECT_SOURCE_DIR}/tests/detent_local")
target_link_libraries(first_read_export PRIVATE detent::local_publication)
add_executable(one_knob_read "$here/docs/perf/one-knob-type-read.cc")
target_compile_definitions(one_knob_read PRIVATE $define)
target_link_libraries(one_knob_read PRIVATE detent::local_client)
EOF
		build "$src" "$src/.scratch/okt-build" first_read_export one_knob_read
		git -C "$src" checkout -- CMakeLists.txt
		cp "$src/.scratch/okt-build/one_knob_read" "$out/$arm/one_knob_read"
		for size in 100k 5m; do
			"$src/.scratch/okt-build/first_read_export" "$(corpus "$size" "$arm")" "$out/$arm/store-$size" 1 3 |
				tee "$out/$arm/export-$size.json"
		done
		rm -rf "$src/.scratch/okt-build"
	done
}

case ${1:-} in
corpora) corpora "${2:?OUT}" ;;
pages) pages "${2:?OUT}" "${3:?100k or 5m}" ;;
prepare) prepare "${2:?OUT}" ;;
measure) "$python" -I docs/perf/one-knob-type.py --prepared "${2:?PREPARED}" --output "${3:?OUT}" --size "${4:?SIZE}" \
	--corpus-main "$(corpus "$4" main)" --corpus-fixed "$(corpus "$4" fixed)" ;;
*) echo "usage: $0 corpora OUT | pages OUT SIZE | prepare OUT | measure PREPARED OUT SIZE" >&2; exit 2 ;;
esac
