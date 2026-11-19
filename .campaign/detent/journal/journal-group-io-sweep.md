# journal-group-io-sweep: journal

State: waiting on jobs 261118-145236-96e656 261118-145245-315199 261118-145253-7b2d4c 261118-145256-9a888f 261118-145301-7efc6f 261118-145320-05f3b3 (fix round 1: F1 F2 F5)
Quest: .campaign/detent/quests/journal-group-io-sweep.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Fix round 1 at journal:4d38a56 (code and test journal:4d2eef6, doc journal:4d38a56). F1: docs/perf/group-io.md
rewritten present tense with the before/after table of 261118-123558-25b19c and F4's bound. F2:
group_io gives a view of a closed (non-resident) group its own Range, as base did; the table is a
multimap again, publish reaches every live range of the group, group_io prunes the group's own dead
entries; host_test closed_view checks 2/2/0/2,2/4,4 artifacts. F5: A2 mutants rebuilt at the fix and
tagged --for A2. F3 is journal-host-ranges-race's.

## Next

1. When the jobs end: suite 261118-145236-96e656 (A1), mutants never 261118-145253-7b2d4c, live
   261118-145256-9a888f (view_sweep), shared 261118-145301-7efc6f (closed_view) all must fail-pass
   (A2), TSan host_test + race probe 261118-145245-315199 (no new race shape: base 2, head 20/21
   reports at ranges_ publish vs group_io, per F3), exclusive A/B rerun 261118-145320-05f3b3 at the fix
   (hot path changed: equal_range instead of operator[]); if its after numbers differ from the doc's,
   update the doc table to it; CI at journal:4d38a56; State "ready for review".

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | 261118-145236-96e656 (suite at journal:4d2eef6, includes host_test view_sweep and closed_view): queued |
| A2 | 261118-165146-365148; fails without: 261118-123533-1944ae, 261118-123541-c1f81b, 261118-145253-7b2d4c, 261118-145256-9a888f, 261118-145301-7efc6f |
| A3 | 261118-123558-25b19c (exclusive, before = ea397ab include/, after = journal:965cd55); rerun at the fix 261118-145320-05f3b3: queued |
| A4 | [artifact] docs/perf/group-io.md at journal:4d38a56: present-tense mechanism, bound, before/after table at 1K/10K/100K/1M (random, ascending, resident), 1M/1K = 1.85 |
| CI | GitHub run on quest/detent/journal-group-io-sweep at journal:4d38a56: pending push |

## Result

<When ready for review: what changed, its measured effect with numbers and the jobs behind them,
and what it leaves open. Results and how it works go in the docs of the repository it changed
(docs/perf/, docs/mechanisms/, a library's own); name them here. `campaign map` shows this.>

## Rulings I made

1. Mechanism: batched sweep, not self-unregistering Ranges. group_io sweeps the range table only
   when its size reaches `sweep_at_`, then sets `sweep_at_ = max(2 * size after sweep, 64)`.
   Correct by inspection: the sweep's predicate is `weak_ptr::expired()`, which never holds for a
   live range and never stops holding for a dead one, so a live range is never dropped and every
   dead one goes at the next sweep. Bound: the table never exceeds `sweep_at_`, i.e. twice the
   live ids at the last sweep (or 64). Cost: a sweep of S entries leaves L and the next comes after
   at least `sweep_at_ - L >= sweep_at_ / 2` inserts, so at most 2 entry visits per call, plus the
   O(log ids) map lookup. A Range unregistering itself would need the Host alive and a lock
   around `ranges_` from whatever thread drops the last view (views outlive Stores and their
   scans run off the loop): more state for no asymptotic gain.
2. Views of a resident group share one Range; the table stays a multimap. A new view of a resident
   group reuses a live Range (publish writes the same values to each); a view of a closed or unborn
   group shares only an unpublished Range (incarnation 0) and otherwise gets its own, so it never
   lists a closed incarnation's artifacts (base behavior, review F2, lead's ruling), while a view held
   across the close keeps its Range and publish still reaches both on reopen. group_io erases the
   group's own dead entries as it walks them, so a group's entries stay at its live ranges plus the
   ones a commit has not yet seen die; with sharing, a group opened N times between sweeps costs one
   Range, not N. Residual difference, unchanged from journal:965cd55: between a resident group's
   update and its commit, a new view reuses the older view's committed values where base published
   the in-memory entry to the new Range; no in-tree reader scans in that window.
3. The bench sizes each run to at least 2N calls when they fit in 10 s, so the amortized sweep
   (one per N calls in the bench's steady state) falls inside every timed run; before, a 0.3 s run
   at 1M ids (about 250K calls) could miss it and report a median that hides the sweep. The before
   arm runs this same bench source against ea397ab's include/ (`--ref base`), so both arms share
   the protocol; at 10K the before arm now runs 20K calls per run instead of about 2K (its cost per
   call is uniform, so the median is comparable to job 261118-072911-17102d).
