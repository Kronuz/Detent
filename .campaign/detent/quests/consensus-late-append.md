# consensus-late-append: Consensus: accept late append responses that still match the log

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status consensus-late-append`.

## Outcome

A leader whose append round trips run past `rpc_timeout` still makes progress: a late append response that matches the follower's log (term, and an index the leader can account for) advances the follower's match index instead of being dropped as unmatched, and re-sending never discards progress a late response proves. Consensus then commits under a slow or loaded event loop instead of livelocking.

## Acceptance criteria

- A1 With N ms of CPU burned per executor cycle (the probe in consensus-fetch-lane'''s journal, D92), a three-voter cluster with rpc_timeout 40 ms commits at N = 5 ms within 2 s, where it commits nothing in 4 s today: a test, and a job showing it fails without the fix
- A2 A stale response (an older term, or an index the follower has since truncated) is still rejected and never advances match or commit: a test plus a mutant that accepts it, killed
- A3 The existing consensus suite and recovery tests pass unchanged, and TSan is clean: the suite and tsan checks
- A4 [artifact] `CHANGELOG.md`: "late"

## Contracts

No API or wire change: the Core'''s handling of append responses only.

## Decisions

D92 (lead): split from consensus-fetch-lane, whose CI exposed it; that quest runs its A2 with a slow-runner timing meanwhile.

## May land early

None.
