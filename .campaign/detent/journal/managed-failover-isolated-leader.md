# managed-failover-isolated-leader: journal

State: waiting on jobs 261121-215618-22a9b6 261121-215622-e0d05c 261121-215644-6517f3 261121-215648-f04fea 261121-215651-6c0e68 261121-215707-6220d4 261121-215711-2be2fe 261121-215714-573de0 261121-215717-b679cd 261121-215719-269505
Quest: .campaign/detent/quests/managed-failover-isolated-leader.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Fix, test and bound committed (Detent:3f8c031e fix and tests, Detent:b16a3ec9 doc); jobs queued.

## Next

1. Read the jobs; A2 times are in the probe's `$JOB_OUT/converged.log`. Write "ready for review" when green.

## Blocked on

Nothing. For the lead, outside my file set: docs/USAGE.md:242, website/src/content/docs/reference/configuration.md:201 and website/src/content/docs/guides/managed-delivery.md:162 say "A fresh Open revalidates authority quorum", which is false for version 2 sessions. Replacement: "A due refresh closes the session after accepted local publication and recovery settle and opens at the next endpoint, because a member answers Open from its own history; an idle member holds an agent for at most one refresh interval."

## Cause (A1)

The managed agent uses version 2 (W5) sessions. The authority answers a version 2 Open at any voter from its own state with no quorum read or leader check (src/detent_authority/service.cc `frames_request` queues it; the owner loop's `frames_sessions->open` replies 200 at once), and every Next from local history, 204 when nothing changed. The client's refresh (coordinator.cc `candidate_loop`, `sources.refresh_due`) closed the session and reopened on `sources.endpoint()`, the same voter, treating a successful Open as quorum revalidation (architecture boundary 5). At a voter cut off from the majority the Open succeeds and every Next is idle, so no error ever reaches the rotation path: no refresh failure, no rotation, 650 idle answers. The refresh interval was honored (idle answers did not extend it); it was the Open at the same endpoint that satisfied it.

Fix: `SourceSelection::refreshed()` moves a due refresh to the next endpoint and starts a fresh round; the coordinator calls it after the Close at the opening endpoint and counts the move as a rotation. Returning to the cut-off member later is safe: the agent's cursor is ahead, which the encoder answers `Ahead`, so idle, never older.

Test: test_namespace_coordinator's pooled block opens an agent at a member that answers idle at head 6 while the other is at 16, refresh 1 s: it must reach 16 within 4 s with one rotation and no refresh failure, then return to the cut-off member, still at 16, with that member having answered frames only once. At the mutant (refs/mutants/managed-failover-isolated-leader-a1, Detent:292f8278, fix reverted) it waits 10 s and fails.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | 261121-215618-22a9b6 (fails without, mutant Detent:292f8278), 261121-215622-e0d05c (passes x5 at the fix) |
| A2 | 261121-215644-6517f3 (forced probe x8 at the fix) |
| A3 | 261121-215648-f04fea partition x50, 261121-215651-6c0e68 failover x50 plain; 261121-215707-6220d4 + 261121-215711-2be2fe partition 2x25, 261121-215714-573de0 + 261121-215717-b679cd failover 2x25 under TSan |
| A4 | docs/MANAGED_FAILOVER_ARCHITECTURE.md "Discovery bound" |
| A5 | suite 261121-215719-269505; CI on push |

Examined: 261121-203249-b164cf (old-flakes-disposition's probe at main, 8 of 8 stuck at 1 of 5).

## Result

Pending jobs.

## Rulings I made

1. Client-side fix, no authority change: the quest requires rotation when the cut-off voter answers idle, which only the client can bound. An authority hardening (a voter with no known leader refusing version 2 Open with source_not_leader) would shorten discovery but is not needed for the bound; not proposed as a blocker.
2. Every due refresh moves, rather than only an all-idle interval: one rule, a one-interval bound instead of two, and Opens per second are unchanged. Cost: one TLS handshake per refresh per agent (the connection cache holds one connection) and one INFO rotation log per refresh in managed_host.
3. A refresh move counts in `source_rotations`; boundary 2 now reads "sticky between refreshes".
4. TSan x50 runs as two x25 jobs so each fits the 30-minute light cap.
5. Merged main (Detent:0ec367ff) so the probe's managed.py diagnostic applies.
