# logger-wheel-ab: journal

State: waiting on jobs 261111-214127-dc777e 261111-214130-4103e4 261111-214132-5d4386 261111-214135-abf741 261111-214137-11d246 261111-214140-2dd32d 261111-214142-0655f8 261111-214145-259bc3 261111-214148-32e29d 261111-214151-0258b9
Quest: .campaign/detent/quests/logger-wheel-ab.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

At logger:91924df (kept-line count made atomic after TSan job 261111-213650-b74b73 flagged the
harness's own counter across libc++'s std::barrier; no backend race). Check at logger:30790d1
passed (261111-213647-db2133): all 18 backend/pattern/P cases exactly once, none early, no
cleared line written. Resubmitted at 91924df (old smoke/rows cancelled):
- PASSED at 91924df: check 261111-214121-f90ca2 (18/18 ok), tsan 261111-214123-23aa11 ((B) 6/6 ok,
  no reports), smoke 261111-214125-385b87 (6 runs, config asserted)
- rows (exclusive): p1 P1 261111-214127-dc777e, p1 P4 261111-214130-4103e4,
  p1 P14 261111-214132-5d4386, p2 P1 261111-214135-abf741, p2 P4 261111-214137-11d246,
  p2 P14 261111-214140-2dd32d, p3 P1 261111-214142-0655f8, p3 P4 261111-214145-259bc3,
  p3 P14 261111-214148-32e29d, formatted p1 P1 261111-214151-0258b9

Order: (B) writes immediate lines out of per-thread order in every check (28,237 of 30,720 at
P=1; 103,959 of 122,880 at P=4); (A) 0. Probe (local, check mode, P=4, not timing): both backends pass exactly-once/no-early/no-cleared
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
7. Heap per call counts `operator new` bytes on every thread through a replacement in the harness
   binary (per-thread uncontended counters; disabled under TSan).
