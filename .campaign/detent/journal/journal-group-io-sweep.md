# journal-group-io-sweep: journal

State: waiting on jobs 261118-123519-0b53cd 261118-123533-1944ae 261118-123541-c1f81b 261118-123558-25b19c
Quest: .campaign/detent/quests/journal-group-io-sweep.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

journal:965cd55 has the fix and test; journal:dbbbf1a the bench's run sizing (bench only, include/
as at ea397ab). Jobs queued: suite 261118-123519-0b53cd (A1); mutants 261118-123533-1944ae (never
sweeps, refs/mutants/journal-group-io-sweep/never = journal:5522996) and 261118-123541-c1f81b (sweeps
a live range, refs/mutants/journal-group-io-sweep/live = journal:6be5c51), both after the suite
(A2); exclusive bench A/B 261118-123558-25b19c, before built from ea397ab's include/ via --ref base,
after from the head, same bench source (A3).

## Next

1. When 261118-123558-25b19c ends: after table into docs/perf/group-io.md (A4), push, CI.

## Blocked on

Nothing.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| <A1> | <from `campaign brief`> |

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
2. One range per group (`std::map`, was `std::multimap`). With batched sweeps a multimap keeps
   every dead view of a group until the next sweep, and `publish` walks them all: a group opened
   N times between sweeps would cost O(N) per commit and per group_io (the bench's "resident"
   case). Sharing one Range among a group's live views is behavior-identical (publish writes the
   same values to each) and keeps `publish` at one O(log ids) find.
3. The bench sizes each run to at least 2N calls when they fit in 10 s, so the amortized sweep
   (one per N calls in the bench's steady state) falls inside every timed run; before, a 0.3 s run
   at 1M ids (about 250K calls) could miss it and report a median that hides the sweep. The before
   arm runs this same bench source against ea397ab's include/ (`--ref base`), so both arms share
   the protocol; at 10K the before arm now runs 20K calls per run instead of about 2K (its cost per
   call is uniform, so the median is comparable to job 261118-072911-17102d).
