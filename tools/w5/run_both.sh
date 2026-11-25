#!/usr/bin/env bash
# Runs the identity drivers of the old tree and the new tree, one after the other.
#   tools/w5/run_both.sh OLD_BUILD NEW_BUILD OUT_DIR
set -uo pipefail
here=$(cd "$(dirname "$0")" && pwd)
"$here/run_identity.sh" "$1" "$3/old" 18412
"$here/run_identity.sh" "$2" "$3/new" 18411
