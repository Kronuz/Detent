# drivers-task-wheel review: correctness

Commit: task:95798a02f68c86f070401329a05c78d326ac2579
Verdict: APPROVE
Date: 2026-11-16

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

Lens: correctness, concurrency included. A re-review limited to task:627aca3..95798a0:
task:a30bb0f (Xapiand X1, X4, X5, X7, X8), task:412b2c2 (F1: `cancel_state::settle()` and the
`delivering` bit; F2: `window-forced` in the suite), task:95798a0 (one docs line). The earlier
review at task:627aca3 was CHANGES on F1. Read with the journal at
`Detent-wt/.campaign/detent/journal/drivers-task-wheel.md` and the Xapiand lead's review
(`Xapiand/.scratch/review-task-wheel/review.md`, APPROVE on a30bb0f and 412b2c2), whose probes
I took as input and checked against the code; I relied on none of its numbers alone.

## Evidence I relied on

Jobs at task:412b2c2 (95798a0 adds only a docs line, `docs/mechanisms/cancellation.md:71-72`):

- Suite Debug 261116-001402-b63645, ASan+UBSan 261116-001403-5aedbf, TSan 261116-001403-f48e86,
  gate 261116-001436-1fcc80: all pass with the new cases in `all`.
- 261116-001420-de8395 (TSan): `race` 1,000 rounds, 0 stalls; `race-forced` cancelled at
  101.8 ms (the fire waited out the 100 ms hold, so the interleaving was reached);
  `window-forced` 0.4 ms. Logs read.
- Mutant `fire-skips-settle` 261116-001426-a47947: `race-forced` stalls at 6.3 ms, killed (F1's
  fails-without). Mutant `blocking-lost-wake` 261116-001432-f76e78 (recheck and both wait
  predicates removed): `window-forced` sleeps 3,000 ms, killed (F2).
- X1, X4, X7, X8 mutants killed at 412b2c2: `unarmed-returns` 261116-001404-83681c,
  `drop-calls-dead-scheduler` 261116-001404-fd0781, `blocking-cancel-unchecked`
  261116-001405-54a740, `expire-when-idle` 261116-001404-86ccb0, `closed-arm-allowed`
  261116-001405-816273; `wakes-unarmed` under TSan 261116-001403-4a12f6; controls
  261116-001403-70a81e, -d41f81, -a03d3e.
- F4 attribution, the paired TSan x200 `task_asio_test` stress: head 261116-001448-e92eeb 6/200,
  main (task:f51aafb) 261116-001458-1a29c1 5/200; every failure I could read in both logs is
  `asio_test.cc:758 CHECK(st.inlined > 0)`. Pre-existing on main.

## What I checked myself

Scripts under `Detent/.scratch/review-drivers-task-wheel-correctness/`: `settle_probes.cc` and
`settle_probes.sh FLAVOR [self-wait|no-settle] -- [CASE...]` (the mutant option copies
`$JOB_SRC` and edits `task.hh`).

1. Q1, F1's `settle()`. Reading `task.hh:1084-1135`, `:1213-1219`:
   - Claimed but not delivered: the canceller's CAS sets `requested|locked` together and sets
     `delivering` before its first unlock, so a losing fire's `settle()` either spins on the
     lock or sees the bit; it can never see the state requested, unlocked and not delivering
     while callbacks remain. A request with no callbacks sets no bit, and nothing waits on it
     (a later `add` finds it requested and runs inline on the driver).
   - Settle's own wake: it reads `epoch_` before checking under the lock; the requester clears
     `locked|delivering` in one RMW and then bumps `epoch_` and notifies. A clear before the
     check is seen under the lock (same atomic); a clear after it changes the epoch first
     loaded. No lost wake, no ABA short of 2^32 bumps.
   - Lifetime: the requester holds its own `retain()` through the final bump and notify, and
     `on_wheel` holds the wheel's reference across `settle()`. No use after free.
   - Deadlock: `settle()` runs only from `on_wheel`, called by `expire()` at the top of
     `drive`, `advance` or asio's `on_wake`; `drive` drops `mutex_` before expiring
     (`task.hh:533-535`), and asio runs `on_wake` as a plain handler. So the settling thread
     holds no scheduler lock and no cancel-state lock, and a callback's `post`/`post_call`
     (the canceller re-entering the scheduler) proceeds. A cycle needs a callback that waits
     for the firing thread; see F7.
   - Nested settle: reachable on `inline_scheduler` (a callback that calls `advance()` fires
     its own deadline while its thread delivers it). Probe `inline-nested` passes at head
     (261116-013254-5b398f); mutant `self-wait` (settle without `requester_ != this thread`)
     hangs it and the watchdog kills it at 5 s (261116-013259-60ec31, exit 1 = killed). The
     clause is load-bearing and no suite case reaches it: F6.
   - Fire on the asio loop: probe `asio-forced` (a cross-thread request holds the deadline's
     callback 20 ms while the loop fires at 5 ms; a handler posted to the loop at 8 ms): at head
     the cross-thread request won 50/50 rounds and the loop ran the 8 ms handler only after the
     callback ended in 50/50, worst 23.3 ms blocked, every post delivered, no hang
     (261116-013254-5b398f). Under `no-settle` the same probe shows 0/50 held
     (261116-013302-56f2f9): the loop really waits in `settle`, and only for the delivery.
     `asio-random` (2,000 rounds, 1,418 requested, 582 expired): one callback, its post runs,
     no hang. These are plain Debug runs; the TSan run of the same cases
     (261116-013257-25d82d) was still queued behind exclusive holds when I wrote this (Not run).
   - Livelock: `settle()` wakes once per callback the requester finishes and rechecks; bounded
     by the callbacks registered. The spin on `locked_bit` yields and the holder's sections are
     a few stores.
2. Q2, X1's unarmed sleep (`task.hh:538-551`). The queue check (`:539`) and both waits run
   under one hold of `mutex_`; `push` signals under the lock (`:566-570`); both waits keep the
   `queued` predicate. A post before the relock is seen by the check, one after it blocks on the
   mutex until the wait releases it and then notifies a waiter already queued: no lost wake.
   `next` is read unlocked, but only the driving thread arms or cancels (now asserted, X7), so
   it cannot change before the wait. `drive(done)`'s default is exactly the old rule: old
   `waits = next != never && !finished && true`; new `waits = !finished && (wheel_ &&
   wheel_->next() != never)`, the same value read at the same point, and with `next == never`
   it returns false before the unarmed wait. The only difference is that a custom `may_wait`
   is now called when nothing is armed; side effects only. Xapiand's probe (`race-unarmed`,
   20,000 TSan rounds; a mutant without the guards hangs) and the suite's `wakes-unarmed`
   (5,000 rounds, mutant `unarmed-returns` killed) agree. Not rerun.
3. Q3, X8 (`task.hh:523-535`). Each pass pops the `ready_.size()` it found from the front, so
   FIFO order is unchanged and a post made during a pass runs in the next one. The wheel is
   expired between passes, so a due timer can now fire before a post queued during the pass;
   before, it fired only on an empty queue. That is a timeliness gain, not a reorder of posts:
   a fire that requests a token whose waiter was already claimed by a queued completion finds
   the claim taken (the waiters in `blocking_test` and Detent's D8 rows claim at post time).
   Starvation: posts cannot be starved (each pass runs its batch whatever expires); timers no
   longer can be (`starve`, mutant `expire-when-idle` killed). `done()` true with work queued
   still continues until the queue is empty, as before.
4. Q4, tests and mutants: F1 (`race`, `race-forced`, mutant `fire-skips-settle`), F2
   (`window-forced`, mutant `blocking-lost-wake` with both guards removed), X1, X4, X7, X8 each
   have a killed mutant. Gaps: F6 (settle's same-thread clause) and F3 (unchanged). Settle's own
   epoch protocol has no mutant; by reading it is right, and a test would need a nanosecond
   window (as N2).

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | no | defect | Resolved in task:412b2c2. A wheel fire whose request loses to a request still delivering on another thread waits for it (`settle()`), so the hop is queued before `expire()` returns and `drive` sees it. Holds on blocking and asio, with no deadlock under the callback contract. | 261116-001420-de8395 (TSan: 0/1,000 stalls, `race-forced` 101.8 ms), mutant 261116-001426-a47947 killed; probes 261116-013254-5b398f (plain, asio included) | Done. |
| F2 | no | gap | Resolved in task:412b2c2: `window-forced` is in `blocking_test`, and the mutant removing both guards is killed (3,000 ms). | 261116-001432-f76e78 | Done. |
| F3 | no | gap | Open, not taken this round: no scheduler-level case shows `drive` runs the queue to empty before returning true; journal-driver's `blocking_entry rules` is to cover the entry. | earlier mutant `blocking-return-early` 261115-225556-f76801 | As before: a `blocking_test` case where `done()` turns true with a post still queued. |
| F4 | no | gap | Attributed to main: `asio_test.cc:758`'s coverage CHECK fails 5/200 on main and 6/200 at head under paired TSan load. Not this quest's. | 261116-001458-1a29c1, 261116-001448-e92eeb | Lead's (ci-test-flakes): report the count instead of failing on it. |
| F5 | no | nit | Resolved in task:a30bb0f: the controls exit 0 when no assertion fires. | 261116-001403-70a81e | Done. |
| F6 | no | gap | `settle()`'s `requester_ != this thread` clause is load-bearing and untested. On `inline_scheduler` a deadline's callback that calls `advance()` fires that deadline while its own thread delivers it; without the clause the fire waits on itself forever. No suite case does this. | probe `inline-nested` passes at head (261116-013254-5b398f); mutant `self-wait` hangs it (261116-013259-60ec31) | Add `inline-nested` as a `deadline_test` case (a callback on the deadline's token advances past it; the request returns, reason `requested`, nothing armed), with `self-wait` as its mutant. |
| F7 | no | gap | Callbacks that wait on the firing thread now deadlock, where before they ran. A callback on a deadline's token (or a parent's) that posts to the deadline's driver and waits for that post blocks the fire's `settle()`, and `settle()` blocks the driver. The public contract does not forbid this: API.md gives `cancel_callback` "`std::stop_callback`'s lifetime rules". "Callbacks never block" exists only as a code comment (`task.hh:1125`), and 95798a0's line says how long the loop blocks, not what a callback must not do. The same applies to a user holding a lock across `inline_scheduler::advance()` that a cross-thread callback takes. | probe `callback-waits`: the callback waited 2,000 ms, until its own timeout, at head; 0 ms with `no-settle` (261116-013254-5b398f, 261116-013302-56f2f9) | Owner's call; options: (a) recommended: one line in API.md under `cancel_callback`: on a deadline, `f` must not wait for the deadline's scheduler or anything its thread holds while it expires timers; (b) keep the code and the comment as they are. |

Non-blocking notes:

- N1, N2 from the earlier review stand (unchanged code paths).
- N3 is resolved by X8; N4 by `cancellation.md:66-67`.
- N5. `race-forced` passes trivially if the canceller thread is late past the 5 ms deadline (the
  deadline then wins). Its printed `elapsed_ms` (101.8) shows the interleaving was reached;
  coverage is reported, not checked, as the standard asks.
- N6. Each request with callbacks now pays one more `epoch_` add and `notify_all`. I did not
  measure it.

## Not run

- 261116-013257-25d82d: `asio-forced`, `asio-random`, `inline-nested` under TSan at 95798a0.
  It was queued behind exclusive measurement holds when I wrote this; the lead reads it. The
  suite's TSan job and Xapiand's TSan `cancel-race` (3,000 rounds) cover settle on
  `blocking_scheduler`. The asio path has only my plain run.
- A mutant of settle's epoch protocol (dropping the final bump, or bumping before the clear):
  its window is a few instructions wide, so I judged it by reading.
- The suite against mutant `self-wait` (by reading, no suite case calls `advance()` from a
  callback).
- Linux, g++ and the CI matrix; Release builds; the cost in N6.
- Anything in Xapiand beyond reading its review.
