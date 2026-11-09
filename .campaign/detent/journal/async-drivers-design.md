# async-drivers-design: journal

State: working
Quest: .campaign/detent/quests/async-drivers-design.md (the lead's; never edit it)

## Doing

Measurements (A7) as jobs at the bench commit (Detent:0b4924a); the design doc follows them in
one commit.

## Next

1. When the bench jobs pass: write docs/design/async-drivers.md from their output, commit it, rerun
   the jobs at the doc commit, send the A8 message to the Xapiand lead.

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
