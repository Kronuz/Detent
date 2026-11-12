# s3b-followups: journal

State: ready for review
Quest: .campaign/detent/quests/s3b-followups.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Fixes and tests committed at 00377bb5. Pass job 261112-152830-3d7cba and the four mutant jobs are done
(all mutants killed); N5 probe done. Full suite 261112-154056-4f72f4 passed (exit 0, 857 s).

## Next

1. Lead review; merge main before landing.

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | pass job 261112-152830-3d7cba (exit 0: `test_testing_pack_scrub_faults` now has 6 resource cases and real EMFILE); mutant 261112-153139-d7786b killed (transient errno treated as damage) |
| A2 | pass job 261112-152830-3d7cba (`test_local_paged_segment_collection` variant 17: a missing pack of the selected root, the orphan is swept and the pass is deferred); mutant 261112-153143-a458f6 killed (ENOENT/EIO fail the pass) |
| A3 | N4 fixed, N5 dropped with a measurement. Pass job 261112-152830-3d7cba (`rebase_keeps_progress`, and `scrub-carry` inside `test_delivery_repair_scrub-worker`); mutants 261112-153147-8762a3 (rebase restarts) and 261112-153151-81a394 (coordinator restarts on a new root) killed. N5 probe 261112-153155-d38d71 |
| A4 | [artifact] `docs/mechanisms/trusted-restart.md`: first Limits bullet (second list) says the scrub withdraws only the agent's own verified bits and a reader process (PreparedReader, LazyPageAccess) keeps its own until it reopens on the repaired generation. d91078d3 |

## Result

N1: EMFILE, ENFILE and ENOMEM on a pack open, fstat or read no longer count as damage: `PackScrub::step` throws
and the pass stays at that pack. N3: a missing or unreadable (ENOENT, EIO) pack of a protected root takes the
conservative `.live` path, so the rest of the collection proceeds and the pass is deferred with backoff. N4:
`PackScrub::rebase` keeps the packs a running pass hashed clean when the pinned root changes
(`scrub-carry`, 4-pack store: a delta mid-pass left 1,138,972 bytes hashed; after it the pass hashed
1,709,027 bytes against a 2,752,016-byte root, a restart would hash all of it). N2: the doc names the reader process. N5 is dropped: the
probe counted 6 and 7 obsolete skips over a whole repair (heal, lease-close: 50 ms, a slice each ~1 ms
only while the store's selection is ahead of the pinned root, which ends when recovery starts and clears
`base->ready`) and 3 in scrub-worker, none elsewhere; each skip is a pace-limited no-op slice. Docs:
`docs/mechanisms/trusted-restart.md`.
Open: `tests/detent_delivery/mutants/s3b-k4-obsolete-report-kept.patch` is unchanged. `scrub-carry`
runs inside the registered `scrub-worker` mode (adds about 12 s); no CMake change was needed.

## Rulings I made

- N1: EMFILE, ENFILE, ENOMEM on a pack open, fstat or pread throw `std::system_error` from `PackScrub::step` and leave
  the pass at that pack (no report, no repair); the coordinator counts the throw as a scrub failure
  and the next tick resumes there. EINTR is not added (quest names three errnos). The directory open keeps its refusal.
- N3: ENOENT and EIO from a pack open or read during a protected root's walk take the proof-failure path (conservative
  `.live` marks, warning, deferred retry with backoff). Other errors still fail the pass.
- N4: `PackScrub::rebase` keeps packs the running pass hashed clean against the same segment and checksum; a
  damaged pack, a changed record and a finished pass are hashed again. The coordinator calls `rebase` when the pinned root changes.
- Mutant patches live in `tests/detent_testing/mutants/` (inside the file set) and run through the existing
  `tests/detent_delivery/repair_mutant.sh`; the older `s3b-k4-obsolete-report-kept.patch` is unchanged and still applies.
- Superseded jobs, not evidence: 261112-151503-6bde7b, 261112-151506-1a27b3, 261112-151509-3df73a, 261112-151512-209bec
  (cancelled: they ran before the variant-17 setup fix); 261112-152833-e6e9c2, 261112-152837-60b9a3,
  261112-152841-7de5cc, 261112-152846-ff0a5f, 261112-152852-df97b2, 261112-153026-51eb32 (no verdict, exit 3: parallel
  jobs configured the same build dir; resubmitted chained with `--env REPAIR_MUTANT_BUILD=.scratch/build`);
  261112-153106-905748 and its three chained successors (exit 127: my shell quoting of `--env`).
- A4 is an [artifact] row (the doc edit at d91078d3); `campaign status` shows it pending until the lead marks it.
