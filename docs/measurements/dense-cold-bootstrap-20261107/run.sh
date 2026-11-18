#!/bin/bash
# Cold bootstrap on quest/dense-pages (+ the two sync-counter commits) against today's S2 before/after arms.
# Same coldboot.py, trial clones, holds and fixture handling as detent-runsh-remainder-r2.
set -u
D=/Users/kronuz/code/Kronuz/Detent
S2=$D/.claude/worktrees/agent-af2c08dd24001ec58
BEFORE=/Users/kronuz/code/Kronuz/Detent-wt/s2-before
DENSE=/Users/kronuz/code/Kronuz/Detent-wt/dense-cb
TRIALS=$D/.scratch/capacity-trials
PY=/usr/local/bin/python3
X=/usr/local/bin/gtimeout
SSL=/usr/local/bin/openssl
O=$D/.scratch/dense-cb
mkdir -p "$O/logs" "$O/coldboot"
note() { echo "$(date '+%F %T') $*" | tee -a "$O/SUMMARY"; }
fresh_fixture() {
  local dir=$1/build-rel/tests/detent_authority
  rm -rf "$dir/tls-fixture"
  OPENSSL_CONF=$1/tests/detent_authority/tls_ambient.cnf cmake -DOUTPUT="$dir/tls-fixture" \
    -DOPENSSL=$SSL -P "$1/tests/detent_authority/tls_certificates.cmake" &&
    $SSL x509 -in "$dir/tls-fixture/one.pem" -noout -checkend 7200
}
cb() { # cb LABEL RECORDS SOURCE TREE MODE STAGE
  local out=$O/coldboot/$1
  rm -rf "$out"
  fresh_fixture "$4" >"$O/logs/$1-fixture.log" 2>&1 || { note "$1 rc=fixture"; return 1; }
  quiet $5 cb-$1 30 "coldboot $1" -- nice -n 19 $X --kill-after=60s 28m \
    $PY $S2/.scratch/cold-bootstrap-5m/coldboot.py $3 "$out" $2 $4/build-rel $4 restart --stage=$6 >"$O/logs/$1.log" 2>&1
  local rc=$?; note "$1 rc=$rc"; return $rc
}

note "start dense=$(git -C $DENSE rev-parse --short HEAD) S2=$(git -C $S2 rev-parse --short HEAD) before=$(git -C $BEFORE rev-parse --short HEAD)"
cb 10k-dense-smoke 10000 $TRIALS/10000-r3 $DENSE strict smoke || { note "dense smoke failed; stopping"; exit 1; }
cb 100k-dense-1 100000 $TRIALS/100000-r3 $DENSE strict confirm
cb 100k-before-c 100000 $TRIALS/100000-r3 $BEFORE strict confirm
cb 100k-dense-2 100000 $TRIALS/100000-r3 $DENSE strict confirm
cb 100k-after-c 100000 $TRIALS/100000-r3 $S2 strict confirm
cb 100k-dense-3 100000 $TRIALS/100000-r3 $DENSE strict confirm
cb 5m-dense-1 5000000 $TRIALS/5000000-flat-r1 $DENSE exclusive confirm
cb 5m-before-c 5000000 $TRIALS/5000000-flat-r1 $BEFORE exclusive confirm
cb 5m-dense-2 5000000 $TRIALS/5000000-flat-r1 $DENSE exclusive confirm
cb 5m-after-c 5000000 $TRIALS/5000000-flat-r1 $S2 exclusive confirm
note "done"
