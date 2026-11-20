# reactor-abortlist-thread: journal

State: waiting on jobs 261119-170249-dacc88 261119-170251-fa0eb2 261119-170254-5ab021 261119-170306-fc93ed
Quest: .campaign/detent/quests/reactor-abortlist-thread.md (the lead's; never edit it)

## Doing

Jobs at reactor:fbad85f queued; CI on the pushed branch. Blocked on the lead's ruling below.

## Next

1. Read the four jobs and CI; fill Evidence; if the lead accepts the reading below, ready for review.

## Blocked on

Lead: the quest's premise does not hold at reactor:1dd5f32. A session that ends on another loop
already untracks on its owning loop: task's awaiter goes home (`task.hh:612` at task:18eb516,
`final_awaiter` posts the continuation to the scheduler that awaited), so `run_session`'s tail,
and the `SessionControl` destructor that untracks, run on the session's own loop. Measured: an
instrumented copy (`~/.scratch/detent/reactor-abortlist-thread/src`, counter in
`~SessionControl`) shows 100 of 100 untracks on the owning loop, 50 plain and 50 throwing
sessions, all ending on server b's loop. There is no fix to make, so "fails without the fix"
has no fix to remove. Options:
1. Accept as is (recommended): the [A] TSan test as the guard, its `--fails-without` run at a
   task mutant with the go-home removed (the only change that makes the untrack leave the
   loop), and the comment at `run_session` naming the mechanism. No reactor code change.
2. Add an explicit hop home in `run_session`: redundant with task's contract, and no mutant
   of it dies while task goes home.
3. Close the quest as void.

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
