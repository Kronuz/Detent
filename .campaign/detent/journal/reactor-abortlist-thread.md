# reactor-abortlist-thread: journal

State: waiting on jobs 261119-171143-723158 261119-171145-ddad3b
Quest: .campaign/detent/quests/reactor-abortlist-thread.md (the lead's; never edit it)

## Doing

At reactor:fbad85f: dacc88, 5ab021, fc93ed passed. The first mutant run (fa0eb2) failed as evidence:
its wrapper grepped for `AbortList::`, but at -O1 `remove` and `each` are inlined, so the race
TSan reported did not name the class. Its report: a read of 8 bytes at Reactor+8 (`aborts_` is
Reactor's first member, so `AbortList::cursor_`) in `run_session` (.resume) `reactor.h:467`, the
frame's end where `~SessionControl` untracks, on b's thread T1 (resumed by
`asio_park::drop`), racing the previous write by a's thread T2 in `abort_all()`'s walk lambda
(`on_loop_sync`). That is the untrack racing the walk. Resubmitted as the plain A1 command at
the mutant with `TSAN_OPTIONS=halt_on_error=1:exitcode=66`: 261119-171143-723158. c578c1 was
skipped because its `--after` held a captured message line ("kept 261119-165221-4a441a: ...")
instead of a job id; fc93ed replaced it. Suite check: 261119-171145-ddad3b (`campaign verify`).

## Next

1. When 723158, ddad3b and CI are green: confirm 723158 exits 66 on the same race, State ready to land.

## Blocked on

Nothing. Lead ruled option 1 (test, task-side mutant, comment; no hop in reactor) and lowered
the quest to low risk with a recorded `--weaken`.

Dependency: the property rests on task's `final_awaiter` going home (`task.hh:612` at
task:18eb516: a continuation awaited from another scheduler is posted back to it). A task pin
that drops that breaks it; [A] under TSan catches it (261119-171143-723158).

Measured before the ruling: an instrumented copy (counter in `~SessionControl`) shows 100 of 100
untracks on the owning loop, 50 plain and 50 throwing sessions ending on server b's loop.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | 261119-170249-dacc88 TSan `[A]` passed; 261119-171143-723158 `--fails-without`, the same command at the task go-home mutant (TSan exitcode 66 on the race) |
| A2 | 261119-170254-5ab021 Release suite passed; 261119-170306-fc93ed TSan suite passed; 261119-171145-ddad3b suite check; CI on `quest/detent/reactor-abortlist-thread` |

## Result

- `test/session_test.cc` `[A]` `cross_loop_untrack()`: 32 of server a's sessions park on b's
  loop; a's `stop()` walks a's abort list, each `abort()` ending a session on b's loop while
  the walk runs. `reactor_session_test untrack` runs only this case.
- `reactor.h` `run_session` comment: why the control is untracked on its own loop.
- Left open: nothing in reactor; the invariant rests on task's documented resume-home contract.

## Rulings I made

- The mutant is in the fetched task (`perl` on `_deps/task-src/task.hh` in the job's build), not
  in reactor: no reactor-side change reintroduces the off-loop untrack.
- No CHANGELOG entry: no behavior changed (a test and a comment).
