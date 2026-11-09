# namespace-s1-s2: journal

State: waiting on jobs at Detent:8a0ba8b: T7 timing A/B 261109-053454-fc078b; positive 053515-0e6248, mutants 053515-ae762d..053525-28e6bf, attribute c 053526-dc9720, gates 053529-40e439 -b273be -9a3f10 -10fd2e, A9 arms 053530-15c25c..053550-5244c6 after the gates (all 261109-)
Quest: .campaign/detent/quests/namespace-s1-s2.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State, the first step under Next, and the Evidence table. State is one of: working,
waiting on jobs <ids>, ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Merged main (Detent:09472b7), pins taken (Detent:c2025c2: journal:e5f6ff1, consensus:ab05507; check_pins 0 mismatches, gate pins job 261108-212143-7ae4f5 done). Job scripts committed in docs/measurements/sync-groups-20261108/ (mutant.sh, build.sh, arm.sh, summarize.py). Any non-.campaign commit changes the job code key, so every row is rerun at the final head (the first gate jobs at c2025c2 were cancelled to free light slots; pins gate 261108-212143-7ae4f5 passed). Submitted: build slots s1s2-before (327ca58: 261108-213700-dc3b49) and s1s2-after (261108-212824-1e1d46); mutant positive 261108-212825-a55bba; mutants unsynced-descriptor 929a69, empty-payload-group ecd616, startup-before-barrier e47da1, closed-session 984fa5, phase-counts edd4b1 (each must FAIL; log shows 'MUTANT.. SURVIVED' if not; exit 125 = build error, not a kill); 10K smoke arms before (resubmitted; see below), after baa7ec (these test arm.sh itself; if they pass, queue 10K/100K x3 and 5M x1 with `--env ROUND=n`).

## Next

1. Mutant jobs (one per name in mutant.sh; each must fail), A6 harness job, A9 arms (10K/100K x3 before/after, 5M x1), A8 reverts.
2. Write docs/mechanisms/sync-groups.md and docs/perf/sync-groups.md from job results.
3. Rerun `campaign verify` and the evidence jobs at the final head; fill the Evidence table; State ready for review.

## Review findings (durability, Detent:b3fef50)

- D1 (blocking, defect): fixed at Detent:08440be. The startup barrier is one SyncFiles over storage.meta (reopened read-only: SyncOpen, Barrier, SyncClose phases in StartupOperation) plus the directory, then ready(). Counters: startup is now 1 file, 1 group, 1 directory sync, 1 flush on macOS (2 elsewhere). Tests: test_publication_power_cut gains `creation_cuts` (kill at every event of a fresh store after storage.meta exists, restart ExistingManaged, require storage.meta durable at ready, cut power at ready in four modes, require the next start to accept it); test_paged_store_startup and test_disk_store updated to the new barrier. Failing-without-fix job: mutant d1-revert 261108-220723-d9e709 (disk_store.cc as of b3fef50 with the new tests; passes iff T7 fails). Confirming probe at the fix: 261108-220732-5cd56d (baseline at b3fef50: 261108-215124-d07d1a).
- D2 (blocking, gap): fixed. mutant.sh configures with DETENT_BUILD_MANAGED_AGENT=ON, a mutant job passes iff its named tests all fail (exit 1 survived, 125 build error). Added rename-before-sync (Replace issued before the last window's SyncFiles) beside unsynced-descriptor for A4/A7's per-file-order revert, and startup-before-barrier now requires T7 itself (not only the startup test) to fail. Jobs: see State.
- N1 (restart-then-republish cycles sample every third event; readiness check only on the first pass), N2 (no fault injection over a multi-window paged publication), N3 (T7's macOS seam treats F_BARRIERFSYNC as a device flush), N4 (kill between Replace and its barrier lets local readers see the new generation before it is durable; unchanged from main): non-blocking, noted, no work. The new creation case checks durability at ready for every kill point, which narrows N1 for the startup barrier.

- Fix-round result: D1 confirmed both ways (probe d07d1a fails at b3fef50, 5cd56d passes at the fix). The positive run 23c2fd failed with 'unknown target': the slot s1s2-mutant kept a build directory configured by the first (AUTHORITY/RELAY/MANAGED_AGENT off) run, and mutant.sh configured only when no cache existed. mutant.sh now reconfigures every run and has a `targets` mode; job ac1932 checks the three test targets exist before the batch is requeued.

- Chain queued 2026-11-09 (all ids carry the 261109-0116 prefix except the gates and 5M): targets check (grep for `^test_local_(disk_store|paged_store_startup|publication_power_cut): phony$` == 3; the first version counted every target line and failed ac1932), then the positive run, the seven mutants (each passes iff killed), build-after, A9 arms interleaved b/a x3 at 10K and 100K, 5M one each (28 minutes), then `campaign verify` gates. A6's harness line is the 10K and later after arms (summarize.py --require-counters); A8 is closed-session (lifecycle, startup test) and phase-counts (accounting, T7). Earlier arms (baa7ec 10K after, d6e0a3 10K before) ran at older code and are superseded.

- 2026-11-09: the tsan gate 4c7c75 (Detent:4128dd7) failed in test_publication_power_cut after 413 s: "cycle at event 0 selected a generation that is neither the one before the publication nor the one it publishes" (line 552, window 2 restart-then-republish cycles); windows 1 and 2 and the new creation case had passed before it. The same T7 passed 7,186 cases at Detent:4c0eb36, so the regression comes from this round: the Tracker refactor (outside-model files skipped, Write on an untracked file ignored) or the startup SyncFiles over storage.meta. I cancelled the whole queued chain (stale at any fix) and ran a diagnostic positive (commit adds the cycle's source event, variant and the three generations to the message). Next: read the log, fix, requeue the chain.

- Attribution per the lead: attribute.sh a (head with disk_store.cc as of b3fef50) and b (head with the power-cut test as of b3fef50), both TSan Debug, one slot each. a fails => the test refactor is the cause; b fails => the restart SyncFiles.

- T7 cycle failure resolved (Detent:e32cdb8). Cause: an oracle gap exposed by event numbering, not a code ordering change. The D1 barrier adds three startup events (OpenInto, SyncFiles, CloseFile), so window 2's Replace completion moved to event 120 and the cycle sampler (every fifth boundary) picked it for the first time. A kill there leaves the rename visible, not durable; a power cut at the rerun's event 0 (before its barrier) correctly selects `g`, the generation before the first publication (the main loop accepts the same cut at event 120). The cycle oracle knew only the rerun's before/after (`n`, `o`). The sampler's `starts_with("replace ")` clause never matched (that note is not a boundary), so the Replace point was covered only by luck of index. Attribution: variant a (36d6c4) failed at the D1 creation case, as designed; variant b (f516c2) failed at the old test's line 442, `publication_counters.groups == run.groups`, because the old Tracker counted the startup SyncFiles as a publication group: accounting, not the invariant. Fix: before the rerun reports ready, the oracle also accepts what the original cut allowed; the sampler always includes the Replace-completion boundary. Fails without it: mutant cycle-oracle 025512-aa1268 (allowance removed, head code; passes iff T7 fails). Predates D1: attribute.sh c 025513-3f17fc (pre-D1 disk_store.cc, allowance removed; passes iff the Replace-completion cycle variant 2 selects g).
- Chain requeued at e32cdb8 (.scratch/queue.out): targets 025502-a25bfb, positive 025503-2ccb31, eight mutants (the seven plus cycle-oracle), build-after 025514-2eafd5, arms b/a x3 at 10K (025515-d3c885 b1, 025517-ec84d1 a1, 025518-a3f71a b2, 025519-135889 a2, 025520-319247 b3, 025521-9b3488 a3) and 100K (025522-f543d6 b1, 025523-7e61b3 a1, 025525-077e5b b2, 025526-33819c a2, 025527-a786bb b3, 025528-99a16c a3), 5M 025529-519b08 b, 025530-734fe1 a; gates native 025531-dd57e8, pins -8e2301, asan -6ba699, tsan -b2d410. Next: read results; write the two docs; rerun `campaign verify` at that final head (docs change the tree key, so the gates rerun there).

- D32 (lead, 2026-11-09): A12 moved to quest sync-groups-perf; docs/perf/sync-groups.md is not written here. A11 written at Detent:802a9df (docs/mechanisms/sync-groups.md: restart barrier over storage.meta, flush counts from T7 and the startup test, 5M 25 vs 5,775 from the 2026-11-07 runs, the power-cut model); the ledger's pointer updated. At e32cdb8 the positive 025503-2ccb31 and all four gates (native dd57e8, pins 8e2301, asan 6ba699, tsan b2d410) passed, confirming the oracle fix; the queued rest was cancelled as stale (d90f68, 3f17fc, d3c885 were running and left to finish). Resubmitted at 802a9df in the lead's order, the 14 arms `--after` the four gates (.scratch/queue.out).

- T7 timeout (lead msg, 2026-11-09): at 802a9df (code = e32cdb8) native 585460 and asan 56f3be timed out at 900 s in T7 with no output; tsan b97278 passed in 237 s. Earlier T7 times: native dd57e8 296 s, asan 6ba699 437 s, tsan b2d410 377 s, tsan 4c7c75 413 s (to its failure). Hypothesis: T7's run time follows the host's device-flush latency. Each of about 6,000 probe restarts (open_and_recover, reopens) ran an unseamed store, whose startup barrier issues a real F_FULLFSYNC (probe on this host at load 6.8/16: median 20.0 ms, max 21.9 ms, so at least 120 s of flushes when idle), and the recording seam forwarded each fsync and flush to the kernel. stdout is block-buffered into ctest's pipe, so a killed run shows nothing. Fix, Detent:8a0ba8b: the seam returns without a kernel sync, the probe stores get a skipping seam (`skip_syncs`), and T7 prints progress lines on stderr. Measurement: t7-timing.sh (interleaved old e32cdb8 / new head x2, flush latency before and after each run, `sample` top of stack at 120 s and at the 1200 s limit), job 261109-053454-fc078b. No timeout was raised. The stale 802a9df chain was cancelled and the whole chain resubmitted at 8a0ba8b.

## Blocked on

Nothing.

## Evidence

One row per criterion, with what its kind needs: a job id (it, or the same job rerun, passed at
the current code), a `path`, a decision id, or for a [manual] one who checked what. Name every job
you examined, failures included, and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| A1 | `docs/status/namespace-s1-s2.md` (completion:0490a5a, df6394a, 560c288; CI 41925122896) |
| A2 | `docs/status/namespace-s1-s2.md` (completion mutants run 41925127096) |
| A3 | pins gate 261109-053529-b273be |
| A4 | positive 261109-053515-0e6248; unsynced-descriptor 053518-443bd3, rename-before-sync 053519-e40466 (each passes iff killed) |
| A5 | positive 261109-053515-0e6248 (test_local_paged_store_startup); startup-before-barrier 053517-765657; d1-revert 053515-ae762d |
| A6 | native gate 261109-053529-40e439; harness: A9 after arms with summarize.py --require-counters (053532-53caa6 and later) |
| A7 | positive 261109-053515-0e6248; mutants unsynced-descriptor 053518-443bd3, rename-before-sync 053519-e40466, empty-payload-group 053521-acfd9c, startup-before-barrier 053517-765657, cycle-oracle 053525-28e6bf; attribute c 053526-dc9720 |
| A8 | positive 261109-053515-0e6248; reverts closed-session 053522-d7b627 (0cd1527), phase-counts 053523-64711f (522452a) |
| A9 | build before 261108-213700-dc3b49, after 261109-053527-aca6f4; 10K b/a 053530-15c25c 053532-53caa6 053533-964ef1 053535-2b8fb5 053536-dd8bad 053538-c75e5b; 100K b/a 053539-396912 053541-d218a2 053542-7d39c5 053544-15e690 053545-889cd2 053547-29b391; 5M b 053548-85185d, a 053550-5244c6 (all 261109-) |
| A10 | asan gate 261109-053529-9a3f10, tsan gate 261109-053529-10fd2e |
| A11 | `docs/mechanisms/sync-groups.md` |
| A13 | [manual] the lead names the completion commit (decision 5 open) |

## Result

<When ready for review: what changed, its measured effect with numbers and the jobs behind them,
and what it leaves open. Results and how it works go in the docs of the repository it changed
(docs/perf/, docs/mechanisms/, a library's own); name them here. `campaign map` shows this.>

## Rulings I made

0. A5's wording "the census issues no per-file sync" is met as: no `Sync` request; the one startup barrier is a one-file SyncFiles (storage.meta) plus the directory, per the lead's D1 ruling (option 1).

1. Job scripts live in docs/measurements/sync-groups-20261108/ (now in the quest's file set). They reuse the unmodified 2026-11-07 harness (docs/measurements/sync-groups-20261107/coldboot.py, SHA-256 518db64e...75a5) and run its 10K smoke inside each arm job so its staged-run precondition holds.
2. A9 design: one arm per exclusive job; builds are light jobs on named slots s1s2-before (Detent:327ca58) and s1s2-after; arm.sh checks the slot's built commit and that the slot's code equals the job's. Before/after arms interleaved b1 a1 b2 a2 b3 a3; the A/A band is the spread among the before arms (10K, 100K); 5M one before, one after.

<Decisions inside the quest's scope, so the reviewer can check them.>
