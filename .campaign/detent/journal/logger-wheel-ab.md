# logger-wheel-ab: journal

State: waiting on jobs 261111-213647-db2133 261111-213650-b74b73 261111-213704-468c65 261111-213706-83fcd7 261111-213709-9eeb14 261111-213711-19f0f9 261111-213714-b4e077 261111-213717-89857f 261111-213720-97a103 261111-213722-d6f730 261111-213725-b37905 261111-213727-d1817f 261111-213730-f2e369
Quest: .campaign/detent/quests/logger-wheel-ab.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Harness committed at logger:30790d1. Jobs queued (all `--slot wab`, tsan in `wab-tsan`):
- A2 correctness: check 261111-213647-db2133 (light), tsan 261111-213650-b74b73 (light)
- smoke 261111-213704-468c65 (strict, after check)
- rows (exclusive, after smoke): p1 P1 261111-213706-83fcd7, p1 P4 261111-213709-9eeb14,
  p1 P14 261111-213711-19f0f9, p2 P1 261111-213714-b4e077, p2 P4 261111-213717-89857f,
  p2 P14 261111-213720-97a103, p3 P1 261111-213722-d6f730, p3 P4 261111-213725-b37905,
  p3 P14 261111-213727-d1817f, formatted p1 P1 261111-213730-f2e369

Probe (local, check mode, P=4, not timing): both backends pass exactly-once/no-early/no-cleared
for patterns 1 and 3; (B) wrote 24,002 of 24,576 immediate lines out of per-thread order (wheel
slot lists are LIFO; inbox stripes are LIFO), (A) 0.

## Next

1. When the jobs finish: `campaign job log <id>` for each; copy each row's verdict table into
   docs/perf/wheel-ab.md "Verdict" with the job ids, commit, and fill the journal's Result.
2. If check/tsan fail: fix in ab.cc, resubmit the chain.

## Blocked on

Nothing.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
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
7. Heap per call counts `operator new` bytes on every thread through a replacement in the harness
   binary (per-thread uncontended counters; disabled under TSan).
