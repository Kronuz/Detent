#!/bin/bash
# Reader core lookup cost against the store's namespace count: 1 and 1,000 namespaces of the same
# 20,000 knob pairs, hot (one key) and scattered (keys across every namespace, each attached).
# Usage: benchmarks/reader_namespaces.sh BUILD_DIR [SCRATCH_DIR]
set -euo pipefail
build=$1
scratch=${2:-$build/.scratch/reader-namespaces}
bench=$build/benchmarks/reader_bench
rm -rf "$scratch" && mkdir -p "$scratch"
for n in 1 1000; do
	store=$scratch/ns$n
	"$bench" --store "$store" --generate 20000 --namespaces "$n" >/dev/null
	echo "namespaces $n:"
	"$bench" --store "$store" --keys "$store.keys" --reader core --hot n0:i0 --calls 1000000 --repetitions 5
done
rm -rf "$scratch"
