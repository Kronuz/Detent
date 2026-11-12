# task-wheel: journal

State: ready for review
Quest: .campaign/detent/quests/task-wheel.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

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

1. Hand to the lead: correctness review, then A7 (the Xapiand lead's review of the diff).

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
| A1 | 261112-080531-e445ce |
| A3 | 261112-080317-77f500; fails without: 261112-080533-082edf |
| A4 | 261112-080331-647f35, 261112-080334-3404ab |
| A6 | `docs/mechanisms/deadlines.md`: "Deadlines on Kronuz/wheel"; `docs/mechanisms/deadlines.md`: "so the scheduler outlives the deadline" |
| A7 | pending: the Xapiand lead's review |

## Result

Pending.

## Rulings I made

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
