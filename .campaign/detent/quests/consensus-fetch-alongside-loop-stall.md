# consensus-fetch-alongside-loop-stall: groups fetch-alongside: two of a node's loops stall 7 s with no journal I/O, after the placement fix

Risk: normal (transport, a test). The lead writes this quest; contributors read it and never edit
it. Its machine state is in `state.json`: `campaign status consensus-fetch-alongside-loop-stall`.

## Outcome

`consensus_transport_groups_fetch-alongside-consensus` failed on macos-x86_64 in CI run
45372452058 at consensus:551a5e6, a branch that does not touch the transport and contains
consensus-fetch-alongside-flake's fix (the group on a loop no link uses). Node 2's loops 0 and 1
went 7,085 and 7,026 ms without turning while their journal I/O held them 0 ms; the host stalled
the process at most 2,600 ms; three fetches from node 2 ended with connection_aborted, node 1 lost
its term, and 13 of 16 fetches were exact. Node 2's links are on loop 3, and loop 0 holds the
acceptor and handshakes (fetch-alongside review F4). Find what holds a loop for about 7 s with no
journal I/O and more than the host's stall, fix the cause, and keep the test's checks as strict.
The log is in `.scratch/detent/ci-pipesim/run-45372452058.log` on the home.

## Acceptance criteria

- A1 A reproduction that shows a loop held beyond the host's stall with no journal I/O (a job),
  with the cause named: which work runs on loops 0 and 1, and why it takes seconds.
- A2 The fix: the reproduction passes; a `--fails-without` job at main; the test's bounds unchanged.
- A3 25 runs of the case beside 4 burners: none fail (strict jobs).
- A4 The suite passes and `ci:ci` is green at the landed commit.

## Contracts

None changed.

## Decisions

Lead, 2026-11-24: its own quest; seen once in CI after the placement fix landed (consensus:512e3ef).
Lands after xapiand:consensus-join-v2, which holds groups_test.cc for a fixture line.

## May land early

Not applicable.

## Tasks

None.
