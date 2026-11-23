#!/bin/bash
# Builds the bindings as CI's `bindings` job does and runs the scenario, export and binding tests with
# --repeat until-fail:3 (B5).
#   run.sh [PATCH [TESTS [TARGET...]]]
# With PATCH (a mutant), applies it first and exits 126 when it does not apply or build: no verdict,
# so any other nonzero exit is the tests killing it. TESTS is a ctest regex, TARGET the targets it needs.
set -uo pipefail
root=$(git rev-parse --show-toplevel)
build=$root/.scratch/bindings
patch=${1:-}
tests=${2:-'^(test_reader_scenarios|test_reader_exports|test_reader_exports_unlisted|test_testing_exports|detent_python_test|detent_rust_test)$'}
shift $(( $# > 2 ? 2 : $# ))
targets=("$@")
(( ${#targets[@]} )) || targets=(detent_python detent_rust detent_reader_shared detent_testing_shared test_reader_scenarios)
no_verdict() { echo "no verdict: $*" >&2; exit 126; }
mkdir -p "$build" || no_verdict "cannot create $build"
cmake -S "$root" -B "$build" -DCMAKE_BUILD_TYPE=Release -DDETENT_BUILD_PYTHON=ON -DDETENT_BUILD_RUST=ON \
	-DDETENT_BUILD_TESTS=ON -DDETENT_BUILD_AUTHORITY=OFF -DDETENT_BUILD_RELAY=OFF -DDETENT_BUILD_MANAGED_AGENT=ON \
	-DDETENT_BUILD_BENCHMARKS=OFF -DDETENT_BUILD_EXAMPLES=OFF > "$build/configure.log" 2>&1 || { tail -40 "$build/configure.log" >&2; no_verdict configure; }
if [[ -n $patch ]]; then
	git -C "$root" apply "$patch" || no_verdict "patch does not apply"
	trap 'git -C "$root" apply -R "$patch"' EXIT
fi
# A mutant restored with an old mtime leaves stale binaries behind (quest decision 5).
touch "$root/bindings/python/detent_python.cc" "$root/bindings/rust/src/lib.rs"
cmake --build "$build" -j4 --target "${targets[@]}" || no_verdict build
ctest --test-dir "$build" --output-on-failure --repeat until-fail:3 -R "$tests"
