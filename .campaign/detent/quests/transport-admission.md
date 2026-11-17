# transport-admission: Pre-authentication admission favors configured peers; transport-connection's notes

Risk: normal. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status transport-admission`.

## Outcome

The host's pre-authentication pool can't be churned into refusing the cluster's own peers: a dial from an address membership names is admitted ahead of unknown sources, and transport-connection's open review notes are closed.

## Acceptance criteria

- A1 N9 (transport-connection security review round 2): under the reviewer's churn probe (about 3,200 connects/s from one unknown address), dials from configured peer addresses still link; an unknown flood cannot evict a configured peer's pending handshake: a test, and a mutant without the priority killed
- A2 The round 2 notes N10-N13 and the journal's N5 test, N6 and N7 are fixed or each dropped with a reason in the journal: tests for the fixed ones
- A3 A keepalive whose timer fires late after a process stop goes out at once instead of taking the late-deadline extension (transport-connection journal: removing the extension cut slow_reader's stop failures from 16 of 40 to 1 of 40); the dead-peer deadline keeps its extension: a test with a placed stop, and a mutant restoring the extension killed

## Contracts

Internal to `consensus:transport`; no wire change.

## Decisions

Lead: N9 lands as a follow-up, since the flood needs both ends hit and is bounded by the handshake deadline; priority by membership needs transport-identity's membership view.

## May land early

Tests.
