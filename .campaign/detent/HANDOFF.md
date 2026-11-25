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
- consensus-inline-sync-loop: LANDED consensus:80540ca (R4-F1 confirmed by suite ...071734-376a14).
- Tell the owner before release (reviewer's ruling): while the leader's own persist is pending, Core sends no heartbeat or append (core.h:716-720), so one leader sync longer than the election timeout costs the term (CI macOS x86: 4.7-6.1 s syncs).
- first-read-1ms round 5 review handed back unfinished (no verdict, no review file). Established at Detent:d9ddc78f: the merge keeps R3-5 and FlushMode::full; the build succeeds; A2 stopped:1 (...055908-8414ff) passes (p99 768 us vs main 856 us; 1,200/1,200 below 1 ms). Pending: A4 A/B ...055929-011192, A2 cells ...873598, ...695193, ...b8463c, the reviewer's probes ...060537-05914c and ...060542-5fd308 (suspected gap: `reader_bench_ab.sh find` reports "A4 within" when the head prints no paged rows or lacks a shape), CI 45629289288. A1's profile (9e9536) ran at older code. First move: resume a reviewer from .scratch/detent/review-first-read-1ms-correctness/ (a4_r5.py, a4_verdict_r5.sh, r5_checks.sh) once those jobs end; check the doc's Measure table against 011192.
- resource-adoption: waits on the Decision 3 A/B ...051931-4c08af (queued behind exclusive jobs). Link line, probe fix and journal committed (Detent:cc061930, 3073fb9f, ccc06eb8; branch pushed at the outage). allocators CI 45622927366 green except macos-arm64 (no runner). First move: resume its contributor: read the A/B, settle allocators' head, do the pin cascade once (completion, faultlab, journal, consensus), queue every criterion, ready for review.
- relay-mirror-idle-wake round 6 (Detent:3449f9bc): CHANGES, but D165 holds (every named interleaving passed in a run; R5-1..R5-3 fixed; R4-1 probes still fail at 13707337). Blocking, both in tests: R6-1 test_relay_seal_stamp's `current` step races the Pair's last seal (3 of 20 runs fail at the fix); R6-2 no test for a 204 without `X-Detent-Seal` (a mutant reading it as idle passes all 8 relay tests; the reviewer's bad_seal.py catches it). A1 (...3344f2) is queued 53rd of 64. First move: resume the contributor on R6-1 (fix the step's race, never loosen it) and R6-2 (bad_seal as a test with its mutant as fails-without), then confirm by jobs and accept; no further review round unless the fix touches the mechanism.
- persistent-map-adoption jobs ended: A6 ...055021-ea057c and A9 ...055028-de0f76 done (exclusive; analyse with .scratch/pm/ab.py); pins ...055734-d461d7 done. Failed: A5 ...055647-e5334d and A1 ...055718-b49932 at CMake configure (tests/detent_relay/CMakeLists.txt:73 links `hashes::hashes`, target not found; the job's configure, likely a partial build or the a8bf087 baseline, lacks it); native ...055733-bfba29 1 of 219: test_authority_restart_wake (it failed in 9 earlier job logs too: likely pre-existing; attribute with runs before treating it as this quest's). First move: resume the contributor with these three.
