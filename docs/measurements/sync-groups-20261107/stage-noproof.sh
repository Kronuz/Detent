#!/bin/bash
set -euo pipefail
T=/Users/kronuz/code/Kronuz/Detent/.claude/worktrees/agent-af2c08dd24001ec58
W=$T/.scratch/recovery-s2
D=/Users/kronuz/code/Kronuz/Detent
BEFORE=/Users/kronuz/code/Kronuz/Detent-wt/s2-before
Q=/Users/kronuz/.local/bin/quiet
X=/usr/local/bin/gtimeout
PY=/usr/local/bin/python3
source "$W/identity.sh"
identity=$(qualification_identity)
stamp=$(date -u +%Y%m%dT%H%M%SZ)-$$
require_marker() { test "$(cat "$W/$1.passed")" = "$identity"; }
mark_passed() { test "$(qualification_identity)" = "$identity"; printf "%s\n" "$identity" > "$W/$1.passed"; }
cd "$T"
kind=$1; shift
mkdir -p "$W/logs" "$W/coldboot"
"$Q" check || true
held() {
  local level=$1 name=$2; shift 2
  "$Q" "$level" "s2-$name" 30 "S2 $name" -- nice -n 19 \
    "$X" --kill-after=60s 28m "$@" >"$W/logs/$name-$stamp.log" 2>&1
}
# The TLS fixture is valid for one day (-days 1), so each arm gets a fresh one.
fresh_fixture() {
  local dir=$1/build-rel/tests/detent_authority
  rm -rf "$dir/tls-fixture"
  OPENSSL_CONF=$1/tests/detent_authority/tls_ambient.cnf cmake -DOUTPUT="$dir/tls-fixture" \
    -DOPENSSL=/usr/local/bin/openssl -P "$1/tests/detent_authority/tls_certificates.cmake"
  /usr/local/bin/openssl x509 -in "$dir/tls-fixture/one.pem" -noout -checkend 7200
}
case "$kind" in
sanitizer)
  san=$1
  case "$san" in
    asan) flags='-fsanitize=address,undefined -fno-omit-frame-pointer' ;;
    tsan) flags='-fsanitize=thread -fno-omit-frame-pointer' ;;
    *) exit 2 ;;
  esac
  b=$T/.scratch/build-$san
  held light "$san-configure" cmake -S "$T" -B "$b" -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="$flags" \
    -DCMAKE_EXE_LINKER_FLAGS="$flags" -DCMAKE_SHARED_LINKER_FLAGS="$flags"
  held light "$san-build" cmake --build "$b" -j4 --target \
    test_local_publication_power_cut test_local_disk_store test_local_paged_publication_artifacts
  for target in test_local_publication_power_cut test_local_disk_store test_local_paged_publication_artifacts; do
    held light "$san-$target" "$b/tests/detent_local/$target"
  done
  mark_passed "$san"
  ;;
smoke)
  # proof gate waived by owner on 2026-11-06; see SUMMARY
  require_marker asan
  require_marker tsan
  for arm in before after; do
    if [ "$arm" = before ]; then tree=$BEFORE; else tree=$T; fi
    held light "smoke-$arm-build" cmake --build "$tree/build-rel" -j4 --target \
      detentd detent-agent detent-relay detent_system_mixed_oracle
    fresh_fixture "$tree"
    out=$W/coldboot/smoke-$arm-$(date -u +%Y%m%dT%H%M%SZ)
    "$Q" strict "s2-coldboot-smoke-$arm" 28 "S2 10K $arm coldboot smoke" -- nice -n 19 \
      "$X" --kill-after=30s 25m "$PY" "$T/.scratch/cold-bootstrap-5m/coldboot.py" \
      "$D/.scratch/capacity-trials/10000-r3" "$out" 10000 "$tree/build-rel" "$tree" restart --stage=smoke
  done
  mark_passed coldboot-smoke
  ;;
pair)
  stage=$1 records=$2 round=$3
  require_marker coldboot-smoke
  case "$stage" in aa|screen|confirm) ;; *) exit 2 ;; esac
  case "$records" in
    10000) source=$D/.scratch/capacity-trials/10000-r3 ;;
    100000) source=$D/.scratch/capacity-trials/100000-r3 ;;
    5000000) source=$D/.scratch/capacity-trials/5000000-flat-r1 ;;
    *) exit 2 ;;
  esac
  if [ "$stage" = aa ]; then arms='before before'; else arms='before after'; fi
  if [ "$stage" = confirm ]; then
    require_marker "screen-$records"
    require_marker "aa-$records"
  fi
  sequence=0
  for arm in $arms; do
    sequence=$((sequence+1))
    if [ "$arm" = before ]; then tree=$BEFORE; else tree=$T; fi
    require_marker coldboot-smoke
    fresh_fixture "$tree"
    out=$W/coldboot/$stage-$records-r$round-$sequence-$arm-$(date -u +%Y%m%dT%H%M%SZ)
    # Each arm is its own bounded hold so a pair cannot monopolize the host.
    held exclusive "$stage-$records-r$round-$sequence-$arm" "$PY" \
      "$T/.scratch/cold-bootstrap-5m/coldboot.py" "$source" "$out" "$records" \
      "$tree/build-rel" "$tree" restart "--stage=$stage"
  done
  mark_passed "$stage-$records"
  ;;
ext4)
  "$X" --kill-after=15s 1m "$T/.scratch/vm.sh" push "$T/.scratch/vm_probe.sh" "$W/vm_probe_bounded.sh"
  # Match the ext4 VM's current command line, never an old numeric PID.
  QUIET_VM='qemu-system-x86_64.*hostfwd=tcp:127.0.0.1:18772' \
    held exclusive vm-ext4 "$T/.scratch/vm.sh" run vm_probe_bounded.sh
  ;;
*) exit 2 ;;
esac
