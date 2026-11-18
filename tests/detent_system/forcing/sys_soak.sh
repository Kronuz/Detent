#!/bin/sh
# Soak a system test (soak.py), then keep each failed run's stage6 directory in $JOB_OUT and print
# its member lifecycle and output tails. Usage: sys_soak.sh BUILD RUNS JOBS TEST
set -u
here=$(dirname "$0")
build=$1 runs=$2 par=$3 name=$4
rm -rf .scratch/stage6
python3 "$here/soak.py" "$build" detentd,detent-agent,detent_system_local_oracle,detent-relay "$runs" "$par" "$name"
code=$?
for d in $(command ls -d .scratch/stage6/*/ 2>/dev/null </dev/null); do
  echo "=== kept run $d"
  [ -f "$d/events.jsonl" ] && python3 "$here/actor_tail.py" "$d/events.jsonl" | tail -150
  if [ -n "${JOB_OUT-}" ]; then
    dst="$JOB_OUT/stage6/$(basename "$d")"
    (cd "$d" && find . -maxdepth 2 \( -name '*.jsonl' -o -name '*.json' \) -type f) | while read -r f; do
      mkdir -p "$dst/$(dirname "$f")" && cp "$d/$f" "$dst/$f"
    done
  fi
done
exit $code
