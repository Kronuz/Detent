> Status: DRAFT (2026-11-21), round 2 of the design review of voter-read-freshness A1 (PLAN.md D101, D144), folding in findings V1-V9 of `.campaign/detent/reviews/voter-read-freshness-design.md` (`Detent:04dd6fbb`).

# Voter read freshness

A voter answers history and sessions only from state its leader confirmed current for that request.

- Author: [Germán Méndez Bravo](mailto:german.mb@gmail.com). Created 2026-11-21.
- Base: `Detent:b56e58e4` (main), `consensus:e35356a`.
- Inputs: managed-failover-isolated-leader's cause, deterministic test, forced probe and "Discovery bound" section (`Detent:b16a3ec9`); its client rotation (`Detent:3f8c031e`) is not used.

## Abstract

Any voter answers a version 2 (W5) session Open and Next and the W8 history routes (`/v2/history/layouts`, `/v2/history/file`) from its own applied state, with no quorum read (`service.cc` `frames_request`, the owner's `frames_sessions->open` and `answer`). A voter cut off from the majority keeps answering: an agent opened there gets idle answers forever, failing failover gate 2 (`docs/MANAGED_FAILOVER_ARCHITECTURE.md`), and a relay starting there, or holding that voter's seal, adopts a frozen mirror (namespace-s8-wiring D11). D101's readiness gate (`serving()`: a leader, or a follower that heard its leader within `election_max`) narrows this to about 2 s; it cannot close it.

The fix is a ReadIndex round, as in the Raft dissertation (section 6.4): before answering, a voter asks its leader for the commit index; the leader confirms its leadership with a quorum, names the index, and the voter answers once it has applied it. Consensus has the leader half today (`Read`, confirmed by quorum probes, no leases); the campaign quest consensus-follower-read adds the follower half to `Kronuz/consensus`, and this quest waits for it, then routes v2 Open, Next and layouts through the read path the version 1 sessions already use. A voter that cannot confirm refuses with an error each client already rotates or skips on. The expected cost is one round, about two network round trips, per owner pass, shared by its requests.

## Why not the obvious things

1. **Tighten the readiness gate.** A follower-side timer is a lease without a lease's clock bound: with five voters, a deposed leader and one follower in a minority exchange heartbeats until CheckQuorum steps the leader down, so the contact looks fresh while the majority has moved on.
2. **Rotate at the client** (`Detent:3f8c031e`). It bounds an agent at one refresh interval (30 s default) but leaves relays and every other W5 reader exposed, and it makes "A fresh Open revalidates authority quorum" (`docs/USAGE.md:242`) a statement about a different endpoint. D144 ruled for the voter.
3. **Serve v2 at the leader only**, as version 1 does. It gives up read scaling across voters, which W5 exists for.

## The mechanism

```text
HTTP lane -> owner pass: batch B = {v2 Open, v2 Next, layouts admitted before this pass}
           -> node.read(id)                     one read per batch
 follower:   ReadIndexRequest{term, start, token} ----> leader: PendingRead (existing probe round)
             <---- ReadIndexResponse{term, start, token, index | refusal}
 voter:      applied >= index within election_max of the send, then answer every task in B
```

**Consensus (quest consensus-follower-read, before this one).** `Read` is accepted at a follower:

| Follower state | Result |
| --- | --- |
| No leader known, candidate, fenced, witness | `Reject NotLeader` at once |
| Leader known | Forward `ReadIndexRequest{term, start, token}`; `ReadReady{request, index}` once the matching response arrives and `applied >= index` |
| The leader refuses | The response's reason, at once: `NotReady` (no entry of its term committed yet, after every election), `Busy` (`limits.reads`), `NotLeader` (step-down, or the peer no longer leads) |
| Leader changes, term moves, or `applied >= index` not reached within `election_max` of the send | `Reject NotLeader` |

The leader treats a forwarded request as one more `PendingRead` under its existing rules (an entry of its own term committed; confirmation by a probe sent after admission; `Reject` on step-down) and answers every outcome, a refusal included, with a `ReadIndexResponse` instead of `ReadReady`; it refuses a request below its term. `limits.reads` bounds forwarded reads as it bounds local ones. Two messages join `Message` and the peer codec (`transport/codec.h`).

**Identity.** A forwarded read is `{start, token}`: `start` is a random 64-bit nonce drawn at each Core start, `token` the Core's counter, which restarts at 0. The follower accepts a response only from the peer it asked, at the request's term, with its own current `start` and a pending token, while its (term, leader) is unchanged; anything else is dropped. A follower that restarts in the same term while the leader still holds its earlier read therefore never matches that read's answer, whose index and probe may predate the new request. Within one start, a response queued during a stop matches only the read that asked it, whose confirmation followed its admission, so it stays linearizable.

**One deadline.** The follower's `election_max` runs from the send to `applied >= index`, not to the response alone, so a follower that learns the index but not the entries (a cut between them, a slow catch-up) refuses within the bound too. The cost: a follower more than `election_max` behind in apply under write load refuses, and the client moves to a current member, which is the better source.

**Rest.** A follower holding a forwarded read never rests and puts its deadline in `Worker::next_deadline()`; a resting leader wakes on a `ReadIndexRequest` (today only an admitted read keeps it awake, core.h:570). The quiescence quests in flight (consensus-peers-wake, consensus-quiesce-election-stall, multi-voter-idle-wake) must keep both.

**Authority.** One read per owner pass covers every v2 Open, v2 Next and layouts task admitted before it (the `Tasks` group a read already completes). A task admitted while a round is in flight never completes from that round, since that round's confirmation may precede the task: it joins the next one. So at most one round is in flight per voter for this class, and latency is at most two rounds. A Next is confirmed once, at arrival: it then answers from applied state at or past the index, and a parked Next that ends idle (`204`) is linearizable at its arrival. Its staleness while parked is bounded by `wait_ms` because the next Next confirms again. Close and `/v2/history/file` need no round (the lead's ruling): Close answers no state, a file answers only names in the voter's current layout, and the relay checks every byte against the layout's digest, so a cut-off voter answers the right bytes or a refusal, never stale state. Reader-facing W5 Next is gated as agents' is (the lead's ruling): it is one route, and the voter cannot tell the two apart. The `serving()` gate stays only as `/v1/health` `ready`.

**Refusals.** A voter that cannot confirm answers:

| Route | Answer | Client behaviour |
| --- | --- | --- |
| v2 Open, Next | `503 source_not_leader` (`read_failure`, as version 1 at a follower) | The agent rotates at once (`unavailable_reset`). A relay's Next throws `UpstreamUnavailable`: that sync fails and backs off, and the next one's layouts refusal skips the voter |
| `/v2/history/layouts` | `503 not_ready` (the contract) | `HttpUpstream` throws `UpstreamBehind`; the sync skips to the next source within the same sync (D9) |
| A leader's `NotReady` | `503 outcome_unknown` (`read_failure`) | Rotates (`Unavailable`) |
| A leader's `Busy` | `503 admission_full` (`read_failure`) | Keeps the route and backs off |
| Deadline passed | `503 outcome_unknown` | Rotates (`Unavailable`) |

Version 1 sessions and member reads (`/v2/read`) keep answering only at the leader: the service checks the role before `node.read`, so the follower half of `Read` changes nothing for them.

## Interaction with D1 and D9

D1 (namespace-s8-wiring) makes a layouts answer a function of its seal, and a relay refuses any answer older than its mirror. A confirmed voter answers at its latest seal at `applied >= index`, and every seal a relay holds was answered before this request, so committed at or before the index: a confirmed answer is never older than the mirror. D1's refusal stays for relays reading relays, which do not confirm. D9's skip becomes the normal path past a cut-off voter. D11's two cases close: at start, the cut-off voter refuses; at the relay's seal, its Next refuses instead of answering `Ahead` with no frames.

## Cost

| Request | Rounds | Expected added latency |
| --- | --- | --- |
| v2 Open | 1, shared per pass | 2 RTT + up to 2 owner passes (leader and voter) |
| v2 Next | 1 at arrival, shared per pass | same; nothing while parked |
| Relay sync (layouts, Next, files) | 2, often sharing one pass | 2 rounds; files 0 |
| At the leader | its probe round only | 1 RTT |

Agents Open once per refresh (30 s default) and park Next up to `subscription_wait` (30 s): at 10,000 agents that is about 333 Opens/s (MANAGED_FAILOVER_ARCHITECTURE item 7), most at followers. These are expectations from the message flow, not measurements. On one host RTT is near zero, so A3 measures the parts that do not scale with it, before and after, in strict jobs against an A/A band:

| Measure | Where | Under |
| --- | --- | --- |
| v2 Open latency, p50 and p99 | a follower and the leader | 333 Opens/s of agent load |
| v2 Next arrival-to-answer latency, p50 and p99 | a follower and the leader | the same load, with writes |
| Relay sync latency; voter CPU per history request | a follower | one relay at its interval |
| Leader CPU | the leader | the same agent load |
| Rounds per pass, tasks per round | each voter | counters in `/v1/health` |

Latency at a real RTT is then rounds × RTT plus the measured pass cost. The design goes back to review if any of these holds:

1. A follower's v2 Open p99 is more than 5 ms above main's at the same load.
2. Leader CPU is more than 10% above main's at the same load.
3. Rounds per pass exceed 1 at that load, which means batching does not hold.

## Discovery bound

The "Discovery bound" section (`Detent:b16a3ec9`) changes: an idle cut-off member no longer costs 1.2 × `quorum_refresh` + 1 s. A cut-off member costs one refusal, at most `election_max` (2 s default) after the request, plus, for a Next parked when the cut happened, the rest of its `wait_ms`. A5 checks this with the forced probe (8 of 8 converge, each time logged) and the partition and failover system tests x50, plain and under TSan.

## Limits

1. **A minority leader stalls reads for up to `election_max`** before CheckQuorum steps it down and they are refused. Silent to the client until then, except as latency.
2. **A relay serving agents does not confirm.** Its answers are as fresh as its last successful sync; a relay whose upstream keeps refusing keeps serving its last mirror. Out of scope; USAGE already says relay Open does not establish quorum.
3. **Health and the request gate diverge**: `/v1/health` may report `ready` while a round would fail. Nothing gates on health.
4. **A relay whose Next is refused after a confirmed layouts answer** (a cut between the two) loses one sync and its backoff before it skips the voter.

## Alternatives considered

**Lease reads at the follower.** Rejected: consensus has no leases by design (`consensus:README.md`), and limit 1 of the readiness gate applies.

**A read watermark on AppendRequest** (the leader names the newest quorum-confirmed probe). Rejected: a probe may be confirmed by acknowledgements older than the request, so its commit index can precede a write completed before the request.

## Status

Designed, not built. Waits for consensus-follower-read (the follower half of `Read`, ordered after the consensus quests holding `core.h`), then for restart-capacity and server-composition-adoption (they hold `service.cc`).

The lead ruled the first round's questions:

1. Files skip the round.
2. Reader-facing W5 Next is gated.
3. Both refusal names stay: `source_not_leader` on sessions, which the agent rotates on (an unknown `503` keeps its route), and `not_ready` on layouts, which the relay skips whatever the name.
4. D12 and D13 close for answers: none depends on `leader_contact`, `returned` or `trusting()`; only a matched response confers authority. They remain for `/v1/health` `ready`.

Tests this design owes:

1. Consensus: a follower restarts in the same term while the leader holds its forwarded read without quorum, forwards again with the same token, and the leader answers the old read; the new read must not complete from it (mutant: drop the `start` check). Each leader refusal reaches the follower at once; a response from another peer or term is ignored; a forwarded read at a rested group completes or refuses within `election_max`.
2. Authority: hold a round's response, admit a task, release it; the task waits for the next round (mutant: late tasks join the in-flight batch).

Notes from review, not changed here:

1. V6: `returned`, `trusting()` and the `leader_contact` stamps feed only `/v1/health` `ready`, where an idle voter's sleep past `election_min` would flap it. Options for the owner: keep it (Limit 3), reduce `ready` to `ready() && leader_known`, or cache the last round's result.
2. V8: the role check before `node.read` for version 1 and `/v2/read` is check-then-act; a node deposed in between forwards the read, which is still linearizable.
3. V9: the quest's Contracts name `503 not_ready` for every route; sessions answer `source_not_leader` (ruling 3). Limit 4 would go if the relay mapped a session `503` to `UpstreamBehind`; optional.
