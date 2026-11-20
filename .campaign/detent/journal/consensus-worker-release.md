# consensus-worker-release: journal

State: waiting on jobs 261119-191100-549567 261119-191103-5a8902 261119-191106-27f3ae 261119-191108-fcbabe 261119-191111-411633 261119-191113-2b1775 261119-191116-d053b8 261119-191119-509297 261119-191121-9e2d59 261119-191124-9c1a6d 261119-191127-8e0f0f 261119-191129-3f9672 261119-191132-a7d1f4 261119-191135-0d8700 261119-191137-b98c7e 261119-191140-4fdf57 261119-191143-2db797 261119-191145-91f9c5 261119-191147-b78d00 261119-191150-03bd0f 261119-191152-8abf98
Quest: .campaign/detent/quests/consensus-worker-release.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Round 3 at consensus:08cdf68 (pushed, CI running): durability review at 5de6c78 CHANGES, F1
(in_doubt_ dies with the Driver). journal repinned to its main journal:9e11713 (past
journal:8c92ed9); in_doubt_ and Worker::close_failed() deleted; create/recover map the Host's
hold EIO to close_in_doubt; second_driver (A7) and entry_throw (N3) added; N2 wording fixed.
Local: default cases 164 checks, 0 failures; close_crash 8 seeds clean.

## Next

1. When the jobs land: paste `campaign brief`'s table; read the reviewer's probes
   261119-184141-7487c2 and 184142-2f5a77; check CI at 08cdf68.
2. Ask the lead to accept durability with the suite 261119-191152-8abf98.

## Blocked on

Nothing.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| A1 | at 08cdf68: reopen_rows 191100-549567, failed_close 191103-5a8902; fails without: main's headers 191106-27f3ae, mutants 191108-fcbabe (table), 191111-411633 (settle), 191113-2b1775 (watch), 191116-d053b8 (EIO not mapped) |
| A2 | at 08cdf68: 191127-8e0f0f; fails without: 191129-3f9672 (main's headers), 191132-a7d1f4 (no table close) |
| A3 | at 08cdf68: 191135-0d8700 (200 seeds); fails without: 191137-b98c7e (loop throw), 191140-4fdf57 (entry drops the loop's throw, N3) |
| A5 | at 08cdf68: 191143-2db797; fails without: 191145-91f9c5 |
| A6 | suite at 08cdf68 (journal 9e11713) 261119-191152-8abf98 |
| A7 | at 08cdf68: second_driver 191119-509297; fails without: at 5de6c78's headers and pin 191121-9e2d59, mutant 191124-9c1a6d |
| A4 | TSan 261119-203855-8c3501 (rerun of b78d00, which failed on a quiet hold-name collision), ASan+UBSan 261119-191150-03bd0f (macOS, no LSan), suite 261119-191152-8abf98; CI green at 18f55c6 including linux-asan-ubsan (the LSan check) |

Review findings: F1 (malformed jobs: `'--quest ...'` as one token, zsh does not split an unquoted
variable) resubmitted from a POSIX sh script, ~/.scratch/detent/consensus-worker-release/submit.sh;
`campaign job list --quest` shows them tagged. F2 fixed at e74fccf (lead's ruling: refuse in
consensus); the reviewer's probe 261119-140902-4049e0 failed at 4fe7df2 (sync 1: "the turn log
lost this group", acknowledged `a` lost), and is now the case failed_close. F3 folded into F2
(nofence mutant on failed_close). F4, F5 nits: unchanged.

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
- consensus:e74fccf (F2): a failed Store::close sets the Worker's close_failed(); the Driver's
  close_step keeps that id in `in_doubt_`, and create/recover of it complete with
  `close_in_doubt` until the host restarts (a new Driver). failed_close: commit's directory
  sync fails after the shard replace, recover and create refused, the refused row closes, a
  later sync and a power cut; the restarted host holds the group closed and recovers `a`.
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
8. F2's refusal lives in the Driver (`in_doubt_`), so it lasts the Driver's life, which is the
   Host's on its loop; a second Driver built over the same live Host would not know the id.
   Journal keeps no per-group trace of a failed close (the lead's follow-up). Any exception from
   Store::close marks the id in doubt, precondition failures included (close_store's frontier
   check makes those unreachable): refusing more is safe, a restart clears it.
9. The refusal is at create/recover (the calls that open the group's storage), not at the plain
   `open`, so the row can still be opened and closed and the error reaches the caller as a call
   result, as every other Driver error does.
10. The CI leak (66 checkpoint call frames) is the crash model meeting a Driver gap. close_crash's
    cut throws PowerLoss, which is not a std::exception: it passes step()'s catch (probe: all 66
    escapes from step on an open, not closing, group; none from close or host settle) and ends
    Driver::run. The loop then left its callers suspended forever. A Driver defect, not a test
    one: DRIVER.md promises every call completes. loop() now runs shutdown() on a throw, then
    rethrows. close_crash checks every call it started completes; without the catch it fails at
    seed 1 cut 53 (local), mutant job 170649-5a5b1e.
11. Round 3 (D128): the in-doubt mark is journal's (Host's GroupEntry::in_doubt, refused by
    Host::hold with EIO). The Driver names it close_in_doubt only when the EIO leaves the Store
    holding nothing (`!host_.holding(id)`): the hold is taken first and refuses before any IO,
    while an EIO from later IO leaves the hold held, so a real disk EIO keeps its own error.
12. Pin journal:9e11713 (its main), not 8c92ed9: A6 asks for main. The blocking entry passes its
    scheduler to TurnDriver::Hold, as journal::run does there.
13. The nofence mutant is dropped: journal fences a failed close itself now (journal:a64c618), so
    the Worker's fence is a second guard and its mutant would survive.
14. Test Loop: the reviewer's second_driver gives the Driver its own stop (driver_stop);
    Loop::shutdown() requests both, the turn driver's first, which shutdown_ends and
    shutdown_idles now call.
