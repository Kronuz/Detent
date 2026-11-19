# consensus-worker-release: journal

State: working
Quest: .campaign/detent/quests/consensus-worker-release.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

consensus:d5059b6 (first cut, not yet compiled): repin journal be8222c + completion d8b538c;
Worker::close_store at drain (table close when frontier.sequence == base_sequence, else release);
Driver settles the host after erase; checkpoint_exceeds_pool in reserve_checkpoint;
driver_close_test reopen_rows (A1), closed_bytes (A2), close_crash (A3), pool_bound (A5).
Local build (quiet light, build-dev/) queued behind load.

## Next

1. Build at the new pin (A6); fix compile errors; run the new cases locally.
2. Mutants: no store_.close (A1/A2), no host_.settle (A2 parked), no pool throw (A5).
3. Jobs: suite (A6), A1 at main + branch, A2 before/after, A3 sweep, A5, TSan + CI (A4).

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

1. Clean close needs the Store's own precondition (journal Journal::close): the last record is a
   checkpoint covering every batch. The Worker cannot write an application checkpoint itself, so a
   row with records after its checkpoint (or none) is released, not closed: its parked entry stays
   in journal (journal's reopen_heap: ~248 B per released group). A2's "returns to pre-open"
   is measured on rows checkpointed before close; released rows are reported.
2. A5's error is raised by the checkpoint call (Worker::reserve_checkpoint), the first point the
   application cap is known (it is the call's argument); checking at configure would refuse
   Workers that never checkpoint (additive rule). The bound is journal's shared peak (two
   artifacts, header + payload each, 2 entries), exact for host groups; an owned Store's peak adds
   its data file header and manifest, which journal does not expose, so there the check is a floor.
3. A failed Store::close fences the Store (any exception), which then releases as a fenced row.
