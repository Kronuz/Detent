#!/bin/bash
# usage: row.sh FIXTURE MODE...   (inside a campaign job at the tree under test, after the build)
# Runs boot.py once per MODE (warm or cold), in order, on the home's capacity-trials/FIXTURE.
set -eu
t=$PWD b=$PWD/.scratch/build-rel
here=$(cd "$(dirname "$0")" && pwd)
tr=/Users/kronuz/code/Kronuz/Detent/.scratch/capacity-trials
py=/usr/local/bin/python3
fixture=$1; shift
trap 'rm -rf "$JOB_OUT"/*/state-* "$JOB_OUT"/*/agent-* "$JOB_OUT"/*/relay-*' EXIT
test -e "$tr/$fixture/seeded.json"
test "$(cat "$b/BUILT_COMMIT")" = "$(git rev-parse HEAD)"
git diff --quiet HEAD -- CMakeLists.txt cmake include src tests
echo "fixture=$fixture modes=$* tree=$(git rev-parse HEAD) seeded=$(tr -d ' \n' <"$tr/$fixture/seeded.json")"
f=$b/tests/detent_authority
rm -rf "$f/tls-fixture"
OPENSSL_CONF=$t/tests/detent_authority/tls_ambient.cnf cmake -DOUTPUT="$f/tls-fixture" \
  -DOPENSSL=/usr/local/bin/openssl -P "$t/tests/detent_authority/tls_certificates.cmake"
/usr/local/bin/openssl x509 -in "$f/tls-fixture/one.pem" -noout -checkend 7200
n=0
for mode in "$@"; do
  n=$((n + 1))
  run=$JOB_OUT/$n-$mode
  "$py" "$here/boot.py" "$mode" "$tr/$fixture" "$run" "$b" "$t" | grep -E '^SUMMARY ' | tee -a "$JOB_OUT/summary.jsonl"
  rm -rf "$run"/state-* "$run"/agent-* "$run"/relay-*
done
