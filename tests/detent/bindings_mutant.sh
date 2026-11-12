#!/bin/bash
# Applies one mutant patch, builds the Python and Rust bindings and runs their suites. Exits 0 when
# they fail (the mutant is killed), 1 when they pass (it survives).
#   bindings_mutant.sh PATCH
set -uo pipefail
patch=${1:?mutant patch}
git apply --check "$patch" && git apply "$patch" || { echo "NO VERDICT: $patch does not apply"; exit 3; }
trap 'git apply -R "$patch"' EXIT
if tests/detent/one_knob_a4.sh .scratch/build-bindings-mutant; then
	echo "MUTANT SURVIVED: $patch"
	exit 1
fi
echo "MUTANT KILLED: $patch"
