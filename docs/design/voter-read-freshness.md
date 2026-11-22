> Status: DRAFT (2026-11-21), for the design review of voter-read-freshness A1 (PLAN.md D101, D144).

# Voter read freshness

A voter answers history and sessions only from state its leader confirmed current for that request.

- Author: [Germán Méndez Bravo](mailto:german.mb@gmail.com). Created 2026-11-21.
- Base: `Detent:b56e58e4` (main), `consensus:e35356a`.
- Inputs: managed-failover-isolated-leader's cause, deterministic test, forced probe and "Discovery bound" section (`Detent:b16a3ec9`); its client rotation (`Detent:3f8c031e`) is not used.

## Abstract

Any voter answers a version 2 (W5) session Open and Next and the W8 history routes (`/v2/history/layouts`, `/v2/history/file`) from its own applied state, with no quorum read (`service.cc` `frames_request`, the owner's `frames_sessions->open` and `answer`). A voter cut off from the majority keeps answering: an agent opened there gets idle answers forever, failing failover gate 2 (`docs/MANAGED_FAILOVER_ARCHITECTURE.md`), and a relay starting there, or holding that voter's seal, adopts a frozen mirror (namespace-s8-wiring D11). The readiness gate of D101 (`serving()`: a leader, or a follower that heard its leader within `election_max`) narrows this to about 2 s but cannot close it, and its stall heuristics leave holes (that review's D12, D13).

The fix is a ReadIndex round, as in the Raft dissertation (section 6.4): before answering, a voter asks its leader for the commit index; the leader confirms its leadership with a quorum, names the index, and the voter answers once it has applied it. Consensus has the leader half today (`Read`, confirmed by quorum probes, no leases); this design adds the follower half to `Kronuz/consensus`, then routes v2 Open, Next and layouts through the read path the version 1 sessions already use. A voter that cannot confirm refuses with an error both clients already rotate on. Expected cost is one round, about two network round trips, per owner pass that has such requests, shared by all of them; A3 measures it.

## Why not the obvious things

1. **Tighten the readiness gate.** Any timer on the follower side (leader contact, absence rule) is a lease without the clock bound a lease needs. With five voters, a deposed leader and one follower in a minority keep exchanging heartbeats until CheckQuorum (the leader steps down after `election_max` without a quorum), so the follower's contact is fresh while the majority has moved on.
2. **Rotate at the client** (`Detent:3f8c031e`). It bounds an agent at one refresh interval (30 s default) but leaves relays and every other W5 reader exposed, and it makes "A fresh Open revalidates authority quorum" (`docs/USAGE.md:242`) a statement about a different endpoint. D144 ruled for the voter.
3. **Serve v2 at the leader only**, as version 1 does. It gives up read scaling across voters, which W5 exists for.

## The mechanism

```text
HTTP lane -> owner pass: batch B = {v2 Open, v2 Next, layouts admitted before this pass}
           -> node.read(id)                     one read per batch
 follower:   ReadIndexRequest{term, token} ----> leader: PendingRead (existing probe round)
             <---- ReadIndexResponse{term, token, index} once a quorum acknowledged a probe sent after it
 voter:      wait applied >= index (Node already does), answer every task in B
```

**Consensus (a quest in Kronuz/consensus first).** `Read` is accepted at a follower:

| Follower state | Result |
| --- | --- |
| No leader known, candidate, fenced, witness | `Reject NotLeader` at once |
| Leader known | Forward `ReadIndexRequest{term, token}`; `ReadReady{request, index}` once the matching response arrives and `applied >= index` |
| Leader changes, term moves, or no response within `election_max` | `Reject NotLeader` |

The leader treats a forwarded request as one more `PendingRead` under its existing rules (an entry of its own term committed, else `NotReady`; confirmation by a probe sent after admission; `Reject` on step-down), and answers with a message instead of `ReadReady`. Two messages join `Message`; the response carries the token, so a response queued during a stop matches only the read that asked it, whose confirmation happened after its admission, which keeps it linearizable. `limits.reads` bounds forwarded reads as it bounds local ones.

**Authority.** One read per owner pass covers every v2 Open, v2 Next and layouts task admitted before it (the `Tasks` group a read already completes); tasks arriving while it is in flight join the next one, so at most one round is in flight per voter for this class and latency is at most two rounds. A Next is confirmed once, at arrival: it then answers from applied state at or past the index, and a parked Next that ends idle (`204`) is linearizable at its arrival. Its staleness while parked is bounded by `wait_ms` because the next Next confirms again. Close and `/v2/history/file` need no round: Close answers no state, and a file's bytes are a function of its name, which a relay takes from a confirmed layouts answer. The `serving()` gate stays only as `/v1/health` `ready`.

**Refusals.** A voter that cannot confirm answers:

| Route | Answer | Client behaviour |
| --- | --- | --- |
| v2 Open, Next | `503 source_not_leader` (`read_failure`, as version 1 at a follower) | The agent rotates at once (`unavailable_reset`). A relay's Next throws `UpstreamUnavailable`: that sync fails and backs off, and the next one's layouts refusal skips the voter |
| `/v2/history/layouts` | `503 not_ready` (the contract) | `HttpUpstream` throws `UpstreamBehind`; the sync skips to the next source within the same sync (D9) |
| Deadline passed | `503 outcome_unknown` | Rotates (`Unavailable`) |

Version 1 sessions and member reads (`/v2/read`) keep answering only at the leader: the service checks the role before `node.read`, so the follower half of `Read` changes nothing for them.

## Interaction with D1 and D9

D1 (namespace-s8-wiring) makes a layouts answer a function of its seal, and a relay refuses any answer older than its mirror. A confirmed voter answers at its latest seal at `applied >= index`, and every seal a relay holds was answered before this request, so committed at or before the index: a confirmed answer is never older than the mirror. D1's refusal stays, for relays reading relays (which do not confirm) and as a check. D9's skip becomes the normal path past a cut-off voter: it answers `503`, the relay reads the next source in the same sync. D11's two cases close: at start, the cut-off voter refuses; at the relay's seal, its Next refuses instead of answering `Ahead` with no frames.

## Cost

| Request | Rounds | Expected added latency |
| --- | --- | --- |
| v2 Open | 1, shared per pass | 2 RTT + up to 2 owner passes (leader and voter) |
| v2 Next | 1 at arrival, shared per pass | same; nothing while parked |
| Relay sync (layouts, Next, files) | 2, often sharing one pass | 2 rounds; files 0 |
| At the leader | its probe round only | 1 RTT |

Agents Open once per refresh (30 s default) and park Next up to `subscription_wait` (30 s). These are expectations from the message flow, not measurements; A3 measures relay sync latency and voter CPU per history request, before and after, in strict jobs against an A/A band.

## Discovery bound

The "Discovery bound" section (`Detent:b16a3ec9`) changes: an idle cut-off member no longer costs 1.2 × `quorum_refresh` + 1 s. A cut-off member costs one refusal, at most `election_max` (2 s default) after the request, plus, for a Next parked when the cut happened, the rest of its `wait_ms`. A5 checks this with the forced probe (8 of 8 converge, each time logged) and the partition and failover system tests x50, plain and under TSan.

## Limits

1. **A minority leader stalls reads for up to `election_max`** before CheckQuorum steps it down and they are refused. Silent to the client until then, except as latency.
2. **A relay serving agents does not confirm.** Its answers are as fresh as its last successful sync; a relay whose upstream keeps refusing keeps serving its last mirror. Out of scope; USAGE already says relay Open does not establish quorum.
3. **A partitioned follower costs one forward and a 2 s wait per batch**, not per request, before it refuses.
4. **Health and the request gate diverge**: `/v1/health` may report `ready` while a round would fail. Nothing gates on health.
5. **A relay whose Next is refused after a confirmed layouts answer** (a cut between the two) loses one sync and its backoff before it skips the voter.

## Alternatives considered

**Lease reads at the follower.** Rejected: consensus has no leases by design (`consensus:README.md`), and limit 1 of the readiness gate applies.

**A read watermark on AppendRequest** (the leader names the newest quorum-confirmed probe). Rejected: a probe may be confirmed by acknowledgements older than the request, so its commit index can precede a write completed before the request.

## Status

Designed, not built. Needs a Kronuz/consensus quest for the follower half of `Read`, then restart-capacity and server-composition-adoption landed (they hold `service.cc`).

For review:

1. Whether files are right to skip the round (the quest's Outcome lists them).
2. Whether W5 Next for readers is gated the same way (namespace-s8-wiring ruling 1 left it ungated; D144 reads as yes, and this design gates it).
3. Two refusal names, `source_not_leader` on sessions and `not_ready` on layouts: unifying them needs `not_ready` added to the agent's rotating errors (`src/detent_delivery/client.cc`, outside this quest) or a contract change for layouts.
4. Whether D12 and D13 close: answers no longer depend on `leader_contact` or the absence rule.
