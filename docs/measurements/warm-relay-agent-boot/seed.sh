#!/bin/bash
# usage: seed.sh SCENARIO FIXTURE   (a light job at the tree that seeds, after the build)
# Seeds the home's capacity-trials/FIXTURE from capacity-trials/SCENARIO.json with
# dense-sync-stack-20261118/seed.py, which names the authority src/detentd: the build is reached
# through an alias directory that maps that name to src/detent-authority.
set -eu
t=$PWD b=$PWD/.scratch/build-rel
tr=/Users/kronuz/code/Kronuz/Detent/.scratch/capacity-trials
scenario=$1 fx=$tr/$2
test "$(cat "$b/BUILT_COMMIT")" = "$(git rev-parse HEAD)"
if test -e "$fx/seeded.json"; then echo "$fx exists"; cat "$fx/seeded.json"; exit 0; fi
f=$b/tests/detent_authority
rm -rf "$f/tls-fixture"
OPENSSL_CONF=$t/tests/detent_authority/tls_ambient.cnf cmake -DOUTPUT="$f/tls-fixture" \
  -DOPENSSL=/usr/local/bin/openssl -P "$t/tests/detent_authority/tls_certificates.cmake"
alias=$JOB_OUT/build-alias
mkdir -p "$alias/src"
ln -s "$b/src/detent-authority" "$alias/src/detentd"
ln -s "$b/tests" "$alias/tests"
rm -rf "$fx.partial"
df -h "$tr" | tail -1
/usr/local/bin/python3 -I "$t/docs/measurements/dense-sync-stack-20261118/seed.py" "$t" "$alias" \
  "$tr/$scenario.json" "$t/tests/detent_system/presets/capacity-5000000.json" "$fx" \
  || { test ! -e "$fx" || mv "$fx" "$fx.partial"; exit 1; }
du -sh "$fx" | tee "$JOB_OUT/fixture-size"
cat "$fx/seeded.json"
