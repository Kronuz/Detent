# Lead handoff, 2026-11-25 (Claude weekly quota at 99%; reset 2026-11-30 05:00 UTC)

Machine jobs keep running through the outage; read their results first.

## In flight
- relay-mirror-idle-wake: round 6 review (max) of D165 at Detent:e200ee50; its jobs are queued (journal Evidence). First move: read review Round 6; APPROVE -> accept, rerun evidence at the merged head, land --push; CHANGES -> fix round (D165 counts as a new design's second round).
- first-read-1ms: round 5 review of D166 at Detent:d9ddc78f; jobs 261125-055929-011192 (A4 A/B), -055908-8414ff, -055912-873598, -055916-695193, -055920-b8463c (A2), CI. First move: read Round 5; land; then start paged-find-absent-cost and http-client-adoption (RC-1).
- consensus-inline-sync-loop: fix round 2 at consensus:624bebe (leader's own slow sync is designed; `leads` refined); round 4 review was asked of the round-1 reviewer. Jobs: 261125-051305-f2273d, fails-without -051306-10a35a, suite -051315-1359b6; CI 45621393108. First move: read review Round 4 (may be missing if the quota ran out); if missing, launch it.
- resource-adoption (RC-1 EX-1): contributor working; Decision 3 A/B 261125-051931-4c08af; then the pin cascade once (completion, journal, faultlab, consensus). First move: read the journal's State.
- persistent-map-adoption (RC-1 EX-4): waits on jobs 261125-055021-ea057c (A6), -055028-de0f76 (A9), -055647-e5334d (A5), -055718-b49932 (A1), -055733-bfba29 (native), -055734-d461d7 (pins). First move: resume its contributor (analyse A6, commit A9, EX-4 row), then review.
- server-composition-adoption: ready except A4's authority half (waits on relay-mirror's service.cc); jobs 261125-061135-284c58, -061139-04c733, -061143-6a11a1.
- warm-relay-agent-boot: ready at f5cbde96; suite 261125-055754-1ed9d8 and CI. First move: land --push (A1 is manual).

## Parked
- multi-voter-idle-wake: until relay-mirror-idle-wake lands.
- voter-read-freshness: until server-composition-adoption lands; restore tests/detent_authority/** to it.
- observability-core, archive-stash-scheduler: owner decisions asked 2026-11-25 (recommend: drop observability-core for after the release; a conditional go to archive).

## Next on the RC path
msgpack-adoption after resource-adoption; http-client-adoption after first-read-1ms and relay-mirror; driver-pipeline (consensus) after consensus-inline-sync-loop; rc1-integration last (fold in F4's FetchReader transport shape, an owner question).

## Standing
Cancel superseded jobs at every new head and landing; list parked quests with age in each report.

## Added at the outage
- warm-relay-agent-boot landed (Detent:59e6a9b1).
- consensus-inline-sync-loop: review round 4 CHANGES R4-F1 (no ctest entry); the lead added it at consensus:80540ca (file set widened to CMakeLists.txt) and queued the suite there. First move: confirm the suite and CI at 80540ca, accept --confirm R4-F1=<suite job>, land --push.
- Tell the owner before release (reviewer's ruling): while the leader's own persist is pending, Core sends no heartbeat or append (core.h:716-720), so one leader sync longer than the election timeout costs the term (CI macOS x86: 4.7-6.1 s syncs).
