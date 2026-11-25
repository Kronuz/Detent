#!/usr/bin/env bash
# Sustained authority writers (three voters, HTTPS, 16 producers), old against new, with an A/A control.
#   tools/w5/run_writers_ab.sh ROUNDS OUT_DIR [OLD_BUILD OLD_SOURCE NEW_BUILD NEW_SOURCE]
# Each tree runs its own tests/detent_system/run_writers.py. Run under `quiet exclusive NAME 10 ...`.
set -uo pipefail
rounds=${1:?rounds}
out=${2:?output dir}
old_build=${3:-.scratch/build-old}
old_source=${4:-.scratch/old-wt}
new_build=${5:-.scratch/build-new}
new_source=${6:-.}
here=$(cd "$(dirname "$0")" && pwd)
ab=${AB_COMPARE:-$HOME/code/Kronuz/cpp-libraries/tools/ab_compare.py}
python3 -I "$here/ab_rounds.py" --a "python3 $here/writers_row.py $old_build $old_source" --b "python3 $here/writers_row.py $new_build $new_source" --rounds "$rounds" --out "$out" || exit 2
echo "--- http_requests_per_s (higher is better)"
python3 -I "$ab" compare --format table --higher-better --include http_requests_per_s --a "$out/a" --b "$out/b" --aa "$out/aa" --label-a old --label-b new | tee "$out/report-rate.txt"
echo "--- command_latency_mean_s (lower is better)"
python3 -I "$ab" compare --format table --include command_latency_mean_s --a "$out/a" --b "$out/b" --aa "$out/aa" --label-a old --label-b new | tee "$out/report-latency.txt"
