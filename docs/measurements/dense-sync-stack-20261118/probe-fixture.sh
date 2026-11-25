#!/bin/bash
# usage: probe-fixture.sh old|fresh   (run in a light job at the tree under test, after build.sh)
# old: a 10K cold bootstrap from the 2026-10-23 fixture. fresh: seed 10K with this tree, then the same bootstrap.
# Exits with the bootstrap's status (124: no leader within 240 s); authority output lands in $JOB_OUT.
set -u
mode=$1
t=$PWD b=$PWD/.scratch/build-rel
here=$(cd "$(dirname "$0")" && pwd)
tr=/Users/kronuz/code/Kronuz/Detent/.scratch/capacity-trials
py=/usr/local/bin/python3
f=$b/tests/detent_authority
rm -rf "$f/tls-fixture"
OPENSSL_CONF=$t/tests/detent_authority/tls_ambient.cnf cmake -DOUTPUT="$f/tls-fixture" \
  -DOPENSSL=/usr/local/bin/openssl -P "$t/tests/detent_authority/tls_certificates.cmake" || exit 3
echo "tree=$(git rev-parse HEAD) built=$(cat "$b/BUILT_COMMIT") mode=$mode"
source=$tr/10000-r3
if [ "$mode" = fresh ]; then
  gtimeout --kill-after=20s 600s "$py" -I "$here/seed.py" "$t" "$b" "$tr/10000.json" \
    "$t/tests/detent_system/presets/capacity-5000000.json" "$JOB_OUT/fixture" || { echo "seed failed"; exit 4; }
  source=$JOB_OUT/fixture
fi
gtimeout --kill-after=20s 240s "$py" "$here/../sync-groups-20261107/coldboot.py" "$source" "$JOB_OUT/smoke" 10000 "$b" "$t" restart >"$JOB_OUT/coldboot.log" 2>&1
rc=$?
echo "coldboot rc=$rc"
grep -h '"actor_output"' "$JOB_OUT/smoke/events.jsonl" | sort -u | head -8 | cut -c1-300
rm -rf "$JOB_OUT"/smoke/state-* "$JOB_OUT"/smoke/agent-* "$JOB_OUT"/smoke/relay-* "$JOB_OUT"/fixture/state-*
exit $rc
