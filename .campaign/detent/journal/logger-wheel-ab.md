# logger-wheel-ab: journal

State: waiting on jobs 261112-035015-2210ff 261112-035018-1410b6 261112-035022-b1f5a9 261112-035026-0fd817 261112-035030-378568 261112-035034-adfe40 261112-035037-bda2f7 261112-035041-cd1116 261112-040502-859c44 261112-040508-910d14 261112-040515-550c07 261112-040521-982cc9 261112-040527-32146a 261112-040534-165b69 261112-040540-dc3982 261112-040546-2390a9 261112-040552-6a1eea 261112-040557-766cf0 261112-040603-2ef310
Quest: .campaign/detent/quests/logger-wheel-ab.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Scope addition from the lead (2026-11-12): arm (C), logger's LOG thread filing deferred lines in a
wheel it owns, against (A) on patterns 2 and 3 at P=1,4,14 under the same rule; pattern 1: C = A.

The first A/B rows (261111-214127..214151) all failed at submission: zsh passed "1 1" as one
argument. Resubmitted A/B rows at logger:91924df with split args (correctness green there):
p1 P1 261112-035007-2f8fd3, p1 P4 261112-035011-fbe2f5, p1 P14 261112-035015-2210ff,
p2 P1 261112-035018-1410b6, p2 P4 261112-035022-b1f5a9, p2 P14 261112-035026-0fd817,
p3 P1 261112-035030-378568, p3 P4 261112-035034-adfe40, p3 P14 261112-035037-bda2f7,
fmt p1 P1 261112-035041-cd1116.

Arm C at logger:9711f36: check A/B 261112-035958-e92d8c and check-c 261112-040001-65b71b passed
(A and C: 0 order and 0 due-order inversions at every P; B pattern 2: 392,823 due inversions of
430,080 at P=14). tsan-c 261112-040003-5f4ecf failed on a harness race (the sink read the post-call
clock with no happens-before); fixed at logger:ef0bb6b (atomics). Chain resubmitted at ef0bb6b:
check 261112-040502-859c44, check-c 261112-040508-910d14, tsan B 261112-040515-550c07, tsan-c
261112-040521-982cc9, smoke 261112-040527-32146a; rows A/C (exclusive): p2 P1 261112-040534-165b69,
p2 P4 261112-040540-dc3982, p2 P14 261112-040546-2390a9, p3 P1 261112-040552-6a1eea,
p3 P4 261112-040557-766cf0, p3 P14 261112-040603-2ef310.

Rows done (A/B at 91924df): p1 P1 261112-035007-2f8fd3 wheel loss (median r 1.844, w 0.103; p99 r
2.707; lag p99 1.77x; LOG CPU 1.26x); p1 P4 261112-035011-fbe2f5 wheel loss (median r 1.522,
w 0.016; p99 r 1.544).

Order: (B) writes immediate lines out of per-thread order in every check; (A) 0.

## Next

1. When the jobs finish: read each log (`~/.scratch/campaign/jobs/<id>/a1/log`); copy each row's
   table and verdict into docs/perf/wheel-ab.md "Verdict" (A/B at 91924df, A/C at 9711f36) with
   job ids, lines-held for pattern 3 (C vs heap), and fill the journal's Result.
2. If check/tsan fail: fix in ab.cc, resubmit the chain.

## Blocked on

Nothing.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| A2 (correctness) | check 261111-214121-f90ca2, TSan (B) 261111-214123-23aa11, smoke 261111-214125-385b87, all at logger:91924df; rows pending |
| A1 | logger:ce34d2d, `docs/perf/wheel-ab.md` "Rule", committed before any measured run |

## Result

<When ready for review: what changed, its measured effect with numbers and the jobs behind them,
and what it leaves open. Results and how it works go in the docs of the repository it changed
(docs/perf/, docs/mechanisms/, a library's own); name them here. `campaign map` shows this.>

## Rulings I made

1. (A) is logger at the base commit: its per-thread byte queues and the LOG thread's binary heap of
   deferred `Logging` records. Logger uses only `ScheduledTask`'s one-shot cancel and `Thread` from
   Kronuz/scheduler; the stash wheel is not on logger's path at all (ARCHITECTURE.md), so "stash"
   in the quest title means logger's own queues+heap here.
2. (B) gives each line a 128-byte record (timer_entry + state + inline message) from a per-thread
   pool (own free list + a Treiber stack the LOG thread returns records to). The wheel needs the
   caller to keep the entry alive until it fires; logger's byte queue has no such need for
   routine lines. A pool, not `new` per line, gives the wheel its best case.
3. Slow-op clear on (B): a flag the fire checks (`fetch_or` on the record's state; whoever ends
   last returns the record). No remote-cancel API added. A cleared record stays filed until its
   200 ms deadline, as (A)'s cancelled record stays in its heap until it reaches the top.
4. (B) copies (A)'s policies where they are policy, not mechanism: the routine-line due rule
   (at the call when a burst starts and nothing is pending, else +1 ms), the sleep handshake
   (state word, wake time, condition variable), the fan-out through `Logging::log`, and emit's
   " ~age" suffix for records older than 100 ms (so the LOG side does the same string work).
5. (B) needs a `seq_cst` fence after `arm_remote` (its CAS is release) for the sleep handshake;
   on x86 that is an `mfence`. (A) gets the same ordering from its one `seq_cst` store.
6. (B)'s producer clock is `logger_detail::steady_now()` (ns at arm time, which the wheel needs);
   (A) stamps a raw `mach_continuous_time` and converts on the LOG thread.
8. Arm (C) (lead's scope addition, 2026-11-12): a copy of logger.{h,cc} in bench/wheel_ab/c/ (the
   library stays unchanged, as test/frozen does it); `Logging` gains a 32-byte `timer_entry` hook,
   a self-reference while filed, and its sort key (+72 bytes per record, counted in heap/call).
   The LOG thread's pass takes `expire(now)`'s due lines into a batch sorted by (due, created,
   seq) and merges it with the queues as the heap top was merged; a deferred line met during a
   pass and already due goes straight into the batch (sorted insert), so due order is exact.
   Cleared lines stay filed until their slot, then are dropped (the heap drops a cancelled top).
   The wait is `next()`: the end of the due line's 1 ms slot, so a deferred line fires up to 1 ms
   later than the heap's exact wait.
9. "Lines held" (pending_count sampled every 1 ms: peak and line-ms per call) measures how long
   cleared lines stay filed, uniformly for A (heap) and C (wheel) without touching the library.
10. The check gates due order for A and C exactly: each call is bracketed by clock reads, and a
   line counts as an inversion only when its due (<= post + delay) is surely before the due of a
   line written earlier (>= pre + delay).
7. Heap per call counts `operator new` bytes on every thread through a replacement in the harness
   binary (per-thread uncontended counters; disabled under TSan).
