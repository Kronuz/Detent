# logger-exit-crash: journal

State: waiting on jobs 261112-164721-1f7329 (A1,A3 pass), 261112-163559-b2eff5 (mutant), 261112-163606-ae7ad9 (A2), 261112-163610-8588d1 (A4), suite 261112-164723-6777b6
Quest: .campaign/detent/quests/logger-exit-crash.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Fix committed on the logger branch (logger:4b5b847, logger:0261865). Waiting on jobs.

## Next

1. When jobs pass: paste `campaign brief` table, write Result, set ready for review.

## Blocked on

<What, and who decides. "Nothing" when nothing.>

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| A5 | `docs/ROADMAP.md`: "stash bundles a no-op `L_*` stub (`L_EXC`) so it builds without a logger." |

## Result

<When ready for review: what changed, its measured effect with numbers and the jobs behind them,
and what it leaves open. Results and how it works go in the docs of the repository it changed
(docs/perf/, docs/mechanisms/, a library's own); name them here. `campaign map` shows this.>

## Rulings I made

- Cause (A1): at exit `LogWorker::get()`'s static Finisher ends and joins the LOG thread for 200 ms;
  on timeout (a handler stalled) it returned, and the statics (`Logging::handlers`, `config`,
  `hooks`, tints) were destroyed while the thread was still in `pass()` -> `Logging::emit`/`log`
  writing through them: SIGSEGV. Fix: after a timed-out join the Finisher calls `abandon()`; the
  thread finishes its current line, then `pass()` returns at its next record, `close()` skips its
  writes and releases, and it sets `parked`; the Finisher waits for `parked` (bounded by one line).
  Queued lines are lost on that path (as with any timed-out join). The timeout is not widened.
- Test `test/exit.cc` (`logger_exit_test`): forced timeout by a 20 ms/line handler against 1200 lines,
  a handler destructor that sets a flag and sleeps; a call after it exits 99. Counting only, no
  wall-clock assertion. Fails on main (exit status nonzero, 3 of 3), passes with the fix (3 of 3).
  `test/exit_mutant.sh` removes `abandon()`; `test/exit_load.sh` is the A2 loop.
- Files outside the quest's file set, needed: CMakeLists.txt (the new test target), ARCHITECTURE.md
  and CHANGELOG.md (the mechanism and the fix).
- A5: the ROADMAP's trace-stub section named scheduler three times and the cancel/swap one once; both
  now name stash and the wheel only (grep -ci scheduler = 0).

## Evidence

