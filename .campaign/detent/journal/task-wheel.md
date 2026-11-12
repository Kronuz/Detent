# task-wheel: journal

State: working
Quest: .campaign/detent/quests/task-wheel.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Wheel, integration, tests and docs are in (task:e0b3bc2 + docs). Suite green 261111-200257-79ef5c
(at task:29efffaa). Mutants caught: no cascade 261111-200754-54595f, no owner assertion
261111-200759-d5c797. CI pushed (run 43152858997, ASan/TSan).
A1 jobs queued exclusive behind light holds: pads probe 261111-194713-13e696, single row
261111-200732-e12dee, cross row 261111-200735-8e28e4; strict probe with t (not evidence)
261111-201226-76b3a1.

## Next

1. Read the strict probe and the single row: t vs a at n=1e3/1e6 within 1.10x? If not, the
   pads probe says whether the gap is placement; fix the layout (<= 5 tries).
2. Cross row: t vs f at P=1/4/14 within 1.10x.
3. A1 numbers into docs/timer-wheel.md; CI green (A4); paste the brief's table; ready for review.

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
- Not in this quest's criteria, so not done here: inline_scheduler `post_call`,
  `task::blocking_scheduler` (async-drivers row 1 lists them; the quest's A3 does not).
