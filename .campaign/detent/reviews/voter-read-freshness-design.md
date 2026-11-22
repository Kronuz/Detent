# voter-read-freshness review: design

Commit: Detent:fa29b47d (round 2; round 1 was Detent:9bc009ce369a2e4a68c4c1f3ca62710ea07fb6de), against consensus:e35356a97d8fc849bd017ba718e625435f3ec10d
Verdict: APPROVE
Date: 2026-11-21

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

The approach is right: a follower ReadIndex in consensus, batched per owner pass, refusing with the
errors each client already rotates or skips on. Two text fixes before code: forwarded-read identity
must survive a follower restart (V1), and A3 must measure the class D144 adds (V2).

## Round 2 (Detent:fa29b47d)

A check that each blocking finding is closed, not a new round. Both are, so the verdict is APPROVE.

- V1 closed: forwarded reads are keyed `{start, token}`, where `start` is a random per-start nonce. A response counts only if it comes from the peer asked, at the request's term, with the current `start` and a pending token, while (term, leader) is unchanged. The restart test and the `start` mutant are listed under "Tests this design owes".
- V2 closed: A3 (quest :19 and design "Cost") measures follower and leader Open/Next p50/p99 at 333 Opens/s, leader CPU, and rounds and tasks per pass. Latency at a real RTT is rounds × RTT plus the pass cost. It goes back to review if Open p99 is more than 5 ms over main, leader CPU more than 10% over, or rounds per pass above 1.
- V3 folded in: every leader refusal answers with a reason (refusal table, `read_failure` mapping).
- V4 folded in: one `election_max` deadline runs from the send to `applied >= index`.
- V5 folded in: the "Authority" invariant, with its test and mutant.
- V6 kept as a note: options for the owner.
- V7 folded in: the "Rest" paragraph and the rested-group test.
- V8 kept as a note.
- V9 kept as a note: the rulings are recorded under "Status"; A1's Decisions row is the lead's to write.

## Evidence I relied on

None: the quest has no jobs (A1-A5 pending, `campaign status voter-read-freshness`). The design
is prose; I checked it against code at the commits above.

## What I checked myself

Code reading only, no probe jobs (nothing is built yet to probe). Sources:
consensus `include/consensus/core.h` (Read handler :983-1001, probe correlation :612-620 and
:1255-1257, `ready_reads` :1462-1469, tokens :637-639 and :1485, step-down :643-651, quiescence
:570), `types.h` (:150, :174, :176, :188, :247); Detent `service.cc` (`read_failure` :259-267,
`serving` gate :1859-1877, session reads :2617-2630, history gate :3145-3152, the read issue
:3155-3166, v2 frames tasks :2786-2860), `detent_delivery/client.cc:30-52`,
`coordinator.cc:32-34`, `detent_relay/http_upstream.cc:247-380`, `namespace_relay.cc:336-343`;
docs/status/namespace-s8.md:76-79, MANAGED_FAILOVER_ARCHITECTURE.md gates, USAGE.md:242, PLAN.md
D144, and the namespace-s8-wiring durability review (D1, D9, D11-D15), which is where the
design's D-numbers come from (dense-distribution's own D1 root check and D9 repair are untouched
by this design).

### The lead's questions

1. **Correctness.** Holds, with V1 as the one hole.
   - Leader change mid-round: the follower rejects on a new term or leader; accepting would also be
     safe (the old leader confirmed after admission), so rejecting is the conservative choice.
   - Stale leader: a probe counts only acknowledgements of a probe sent after admission
     (core.h:612-620, :1255-1257), so a minority leader never confirms; it steps down at CheckQuorum
     and rejects (core.h:650). Limit 1 states the stall.
   - Voter resumed after a stop with queued frames: a queued response matching a still-pending
     token was confirmed after that read's admission, so the answer is linearizable at the
     confirmation point; tasks the lanes queued during the stall wait for the next round. That
     invariant needs its own test (V5). A restart is not a stop: tokens restart at 0 (V1).
   - Term checks on the response: accept only from the peer the request went to, at the request's
     term, while the follower's (term, leader) is unchanged; a ReadIndexRequest below the leader's
     term is refused. The design implies this; the consensus quest's tests should state it (V3).
2. **Smallest change.** A consensus quest is needed. `Read` rejects at a follower
   (core.h:985), and `read_probe` is the leader's own correlation token on AppendRequest, echoed in
   AppendResponse (types.h:150, :174; core.h:1255-1257): a follower can neither start nor observe
   a probe round. The alternative without consensus changes, a voter asking the leader over HTTP
   for a `node.read` index, needs voter-to-voter HTTP addressing and credentials Detent does not
   have, and re-derives term and leader tracking the Core already owns. Two messages, their codec
   entries (`transport/codec.h`, not named in the design) and a follower `PendingRead` are close
   to minimal. The rejection of the AppendRequest watermark is sound: a probe received after the
   request may have been sent before it.
3. **Cost.** A3 as written cannot decide (V2): it measures relays only, while D144's class is
   agent Open and Next at followers, and on one host the 2 RTT term is near zero.
4. **The doc's four questions.**
   1. Files skip the round: yes. A file answers only names in the current layout
      (namespace-s8.md:76), the relay checks every byte against the layout's digest and asks the
      next voter on a 503 (http_upstream.cc:298-301); a cut-off voter can answer right bytes or
      a refusal, never stale state. Record it in the Decisions row, since the quest's Outcome
      lists files.
   2. Reader-facing W5 Next gated: yes. It is one route; the voter cannot tell an agent from a
      reader, and gate 1 includes an independent reader. Gating both keeps one shape.
   3. Two refusal names: keep them. Each consumer already reacts right: the agent rotates on
      `source_not_leader` (client.cc:44-45, coordinator.cc:32-34) and maps an unknown 503 to
      AdmissionFull, keeping the route (client.cc:50-51), so `not_ready` on sessions would break
      gate 2 without a client change; the relay maps any layouts 503 to UpstreamBehind regardless
      of the name (http_upstream.cc:255-256). If one name is wanted, `source_not_leader` on
      layouts is the cheaper direction (no client change, one contract line). Owner's call (V9).
   4. D12 and D13 close for answers: no answer depends on `leader_contact`, `returned` or
      `trusting()`; only a token-matched response confers authority. They stay true for
      `/v1/health` `ready` (Limit 4), which nothing in `src/` gates on. See V6 for removing the
      leftover.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| V1 | no (fixed at fa29b47d) | defect | A forwarded read is identified by `{term, token}`, and tokens are a per-Core counter from 0 (core.h:637-639, :1485). A follower that restarts in the same term while the leader holds its forwarded read (no quorum yet, up to `election_max`) can reuse that token; when the leader regains quorum it answers the old read, and the new read matches it. The old read's index is the commit at the old admission, and its probe may predate the new request: a stale answer. "A response queued during a stop matches only the read that asked it" holds for a stop, not a restart. | core.h:637-639, :1485; design "The mechanism" | Give forwarded reads an identity unique across a follower's restarts (a random per-start nonce carried and echoed, or the follower's start epoch beside the token), checked on the response. Test in the consensus quest: follower forwards, leader holds it without quorum, follower restarts and forwards again with the same token, leader regains quorum and answers the old read; the new read must not complete from it. Mutant: drop the nonce check (`--fails-without`). |
| V2 | no (fixed at fa29b47d) | gap | A3 measures relay sync latency and voter CPU only. D144's added cost is agent v2 Open and Next at followers (about 333 Opens/s at 10,000 agents on the 30 s refresh, MANAGED_FAILOVER item 7) and the leader's probe work for every follower's forwarded reads; neither is measured. On one host RTT is near zero, so "2 RTT" stays an estimate, and no result is named that would change the design. | design "Cost"; quest A3 | Add to A3: v2 Open and Next latency (p50, p99) at a follower and at the leader under an agent load, leader CPU, and two counters (rounds per pass, tasks per round), so latency at any RTT is rounds times RTT plus the measured pass cost; and state the threshold that would send it back (for example, follower Open p99 over the leader's by more than N passes). Strict jobs against an A/A band, as A3 says. |
| V3 | no | gap | The leader's refusals of a forwarded read (NotReady before its term commits an entry, Busy at `limits.reads`, NotLeader on step-down or at a non-leader) have no response message: each would cost the follower `election_max`. NotReady follows every election, so every follower stalls reads 2 s after each one. | core.h:986-989, :650; design table | `ReadIndexResponse` carries a refusal reason, mapped by `read_failure` as today (NotReady to `outcome_unknown`, Busy to `admission_full`). Tests: each refusal reaches the follower at once; a response from a non-leader peer or at another term is ignored. |
| V4 | no | gap | The follower's timeout covers the response only; after it, "applied >= index" has no bound but the request deadline (60 s default), so a follower that gets the index but not the entries (a cut between them, a slow catch-up) breaks the Discovery bound's "at most `election_max`". | design follower table rows 2-3, "Discovery bound" | One deadline from send to `applied >= index`, or the bound names the request deadline for that case. The trade-off (a lagging follower refusing at 2 s under write load) is the contributor's to state. |
| V5 | no | gap | Closing D12 rests on one invariant: a task admitted while a round is in flight never completes from that round. The design states it; A2's listed tests (relay D cases, the skip mutant) do not force it. | design "Authority" | A deterministic service test: hold a round's response, admit a task, release the response; the task waits for the next round. Mutant: put late tasks in the in-flight batch. |
| V6 | no | nit | After this design, `returned`, `trusting()` and the `leader_contact` stamps (service.cc:1862-1877, :2206, :2213) only feed `/v1/health` `ready`, where D12 and D13 remain. With multi-voter-idle-wake the loop sleeps past `election_min` by design, so the absence rule would flap `ready` on an idle voter. | service.cc:1859-1877 | Owner's call, user-visible: 1. keep as is (Limit 4); 2. reduce `ready` to `ready() && leader_known` and remove the absence rule; 3. make `ready` a cached result of the last round. |
| V7 | no | gap | Quiescence and idle wake are in flight (consensus-peers-wake, consensus-quiesce-election-stall, multi-voter-idle-wake). A follower holding a forwarded read must not rest and its deadline must appear in `Worker::next_deadline()`; a resting leader must wake on a ReadIndexRequest (core.h:570 keeps it awake only once the read is admitted). | core.h:570, :409; multi-voter-idle-wake quest | Name both in the consensus quest's criteria, with a test that a forwarded read at a rested group completes or refuses within `election_max`. |
| V8 | no | nit | Keeping v1 and `/v2/read` leader-only by a service role check before `node.read` is a check-then-act: a node deposed between the check and the call forwards the read. Harmless, since the forwarded read is linearizable too. | design "Refusals" last paragraph | None needed; or a flag on `Read` if the leader-only rule must be exact. |
| V9 | no | nit | The quest's Contracts say a not-yet-confirmed voter answers `503 not_ready`; the design answers sessions with `source_not_leader`, which gate 2 needs (see question 4.3). Limit 5 (relay Next refused after a confirmed layouts answer loses a sync) could go if the relay mapped a session 503 to UpstreamBehind (http_upstream.cc:355-357, namespace_relay.cc:339-343). | quest Contracts; design Limit 5 | Record the refusal names, files leaving the round, and gated reader Next in the Decisions row (A1). The relay mapping is optional. |

Blocking: yes or no. Kind: defect (wrong behavior), gap (unproven claim or missing test), nit. A
finding that would remove or narrow user-visible behavior offers options, including keeping it,
and is never blocking.

## Not run

No jobs and no probes: there is no implementation. The token-collision window in V1 comes from
code reading, not a reproduction; its consensus test settles it. I did not check the website
pages, the codec's size bounds for the new messages, or the restart-capacity and
server-composition-adoption branches that hold `service.cc`.
