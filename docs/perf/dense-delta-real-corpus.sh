#!/bin/bash
set -euo pipefail
set -x

mode=${1:?smoke (100K corpus) or 5m}
build=${2:?build directory}
: "${JOB_OUT:?campaign output directory}"
model=docs/design/dense-pages-model.py
model_sha=96dfd0a7aad2af1f8bd8dead7f85729ddd5e6aeac4ba03ce49ce1b7ea3906bd1
test "$(shasum -a 256 "$model" | cut -d ' ' -f1)" = "$model_sha"
/usr/local/bin/python3 --version
uname -a
/usr/bin/clang++ --version

cmake -S . -B "$build" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_COMPILER=/usr/bin/clang++ -DCMAKE_C_COMPILER=/usr/bin/clang \
    -DCMAKE_OSX_SYSROOT= -DDETENT_READ_COUNTERS=ON \
    -DDETENT_BUILD_MANAGED_AGENT=ON -DDETENT_BUILD_AUTHORITY=OFF \
    -DDETENT_BUILD_RELAY=OFF -DDETENT_BUILD_BENCHMARKS=OFF -DDETENT_BUILD_EXAMPLES=OFF
cmake --build "$build" --target test_local_dense_delta_assembly -j4
binary="$build/tests/detent_local/test_local_dense_delta_assembly"
file "$binary"
shasum -a 256 "$binary"

# Both modes read an extracted store corpus: the model's synthetic corpus holds keys outside the
# store key grammar (its 200-way "wide" branch), which the dense reader refuses.
: "${DENSE_CORPUS:?pinned corpus path}"
corpus=$DENSE_CORPUS
if test "$mode" = smoke; then
    "$binary"
    corpus_sha=3214561e0d3e065a94e11a2765976a0834d54ee3b5cd5e332a14c8812a1a5740
elif test "$mode" = 5m; then
    corpus_sha=bb87ae5d3a30f666e06bb236f1f826d7afd0ba8d942f9fffcec1db76152bc835
else
    exit 2
fi
test "$(shasum -a 256 "$corpus" | cut -d ' ' -f1)" = "$corpus_sha"
/usr/local/bin/python3 -I "$model" bytes "$corpus" 4096 --rule=height \
    | tee "$JOB_OUT/base-bytes.log"

for arm in one mixed; do
    if test "$arm" = one; then
        updates=1 inserts=0 deletes=0
    else
        updates=9800 inserts=100 deletes=100
    fi
    after="$JOB_OUT/$arm.bin"
    /usr/local/bin/python3 -I "$model" churn "$corpus" 4096 \
        "$updates" "$inserts" "$deletes" --rule=height --out="$after" \
        | tee "$JOB_OUT/$arm-model.log"
    shasum -a 256 "$after"
    /usr/local/bin/python3 -I "$model" bytes "$after" 4096 --rule=height \
        | tee "$JOB_OUT/$arm-bytes.log"
    "$binary" --corpus "$corpus" "$after" | tee "$JOB_OUT/$arm-native.log"
    awk '
        FILENAME == ARGV[1] {
            gsub(/,/, "")
            if (/dense projected occurrence writes/) { pages = $5; bytes = $7 }
            if (/on a changed path/) { off = $6 }
            if (/^  \(/) { gsub(/\(/, ""); offbytes = $1 }
        }
        FILENAME == ARGV[2] {
            gsub(/[,;]/, "")
            if (/^C /) { totalpages = $4; totalbytes = $6 }
            if (/root digest/) { root = $3 }
        }
        FILENAME == ARGV[3] && /^actual:/ {
            for (i = 2; i <= NF; i++) {
                split($i, kv, "="); gsub(/;/, "", kv[2]); actual[kv[1]] = kv[2]
            }
            if (actual["pages"] != totalpages || actual["pageB"] != totalbytes ||
                actual["newPages"] != pages || actual["newPageB"] != bytes ||
                actual["offPages"] != off || actual["offB"] != offbytes ||
                actual["root"] != root) {
                print "FAIL: native output differs from corrected model"; exit 1
            }
            print "PASS: exact model root, pages, page bytes, occurrence writes and off-path bytes"
            checked = 1
        }
        END { if (!checked) exit 1 }
    ' "$JOB_OUT/$arm-model.log" "$JOB_OUT/$arm-bytes.log" "$JOB_OUT/$arm-native.log"
done
