#!/bin/bash
# A4 of one-knob-type: no trace of two knob types in code, then the Python and Rust bindings build
# and pass their suites on the single-type API.
#   one_knob_a4.sh BUILD
set -euo pipefail
build=${1:?BUILD}
pattern='\b(BoolDef|IntDef|is_bool|is_int|bool_count|int_count|eval_or_panic)\b|\.eval\('
# Excluded: history (prototype patches for an isolated old checkout, recorded benchmark JSON), the
# recorder that runs against main's tree for A1, and this script.
if git grep -nE "$pattern" -- src include bindings tests tools examples benchmarks docs/LOCAL_IMAGE_FORMAT.md \
	':!benchmarks/container-alternative/*.patch' ':!benchmarks/*.json' ':!tests/detent/one_knob_record.cc' \
	':!tests/detent/one_knob_a4.sh'; then
	echo "A4: two-type traces above" >&2
	exit 1
fi
echo "A4 grep: no two-type traces"
cmake -S . -B "$build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=/usr/bin/clang++ -DCMAKE_C_COMPILER=/usr/bin/clang \
	-DCMAKE_OSX_SYSROOT= -DDETENT_BUILD_TESTS=OFF -DDETENT_BUILD_EXAMPLES=OFF -DDETENT_BUILD_BENCHMARKS=OFF \
	-DDETENT_BUILD_AUTHORITY=OFF -DDETENT_BUILD_RELAY=OFF -DDETENT_BUILD_PYTHON=ON -DDETENT_BUILD_RUST=ON \
	-DPython3_EXECUTABLE=/usr/local/bin/python3.14 >/dev/null
cmake --build "$build" -j4 --target detent_python_test detent_rust_test
echo "A4 bindings: Python and Rust suites passed"
