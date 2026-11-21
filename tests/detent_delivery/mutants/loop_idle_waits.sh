#!/usr/bin/env bash
# usage, from the checkout root, in a job: loop_idle_waits.sh MUTANT
# Drops one wake of loop-idle-waits, builds the test that covers it and runs it; run with
# --fails-without, the test must fail. A build that fails exits 126, which proves nothing.
#   owner-wake       OwnerWake::raise sets its flag but never ends a sleep    test_owner_wake
#   authority-enqueue  Service::enqueue queues a task without a wake          test_authority_idle_wake
#   relay-enqueue    the relay's Service::handle slots a task without a wake  test_delivery_coordinator_subscription-relay-http
#   reply-writable   OwnedReplyPump's writable callback skips the wake        test_distribution_reply_pump
#   coordinator-disk the disk worker never settles its event on the owner     test_delivery_coordinator
#   coordinator-work a worker job does not hold the owner's io_context as work test_delivery_worker_lifetime
#   absence-merged   owner_absence counts from the later of the pass and the planned deadline test_owner_absence
#   start-stop-sleep start, replay and stop sleep with no I/O in flight      test_authority_restart_wake
#   stop-sleep       only stop: its pre-39e5ab7e rule (sleep once retired)    test_authority_restart_wake
#   stop-peers-sleep a multi-voter stop sleeps 1 ms per retirement visit    compare.sh stop (100k keys vs main)
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
	target=test_delivery_coordinator test=test_delivery_coordinator_subscription-relay-http ;;
reply-writable)
	mutate src/detent_distribution_http/reply_pump.cc $'held->writable.store(true);\n\t\t\tchanged();' 'held->writable.store(true);'
	target=test_distribution_reply_pump ;;
coordinator-disk)
	mutate src/detent_delivery/coordinator.cc $'work->run();\n\t\t\t\t\tasio::post(owner, [worker_done] { worker_done->set(); });' 'work->run();'
	target=test_delivery_coordinator ;;
coordinator-work)
	mutate src/detent_delivery/coordinator.cc \
		'asio::prefer(options.source.scheduler->executor(), asio::execution::outstanding_work.tracked)' \
		'options.source.scheduler->executor()'
	target=test_delivery_worker_lifetime ;;
absence-merged)
	mutate include/detent/owner_loop.hh \
		'return (slept - pass) + std::max(late, std::chrono::steady_clock::duration::zero());' \
		'(void)late; return next - std::max(pass, std::min(planned, next));'
	target=test_owner_absence ;;
start-stop-sleep)
	mutate src/detent_authority/service.cc 'auto in_flight = [&] { return active || history_active || peers; };' \
		'auto in_flight = [&] { return true; };'
	mutate src/detent_authority/service.cc 'if (active || history_active || (peers && !retiring))' 'if (true)'
	target=test_authority_restart_wake ;;
stop-sleep)
	mutate src/detent_authority/service.cc 'if (active || history_active || (peers && !retiring))' \
		'if (!retiring || active || history_active)'
	target=test_authority_restart_wake ;;
stop-peers-sleep)
	mutate src/detent_authority/service.cc 'if (active || history_active || (peers && !retiring))' \
		'if (active || history_active || peers)'
	bench/idle-wakeups/compare.sh build "${JOB_REF_MAIN:?the baseline checkout}" || exit 126
	exec bench/idle-wakeups/compare.sh stop "$JOB_REF_MAIN" ;;
*)
	echo "unknown mutant $mutant" >&2
	exit 126 ;;
esac
cmake -S . -B "$build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=/usr/bin/clang++ -DCMAKE_C_COMPILER=/usr/bin/clang \
	-DCMAKE_OSX_SYSROOT= -DDETENT_BUILD_AUTHORITY=ON -DDETENT_BUILD_RELAY=ON -DDETENT_BUILD_MANAGED_AGENT=ON \
	-DDETENT_BUILD_EXAMPLES=OFF -DDETENT_BUILD_BENCHMARKS=OFF -DDETENT_BUILD_TESTS=ON >/dev/null || exit 126
cmake --build "$build" -j4 --target "$target" >/dev/null || exit 126
ctest --test-dir "$build" -R "^${test:-$target}\$" --output-on-failure --timeout 120
