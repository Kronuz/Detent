# reactor-abortlist-thread: journal

State: waiting on jobs 261119-170249-dacc88 261119-170251-fa0eb2 261119-170254-5ab021 261119-170306-fc93ed
Quest: .campaign/detent/quests/reactor-abortlist-thread.md (the lead's; never edit it)

## Doing

Jobs at reactor:fbad85f queued; CI on the pushed branch. Check the mutant job's log names an `AbortList::` race (not a build or fetch error).

## Next

1. When the four jobs and CI are green: confirm the mutant log, fill Evidence, State ready to land.

## Blocked on

Nothing. Lead ruled option 1 (test, task-side mutant, comment; no hop in reactor) and lowered
the quest to low risk with a recorded `--weaken`.

Dependency: the property rests on task's `final_awaiter` going home (`task.hh:612` at
task:18eb516: a continuation awaited from another scheduler is posted back to it). A task pin
that drops that breaks it; [A] under TSan catches it (261119-170251-fa0eb2).

Measured before the ruling: an instrumented copy (counter in `~SessionControl`) shows 100 of 100
untracks on the owning loop, 50 plain and 50 throwing sessions ending on server b's loop.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | 261119-170249-dacc88 TSan `[A]`; 261119-170251-fa0eb2 `--fails-without` at the task go-home mutant (passes when TSan reports a race naming `AbortList::`) |
| A2 | 261119-170254-5ab021 Release suite; 261119-170306-fc93ed TSan suite; CI on `quest/detent/reactor-abortlist-thread` |

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
