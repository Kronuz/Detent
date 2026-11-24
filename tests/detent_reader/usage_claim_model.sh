#!/bin/sh
# Builds and runs the usage claim-protocol model under TSan (docs/design/observability.md P2).
# Output goes to $JOB_OUT, or .scratch/usage-claim-model under the repository.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
out=${JOB_OUT:-$here/../../.scratch/usage-claim-model}
mkdir -p "$out"
${CXX:-c++} -std=c++20 -O1 -g -fsanitize=thread -o "$out/usage_claim_model" "$here/usage_claim_model.cc"
TSAN_OPTIONS=halt_on_error=1 "$out/usage_claim_model" | tee "$out/usage_claim_model.txt"
