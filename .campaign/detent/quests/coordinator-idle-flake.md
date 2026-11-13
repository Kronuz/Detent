# coordinator-idle-flake: Flaky retained-idle check in test_delivery_coordinator under ASan

Risk: normal. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status coordinator-idle-flake`.

## Outcome

`test_delivery_coordinator`'s two fixture close waits (retained idle, session close) read until the peer's close reaches the stream instead of trusting one `read_some` after `poll`, and fail exactly where main failed, printing any bytes that arrived before the close (`STRAY <checkpoint> count= hex= text=`). The flake (CI runs 43002602154, 43332392043, 43332885078) did not recur in six CI attempts or 620 local runs, so its sender is named by coordinator-idle-cause at the first STRAY line (D89).

## Acceptance criteria

- A1 `peer_closed` counts a byte that precedes the close and reports the close after it: test_delivery_peer_close passes, and with `PEER_CLOSE_SINGLE_READ` it fails: jobs, `--fails-without`
- A2 The coordinator test passes 200 repeats under ASan+UBSan at the diagnostic: one light job
- A3 [decision] The cause moves to coordinator-idle-cause: D89

## Contracts

None.

## Decisions

D54, D55.

## May land early

Nothing.
