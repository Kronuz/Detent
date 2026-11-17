# consensus-quiescence: Consensus quiescence: idle groups cost no CPU, reopen without election (D97 Q4)

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status consensus-quiescence`.

## Outcome

Idle consensus groups cost bytes, not CPU (Track C D12, D97 Q4): a quiescent follower stops heartbeats, a closed or quiescent group reopens on a frame or a request without an election when its leader is unchanged, and failure detection runs once per node pair on the shared connection, raising `LeaderGone` for every group it hosts (`Wake`, `LeaderGone` in Core).

## Acceptance criteria

- A1 A quiescent group reopens on a frame or a request with no election and no term change when its leader is unchanged: tests in `simulation_test`
- A2 One failure detector per node pair raises `LeaderGone` for every group on that pair, and those groups elect within their timeout; a leader change while quiescent is never missed: tests, plus a mutant that drops LeaderGone for one group, killed
- A3 Safety: quiescence never lets two leaders commit in one term nor a stale leader serve a read: the simulation suite under random partitions and TSan
- A4 Idle CPU per group measured at 1M groups (wakes per second per host, CPU per idle group), against the per-group-heartbeat baseline: an exclusive job and a results doc
- A5 Closed-state hard-state bytes, fixed in docs before any code (first contract; Xapiand group-core A5 consumes them): current term and voted-for id; commit index plus the last log index and term at close; the membership configuration (voters, witness, learners) and its index; the incarnation (journal's opaque id plus incarnation naming), so a replaced replica's old bytes are refused; the leader id at close, which reopen-without-election needs; a version and a CRC32C over the record (REPLICATION.md G6 style). Applied position stays in strata's checkpoint bundle. Close only after a checkpoint covers the applied position, and a reopen writes its term record in the same turn as its first write. Tests: a reopen never votes twice in a term and needs no log scan; a stale-incarnation or corrupted record is refused; a crash at every step of close and reopen

## Contracts

Provides the closed-state hard-state bytes (fixed first, A5), `Wake` and `LeaderGone` in Core and the per-node-pair detector on the host transport; Xapiand orders after it (`--after detent:consensus-quiescence`).

## Decisions

D96, D97 (owner: Q2 Sink, Q3 one directory per host, Q4 quiescence quest, Q7 node identity). Xapiand cluster-release (Xapiand:15c3e05ac) orders after this quest by this id.

## May land early

Nothing: it changes Core's protocol.
