# voter-read-freshness: journal

State: ready for review (design ready, round 2: docs/design/voter-read-freshness.md folds in review V1-V9)
Quest: .campaign/detent/quests/voter-read-freshness.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

design ready. Round 2 folds in .campaign/detent/reviews/voter-read-freshness-design.md (CHANGES at Detent:9bc009ce, review Detent:04dd6fbb): V1 per-start nonce `{start, token}` with the follower's drop rule; V2 A3 measures follower/leader Open and Next p50/p99, leader CPU, rounds per pass, tasks per round, with send-back thresholds (follower Open p99 > main + 5 ms; leader CPU > main + 10%; rounds per pass > 1); V3 refusals carry a reason in ReadIndexResponse; V4 one election_max deadline from send to applied; V5 late tasks never join the in-flight round, with its test; V7 rest/wake; V6, V8, V9 as notes. Lead's rulings on the four questions recorded in the doc's Status.

## Next

1. After the design review: consensus-follower-read (the lead's quest for the follower half of `Read`) lands, then code here once restart-capacity and server-composition-adoption land (they hold service.cc).
2. Port managed-failover-isolated-leader's deterministic test (test_namespace_coordinator pooled block, adapted: the cut-off member now refuses), forced probe (job 261121-215644-6517f3's command) and the "Discovery bound" section (Detent:b16a3ec9, rewritten per the design).
3. A2 mutant: skip the confirmation; relay D's A5 cases 20 of 20 under load (adapt D15: relay D started twice).
4. A3 strict A/A + before/after; A4 docs; A5 probe 8/8 and system tests x50 plain and TSan.

## Blocked on

The lead: re-review of the design (round 2), and consensus-follower-read landing. The A3 thresholds are in the doc's Cost section for the quest file.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | docs/design/voter-read-freshness.md; review round 1 CHANGES (Detent:04dd6fbb), round 2 pending; Decisions row pending |
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
5. Answered review V1-V5 in the design text; V6, V8, V9 recorded as notes (V6 is the owner's, user-visible); V7 folded in as the Rest paragraph.
