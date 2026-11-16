# worker-pump-removal: async-drivers 7: delete the Worker pump API and today's Peers

Risk: normal. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status worker-pump-removal`.

## Outcome

consensus's old pump API leaves its public surface: `run_one`, `take_io_operation`, `take_actions`, `try_submit`, `try_submit_retained`, `storage_operation_failed` and today's `Peers` are deleted, and Detent pins the result.

## Acceptance criteria

- A1 Those names leave the public API: a grep job over consensus and Detent finds no caller, and the suites pass
- A2 Detent's pin moves to the removal commit and its native suite passes: a Detent native job at the new pin

## Contracts

Removes the Worker pump and Peers.

## Decisions

D96, D97 (owner: Q2 Sink, Q3 one directory per host, Q4 quiescence quest, Q7 node identity). Xapiand cluster-release (Xapiand:15c3e05ac) orders after this quest by this id.

## May land early

Nothing.
