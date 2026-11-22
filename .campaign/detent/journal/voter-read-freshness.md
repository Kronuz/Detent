# voter-read-freshness: journal

State: ready for review (design ready: docs/design/voter-read-freshness.md, for the design review)
Quest: .campaign/detent/quests/voter-read-freshness.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

design ready. Phase 1 (A1) done: docs/design/voter-read-freshness.md. Stopped for the lead's design review.

## Next

1. After the design review: the Kronuz/consensus quest for the follower half of `Read` (ReadIndexRequest/Response), then code here once restart-capacity and server-composition-adoption land (they hold service.cc).
2. Port managed-failover-isolated-leader's deterministic test (test_namespace_coordinator pooled block, adapted: the cut-off member now refuses), forced probe (job 261121-215644-6517f3's command) and the "Discovery bound" section (Detent:b16a3ec9, rewritten per the design).
3. A2 mutant: skip the confirmation; relay D's A5 cases 20 of 20 under load (adapt D15: relay D started twice).
4. A3 strict A/A + before/after; A4 docs; A5 probe 8/8 and system tests x50 plain and TSan.

## Blocked on

The lead: design review of docs/design/voter-read-freshness.md, and a Kronuz/consensus quest for follower ReadIndex (the quest's Contracts anticipate it). Its four review questions are at the doc's end (files skip the round; reader W5 Next gated; two refusal names; D12/D13 closed).

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | docs/design/voter-read-freshness.md; design review and Decisions row pending |
| A2 | <no evidence yet> |
| A3 | <no evidence yet> |
| A5 | <no evidence yet> |
| A4 | <no evidence yet> |

## Result

Design only, so far.

## Rulings I made

1. The design doc is at docs/design/voter-read-freshness.md, outside the quest's file set, because the assignment names that path.
2. The round runs in consensus (follower ReadIndex forwarded to the leader), not as a service timer or lease; the service batches one read per owner pass.
3. Sessions refuse with `source_not_leader` (the agent already rotates on it, no client change); layouts keep `not_ready` per the contract.
4. Version 1 sessions and member reads stay leader-only through an explicit role check, so the consensus change alters nothing for them.
