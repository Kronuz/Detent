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
- **(f) (e) with a striped inbox**: 64 inbox heads, one cache line each, a producer thread on its
  own (by first use); the push that finds its stripe empty sets the stripe's bit in one word,
  and the owner drains only the stripes whose bits it took. The entry and the owner's path stay
  (e)'s. Added after the first cross-thread row (261111-121551-0e58b1) showed (d) and (e) losing
  to stash at 4 and 14 producers on their one shared inbox head.

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

## Changes after the first rows

The first rows ran at 22a10ab7: single 261111-121549-8cd0f8, cross 261111-121551-0e58b1, memory
261111-121553-d6c80e. Three changes follow from them; the thresholds are unchanged and every row
reruns under them.

1. **Candidate (f)**, above.
2. **The cross-thread workload overloaded every owner.** 2,000,000 arms due within 1-50 ms of
   arming left no candidate's owner within 100 ms of the last deadline in every run, stash's
   own (b) included at 4 and 14 producers, so no row was valid. The row now arms 1,000,000
   entries due uniform in [1, 1,000] ms; the producers still arm back to back, so the
   contention the score measures is unchanged, and only the owner's fire rate is.
3. **Rule 3's second run.** (d) and (e) were inconclusive on the single-loop score (ratios 1.03
   to 1.12 against bands of 13% and 19%, the bands set by the min-max of five runs). The R = 10
   rerun decides by its point estimate (the median ratio against 1.10), since a min-max band
   only widens with more runs.

After the rows below, the owner asked for more measurement before a pick (PLAN.md D69). Two more
rows run under the same rule and thresholds, each a change made after data:

4. **Longer single-loop samples (A5).** A sample repeated the lifecycle 1,000,000 / n times, 40 ms
   at n = 1,000 for (a), and the A/A band there was 48%. A sample now also repeats it until its
   timed passes add up to 100 ms (at n = 1,000,000 one repetition already takes 350 ms or
   more). The target was a band of at most 5% at n = 1,000; the band reached is reported.
5. **Linux (A6).** The single-loop, cross-thread and memory rows for (a), (b), (c) and (f) run on
   the local QEMU Linux VM (`wheel_vs_stash_vm.sh`), the host held `exclusive`, cross-thread at
   P = 1, 4 and the VM's CPUs minus 1. The rule applies to each host on its own rows.

## Results

Rows at df21a772, stash pinned at stash:d249352, on one Intel Core i9-9980HK (8 cores, 16
threads, macOS 15, Apple clang 17, x86-64 binaries, `-O2 -DNDEBUG`), each an `exclusive` job:
single 261111-130753-1687cf (R = 10), cross 261111-130755-91093a (R = 5), memory
261111-130756-004da8. Correctness, every candidate against the reference model and in
cross-thread runs under ASan+UBSan and TSan: 261111-130750-f599af. Medians, ns; A/A bands from
the pair named in Noise.

**Single loop**, score = arm + re-arm + cancel + expire, ns per entry:

| Candidate | n = 1,000 | ratio to best | n = 1,000,000 | ratio to best |
|---|---|---|---|---|
| (a) prototype | 39.9 (A/A 39.9) | 1.00 | 346.7 (A/A 346.0) | 1.00 |
| (b) stash as is | 1,075.1 | 26.97 | 1,217.0 | 3.51 |
| (c) stash, intrusive leaf | 852.3 | 21.38 | 598.7 | 1.73 |
| (d) prototype + inbox, 56 B | 42.4 | 1.06 | 388.6 | 1.12 |
| (e) prototype + inbox via `next` | 47.1 | 1.18 | 348.9 | 1.01 |
| (f) (e), striped inbox | 44.5 | 1.12 | 347.2 | 1.00 |

A/A bands on the score (min-max of ten runs): 48% at n = 1,000, 19.5% at 1,000,000; the A/A
medians agree within 0.2%. Stash's cost is in `expire`: 835 ns per entry at n = 1,000 and 863 at
1,000,000 for (b), which walks every stale filing a re-arm or cancel leaves; (c) leaves none and
expires at 157 ns at 1,000,000 against (a)'s 106, but still walks four levels per tick, 664 ns per
entry when the entries are sparse (n = 1,000). Arm and re-arm cost stash 80-213 ns, the
prototype 5-7 ns at n = 1,000 and 98-116 ns at 1,000,000 (the entries visited in random order,
so a cache miss per entry).

**Cross-thread**, producer ns per arm (one owner firing; every run valid: each entry fired once,
none early, the last within 1 ms of the last deadline):

| Candidate | P = 1 | P = 4 | P = 14 |
|---|---|---|---|
| (b) stash as is | 157.1 (A/A 155.8) | 195.7 (A/A 198.8) | 291.5 (A/A 295.2) |
| (c) stash, intrusive leaf | 126.3 (0.80x) | 157.5 (0.80x) | 216.1 (0.74x) |
| (d) prototype + inbox, 56 B | 21.2 (0.13x) | 230.8 (1.18x) | 1,347.5 (4.62x) |
| (e) prototype + inbox via `next` | 14.7 (0.09x) | 267.1 (1.37x) | 1,378.9 (4.73x) |
| (f) (e), striped inbox | 13.6 (0.09x) | 11.6 (0.06x) | 32.6 (0.11x) |

A/A bands: 19.2%, 28.8% and 15.0%. One inbox head is one contended cache line: (d) and (e) lose
to stash from four producers on. Striped, (f) arms 9 to 17 times cheaper than stash at every P.
The owner does the filing for (d)-(f) (an arm into its wheel per entry, about 100 ns at this
scale), which stash's producers do themselves: (f)'s latest fire came 3.7 ms (P = 4) and 4.3 ms
(P = 14) after its deadline against stash's 1.0 ms, while 1,000,000 arms arrived within 10 ms.

**Memory**, the counting build:

| Candidate | entry bytes | bytes per entry, n = 1,000 / 1,000,000 | heap per re-arm, n = 1,000 / 1,000,000 |
|---|---|---|---|
| (a) prototype | 32 | 97.6 / 32.1 | 0 / 0 |
| (b) stash as is | 16 | 269.3 / 43.5 | 101.3 / 27.2 |
| (c) stash, intrusive leaf | 16 | 227.9 / 27.5 | 69.8 / 11.2 |
| (d) prototype + inbox, 56 B | 56 | 121.7 / 56.1 | 0 / 0 |
| (e) prototype + inbox via `next` | 32 | 97.7 / 32.1 | 0 / 0 |
| (f) (e), striped inbox | 32 | 101.8 / 32.1 | 0 / 0 |

The prototype's 64 KiB of slot heads is the 65 bytes per entry at n = 1,000 ((f)'s 64 stripes
add 4 KiB); stash allocates per
arm (a box, slots, leaves and level nodes) and frees only when `clean` passes the slot, so a
re-arm costs heap until the old deadline passes.

**Manual clock.** Every candidate runs its model test on a clock that starts at 7 s, far below
`steady_clock`, and no fire is early (261111-130750-f599af). stash reads no clock; only
`SchedulerQueue` fixes `steady_clock`.

## The pick

By the rule, **no candidate passes**, so there is no pick:

- (b) and (c) fail the single loop by 3.5x to 27x; (c) passes the cross-thread bound (0.74x to
  0.80x of stash).
- (d) fails the cross-thread bound at P = 14 (4.62x) and the single loop at n = 1,000,000 (1.12x);
  (e) fails both (4.73x; 1.18x at n = 1,000).
- (f) passes the cross-thread bound at every P (0.06x to 0.11x) and the single loop at
  n = 1,000,000 (1.00x), and misses it at n = 1,000 by its median: 44.5 ns against (a)'s 39.9,
  1.12x against 1.10. Its owner path is (a)'s source (`cand_f::arm` and `cancel` call
  `wheel.hh` directly; `expire` adds one relaxed load); (d), (e) and (f), three such paths, land
  at 1.06x, 1.18x and 1.12x, so the n = 1,000 row does not resolve 10% between them.

The gap the lead brings to the owner: (f) against the prototype, 4.7 ns per entry's lifecycle at
1,000 entries (a 32-byte entry, no heap); (f) against stash, 9 to 17 times cheaper per
cross-thread arm, 3.5x to 27x cheaper on the loop path, no heap per arm.

## What it means for Kronuz/stash and Kronuz/scheduler

The owner's call, on these numbers:

- **stash.** Its multi-producer design is beaten on its own case by a 64-stripe intrusive inbox in
  front of a single-owner wheel, and its leaf-per-slot structure costs the loop path 3.5x to 27x,
  most of it in `expire`. The improved leaf (c) is the smallest change that helps (cross-thread
  0.74x to 0.80x of stash, single loop 1.7x better at 1,000,000) and does not close the loop gap.
  Options: retire stash as a timer wheel in favor of (f); or keep it for its non-timer users, if
  any, and improve it with (c)'s leaf.
- **scheduler.** `SchedulerQueue` can sit on (f): `ThreadedScheduler` and `Debouncer` arm from
  any thread through the stripes, and the scheduler's thread owns the wheel. Two contracts change:
  another thread arms only an unarmed entry, and only the owner cancels or re-arms. A
  cross-thread `clear()` stays the flag a `ScheduledTask` already carries (`atom_cleared_at`,
  checked at fire); a debounce from another thread either keeps a fresh task per debounce, as
  today, or uses (d)'s requested-deadline field for an entry that re-arms in place (24 bytes more
  per entry).
- **Not measured:** the wake of a sleeping owner (the same for every candidate), other hosts and
  architectures, and more than 64 producer threads (stripes are then shared).
