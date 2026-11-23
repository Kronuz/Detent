#!/bin/bash
# usage: submit-main.sh RECORDS ROUND SEED_JOB...   (main's arm on main's own fixtures: 10K smoke, then 100K or 5M)
# Main cannot read the Oct-23 fixtures (its detentd fences the old journal), so main's arms read 10000-main
# and 100000-main or 5000000-flat-main, seeded at main by seed-fixture.sh.
set -eu
records=$1 round=$2; shift 2
after=$(IFS=,; echo "$*")
case $records in
  100000) fixture=100000-main minutes=10 long=() ;;
  5000000) fixture=5000000-flat-main minutes=28
    long=(--long "5M cold bootstrap takes 8-18 min plus the 10K smoke (2026-11-07); campaign caps exclusive at 10") ;;
esac
B='mkdir -p .scratch && cmake -S . -B .scratch/build-rel -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=/usr/bin/clang++ -DCMAKE_C_COMPILER=/usr/bin/clang -DCMAKE_OSX_SYSROOT= -DDETENT_BUILD_AUTHORITY=ON -DDETENT_BUILD_RELAY=ON -DDETENT_BUILD_MANAGED_AGENT=ON -DDETENT_BUILD_EXAMPLES=OFF -DDETENT_BUILD_BENCHMARKS=OFF -DDETENT_BUILD_TESTS=ON && cmake --build .scratch/build-rel -j4 --target detentd detent-agent detent-relay detent_system_mixed_oracle && git rev-parse HEAD >.scratch/build-rel/BUILT_COMMIT'
campaign job submit --quest dense-sync-stack --level exclusive --minutes $minutes ${long[@]+"${long[@]}"} --after "$after" \
  --commit ea1fddc8af10b3fffa44e71e8800c270fd7218ba --ref h=/Users/kronuz/code/Kronuz/Detent@ea1fddc8af10b3fffa44e71e8800c270fd7218ba \
  --slot dss-main --env ARM=main --env ROUND=$round --env FIXTURE=$fixture --env RECORDS_=$records \
  --what "A4 $records main round $round (main's own fixture)" --build "$B" -- \
  bash -c 'set -eu; t=$PWD; b=$t/.scratch/build-rel; h=$JOB_REF_H/docs/measurements; py=/usr/local/bin/python3; tr=/Users/kronuz/code/Kronuz/Detent/.scratch/capacity-trials; trap "rm -rf $JOB_OUT/*/state-* $JOB_OUT/*/agent-* $JOB_OUT/*/relay-* $JOB_OUT/*/runtime-*" EXIT; test -e $tr/10000-main/seeded.json; test -e $tr/$FIXTURE/seeded.json; test "$(cat $b/BUILT_COMMIT)" = "$(git rev-parse HEAD)"; git diff --quiet HEAD -- CMakeLists.txt cmake include src tests; echo "arm=$ARM round=$ROUND fixture=$FIXTURE tree=$(git rev-parse HEAD) harness=$(git -C $JOB_REF_H rev-parse HEAD)"; file $b/src/detentd $b/src/detent-agent $b/src/detent-relay; f=$b/tests/detent_authority; rm -rf $f/tls-fixture; OPENSSL_CONF=$t/tests/detent_authority/tls_ambient.cnf cmake -DOUTPUT=$f/tls-fixture -DOPENSSL=/usr/local/bin/openssl -P $t/tests/detent_authority/tls_certificates.cmake; /usr/local/bin/openssl x509 -in $f/tls-fixture/one.pem -noout -checkend 7200; $py $h/sync-groups-20261107/coldboot.py $tr/10000-main $JOB_OUT/smoke 10000 $b $t restart; $py $h/sync-groups-20261108/summarize.py $JOB_OUT/smoke; $py $h/sync-groups-20261107/coldboot.py $tr/$FIXTURE $JOB_OUT/run $RECORDS_ $b $t restart --stage=confirm; $py $h/sync-groups-20261108/summarize.py $JOB_OUT/run' 2>&1 | tail -1
