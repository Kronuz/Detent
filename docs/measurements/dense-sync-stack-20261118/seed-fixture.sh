#!/bin/bash
# usage: seed-fixture.sh SCENARIO=FIXTURE ...   (a light job at the tree that seeds, after build.sh)
# Seeds each fixture under the home's capacity-trials with seed.py and this tree's harness; one that
# already holds seeded.json is kept, and a failed seed is renamed FIXTURE.partial.
set -eu
t=$PWD b=$PWD/.scratch/build-rel
here=$(cd "$(dirname "$0")" && pwd)
tr=/Users/kronuz/code/Kronuz/Detent/.scratch/capacity-trials
test "$(cat "$b/BUILT_COMMIT")" = "$(git rev-parse HEAD)"
f=$b/tests/detent_authority
rm -rf "$f/tls-fixture"
OPENSSL_CONF=$t/tests/detent_authority/tls_ambient.cnf cmake -DOUTPUT="$f/tls-fixture" \
  -DOPENSSL=/usr/local/bin/openssl -P "$t/tests/detent_authority/tls_certificates.cmake"
/usr/local/bin/openssl x509 -in "$f/tls-fixture/one.pem" -noout -checkend 14400
for pair in "$@"; do
  scenario=${pair%%=*} fx=$tr/${pair#*=}
  if test -e "$fx/seeded.json"; then echo "$fx exists"; cat "$fx/seeded.json"; continue; fi
  rm -rf "$fx.partial"
  df -h "$tr" | tail -1
  /usr/local/bin/python3 -I "$here/seed.py" "$t" "$b" "$tr/$scenario.json" \
    "$t/tests/detent_system/presets/capacity-5000000.json" "$fx" || { test ! -e "$fx" || mv "$fx" "$fx.partial"; exit 1; }
  du -sh "$fx" | tee -a "$JOB_OUT/fixture-size"
  cat "$fx/seeded.json"
done
echo tree=$(git rev-parse HEAD)
