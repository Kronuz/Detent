# coordinator-idle-cause: Name the sender of the coordinator fixture's stray bytes

Risk: normal. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status coordinator-idle-cause`.

## Outcome

The bytes the delivery client sends on a connection the coordinator fixture expects to see close are named by their sender and path, and either the client stops sending them or the fixture accepts them by name. Starts at the first `STRAY` line in any CI log (D89); until then it stays blocked.

## Acceptance criteria

- A1 The sender and why, from the STRAY line's bytes and the client code: the journal's row and a reproduction that forces it (an interleaving or a delay injection), failing before the fix: jobs, `--fails-without`
- A2 The coordinator test passes 200 repeats under ASan+UBSan at the fix: one light job

## Contracts

None.

## Decisions

D54, D55, D89.

## May land early

Nothing.
