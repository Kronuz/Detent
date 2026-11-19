#!/usr/bin/env bash
# usage, from the checkout root, in a job: loop_idle_waits.sh MUTANT
# Drops one wake of loop-idle-waits, builds the test that covers it and runs it; run with
# --fails-without, the test must fail. A build that fails exits 126, which proves nothing.
#   owner-wake       OwnerWake::raise sets its flag but never ends a sleep    test_owner_wake
#   authority-enqueue  Service::enqueue queues a task without a wake          test_authority_idle_wake
#   relay-enqueue    the relay's Service::handle slots a task without a wake  test_delivery_coordinator
#   reply-writable   OwnedReplyPump's writable callback skips the wake        test_distribution_reply_pump
#   coordinator-disk the disk worker never settles its event on the owner     test_delivery_coordinator
set -u
mutant=${1:?mutant}
build=.scratch/loop-idle-waits-mutant
mutate() { # FILE OLD NEW: replace exactly one occurrence, or exit 126
	/usr/local/bin/python3 -I - "$@" <<'EOF' || exit 126
import sys
path, old, new = sys.argv[1:4]
text = open(path).read()
if text.count(old) != 1:
    sys.exit(f"{path}: {text.count(old)} matches")
open(path, "w").write(text.replace(old, new))
EOF
}
case $mutant in
owner-wake)
	mutate include/detent/owner_loop.hh 'if (state->sleeper) { state->sleeper->cancel(); }' '(void)state;'
	target=test_owner_wake ;;
authority-enqueue)
	mutate src/detent_authority/service.cc $'queue_.push_back(task);\n\t\t\towner_.wake().raise();' 'queue_.push_back(task);'
	target=test_authority_idle_wake ;;
relay-enqueue)
	mutate src/detent_relay/service.cc $'\tif (!task) {\n\t\tp.wake();' $'\tif (!task) {'
	target=test_delivery_coordinator ;;
reply-writable)
	mutate src/detent_distribution_http/reply_pump.cc $'held->writable.store(true);\n\t\t\tchanged();' 'held->writable.store(true);'
	target=test_distribution_reply_pump ;;
coordinator-disk)
	mutate src/detent_delivery/coordinator.cc $'work->run();\n\t\t\t\t\tasio::post(owner, [worker_done] { worker_done->set(); });' 'work->run();'
	target=test_delivery_coordinator ;;
*)
	echo "unknown mutant $mutant" >&2
	exit 126 ;;
esac
cmake -S . -B "$build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=/usr/bin/clang++ -DCMAKE_C_COMPILER=/usr/bin/clang \
	-DCMAKE_OSX_SYSROOT= -DDETENT_BUILD_AUTHORITY=ON -DDETENT_BUILD_RELAY=ON -DDETENT_BUILD_MANAGED_AGENT=ON \
	-DDETENT_BUILD_EXAMPLES=OFF -DDETENT_BUILD_BENCHMARKS=OFF -DDETENT_BUILD_TESTS=ON >/dev/null || exit 126
cmake --build "$build" -j4 --target "$target" >/dev/null || exit 126
ctest --test-dir "$build" -R "^$target\$" --output-on-failure --timeout 120
