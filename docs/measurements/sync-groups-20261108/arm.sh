#!/bin/bash
# usage: arm.sh before|after RECORDS ROUND [aa]     (run in an exclusive job)
# One arm of the cold-bootstrap and restart measurement: a 10K smoke on the same binaries, then the
# timed run (cold bootstrap of three restored voters, one relay and one agent, then three agent
# restarts). The arm's binaries and tree are a build slot made by build.sh; the 1-day TLS fixture is
# regenerated first (it stalls the run with no leader once stale).
set -eu
arm=$1 records=$2 round=$3 stage=${4:-confirm}
here=$(cd "$(dirname "$0")" && pwd)
slots=${CAMPAIGN_JOBS:-$HOME/.scratch/campaign/jobs}/slots
before_commit=327ca582f653f913142138449ab4795debc03e41
tree=$slots/s1s2-$arm
build=$tree/.scratch/build-rel
trials=/Users/kronuz/code/Kronuz/Detent/.scratch/capacity-trials
case $records in
  10000) source=$trials/10000-r3 ;;
  100000) source=$trials/100000-r3 ;;
  5000000) source=$trials/5000000-flat-r1 ;;
  *) echo "unknown records $records" >&2; exit 2 ;;
esac
py=/usr/local/bin/python3
out=${JOB_OUT:?}
cleanup() { rm -rf "$out"/smoke/state-* "$out"/run/state-* "$out"/run/agent-* "$out"/run/relay-* "$out"/run/runtime-*; }
trap cleanup EXIT

# The slot must hold the code this arm names, built by build.sh.
test "$(cat "$build/BUILT_COMMIT")" = "$(git -C "$tree" rev-parse HEAD)"
if [ "$arm" = before ]; then
  test "$(git -C "$tree" rev-parse HEAD)" = "$before_commit"
else
  git diff --quiet "$(git -C "$tree" rev-parse HEAD)" HEAD -- CMakeLists.txt cmake include src tests
fi
git -C "$tree" diff --quiet HEAD -- CMakeLists.txt cmake include src tests
echo "arm=$arm records=$records round=$round stage=$stage tree=$(git -C "$tree" rev-parse HEAD) job=$(git rev-parse HEAD)"
file "$build/src/detentd" "$build/src/detent-agent" "$build/src/detent-relay"

fixture=$build/tests/detent_authority
rm -rf "$fixture/tls-fixture"
OPENSSL_CONF=$tree/tests/detent_authority/tls_ambient.cnf cmake -DOUTPUT="$fixture/tls-fixture" \
  -DOPENSSL=/usr/local/bin/openssl -P "$tree/tests/detent_authority/tls_certificates.cmake"
/usr/local/bin/openssl x509 -in "$fixture/tls-fixture/one.pem" -noout -checkend 7200

# Smoke: 10K on the same binaries (the harness refuses a staged run without it).
"$py" "$here/../sync-groups-20261107/coldboot.py" "$trials/10000-r3" "$out/smoke" 10000 "$build" "$tree" restart
"$py" "$here/summarize.py" "$out/smoke"

"$py" "$here/../sync-groups-20261107/coldboot.py" "$source" "$out/run" "$records" "$build" "$tree" restart "--stage=$stage"
"$py" "$here/summarize.py" "$out/run" --require-counters
