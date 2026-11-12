# task-wheel: journal

State: waiting on jobs 261112-083949-3c7f66 261112-083952-122d9e 261112-084006-e40030 261112-084016-071404 261112-084024-dcd8a5
Quest: .campaign/detent/quests/task-wheel.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Fixing correctness review F1 (blocking) plus N1-N3 and Xapiand fit N2/N3, at task:9a1bb31
(pushed for CI):
- F1: the wheel is private. `inline_scheduler` and `asio_scheduler` gain owner-thread
  `arm(entry, at)` / `cancel(entry) -> bool`; asio's `arm` calls `wake_by`, and a `cancel` that
  empties the wheel cancels the wake. `file`/`unfile`/`drop_timer` and inline's `start_timer`
  go through them (one way to arm). `scheduler_ref::wheel()` is replaced by `now()`, `arm`
  (false without timers) and `cancel`. A read-only `armed_timers()` replaces tests' `wheel().size()`.
- Test: `deadline_asio_test direct` (probe A7): an entry armed through `scheduler_ref::arm`, no
  deadline armed, fires once, never early, under 200 ms late; one cancelled through it never
  fires. `deadline_test ref` covers arm/cancel/now on inline_scheduler and false without timers.
- Xapiand fit N2: a deadline on a scheduler whose `start_timer` is not a `wheel_timer`
  (engine::Host) gets a plain `cancel_state::make(parent)` again: no 32-byte entry, no link
  without a parent (sync_test's fake-timer deadlines cover that path).
- Docs: deadlines.md "Arming a timer"; N1 (one tick plus the loop's wake, measured 0.5-2.2 ms);
  N3 (inline_scheduler deadlines on its thread; asio any thread); N2 (one-thread executor, in
  task_asio.hh's class comment, API.md, deadlines.md, ARCHITECTURE.md); Xapiand N3 (API.md: what
  `start_timer` returns decides what outlives the deadline; engine::Host's io_context or
  blocking scheduler).

`wheel()` callers (grep of Detent, Xapiand, every repo under ~/code/Kronuz, worktrees included):
- task (this branch): task.hh vtable, deadline_test.cc:70,72,97,111-114,
  deadline_asio_test.cc:71,90, deadline_alloc_test.cc:49: all moved to arm/cancel/armed_timers.
- Detent docs/design/async-drivers.md:126 and :638 name `scheduler_ref::wheel()` (X14; the
  lead's, outside my file set): now `scheduler_ref::arm`/`cancel`/`now`.
- Detent .campaign quest task-wheel.md:19 (A3 "`scheduler_ref` gains `wheel()`") and
  journal/async-drivers-design.md: the lead's.
- Xapiand .campaign/xapiand/hunt.md:108 and reviews/detent-task-wheel-fit.md:67 (N1) talk of
  `scheduler().wheel()`; reviews/detent-async-drivers-design.md (and its copies in Xapiand-wt).
  No Xapiand code calls it.
- wheel/wheel.hh, Detent's stash_niche_wheel.hh: the class's own members, not this accessor.
No code outside task calls `wheel()`.

Earlier, at task:c0cda23:
Quest rewritten for D72: task depends on Kronuz/wheel and keeps only the integration.

At task:c0cda23 (pushed; CI run 43254372787 green on FreeBSD, Linux gcc/clang/arm64, ASan+UBSan,
TSan, macOS x86_64; macOS arm64 still queued for a runner when this was written):
- task:12c0443 CMake fetches Kronuz/wheel at wheel:bfd72bc (full hash), `task` links
  wheel::wheel; task.hh includes wheel.hh and names wheel::timer_wheel / wheel::timer_entry; the
  inline wheel, the wheel-only tests and bench/wheel are gone (they live in Kronuz/wheel). Tests
  renamed: test/deadline_test.cc (manual, chain, ref), deadline_alloc_test.cc,
  deadline_asio_test.cc (offloop, teardown, idle). Local Debug suite green (7/7, Apple clang).
- task:c0cda23 docs/mechanisms/deadlines.md replaces docs/timer-wheel.md (A6); README, API.md,
  ARCHITECTURE.md, cancellation.md point at it.

CI red at task:5a1e516: macOS arm64 and FreeBSD failed `task_wheel` at wheel_test.cc:230
(`cross`, f == 1). The test stopped expiring 60 s in once producers finished, but slow producers
read a clock already far ahead, so arms 10 s past it never came due. Test bug, fixed in the
library at wheel:4a5bef4; task's copy of the test is gone with the inline wheel.

Wheel rules checked against task (docs/mechanisms/deadlines.md "The wheel's rules, kept"):
1. false fire: on_wheel(false) only releases; only asio's wheel dies with entries filed, after
   close(), and a closed scheduler neither files nor unfiles.
2. a fire may free its own entry: on_wheel's release frees the state when the deadline is gone
   (deadline_asio teardown, live_frames back to baseline; mutant 261112-080321-77ecd0).
3. entries outlive the wheel or are cancelled: refcounted state; drop_timer cancels.
Exact cancel holds: task never arm_remote()s; off-loop filing/unfiling go through asio::post.

Failed jobs at task:c0cda23, both my commands' faults, superseded: 261112-080318-bdea92 (the grep
matched sync_test.cc's `queued_`; narrowed in 261112-080531-e445ce), 261112-080321-77ecd0 (the
mutant was killed, but the command exited 1 instead of 0; inverted in 261112-080533-082edf).

## Next

1. When the jobs pass: `campaign accept task-wheel correctness --confirm F1=261112-083949-3c7f66`
   (the lead's), then A7 (the Xapiand lead's review of the diff at task:9a1bb31).

## Blocked on

Nothing blocks the work. Question for the lead (contract): API.md on main says a deadline "keeps
no reference to `sched`"; on the wheel its `wheel_timer` points at the scheduler, so the
scheduler must outlive the deadline (asio_test.cc and every in-repo user already do; A3's
operational test passes). Options: (a) accept and document it (done in API.md and deadlines.md);
(b) keep the old promise with a per-deadline liveness handle (an allocation or a shared
pointer per scheduler, plus a bit for "wheel released"). Recommend (a).

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | 261112-084024-dcd8a5 (at task:9a1bb31; earlier 261112-080531-e445ce) |
| A3 | 261112-083949-3c7f66 (suite at task:9a1bb31); fails without: 261112-083952-122d9e (F1 mutant), 261112-080533-082edf (false fire) |
| A4 | 261112-084006-e40030 (TSan), 261112-084016-071404 (ASan+UBSan), at task:9a1bb31 |
| A6 | `docs/mechanisms/deadlines.md`: "Deadlines on Kronuz/wheel"; `docs/mechanisms/deadlines.md`: "so the scheduler outlives the deadline" |
| A7 | pending: the Xapiand lead's review |

## Review findings

| Finding | Fix | Confirming job |
|---|---|---|
| F1 (correctness, blocking) | task:9a1bb31 scheduler `arm`/`cancel` wake the loop; wheel private | suite 261112-083949-3c7f66 (`deadline_asio_test direct`); mutant 261112-083952-122d9e; at c0cda23 the probe 261112-082613-74dd5c shows the defect |
| N1 | deadlines.md, API.md, task_asio.hh: one tick plus the loop's wake, 0.5-2.2 ms | [artifact] |
| N2 | one-thread executor documented (task_asio.hh, API.md, deadlines.md, ARCHITECTURE.md); new arm/cancel assert `!io_ \|\| runs_here()` | [artifact] |
| N3 | "any thread" scoped to asio_scheduler; inline on its thread | [artifact] |
| Xapiand fit N2 | wheel-less deadlines carry no entry | suite 261112-083949-3c7f66 (sync_test) |
| Xapiand fit N3 | API.md: the timer type decides what outlives a deadline | [artifact] |

## Result

Pending.

## Rulings I made

- F1 mutant: the fixed `direct` test cannot compile at task:c0cda23 (no `arm`), so the mutant
  restores c0cda23's shape on the fix (wake_by back in `file()` only, out of `arm()`); script
  Detent/.scratch/task-wheel-f1/mutant.sh. The review's probe is the c0cda23 run.
- `scheduler_ref` gains `now()` (every scheduler has it): a driver arms at `now() + d`.
  `arm` returns false and `cancel` false on a scheduler without timers, as `post_call` does.
- `armed_timers()` (read-only count, owner thread) stays public for tests and metrics; it arms
  nothing. The private `wheel()`/`timers()` helpers create the wheel on first use.
- asio `arm` after close(): allowed and documented ("once closed, nothing fires; the destroyed
  scheduler fires what is still armed false"), matching Xapiand's hunt.md N1 hazard; no new
  behaviour there.
- Quest A3 says "`scheduler_ref` gains `wheel()`"; the lead's ruling replaces it with
  arm/cancel. Source-compatible for every caller (none outside task).

- Since D72 the first three rulings below (harness, far deadlines, fire(e, false)) describe code
  that now lives in Kronuz/wheel; task keeps only the integration rulings.
- The harness is ported to task as `bench/wheel/` (Detent's wheel_vs_stash.cc at Detent:5ecbeb4c,
  workload verbatim, stash arms dropped; arms a, a2, f, f2, t). A1's rows run there.
- Far deadlines (coordinator note via stash-niche): the wheel is hierarchical, as the quest's
  Outcome says: (f)'s 4,096 x 1 ms level plus six 64-slot upper levels that cascade when the
  cursor enters a slot's span (at most six re-files per entry, no tick walks undue entries);
  their heads extend the same array, so unlink is (f)'s. Tested in wheel_test `model` (deadlines
  to 40 days, multi-day jumps) and `far` (1M entries a day out; next() wakes per level).
- `timer_entry::fire(entry&, bool expired)`: false when a destroyed wheel drops an armed entry,
  so a deadline's state releases the wheel's reference ("a wheel destroyed with deadlines armed
  releases them").
- Owner arm/cancel of an unarmed entry drains the inbox first when it holds anything, so an
  owner re-arm or cancel of an entry another thread just armed is exact.
- `start_timer` on inline_scheduler and asio_scheduler takes task::deadline's fire only (static
  assert): nothing else calls asio_scheduler::start_timer (git grep over task, Detent, http,
  reactor, Xapiand, consensus, strata, journal, completion, scheduler). The deadline's entry lives
  in its cancel state (`timed_cancel_state`, linked_bit|timed_bit), so a deadline allocates only
  that pooled state, as before.
- asio_scheduler off-loop: creation posts the filing; destruction posts the unfile (holding a
  reference, released if dropped); once closed neither touches the wheel, and the destroyed
  scheduler's wheel drops what is left. A destroyed on-loop deadline that empties the wheel
  cancels the wake timer, so `io.run()` returns as it did with one steady_timer per deadline.
- drain() files inbox entries with file(), not arm(): arm()'s unarmed path drains again, so
  producers refilling the inbox could nest drains without bound (task:5a1e516).
- Names: task uses wheel::timer_wheel and wheel::timer_entry directly, no task:: alias (one name
  per type). The integration tests are renamed deadline_* since they test deadlines, not the wheel.
- AGENTS.md's repository map (outside this quest's file set) does not list the deadline_* tests;
  left for the lead.
- Not in this quest's criteria, so not done here: inline_scheduler `post_call`,
  `task::blocking_scheduler` (async-drivers row 1 lists them; the quest's A3 does not).
