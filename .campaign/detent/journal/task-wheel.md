# task-wheel: journal

State: waiting on jobs 261111-194713-13e696 261111-201226-76b3a1 261111-201326-d9faea 261111-201331-e63fee
Quest: .campaign/detent/quests/task-wheel.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Direction change (coordinator, D72 coming): the wheel becomes Kronuz/wheel, header-only, no
dependencies; task integration stops and moves to a later quest. A1 carries over unchanged.

Where things are on quest/detent/task-wheel (task:5a1e516, pushed):
- The wheel: task.hh "// Timers" section, `struct timer_entry` and `class timer_wheel` (the block
  before `namespace detail { struct timed_cancel_state; ...}`). It uses only std (<algorithm>,
  <array>, <atomic>, <bit>, <cassert>, <cstdint>, <thread>) and no task type: it lifts out as a
  standalone header by moving that block and renaming the namespace.
- Wheel-only tests: test/wheel_test.cc `reentry`, `model`, `far`, `cross`, `owner` (the rest,
  `manual`, `chain`, `ref`, are integration) and test/wheel_alloc_test.cc `wheel()` (`deadlines()`
  is integration).
- Harness: bench/wheel/ (proto.hh = (a), wheel_bench.cc arms a a2 f f2 t, wheel_bench.sh,
  wheel_bench_summary.py); `t` includes task.hh only for timer_wheel.
- Integration already written and suite-green (stopped, not to be extended here): deadline<S> on
  S::clock with the entry in `detail::timed_cancel_state`, inline_scheduler clock/wheel/advance,
  asio_scheduler start_timer on the wheel, scheduler_ref::wheel(); test/wheel_asio_test.cc.
  It is for the later task-integration quest to take or redo against Kronuz/wheel.

Jobs: suite 261111-200257-79ef5c green (at task:29efffaa), 261111-201335-80a7d2 queued at head;
mutants caught 261111-200754-54595f (no cascade), 261111-200759-d5c797 (no owner assertion);
A1 queued exclusive: pads probe 261111-194713-13e696, single row 261111-201326-d9faea, cross row
261111-201331-e63fee; strict probe with t (not evidence) 261111-201226-76b3a1. CI run 43152858997.

## Next

1. Read the A1 rows: t vs a single-loop at n=1e3/1e6 within 1.10x, t vs f cross within 1.10x;
   if the n=1e3 gap shows, the pads probe says whether it is placement; up to five layout tries.
2. Wait for the lead to name the Kronuz/wheel quest and worktree; move the wheel there.

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | pending |

## Result

Pending.

## Rulings I made

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
- Not in this quest's criteria, so not done here: inline_scheduler `post_call`,
  `task::blocking_scheduler` (async-drivers row 1 lists them; the quest's A3 does not).
