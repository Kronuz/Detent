# loop-idle-waits: journal

State: blocked on lead: file set widening and the multi-voter ruling (msg-103); suite job queued at the head
Quest: .campaign/detent/quests/loop-idle-waits.md (the lead's; never edit it)

## Doing

Waiting for the lead's answer to msg-103. The coordinator step is committed (Detent:9ea25ab3);
its suite job checks it builds and passes.

## Next

1. On widening: add `OwnerWake` to include/detent/owner_loop.hh (atomic `raised` + posted cancel
   of the loop's steady_timer; the loop clears `raised` at turn start and never sleeps while it is
   set), and wake OwnedReplyPump's writable/settled callbacks through it.
2. Relay: host.cc turn sleeps until min(health_due, snapshot_retry, runtime's next deadline);
   wakes from Service::handle enqueue, mirror job done, coordinator hook, listener_stopped, shutdown.
3. Authority (service.cc): wakes at the four enqueue sites (1010, 1074, 1133/1135, 1186), stop
   (726, 734), storage and history driver completions (dispatch), reply pump; sleep until
   min(health 250 ms, earliest task/poll deadline); the yield predicate gains every budgeted ring
   (session_retirement, legacy control ring, authorize queues, expiry cursors). Recovery waits
   (2030, 2046) and the stop drain (3317) wait on the same wake. Peers configured: per the ruling.
4. A2 test (one per loop) + mutant dropping one wake, `--fails-without`; A1 counts with the
   codex/idle-wakeups harness at main and head; A3 relay and authority benches under strict.

## Producer inventory

Relay turn (host.cc:290-410): runtime->step (lane slots filled by Service::handle on HTTP threads;
per task: settled/writable from OwnedReplyPump callbacks, mirror job->done, sessions.prepare
Parked until a bundle publication or poll_deadline, task deadline, session expiry in collect);
coordinator status (failures, Fenced, drained; coordinators are tasks on the same owner);
bundle snapshot_step/cleanup_step (publications from coordinators; snapshot_retry 100 ms);
mirror status; readiness; health_due every 100 ms (a real deadline); listener_stopped (posted);
shutdown (signal_set, failures).

Authority runtime (service.cc:2060-3190): HTTP enqueue into queue_, subscription(_close)_queue_,
frames_queue_ under mutex_; stopping_; storage/history driver completions on the owner; peer
frames held in Peers' connection->input until admit() (no hook at consensus a197efb); consensus
ticks (no next_deadline at the pin); task, poll and session deadlines; health every 250 ms;
writable/settled from OwnedReplyPump; budgeted rings that progress only per turn.

Coordinator (coordinator.cc): five worker-job polls; done in Detent:9ea25ab3.

## Blocked on

The lead (msg-103): widen files to include/detent/owner_loop.hh,
include/detent_distribution_http/reply_pump.hh, src/detent_distribution_http/reply_pump.cc,
src/detent_relay/service.cc, include/detent_relay/service.hh, src/detent_relay/mirror_service.cc,
include/detent_delivery/coordinator.hh, tests/detent_relay/**, tests/detent_authority/**,
tests/detent_delivery/**, bench/idle-wakeups/**. Ruling: multi-voter authority keeps the 1 ms
cadence until a consensus pin bump past 0acfaff plus a Peers input wake (A, recommended), or this
quest takes the bump and the hook (B).

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | <no evidence yet> |
| A2 | <no evidence yet> |
| A3 | <no evidence yet> |
| A4 | <no evidence yet> |

## Result

<Not yet.>

## Rulings I made

- R1: Worker jobs settle a per-job `task::event` posted to the owner executor, not set on the
  worker thread: the waiting frame lives on the owner, so set() never races its destruction at
  teardown (task::event's contract forbids set() during teardown). The pinned task (bdafa5f) has
  no wheel `arm`, `sleep_until` or `periodic`; `task::event` is what it offers.
