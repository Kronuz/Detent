#!/bin/bash
# usage: submit.sh ARM ROUND   (5M arm as one exclusive job after the seed job)
set -eu
arm=$1 round=$2 seed=261118-064312-81a63c
case $arm in
  dense) commit=9852a3c498e97486551c374e6c5d025cb7c1ae26 slot=dss-dense stage=confirm ;;
  s2) commit=35296bada slot=dss-s2 stage=confirm ;;
  stacked) commit=d04794d7c slot=dss-stacked stage=confirm ;;
  main) commit=ea1fddc8a slot=dss-main stage=confirm ;;
  aa) commit=d04794d7c slot=dss-aa stage=aa ;;
esac
B='mkdir -p .scratch && cmake -S . -B .scratch/build-rel -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=/usr/bin/clang++ -DCMAKE_C_COMPILER=/usr/bin/clang -DCMAKE_OSX_SYSROOT= -DDETENT_BUILD_AUTHORITY=ON -DDETENT_BUILD_RELAY=ON -DDETENT_BUILD_MANAGED_AGENT=ON -DDETENT_BUILD_EXAMPLES=OFF -DDETENT_BUILD_BENCHMARKS=OFF -DDETENT_BUILD_TESTS=ON && cmake --build .scratch/build-rel -j4 --target detentd detent-agent detent-relay detent_system_mixed_oracle && git rev-parse HEAD >.scratch/build-rel/BUILT_COMMIT'
campaign job submit --quest dense-sync-stack --level exclusive --minutes 28 --long "5M cold bootstrap takes 8-18 min plus the 10K smoke (2026-11-07); campaign caps exclusive at 10" --after $seed \
  --commit $commit --ref h=/Users/kronuz/code/Kronuz/Detent@ea1fddc8af10b3fffa44e71e8800c270fd7218ba \
  --slot $slot --env ARM=$arm --env ROUND=$round --env FIXTURE=5000000-flat-r1 \
  --what "A4 5M $arm round $round" --build "$B" -- \
  bash -c 'set -eu; t=$PWD; b=$t/.scratch/build-rel; h=$JOB_REF_H/docs/measurements; py=/usr/local/bin/python3; tr=/Users/kronuz/code/Kronuz/Detent/.scratch/capacity-trials; trap "rm -rf $JOB_OUT/*/state-* $JOB_OUT/*/agent-* $JOB_OUT/*/relay-* $JOB_OUT/*/runtime-*" EXIT; test -e $tr/5000000-flat-r1/seeded.json; test "$(cat $b/BUILT_COMMIT)" = "$(git rev-parse HEAD)"; git diff --quiet HEAD -- CMakeLists.txt cmake include src tests; echo "arm=$ARM round=$ROUND tree=$(git rev-parse HEAD) harness=$(git -C $JOB_REF_H rev-parse HEAD)"; file $b/src/detentd $b/src/detent-agent $b/src/detent-relay; f=$b/tests/detent_authority; rm -rf $f/tls-fixture; OPENSSL_CONF=$t/tests/detent_authority/tls_ambient.cnf cmake -DOUTPUT=$f/tls-fixture -DOPENSSL=/usr/local/bin/openssl -P $t/tests/detent_authority/tls_certificates.cmake; /usr/local/bin/openssl x509 -in $f/tls-fixture/one.pem -noout -checkend 7200; $py $h/sync-groups-20261107/coldboot.py $tr/10000-r3 $JOB_OUT/smoke 10000 $b $t restart; $py $h/sync-groups-20261108/summarize.py $JOB_OUT/smoke; $py $h/sync-groups-20261107/coldboot.py $tr/5000000-flat-r1 $JOB_OUT/run 5000000 $b $t restart --stage='$stage'; $py $h/sync-groups-20261108/summarize.py $JOB_OUT/run'
