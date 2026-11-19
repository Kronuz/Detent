#!/bin/bash
# Applies one mutant patch, builds the Python and Rust bindings and runs their suites. Exits 0 when
# they fail (the mutant is killed), 0 when they pass (it survives) and 126 when the patch does not apply:
# `campaign job submit --fails-without` passes on a nonzero status but 124, 126, 127.
#   bindings_mutant.sh PATCH
set -uo pipefail
patch=${1:?mutant patch}
git apply --check "$patch" && git apply "$patch" || { echo "NO VERDICT: $patch does not apply"; exit 126; }
trap 'git apply -R "$patch"' EXIT
tests/detent/one_knob_a4.sh .scratch/build-bindings-mutant
status=$?
if test "$status" -eq 0; then
	echo "MUTANT SURVIVED: $patch"
else
	echo "MUTANT KILLED: $patch (status $status)"
fi
exit "$status"
