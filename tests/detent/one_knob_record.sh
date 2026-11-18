#!/bin/bash
# Records main's answers for A1's cells (one_knob_record.cc) into OUT, which is committed as
# tests/detent/one_knob_main.hex. Run as a job with --ref BASELINE=<main>.
#   one_knob_record.sh OUT
set -euo pipefail
out=${1:?OUT}
here=$PWD
base=${JOB_REF_BASELINE:?--ref BASELINE}
cat >> "$base/CMakeLists.txt" <<CMAKE

add_executable(one_knob_record "$here/tests/detent/one_knob_record.cc")
target_include_directories(one_knob_record PRIVATE "$here/tests/detent")
target_link_libraries(one_knob_record PRIVATE detent::detent)
CMAKE
cmake -S "$base" -B "$base/.scratch/okt-record" -DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_CXX_COMPILER=/usr/bin/clang++ -DCMAKE_C_COMPILER=/usr/bin/clang -DCMAKE_OSX_SYSROOT= \
	-DDETENT_BUILD_AUTHORITY=OFF -DDETENT_BUILD_RELAY=OFF -DDETENT_BUILD_EXAMPLES=OFF \
	-DDETENT_BUILD_BENCHMARKS=OFF -DDETENT_BUILD_MANAGED_AGENT=OFF -DDETENT_BUILD_TESTS=OFF >/dev/null
cmake --build "$base/.scratch/okt-record" -j4 --target one_knob_record
git -C "$base" checkout -- CMakeLists.txt
mkdir -p "$(dirname "$out")"
{
	echo "# A1: main's eval for every cell of one_knob_cells.hh, one bit per cell (tests/detent/one_knob_record.sh)"
	echo "# main $(git -C "$base" rev-parse HEAD)"
	"$base/.scratch/okt-record/one_knob_record"
} > "$out"
rm -rf "$base/.scratch/okt-record"
cat "$out"
