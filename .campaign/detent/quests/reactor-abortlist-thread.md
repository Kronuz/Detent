# reactor-abortlist-thread: reactor: a session ending on another loop untracks on the owning loop

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status reactor-abortlist-thread`.

## Outcome

A session that ends on another loop untracks itself from the AbortList off-thread, while `reactor.h:96-103` says the list is loop-thread only and unlocked (reactor-task-sessions journal:35, AbortList F1). After this quest untracking always runs on the owning loop, so the list is touched by one thread only.

## Acceptance criteria

- A1 A session ending on a second loop untracks on its owning loop: a TSan test that reports a race without the fix (--fails-without): jobs.
- A2 The suite, TSan and CI pass: jobs.

## Notes

Filed 2026-11-19 from the untracked-commitment audit (J5).
