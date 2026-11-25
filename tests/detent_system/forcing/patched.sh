#!/bin/sh
# Applies a forcing or mutant patch to the job's checkout, then runs the command.
# Usage: patched.sh PATCH CMD [ARG ...]. The slot's next job checks out with --force, which drops it.
set -eu
git apply --whitespace=nowarn "$1"
shift
exec "$@"
