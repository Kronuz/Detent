#!/usr/bin/env bash
# The rows of wheel_vs_stash.sh on the local QEMU Linux VM (strata's tools/vm/aws-linux.sh run,
# never AWS), for the candidates a b c f, cross-thread at P = 1, 4 and the VM's CPUs minus 1:
#   wheel_vs_stash_vm.sh MODE [R]   MODE is smoke, single, cross or mem, as in wheel_vs_stash.sh
# Run it as an exclusive job on the host, so nothing else runs while the VM measures. stash comes
# from $JOB_REF_STASH or $STASH_DIR; $AWS_LINUX names the VM tool.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
mode=${1:?mode}
rounds=${2:-5}
stash=${JOB_REF_STASH:-${STASH_DIR:?set STASH_DIR or pin --ref stash=...}}
vm=${AWS_LINUX:-$HOME/code/Kronuz/strata/tools/vm/aws-linux.sh}
unset STRATA_AWS_VM_ALLOW
echo "pin stash=$(git -C "$stash" rev-parse HEAD 2>/dev/null || echo "$stash")"
echo "vm=$("$vm" status)"
timeout 120 "$vm" ssh true || { echo "the VM does not answer ssh within 120 s" >&2; exit 3; }

stage=$(mktemp -d "${BUILD_DIR:-$here/build-wheel-vs-stash}-vm.XXXXXX")
trap 'rm -rf "$stage"' EXIT
mkdir -p "$stage/bench" "$stage/stash"
cp "$here"/{wheel.hh,probe.hh,wheel_vs_stash.cc,wheel_vs_stash_leaf.hh,wheel_vs_stash.sh,wheel_vs_stash_summary.py} "$stage/bench/"
git -C "$stash" archive HEAD | tar -xf - -C "$stage/stash"
cat >"$stage/run.sh" <<EOF
set -euo pipefail
cd bench
export STASH_DIR=\$PWD/../stash BUILD_DIR=\$PWD/build CANDIDATES="a b c f"
export PRODUCERS="1 4 \$((\$(nproc) - 1))"
echo "vm_kernel=\$(uname -sr) vm_cpus=\$(nproc)"
rc=0
./wheel_vs_stash.sh $mode $rounds || rc=\$?
cp build/*.log "\$OUT/" 2>/dev/null || true
exit \$rc
EOF
timeout 1500 "$vm" run "$stage" "$stage/run.sh"
