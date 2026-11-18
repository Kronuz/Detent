# logger-wheel-ab: journal

State: ready for review
Quest: .campaign/detent/quests/logger-wheel-ab.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Done. The tsan-c failure 261112-040003-5f4ecf was a harness race, fixed at logger:ef0bb6b and
rerun clean (261112-040521-982cc9). The first diagnostic pair (261112-052830-0b2f0e,
261112-052833-be4dfa) failed on my zsh loop passing "3 1" as one argument; resubmitted with split
args as 261112-055705-1b993a and 261112-055709-999bf4. Suite 261112-053405-710f23 green.

## Next

1. Review.

## Blocked on

Nothing.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| A1 | logger:ce34d2d, `docs/perf/wheel-ab.md` "Rule", committed before any measured run |
| A2 | 261112-040502-859c44 (passed at ef0bb6bbdd36; ignores docs-only docs/perf/wheel-ab.md), 261112-040515-550c07 (passed at ef0bb6bbdd36; ignores docs-only docs/perf/wheel-ab.md), 261112-040508-910d14 (passed at ef0bb6bbdd36; ignores docs-only docs/perf/wheel-ab.md), 261112-040521-982cc9 (passed at ef0bb6bbdd36; ignores docs-only docs/perf/wheel-ab.md), 261112-040534-165b69 (passed at ef0bb6bbdd36; ignores docs-only docs/perf/wheel-ab.md), 261112-040540-dc3982 (passed at ef0bb6bbdd36; ignores docs-only docs/perf/wheel-ab.md), 261112-040546-2390a9 (passed at ef0bb6bbdd36; ignores docs-only docs/perf/wheel-ab.md), 261112-040552-6a1eea (passed at ef0bb6bbdd36; ignores docs-only docs/perf/wheel-ab.md), 261112-040557-766cf0 (passed at ef0bb6bbdd36; ignores docs-only docs/perf/wheel-ab.md), 261112-040603-2ef310 (passed at ef0bb6bbdd36; ignores docs-only docs/perf/wheel-ab.md) |
| A3 | `docs/perf/wheel-ab.md` "Verdict" |

Also: A/B rows at logger:91924df 261112-035007-2f8fd3 .. 261112-035041-cd1116 (10 rows); suite
261112-053405-710f23; diagnostics 261112-055705-1b993a, 261112-055709-999bf4.

## Result

`docs/perf/wheel-ab.md` "Verdict" (logger:c93ace2): logger stays on its queues and heap for every
pattern. (B) loses pattern 1 (r 1.36 to 1.84) and wins 2 and 3 (r 0.26 to 0.69) but breaks
per-thread and due order. (C) keeps order and loses patterns 2 and 3 at every P except a no win
at pattern 2, P = 4: producer median +24% to +55%, heap +80 B/line (fails 1.10 everywhere), fire
lag +1 ms, LOG thread behind in pattern 3 from P = 4. Most of (C)'s producer gap is the make_shared
block (280 B) crossing macOS's 256 B nano allocator: with it off, r falls to 1.048 (pattern 3) and
1.112 (pattern 2). wheel:45f98eb cannot move (C)'s deciding metrics (from the diff, not rerun).

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
