#!/bin/bash
# usage: dense-distribution-row.sh FIXTURE ARM...   (in a campaign job, after dense-distribution-bins.sh)
# Boots a priming pages agent, a priming records agent, then one cold agent per ARM (pages or
# records), in order, from one warm relay over the home's capacity-trials/FIXTURE.
set -eu
t=$PWD b=$PWD/.scratch/build-dd
tr=/Users/kronuz/code/Kronuz/Detent/.scratch/capacity-trials
py=/usr/local/bin/python3
fixture=$1; shift
run=$JOB_OUT/run
trap 'rm -rf "$run"/state-* "$run"/*/agent-* "$run"/relay-*' EXIT
test -e "$tr/$fixture/seeded.json"
test "$(cat "$b/dd/BUILT_COMMIT")" = "$(git rev-parse HEAD)"
git diff --quiet HEAD -- CMakeLists.txt cmake include src tests
echo "fixture=$fixture arms=$* tree=$(git rev-parse HEAD) seeded=$(tr -d ' \n' <"$tr/$fixture/seeded.json")"
f=$b/tests/detent_authority
rm -rf "$f/tls-fixture"
OPENSSL_CONF=$t/tests/detent_authority/tls_ambient.cnf cmake -DOUTPUT="$f/tls-fixture" \
  -DOPENSSL=/usr/local/bin/openssl -P "$t/tests/detent_authority/tls_certificates.cmake"
/usr/local/bin/openssl x509 -in "$f/tls-fixture/one.pem" -noout -checkend 7200
"$py" "$t/docs/perf/dense-distribution-boot.py" "$tr/$fixture" "$run" "$b/dd" "$t" "$@" | tee "$JOB_OUT/boot.log" |
  grep --line-buffered -E '^(PHASE|AGENT|BOOT FAILED)' | cut -c1-400
status=${PIPESTATUS[0]}
grep -E '^AGENT ' "$JOB_OUT/boot.log" | sed 's/^AGENT //' >"$JOB_OUT/agents.jsonl" || true
test "$status" = 0
shopt -s nullglob
samples=("$run"/*/sample.txt)
if [ ${#samples[@]} -gt 0 ]; then
  "$py" "$t/docs/perf/dense-distribution-profile.py" "${samples[@]}" | tee "$JOB_OUT/profile.jsonl"
fi
