# One timer wheel for Kronuz

Which timer wheel the Kronuz libraries use: Kronuz/stash (the lock-free multi-level wheel under
Kronuz/scheduler, its debouncer and Xapiand's logger), the intrusive single-owner wheel
async-drivers D4 proposes, or one shape serving both. Quest `wheel-evidence` (campaign `detent`).
D4 measured its prototype (`async-drivers-bench/wheel.hh`) against scheduler's `SchedulerQueue`
as it is (a `shared_ptr` task per arm, cancelled tasks left filed, `steady_clock` fixed) and
against one `asio::steady_timer` per deadline; it never measured stash itself or an improved
stash. Library code is read at stash:d249352 and scheduler:73e582c.

## The two uses

- **Single loop.** A reactor's loop owns the wheel and every entry it files (D4): group timers
  and `task::deadline`s are armed, re-armed, cancelled and fired on the loop's thread, on its
  clock, steady or the simulator's manual one. Re-arm and cancel are as frequent as arm (a
  heartbeat re-arms every follower's election timer).
- **Cross-thread.** Many threads arm, one thread fires: `Kronuz/scheduler`'s `ThreadedScheduler`
  and `Debouncer`, and Xapiand's logger. Producers never block and never take a lock; this is
  the case stash is built for (announce slots, a reserve frontier per leaf).

## Candidates

All in `async-drivers-bench/wheel_vs_stash*`, one interface (`arm`, `cancel`, `expire(now)`, and
`arm_from` for another thread where the candidate supports it), on the same entry workload.

- **(a) The prototype wheel**, `wheel.hh` unchanged: 4,096 slots of 1 ms, a 32-byte intrusive
  entry with O(1) unlink, owner thread only, the caller's clock. No cross-thread arm.
- **(b) stash as it is**, Xapiand's four-level configuration (`scheduler.h:226-230`), values
  `entry*` (raw pointers, no `shared_ptr`). stash has no cancel: an entry carries an armed flag
  and its deadline, a cancel clears the flag, a re-arm files the entry again, and a walk skips
  a filing whose entry is unarmed or due later (the stale filing of a re-arm). Each add boxes
  the pointer (`StashValues::put`, `stash.h:1010`).
- **(c) stash improved for a loop owner**: stash's levels unchanged (`StashSlots`), a new leaf
  that stores the entry pointer in the slot with no box, and a back-pointer in the entry to its
  slot, so the owner's cancel is one exchange (O(1)), and the walk never sees a cancelled
  entry. The clock is the caller's (stash's keys are any `unsigned long long`; only
  `SchedulerQueue` fixes `steady_clock`). Slot storage is still allocated in chunks per leaf and
  freed by `clean`.
- **(d) the prototype with an inbox**: (a) for the owner, plus an intrusive lock-free inbox for
  other threads: `arm_from` stores the requested deadline in the entry and pushes it on a
  Treiber stack (one CAS, no allocation) unless it is already queued; the owner drains the
  inbox into the wheel before each `expire`. One wheel serves both uses; any thread may re-arm
  any entry (the debouncer's case), at 56 bytes an entry.
- **(e) the prototype with the inbox through the entry's own link**: (d) without its extra
  fields. Another thread arms only an unarmed entry, linking it into the inbox through the
  entry's `next` (unused while unarmed); only the owner re-arms or cancels, which is (c)'s
  contract. The entry stays 32 bytes, so the owner's path is (a)'s. Added after the smoke
  (261111-120056-0484f0, strict, not a decision row) showed (d)'s single-loop score 14% to 31%
  above (a)'s, which only the entry size separates; the rule is unchanged and every row runs
  with (e).

## Workload and metrics

Every run prints its configuration and its start, timed part and end; the timing binary has no
allocation hook, and a separate build counts heap (`probe.hh`'s allocator).

- **Single loop** (one thread, a manual clock, no clock reads), at n = 1,000 and 1,000,000
  entries, deadlines uniform in [1, 2,000] ms past the base (D4's spread):
  arm each entry once; re-arm each once; cancel each; then arm each again and expire in 1 ms
  steps to 2,001 ms (stash: walk until empty, then `clean`, as scheduler's loop does), per entry
  fired. Each in ns per operation. The **single-loop score** is arm + re-arm + cancel + expire.
- **Memory** (the counting build): bytes per entry (the entry's size in its owner plus the
  wheel's heap per armed entry) and heap per arm (heap growth over the re-arm pass, per arm).
- **Cross-thread**: P = 1, 4 and 14 producer threads and one owner thread (16 hardware threads
  on the host). Each producer arms 2,000,000 / P distinct entries once, at `steady_clock` now
  plus uniform [1, 50] ms; the owner fires on `steady_clock` until every entry fired. The
  **cross-thread score** is producer ns per arm, the median over producers of its wall time
  divided by its arms. A run is valid only if every entry fired exactly once and the last fire
  came within 100 ms of the last deadline (the owner kept up); the time from the first arm to
  the last fire is reported with it. A wake of a sleeping owner (a condition variable or an
  eventfd) costs every candidate the same and is not measured.
- **Manual clock**: each candidate runs on a counter the test advances: nothing fires before
  its deadline, everything fires within one tick after it, and the wheel reads no clock.

## Noise

Each row runs R = 5 interleaved rounds; a round runs every arm once, each in its own process,
in a rotated order. A number is the median over the rounds, with its min-max. One arm runs
twice per round as an A/A pair: (a) twice in single-loop rows, (b) twice in cross-thread rows.
The **A/A band** of a metric is the relative difference between the pair's medians, or the
pair's larger relative min-max spread if wider. A difference counts only when it clears the
band.

## The decision rule

Fixed before the first measured run (A3); the rows are `exclusive` jobs at named commits.

1. A **unified candidate** is one that serves both uses: (b), (c), (d) or (e). (a) has no
   cross-thread arm and is the single-loop reference.
2. It **passes** when, at both n = 1,000 and n = 1,000,000, its single-loop score is at most
   1.10 times the lowest single-loop score of all candidates (a)-(e) at that n, and, at each
   P in {1, 4, 14}, its cross-thread score is at most 1.10 times stash's (b) at that P, and it
   passes the manual-clock test.
3. When a candidate's ratio is within the A/A band of 1.10, that row is inconclusive and runs
   once more with R = 10; the second run decides.
4. **The pick** is the passing unified candidate with the lowest single-loop score at
   n = 1,000,000. If none passes, there is no pick: this note gives both paths' numbers and
   the lead brings the gap to the owner (a second wheel is acceptable only for a measured gap
   the owner accepts).
5. Memory is reported and does not enter the rule.

The PLAN.md decision records the result, and task-wheel's quest follows it.

## Results

Pending the measured rows.
