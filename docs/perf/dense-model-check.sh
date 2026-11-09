#!/bin/bash
# Builds the dense packer and full-build tests and compares their pages, placements, packs and
# root against the model on a pinned corpus: `100k` (also the model's synthetic corpus) or `5m`.
set -euo pipefail
set -x

mode=${1:?100k or 5m}
build=${2:?build directory}
: "${JOB_OUT:?campaign output directory}"
: "${DENSE_CORPUS:?pinned corpus path}"
model=docs/design/dense-pages-model.py
test "$(shasum -a 256 "$model" | cut -d ' ' -f1)" = \
    96dfd0a7aad2af1f8bd8dead7f85729ddd5e6aeac4ba03ce49ce1b7ea3906bd1
if test "$mode" = 100k; then
    corpus_sha=3214561e0d3e065a94e11a2765976a0834d54ee3b5cd5e332a14c8812a1a5740
elif test "$mode" = 5m; then
    corpus_sha=bb87ae5d3a30f666e06bb236f1f826d7afd0ba8d942f9fffcec1db76152bc835
else
    exit 2
fi
test "$(shasum -a 256 "$DENSE_CORPUS" | cut -d ' ' -f1)" = "$corpus_sha"
/usr/local/bin/python3 --version
/usr/bin/clang++ --version

cmake -S . -B "$build" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_COMPILER=/usr/bin/clang++ -DCMAKE_C_COMPILER=/usr/bin/clang \
    -DCMAKE_OSX_SYSROOT= -DDETENT_BUILD_MANAGED_AGENT=ON -DDETENT_BUILD_AUTHORITY=OFF \
    -DDETENT_BUILD_RELAY=OFF -DDETENT_BUILD_BENCHMARKS=OFF -DDETENT_BUILD_EXAMPLES=OFF
cmake --build "$build" --target test_local_paged_packer test_local_dense_fullbuild -j4
packer="$build/tests/detent_local/test_local_paged_packer"
fullbuild="$build/tests/detent_local/test_local_dense_fullbuild"
shasum -a 256 "$packer" "$fullbuild"
"$packer"
"$fullbuild"

compare() {
    local name=$1 corpus=$2 out="$JOB_OUT/$1"
    /usr/local/bin/python3 -I "$model" bytes "$corpus" 4096 "$out" --rule=height \
        | tee "$JOB_OUT/$name-model.log"
    "$packer" "$corpus" "$out" | tee "$JOB_OUT/$name-packer.log"
    if test "$name" != synthetic; then
        "$fullbuild" "$corpus" "$out" | tee "$JOB_OUT/$name-fullbuild.log"
    fi
    rm -rf "$out"
}
if test "$mode" = 100k; then
    /usr/local/bin/python3 -I "$model" synthetic "$JOB_OUT/synthetic.bin"
    compare synthetic "$JOB_OUT/synthetic.bin"
fi
compare "$mode" "$DENSE_CORPUS"
