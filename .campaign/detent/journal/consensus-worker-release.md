# consensus-worker-release: journal

State: waiting on jobs 261119-140158-51b1ce 261119-135117-bf02e4 261119-135119-b6b882 261119-135121-52f24d 261119-135123-4b09cc 261119-135125-1a5534 261119-135126-88fd6c 261119-135138-89cbce 261119-135140-f9a18c 261119-135142-11af82 261119-135144-860ad8 261119-135146-52f61f 261119-135147-8ca554
Quest: .campaign/detent/quests/consensus-worker-release.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Jobs queued at consensus:4fe7df2 (pushed; CI runs on the branch). Local runs at 4fe7df2 all pass:
default driver_close_test (137 checks), closed_bytes 10,000 rows, close_crash 64 seeds.

## Next

1. When the jobs land: paste `campaign brief`'s table; check CI (linux-tsan is the full TSan suite).
2. Set "ready for review" once suite, criteria and mutants pass; durability review is the lead's.

## Blocked on

Nothing.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| A6 | gate suite 261119-140158-51b1ce (campaign verify, 40-min bound; the direct 50-min 135115-1d13cf cancelled as a duplicate) |
| A1 | 261119-135117-bf02e4 (branch); fails at main's headers: 261119-135119-b6b882; mutants 135138-89cbce (no table close), 135142-11af82 (no settle), 135146-52f61f (watch) |
| A2 | after 261119-135121-52f24d; before (main's headers) 261119-135123-4b09cc; mutant 135140-f9a18c |
| A3 | 261119-135125-1a5534 (200 seeds) |
| A5 | 261119-135126-88fd6c; mutant 135144-860ad8 |
| A4 | TSan 261119-135147-8ca554 (close, driver, shared, worker, wave, blocking tests); CI pending |

## Result

consensus:4fe7df2 (local numbers; jobs pending).
- Pin: journal be8222c, completion d8b538c. No consensus source change was needed for
  `sync_data_files`: everything builds at the new pin (the suite job is the evidence).
- worker.h `close_store`: a drained closing Worker closes its shared Store into the host's table
  when its last record is a checkpoint covering every batch; otherwise the Store's end releases it.
  A failed close fences (then releases). `checkpoint_exceeds_pool` from `reserve_checkpoint`.
- driver.h: `host_.settle(loop_)` after erasing a closed row (released rows parked at once);
  `watch()` waits past `max(sequence, retired)`: a table-reopened group numbers its records past
  the closed incarnation's retired floor, which the log no longer counts, so the old watch
  completed at once with group_closed and the loop spun (found by reopen_rows).
- Heap (closed_bytes, 10,000 rows, HeapIO so the disk's allocations never count): covered rows
  6,761 B open, 168 B after close before the Host's range-table sweep, 0.0 B after (second pass;
  the first pass keeps ~16 B/row of the Driver's unordered_map buckets). Released rows (a record
  after the checkpoint): 282.6 B per row after close (journal's parked entry). Before, at main's
  headers: job 135123-4b09cc.
- close_crash: 64 seeds, 6,642 power cuts, 6,648 table closes and 9,147 releases, 0 failures.
- Docs: docs/DRIVER.md close row; CHANGELOG 2026-11-19.
- Open: journal's memory disk `sync_directory` is linear in its names (test-only cost, why
  closed_bytes uses HeapIO); journal's range table keeps ~168 B per closed row until it doubles.

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
4. "Main's code" for A1/A2 at-main jobs is consensus main's worker.h and driver.h (5935efa) under
   this branch's test and pin (at_main.sh, with a stub of checkpoint_exceeds_pool so it builds):
   main's own pin cannot run a live-host recover at all.
5. A3 has no consensus mutant: the durability is journal's; the sweep checks the close path end to
   end (acknowledged prefix, nothing unattempted, a table close never replays covered records).
6. A4 locally is TSan on the tests that exercise the changed code; the full TSan suite is CI's
   linux-tsan (the global rule: sanitizer matrices are CI's).
7. Job scripts (mutant.sh, at_main.sh) live in ~/.scratch/detent/consensus-worker-release/.
