# multi-voter-idle-wake: Multi-voter detent-authority sleeps until work or deadline (D124)

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status multi-voter-idle-wake`.

## Outcome

D124: loop-idle-waits stops polling everywhere except a multi-voter detent-authority, where incoming peer frames wait in consensus `Peers` until the turn calls `admit()` and nothing wakes the loop (consensus pin a197efb). After this quest, the Detent pin is past consensus:0acfaff (`Worker::next_deadline()`), Peers wakes the owner loop when a frame arrives, and the multi-voter turn sleeps until a wake or its next deadline.

## Acceptance criteria

- A1 A three-voter test with idle peers counts owner-loop wakeups over 10 s at main and at the head; the head wakes only for deadlines and frames: jobs.
- A2 A test where a peer frame arrives during an idle wait is admitted within one turn, with a mutant that drops the Peers wake failing it (`--fails-without`): jobs.
- A3 The suite and CI pass, and the consensus pin bump carries its own changelog check: jobs.

## Notes

Filed 2026-11-19 (D124, from loop-idle-waits msg-103). Needs the consensus pin bump; if a consensus quest is needed for the Peers wake hook, file it in ../consensus and order this after it.
