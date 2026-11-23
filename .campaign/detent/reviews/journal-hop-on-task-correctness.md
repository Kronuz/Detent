# journal-hop-on-task review: correctness

Commit: journal:485cd23dfc960661d522d9be15b11eac80221a05
Verdict: APPROVE
Date: 2026-11-19

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

## Evidence I relied on

- 261119-181300-c03364 (journal:8a79661): `cancel_bare_home` fails on "the cancelling thread
  leaves the waiter linked for the loop", so 904037a does not close F6 (D129, A1).
- 261119-181456-c79217 (head): journal_driver_test, 49 checks, 0 failures, `bare_home_refused` ok.
- 261119-181502-b6d697 (head, bare-home mutant): both `bare_home_refused` checks fail; the mutant
  script refuses stale text (test/turn_seal_mutants.sh:39-45), so it applied.
- 261119-181505-2fa91c (head): TSan over journal_driver and the turn_seal thread tests, passed;
  it carries `cancel_hops`, `cancel_races_barrier` and `entry_threads_busy`.
- 261119-181508-6b48ee (head): the suite, passed.

## What I checked myself

The diff journal:9e11713..485cd23 against task:95798a0 (the pin), and probe job
261119-190503-01756b from `.scratch/detent/review-journal-hop-on-task-correctness/probe.sh`
(run it as `sh probe.sh <that dir>` in a journal checkout). It adds three cases to driver_test:
`probe_bare_home_no_token`, `probe_noop_bound` (1 and 100 waits) and `probe_cancel_after_stop`.

1. Every unlink is on the waiter's home. `link` is called only from `await_suspend`
   (turn_driver.h:406). Unlinks happen in `hop` (426), `resume` (465) and `~Wait` (397). The
   cancel callback now only claims and posts (420-421), with no inline fallback.
   - Cancel before link: `await_ready` returns on `token.requested()` (401). Nothing links and
     nothing is probed.
   - Cancel between `await_ready` and `emplace`: `cancel_callback`'s constructor runs `Cancel`
     inline on the caller (task.hh:1359-1367), which posts `hop`.
   - Cancel racing completion: the claim CAS picks one winner. `resume` unlinks on the loop
     whether or not it wins (465-466), and `hop` reads `owner` on the loop.
     `cancel_races_barrier` forces this interleaving, and TSan 2fa91c covers it.
   - Cancel after `run()` stops: the waiter stays linked, and the hop unlinks it on the loop's
     next drive (`probe_cancel_after_stop`).
   - A frame destroyed with its waiter linked: `~Wait` resets the callback, which waits for a
     running `Cancel` (now only a claim and a post), then unlinks on the destroying thread. That
     is the loop's for the blocking entry's root. F7's queued hop into a destroyed frame is
     unchanged and documented (blocking.h:88-89).
2. The refusal is at the only linking point. `await_ready`'s fast paths return before any
   callback registers. Every retry, including `run()`'s idle loop (334), builds a new `Wait`, so
   each one links through `await_suspend`'s check.
3. The no-query claim is correct. In task:95798a0, `scheduler_ref` exposes `post_call` only by
   calling it, and `vt()` is private (task.hh:221-225, 243-244). The probe queues `([](void*){},
   nullptr)`, which never touches a waiter, so its running after the waiter is gone is harmless.
   It is one call per suspension, queued on the suspending thread and drained in the scheduler's
   normal FIFO. The blocking entry's `drive` runs it in the same pass (`if (!ready_.empty())
   continue;`), so it neither masks nor causes `stalled`. Nothing re-suspends because of it, so
   it cannot pile up (`probe_noop_bound`: the run count is linear in the number of waits, and the
   queue is empty afterwards).
4. A throw from `await_suspend` resumes the coroutine with the exception ([expr.await]/5). At
   that point `link` and `callback.emplace` have not run, so `~Wait` finds no callback and a null
   `owner`, and does nothing. A `false` `post_call` queued nothing. `bare_home_refused` checks the
   error and an empty `waiting_groups()`. A leak is ASan's, in CI.
5. The docs are present tense and match the code, with the two wording nits below.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | no | nit | The refusal is unconditional. A suspending `durable()` with an empty token, which no source can cancel and so never hops, also throws on a scheduler without `post_call`, where it waited before. The CHANGELOG entry ("such a wait throws") reads as if only a cancelled wait is refused. Narrowing it is the owner's call. | turn_driver.h:405; CHANGELOG.md:13-16; `probe_bare_home_no_token` in 261119-190503-01756b | Options: (a) keep it, with one rule and every task scheduler declaring `post_call`, and reword the entry to "a `durable()` wait that suspends on a scheduler without `post_call` throws..."; (b) refuse only when `!task::detail::token_empty(token)` (task.hh:1312), which also skips the no-op probe for uncancellable waits. |
| F2 | no | nit | `run()`'s idle `Wait` (group 0) repeats `run()`'s own `post_call` check on every idle suspension, at the cost of one queued no-op each. It is bounded and harmless (question 3). A task-side `scheduler_ref` query would remove both probes, as the journal says. | turn_driver.h:314, 334, 405 | None needed for this quest. Optionally file the query in task. |
| F3 | no | nit | turn-seal.md's "needs the same of its caller's scheduler" leaves the referent open. The preceding sentence is about a thread-safe `post_call`, which a cross-thread cancel also needs (an `inline_scheduler` home cancelled from another thread while its loop runs races its deque). The hop reaches the driver's loop only because `durable()` callers are loop-confined, and the "loop-confined" sentence names `seal`, `forget` and the sealers but not `durable()`. | docs/mechanisms/turn-seal.md:28-30, 36-38 | State it directly: `durable()` runs on the loop's thread; its scheduler must declare `post_call`, thread-safe when the token is cancelled from another thread; on one that declares none, a suspending wait throws `std::logic_error` before it links. |

## Not run

- I did not see probe 261119-190503-01756b's result before writing this file. My conclusions
  rest on the reading cited above. If the probe fails, the failing case names which conclusion to
  revisit.
- CI 44659451731 at head (ASan/UBSan, Linux TSan, FreeBSD) was still running. The leak check in
  question 4 is CI's.
- I did not run the `cancel-without-hop` mutant (test/mutants.py:37-39) at head. Its text matches
  turn_driver.h:421 once.
- No cost measurement of the per-suspension no-op.
