# async-drivers-design: journal

State: waiting on jobs 261109-145202-956fb1 261109-145203-34a395
Quest: .campaign/detent/quests/async-drivers-design.md (the lead's; never edit it)

## Doing

Measurements (A7) queued at Detent:87df68b (bench committed at Detent:0b4924a): 261109-145202-956fb1
(idle_cost, strict) and 261109-145203-34a395 (driver_cost, exclusive, after the first, same slot
`adb`). The design doc's full draft, with {PLACEHOLDERS} for the job numbers, is
`.scratch/async-drivers-draft.md` (untracked, worktree); dev runs (not evidence) gave the shape:
idle Worker::run_one ~2.7 us with 1.6 allocations, a 3-voter SnapshotChannel ~164 KB, 2-3 fds per
group, today's lone 1 ms loop ~21 ms CPU per second.

## Next

1. When both jobs pass (`campaign job log <id>`): fill the draft's placeholders from their output,
   move it to docs/design/async-drivers.md, commit it alone, rerun both jobs at that commit, send
   the A8 message to the Xapiand lead, fill Evidence, mark ready for review.

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1-A8 | pending |

## Result

Pending.

## Rulings I made

1. Library heads read and measured: origin/main of journal (e5f6ff1), consensus (ab05507), task
   (bdafa5f), completion (560c288); local `main` of journal and consensus is behind origin, so
   the design cites origin/main. Detent pins older journal (bfad97f) and consensus (cef08eb).
2. Bench lives in docs/design/async-drivers-bench/ (file set) and pins the libraries through
   `--ref`, passed to CMake as FETCHCONTENT_SOURCE_DIR_<NAME>.
3. Task frames are counted with TASK_FRAME_POOL=0 (task.hh:438) so the pool hides nothing; the
   create+destroy time is therefore malloc's, an upper bound on the pooled path.
