# Scheduler review: wakeup notification

Date: 2026-10-29. Reviewed: `scheduler:2cbea95` plus the uncommitted
`scheduler.h`, `CMakeLists.txt`, and `test/wakeup.cc` in
`.scratch/scheduler-wakeup-20261029/upstream`. Dependency inspected:
`stash:351ef2e`. Scope: the notifier mutex change, including shutdown,
recursive scheduling, tracing, and the regression test.

## Verdict

**APPROVE WITH CONDITIONS.** Holding the notifier mutex from queue inspection
through the wait closes the demonstrated early-notification race. No blocking
algorithm defect is identified in this change. Conditions below cover the
regression claim, trace callbacks, and completion of qualification.

## Evidence and reasoning

1. The unchanged-header mutant differs from the candidate only in mutex placement.
   Independent replay under `quiet light scheduler-wakeup-review` returns exit 1,
   `scheduled task slept through enqueue`, for `latest-baseline-probe`; the
   candidate `build/scheduler_wakeup_test` returns exit 0. Both subprocesses have
   15-second limits. The native `build/Testing/Temporary/LastTest.log` records
   three passing tests. The lead's repeated runs are supplementary evidence;
   this review independently runs each probe once.
2. A producer publishes the task before updating the deadline
   (`scheduler.h:347`). If it must notify, its mutex acquisition at line 358
   cannot complete between the consumer's inspection and its atomic
   unlock-and-wait at line 324. A deadline update during inspection either
   constrains the consumer's CAS or leaves that notification outstanding.
3. A producer skips notification only when its observed deadline is strictly
   between its sampled current time and its task's deadline (`scheduler.h:357`).
   That earlier future wakeup remains backed by an earlier queued task, a
   consumer proposal, or another producer's outstanding notification. After a
   timed wakeup consumes the earlier task, the old deadline is expired and
   producers notify. After an early or spurious wakeup, the earlier task remains
   visible to peep. This reasoning depends on stash's existing publication and
   read-only peep contract; the patch does not replace it.
4. Queue walking, callback execution, and cleanup remain after `lk.unlock()`
   (`scheduler.h:326`). Ordinary callback rescheduling therefore acquires no
   recursively held notifier mutex. The empty-shutdown break releases the lock
   through `unique_lock` destruction. Existing shutdown admission/countdown
   races and self-join behavior are outside this fix.

## Findings and conditions

1. **C1: The test does not force notification before consumer release.**
   `test/wakeup.cc:47` waits for `ENQUEUED`, which occurs before the producer
   acquires the notifier mutex (`scheduler.h:354`). The consumer is then
   released at test line 48. On the mutant, the consumer can reach its wait
   before the producer notifies, allowing a pass. The observed mutant failure
   proves the defect, but repeated failures do not make this orchestration
   deterministic. Before describing the regression as deterministic, record or
   enforce the notification-before-proposal ordering in the baseline probe.
   Otherwise retain this regression with its limitation stated explicitly.
2. **C2: Trace callbacks gain a mutex constraint.** The existing `PEEPING`,
   `PEEP_UPDATED`/`PEEPED`, and `LOOP` callouts now execute under `mtx`
   (`scheduler.h:297`, `303`, `305`, `321`); `BEFORE_PROPOSE` does too. A
   `LOOP` hook that schedules an immediate task on this scheduler can newly
   deadlock on `add()`'s notifier lock. Calling `finish()` from these hooks also
   reacquires that lock. The shipped no-op and printing adapters do not re-enter
   the scheduler. Before landing, document the non-reentrancy constraint and
   verify active consumer adapters do not take locks in the opposite order or
   synchronously schedule back into this scheduler. If a supported adapter does,
   keep that callout outside the critical section or change its adapter.
3. **C3: Finish qualification without widening the claim.** Complete the lead's
   separate ASan/UBSan and TSan runs before landing. At consumer adoption, add
   coverage for a later task taking the no-notify branch and for callback
   rescheduling. Qualify Detent with the current matching stash API separately.
   The original GCC readiness timeout remains unattributed by this review.
4. **C4: Producer latency is unmeasured.** Lock acquisition count is unchanged,
   but notifying producers and shutdown now wait for peep and its trace work.
   Stash's inspected traversal adds no nested mutex acquisition. Make no
   performance claim; check enqueue latency under a representative populated
   wheel when qualifying consumer adoption.

## Not run

No new compilation, full suite rerun, sanitizer run, benchmark, Linux/GCC run,
custom reentrant-trace probe, or exhaustive interleaving exploration is performed
by this reviewer. The review writes no source changes and makes no attribution
claim about the original Detent symptom.

## Design follow-up: enqueue generation

The lead proposes preserving the original callout scope and adding an atomic
generation. **Prefer this alternative**, conditional on implementation review:
it avoids the trace reentrancy change and permits the regression to wait for
producer completion before releasing the consumer. C1 and C2 remain open for
the reviewed mutex-placement prototype.

1. Capture the generation before peep. Increment it after successful queue
   publication, before deadline bookkeeping. Under the existing final notifier
   mutex, wait only when the generation still matches and the deadline is future.
   Keep these operations sequentially consistent, matching the deadline atomics;
   acquire/release alone requires a separate proof.
2. A producer that observes the old deadline and skips notification changes the
   generation before the consumer's final check. If the check precedes that
   increment instead, the producer observes the consumer's published deadline:
   it either lowers and notifies, or preserves an earlier wakeup. A producer
   completed before the initial generation capture has already published its
   task for peep.
3. Increment the same generation in `end()`, after publishing `ending` and before
   taking the notifier mutex. An enqueue-only generation preserves the existing
   shutdown window: shutdown can notify after the consumer's proposal checks
   but before its final lock, leaving a stale long wait. Shutdown must invalidate
   that proposal too.
4. Disclose the added per-scheduler field and one atomic RMW per accepted task;
   rebuild consumers and qualify cost. Use an unsigned counter and state its
   wrap assumption. No implementation or performance approval is implied here.

The lead additionally reports ASan/UBSan 3/3 and separate TSan 2/2 passing for
the mutex-placement prototype. Those results are not independently replayed by
this review and do not qualify the generation alternative.
