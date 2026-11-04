#!/usr/bin/env bash
# One hot-path A/B: old against new, interleaved, with an A/A control, then the ab_compare.py report.
#   tools/w5/run_ab.sh KIND ROUNDS OUT_DIR [OLD_BUILD NEW_BUILD]
# KIND is a bench_rows.sh kind (read, authority-update, authority-durable, authority-read). Run it under
# `quiet exclusive NAME 10 "what" -- tools/w5/run_ab.sh ...`.
set -uo pipefail
kind=${1:?kind}
rounds=${2:?rounds}
out=${3:?output dir}
old=${4:-.scratch/build-old}
new=${5:-.scratch/build-new}
here=$(cd "$(dirname "$0")" && pwd)
ab=${AB_COMPARE:-$HOME/code/Kronuz/cpp-libraries/tools/ab_compare.py}
export W5_SCRATCH=${W5_SCRATCH:-$PWD/.scratch}
python3 -I "$here/ab_rounds.py" --a "$here/bench_rows.sh $old $kind" --b "$here/bench_rows.sh $new $kind" --rounds "$rounds" --out "$out" || exit 2
python3 -I "$ab" compare --format unit --a "$out/a" --b "$out/b" --aa "$out/aa" --label-a old --label-b new | tee "$out/report.txt"
