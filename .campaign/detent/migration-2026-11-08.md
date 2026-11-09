# detent: mapping report

Sources: `~/.scratch/migrate/Detent` (all branches under `origin/`; main is
`origin/wip/main-parked-2026-11-08`, `Detent:edf685a`), the preflight extracts, and the tool at
`~/code/agent-campaign`. Output: `PLAN.md`, `quests/` (11 new quests), `history/` (25 history
quests with journals), `dense-pages-decision.md`, `map.sh`, `history.sh`. Both scripts pass
`bash -n` and were rehearsed end to end in a throwaway sandbox clone with an isolated
`CAMPAIGN_JOBS` (since deleted): map.sh twice (idempotent), then history.sh; `campaign map` shows
all 37 quests, 25 landed before the campaign.

## Coverage

### 1. Ledgers: all 37 across main and every branch

| Ledger (`docs/status/`) | Where | Became |
|---|---|---|
| `README.md` | main | PLAN.md Goals and Milestones (the index); its rows in table 2 |
| `campaign-lead.md` | main (narrowed at `785fb34`; broad at `8c76f7e`) | PLAN.md Goals, "the lead's route"; criteria 1-7 in table 2 |
| `actors-docs.md` | main | history `actors-docs` |
| `client-local-source.md` | main | history `client-local-source`; built by `legacy-h` |
| `container-selection.md` | main | history `container-selection` |
| `dense-pages.md` | main | `dense-pages` (live; its History); its design landed, its build is the quest |
| `hash-once.md` | main | history `hash-once` |
| `how-it-works.md` | main | history `how-it-works` |
| `http-coroutines.md` | main | history `http-coroutines`; flakes 3-5, 8: PLAN.md not-quests 1 |
| `legacy-wave2.md` | main, newest on `legacy/wave2-h` | history `legacy-wave2` (B-G); `legacy-h` (H, blocked); PLAN.md not-quests 3, 4 |
| `m1-pins.md` | main | history `m1-pins`; W5 owner items: PLAN.md not-quests 7 |
| `namespace-design.md` | main | history `namespace-design`; its S1/S2, S3, S3b criteria in those quests; PLAN.md not-quests 2, 9 |
| `namespace-s3.md` | main | history `namespace-s3`; repair: `namespace-s3b` |
| `post-w2.md` | main | history `post-w2`; PLAN.md not-quests 5 |
| `quickstart-managed.md` | main | history `quickstart-managed`; `daemon-ready-leader` |
| `reader-acl.md` | main | history `reader-acl`; platform conditions: M5 |
| `reader-design.md` | main | history `reader-design`; S7-S9: PLAN.md not-quests 9 |
| `reader-s1-s4.md` | main | history `reader-s1-s4` |
| `reader-s3.md` | main | history `reader-s3` |
| `reader-s5-s6.md` | main, newest on `reader/s6` | history `reader-s5`; `reader-s6` (blocked) |
| `relay-snapshot-lifecycle.md` | main | history `relay-snapshot-lifecycle`; sustained load: M4 |
| `retained-state.md` | main | history `retained-state` |
| `salvage-2026-11-03.md` | main | history `salvage` |
| `salvage-followups.md` | main | history `salvage-followups` |
| `seeding-waits.md` | main | history `seeding-waits`; SC-4: M4 |
| `dense-deltas.md`, `dense-reads.md` | `quest/dense-pages*` | `dense-pages` (moved in place, not remapped) |
| `dense-first-read.md` | `dense-pages-first-read` | `dense-pages` A15 (O28) |
| `namespace-s1-s2.md` | `namespace/s1-s2`, `wip/dense-s2-parked` | `namespace-s1-s2` (blocked); `dense-sync-stack` (blocked) |
| `namespace-s3b.md` | `namespace/s3b` | `namespace-s3b` (blocked) |
| `resource-adoption.md` | `phase/resource-adoption` | `resource-adoption` (blocked) |
| `http-adoption.md` | `track/http-adoption` | `http-client-adoption` (blocked) |
| `msgpack-adoption.md` | `track/msgpack-adoption` | `msgpack-adoption` (blocked) |
| `persistent-map-adoption.md` | `track/persistent-map-adoption` | `persistent-map-adoption` (blocked) |
| `legacy-removal-inventory.md` | `explore/legacy-inventory` | history `legacy-wave2` (its cut order); `legacy-h`; step I: PLAN.md not-quests 3 |
| `reader-capsule.md`, `resource-capsule.md` | `track/reader-capsule`, `track/resource-capsule` | PLAN.md not-quests 8 (M3, D27): acceptance fails 3 of 4 rows, no adoption |

### 2. Release exits, blockers, handoff items and the lead ledger

| Item | Became |
|---|---|
| RC-1 required integration | Goal; M2: the 4 adoption quests; CL-2, consensus transport, async HTTP log to plan |
| RC-2 client performance | Goal; M3 (to plan); `reader-s6`, `dense-pages` A15; history `prepared-reader`, `prepared-activation` |
| RC-3 sustained capacity | Goal; M4 (to plan); `namespace-s1-s2`, `dense-pages`, `dense-sync-stack` |
| RC-4 failure and restoration | Goal; M4 (to plan); `namespace-s3b` |
| RC-5 composed release candidate | Goal; M5 (to plan); `legacy-h`; reader-acl's platform conditions |
| Release plan rows: libraries A-E, retained-state resources, extraction, containers, recovery/delivery, client attachment, HTTP logs, consensus/journal, capacity, restoration, packaging | M5; `retained-state` + M2; M2; `container-selection`; `recovery-delivery` + M4; `prepared-activation` + M3; M2; M2 + M1b; M4; M4; M5 |
| Blocker P0 SC-5 container safety | history `container-selection` (EX-0 closed it) |
| Blocker P1 SC-2/SC-3 segment storage | Goal; M4 (to plan, no quest yet, O31); `dense-pages`; `namespace-design` D6/D8 |
| Blocker P1 SC-4 ingestion waits | history `seeding-waits`; `namespace-s1-s2`; M4 calibration |
| Blockers P1 resources, P2 HTTP clients, bounded MessagePack, persistent map | `resource-adoption`, `http-client-adoption`, `msgpack-adoption`, `persistent-map-adoption` |
| SC-1 relay lifecycle (lead-owned) | history `relay-snapshot-lifecycle`; sustained load M4 |
| Extraction EX-0; EX-1 to EX-4 | history `container-selection`; the 4 adoption quests |
| Extraction conditional candidates 1-4; old worktrees | M5 keep-local decisions; sources named in the adoption quests |
| Handoff landing order 1-5 | `reader-s6`, `legacy-h`, `namespace-s3b`, `namespace-s1-s2`, `dense-pages`; D25 |
| Handoff owner questions 1-7 | D17, D18, D19, D20, D21, D22, D23 (`daemon-ready-leader`) |
| Handoff follow-ups 1-5 | PLAN.md not-quests 1, 2, 3; `pin-wave`; not-quests 4 |
| Handoff rules 1-8; run queue; housekeeping | PLAN.md Constraints; campaign jobs replace the queue; `land` cleans worktrees |
| Lead ledger criteria 1-5 | `reader-s6`, `legacy-h`, `namespace-s3b`, `namespace-s1-s2`, `dense-pages` |
| Lead ledger criteria 6, 7 | N2 (`pin-wave`, not-quests); milestone validations and the Goals' closing line |
| Status index rows (2026-10-29): startup capacity, CLI reader capacity, cardinality, ingestion, scaling, relay, ACL, containers, recovery, client attachment, resources, capsule, HTTP clients, storage barriers, online capacity | not-quests 11; `reader-acl` + 11; M4; `seeding-waits` + M4; Goals + M4; `relay-snapshot-lifecycle`; `reader-acl`; `container-selection`; `recovery-delivery`; `prepared-activation`; `resource-adoption`; not-quests 8; `http-client-adoption`; `namespace-s1-s2`; not-quests 10 |

### 3. Reviews: all 39 in `docs/reviews/` on any ref, plus Dense's 3

| Review | Belongs to |
|---|---|
| `container-adoption-code-2026-10-24`, `container-extraction-architect-2026-10-24`, `container-fix-comparison-2026-10-24`, `container-std-map-code`, `flat-map-code` | history `container-selection` |
| `reader-capacity-security-2026-10-23` | history `reader-acl` |
| `relay-bootstrap-code-2026-10-23` | history `relay-snapshot-lifecycle` |
| `recovery-delivery-component-2026-10-25`, `coordinator-feedback-deadline-2026-10-25`, `poll-publisher-qualification-2026-10-25` | history `recovery-delivery` (the poll fixture's subject went with legacy-wave2) |
| `prepared-reader-component-2026-10-26`, `hosted-reader-fixtures-2026-10-26` | history `prepared-reader` |
| `prepared-activation-component-2026-10-26`, `prepared-managed-qualification-2026-10-26` | history `prepared-activation` |
| `http-coroutines-design`, `w2-s1-s8-code-review`, `w2-s2-code-review` | history `http-coroutines`; CR3-CR6: not-quests 6 |
| `retained-state` | history `retained-state` |
| `reader-design-preliminary` | history `reader-design` |
| `reader-s3` | history `reader-s3` |
| `hash-once` | history `hash-once` |
| `namespace-delivery-preliminary`, `namespace-blocks-preliminary-2026-11-04` | history `namespace-design` |
| `namespace-s3-architect-2026-11-04`, `namespace-s3-security-2026-11-04` (review branches only) | history `namespace-s3` |
| `namespace-s3b-architect-2026-11-04` | `namespace-s3b` (L1, C1-C5 are A8-A14) |
| `reader-s6-security` | `reader-s6` (A10-A14) |
| `dense-pages-design`; `docs/campaign/reviews/dense-pages-{contracts,correctness,security}` | `dense-pages` |
| `resource-adoption-component-2026-10-27`, `resource-adoption-qualification-2026-10-27` (track branches) | `resource-adoption` |
| `http-adoption-component-2026-10-27` (track branch) | `http-client-adoption` |
| `persistent-map-component-2026-10-28` | `persistent-map-adoption` |
| `reader-capsule-component-2026-10-29`, `reader-capsule-next-component-2026-10-29`, `reader-ancestor-storage-blueprint-2026-10-29` (branch) | not-quests 8 (M3); the first-use verification idea became `reader-s3` |
| `client-reader-measurement-2026-10-25` | M3 baseline; not-quests 8 |
| `scaling-2026-10-23` | Goals (SC-1 to SC-5); M4 |
| `scheduler-wakeup-2026-10-29` | not-quests 8 |
| `calibration` | PLAN.md Documents (reviewer calibration); not-quests 11 |

### 4. Branches

| Branch (head) | Became |
|---|---|
| `reader/s6` (`b1d281c`) | `reader-s6`, adopted as `quest/detent/reader-s6` |
| `legacy/wave2-h` (`aac6cb6`) | `legacy-h`, adopted |
| `namespace/s3b` (`c510832`) | `namespace-s3b`, adopted |
| `namespace/s1-s2` (`4c0eb36`) | `namespace-s1-s2`, adopted |
| `phase/resource-adoption` (`6d2cb2a`) | `resource-adoption`, adopted |
| `wip/dense-s2-parked` (`f7bf999`) | `dense-sync-stack` (reference, not adopted: conflict markers committed) |
| `wip/dense-t2-base-parked`, `dense-pages-first-read`, `dense-pages-grouped-sync`, `phase/dense-pages`, `quest/dense-pages`, `quest/dense-pages--deltas`, `quest/dense-pages--integration`, `quest/dense-pages--overlay-bounds`, `quest/dense-pages--performance`, `quest/dense-pages--reads`, `quest/dense-pages--real-deltas` | `dense-pages` (O28) |
| `wip/main-parked-2026-11-08` | the home `main` |
| `wip/laptop/legacy-wave2`, `phase/legacy-wave1` | history `legacy-wave2` (pre-rebase copies; each commit has a twin on main: `7716a58`, `7988bc7`, `0762f1a`, `4f49f4b`) |
| `wip/laptop/ci-fixture-logs` | landed as `631ed24` (CI tooling; flake 6 in history `http-coroutines`) |
| `design/client-local-source` (merged `4872b7a`) | history `client-local-source` |
| `explore/legacy-inventory` | history `legacy-wave2`; not-quests 3 |
| `explore/agent-startup` | history `hash-once` (its note); its code probe: not-quests 8 |
| `explore/cold-bootstrap` (merged `6733937`) | `namespace-s1-s2` (its 5M baseline, `docs/perf/cold-bootstrap-5m.md`) |
| `phase/salvage-followups` (rebased as `9abad85`) | history `salvage-followups` |
| `phase/w2-s2`, `design/http-coroutines`, `review/w2-s2`, `phase/w2-s1`, `phase/w2-s2-series` | history `http-coroutines` |
| `design/reader`, `design/reader-rev`, `design/reader-rev2`, `review/reader`, `review/reader-recheck` | history `reader-design` |
| `design/namespace-delivery`, `design/namespace-rev`, `design/namespace-rev2`, `design/namespace-rev3`, `design/compacted-blocks`, `review/namespace-delivery`, `review/namespace-recheck`, `review/namespace-recheck2` | history `namespace-design` |
| `namespace/s3`, `review/namespace-s3-architect`, `review/namespace-s3-security` | history `namespace-s3` |
| `review/namespace-s3b-architect` | `namespace-s3b` |
| `phase/hash-once`, `review/hash-once` | history `hash-once` |
| `phase/reader-s3`, `review/reader-s3` | history `reader-s3` |
| `phase/retained-state`, `review/retained-state` | history `retained-state` |
| `phase/reader-s1-s4`; `reader/s5` | history `reader-s1-s4`; history `reader-s5` |
| `m1/pin-check`, `m1/pins-to-head`, `m1/w5-evidence`, `track-pin/http-parser-fallback` (superseded by `10cd6ab`) | history `m1-pins` |
| `phase/post-w2` | history `post-w2` |
| `legacy/wave2`, `legacy/wave2-f`, `legacy/wave2-g` | history `legacy-wave2` |
| `docs/how-it-works`, `docs/quickstart-managed` | histories `how-it-works`, `quickstart-managed` |
| `track/container-selection`, `track/container-adoption`, `diagnostics/managed-process`, `track/ci-baseline` (superseded: CI builds with `macos_llvm`) | history `container-selection` |
| `track/recovery-reclamation`, `diagnostics/coordinator-cleanup`, `diagnostics/session-reset-reasons`, `diagnostics/writer-shutdown` | history `recovery-delivery` |
| `track/prepared-reader`; `track/prepared-activation` | histories `prepared-reader`; `prepared-activation` |
| `track/resource-adoption`, `fix/variant13` | `resource-adoption` (source; A9) |
| `track/http-adoption`; `track/msgpack-adoption`; `track/persistent-map-adoption` | the matching adoption quests (sources, not adopted) |
| `track/reader-capsule`, `track/resource-capsule`, `track/reader-ancestor-storage`, `track/reader-directory-trust`, `diagnostics/client-read` | not-quests 8 (M3) |
| `ci/fixture-logs`, `design/dense-pages`, `main` | landed (CI tooling; Dense design; old main) |

## New quests

| Quest | Branch (head) | Risk, why | Criteria (source) | Status |
|---|---|---|---|---|
| `reader-s6` | `reader/s6` (`b1d281c`) | high: concurrency, untrusted files, public C ABI | 15 (reader-s5-s6 S6, security review) | blocked (parked) |
| `legacy-h` | `legacy/wave2-h` (`aac6cb6`) | critical: deletes Client, `health()`, `--defaults` | 17 (legacy-wave2 wave and H) | blocked, needs `reader-s6` |
| `namespace-s3b` | `namespace/s3b` (`c510832`) | high: durability | 14 (namespace-s3b 1-7, K1-K6, review) | blocked (parked) |
| `namespace-s1-s2` | `namespace/s1-s2` (`4c0eb36`) | high: durability | 13 (namespace-design S1, S2; handoff 4) | blocked (parked) |
| `dense-sync-stack` | none (`wip/dense-s2-parked` reference) | high: durability | 6 (owner ask 2026-11-07, S2 criteria) | blocked, needs Dense and S1/S2 |
| `daemon-ready-leader` | none | normal: readiness fix | 4 (handoff Q7, quickstart ledger) | planned |
| `pin-wave` | none | high: journal framing, persisted bytes | 8 (handoff follow-up 4, M1 criteria) | planned, needs N1 + Dense |
| `resource-adoption` | `phase/resource-adoption` (`6d2cb2a`) | high: accounting and retirement, shared API | 10 (ledger, EX-1, D6) | blocked |
| `http-client-adoption` | none | high: TLS and wire | 8 (EX-2) | blocked |
| `msgpack-adoption` | none | high: wire decoder | 8 (EX-3, ledger) | blocked |
| `persistent-map-adoption` | none | high: authority state bytes | 7 (EX-4, ledger, review) | blocked |

## History quests (landed `--earlier` by history.sh)

| Quest | Landed | Evidence | Milestone |
|---|---|---|---|
| `container-selection` | `4f1a086` 2026-10-24 | `docs/status/container-selection.md` | H0 |
| `reader-acl` | `a963c4e` 2026-10-23 | `docs/status/reader-acl.md` | H0 |
| `relay-snapshot-lifecycle` | `c5680e4` 2026-10-23 | `docs/status/relay-snapshot-lifecycle.md` | H0 |
| `seeding-waits` | `1cc2f38` 2026-10-23 | `docs/status/seeding-waits.md` | H0 |
| `recovery-delivery` | `19a734d` 2026-10-25 | `docs/reviews/recovery-delivery-component-2026-10-25.md` (no ledger) | H0 |
| `prepared-reader` | `205ece1` 2026-10-26 | `docs/reviews/prepared-reader-component-2026-10-26.md` (no ledger) | H0 |
| `prepared-activation` | `479c4bc` 2026-10-27 | `docs/reviews/prepared-activation-component-2026-10-26.md` (no ledger) | H0 |
| `m1-pins` | `3e8bdbc` 2026-11-04 | `docs/status/m1-pins.md` | M1 |
| `http-coroutines` | `d0a5cd8` 2026-11-03 | `docs/status/http-coroutines.md` | M1 |
| `post-w2` | `43d1ce9` 2026-11-03 | `docs/status/post-w2.md` | M1 |
| `salvage` | `5207f96` 2026-11-03 | `docs/status/salvage-2026-11-03.md` | H1 |
| `salvage-followups` | `9abad85` 2026-11-03 | `docs/status/salvage-followups.md` | H1 |
| `retained-state` | `ebfbcab` 2026-11-03 | `docs/status/retained-state.md` | H1 |
| `reader-design` | `b607b5c` 2026-11-03 | `docs/status/reader-design.md` | H2 |
| `reader-s1-s4` | `02675d3` 2026-11-03 | `docs/status/reader-s1-s4.md` | H2 |
| `reader-s3` | `132b6bd` 2026-11-03 | `docs/status/reader-s3.md` | H2 |
| `hash-once` | `9b24fb3` 2026-11-03 | `docs/status/hash-once.md` | H2 |
| `namespace-design` | `814b605` 2026-11-04 | `docs/status/namespace-design.md` | H2 |
| `namespace-s3` | `5475baf` 2026-11-04 | `docs/status/namespace-s3.md` | H2 |
| `reader-s5` | `d1e9ee6` 2026-11-04 | `docs/status/reader-s5-s6.md` | H2 |
| `client-local-source` | `4872b7a` 2026-11-04 | `docs/status/client-local-source.md` | H2 |
| `legacy-wave2` | `72ed8ee` 2026-11-04 | `docs/status/legacy-wave2.md` | H3 |
| `actors-docs` | `74e07c9` 2026-11-03 | `docs/status/actors-docs.md` | H4 |
| `how-it-works` | `dba0ef8` 2026-11-04 | `docs/status/how-it-works.md` | H4 |
| `quickstart-managed` | `bea5c43` 2026-11-04 | `docs/status/quickstart-managed.md` | H4 |

Every evidence path exists at its landing commit and on main. Not placed as history: `README.md`
and `campaign-lead.md` (coordination, in PLAN.md), `dense-pages.md` and the Dense branch ledgers
(the live Dense quest), and the branch-only ledgers of unlanded work (live or blocked quests above).

## Open questions for the lead (owner where marked)

1. O1 `dense-pages` owns `src/detent_local/**`, `tests/**`, `benchmarks/**`, `docs/mechanisms/**`, CMake and CI: every new quest overlaps it; narrow Dense's files or start with `--force`.
2. O2 CI is a landing gate only with `campaign config ci=github` (campaign-wide, gates Dense too); branch pushes do not trigger `ci.yml`, so dispatch it per head.
3. O3 Make D25 binding: `campaign set dense-pages after=reader-s6,namespace-s3b,namespace-s1-s2` (`dense-pages-decision.md` section 3).
4. O4 Owner: confirm D17 and D18 (S3b Q1, Q2), which change read results and recovery.
5. O5 Owner: confirm D21 (`detent get` loses `--defaults` until reader S7).
6. O6 `legacy-h` is critical, so it needs a `design` acceptance; the client-local-source design had owner approval only.
7. O7 `dense-sync-stack`: keep it, or fold it into Dense's merge of `main` after S1/S2 lands.
8. O8 S1/S2's 2026-11-07 timings are not campaign jobs; A9 reruns them `exclusive`, one arm per job (a 5M arm takes about 1,000 s).
9. O9 S1/S2 A1-A2 cite completion's landed work as `[artifact]` (the ledger); A13 (completion's ext4 note) is `[manual]` or a `--repo ../completion` quest.
10. O10 `namespace-s3b` and `namespace-s1-s2` share `disk_store.cc`, `coordinator.cc`, `paged_segment_collection.cc`: one after the other.
11. O11 `pin-wave` carries M1's criteria to a later wave (a judgment); which libraries strata's items moved needs `xapiand-74` (Detent pins no strata).
12. O12 The four adoptions are blocked quests (per the owner's lead prompt); only `resource-adoption` adopts a branch; the track branches are 300+ commits behind.
13. O13 Parked workstreams are `blocked` though the scope is restored; `campaign start` accepts blocked, or set them `planned`.
14. O14 `reader-s6`'s security review: whole diff, or scoped to the code since `Detent:78195dd`.
15. O15 `reader-s6` A7: the exact `reader_bench` command line is not in the sources.
16. O16 Sanitizer gate commands (Apple Clang, `CMAKE_C/CXX_FLAGS`) and their minutes are this mapping's; the first jobs confirm TSan links the operator-new tests.
17. O17 `origin/review/reader-s6-security` does not exist in the clone; the review is on `reader/s6` and main.
18. O18 Owner: legacy step I (flat-v1) removes `image_format`; make it a critical quest or drop it.
19. O19 Owner: W5's 2.6% first-read loss and http-parser strict mode (M1).
20. O20 M1b and M3-M5 have no quests; SC-2/SC-3 segment storage has no quest of its own.
21. O21 History `http-coroutines` has six criteria unticked in its ledger though the lead declared M1 done; recorded as landed with that stated.
22. O22 History quests with no review on record (`legacy-wave2` critical, `reader-s5`, `reader-s1-s4`, `salvage*`, `post-w2`, `m1-pins`) or no hosted run (`reader-acl`, `relay-snapshot-lifecycle`, `reader-s1-s4`): `--earlier` records them as their process left them.
23. O23 History risk: designs and docs are `low`; `legacy-wave2` is `critical`.
24. O24 `namespace-s3` (`5475baf`) and `how-it-works` (`dba0ef8`) are ancestors of main but off its first-parent line (main was fast-forwarded through legacy F/G merges); the ledgers name them.
25. O25 W5's CI run 41835299429 has no terminal result in the ledger.
26. O26 `daemon-ready-leader`: whether the quick-start drops its open retry.
27. O27 `resource-adoption`: the ledger's `allocators` change (1) needs `xapiand-74`; `fix/variant13` is unmerged.
28. O28 `dense-pages-first-read` (3 commits) and `wip/dense-t2-base-parked` are Dense's: adopt as tasks or keep as references.
29. O29 `recovery-delivery`, `prepared-reader`, `prepared-activation` have no ledger; their reviews are the evidence. Startup runtime capacity (`5721d53`) has neither, so no history quest.
30. O30 `map.sh` installs this `PLAN.md` over Codex's Dense-only plan; the dense quest file edit is the lead's (`dense-pages-decision.md`).

## History fidelity: criteria the ledgers left open

Each history journal's Evidence rows now follow its ledger: a criterion the ledger ticked (or
reports done) cites its path; one it left unticked, partial or deferred starts "open:" with the reason and the
ledger path, and every Result ends with "Left open: ...". Checked with the tool's own `evidence`
parser: 12 criteria open across 5 quests; every cited path exists at
`origin/wip/main-parked-2026-11-08`. Scripts and quest files are unchanged.

| History quest | Left open | Why (ledger) |
|---|---|---|
| `http-coroutines` | A2, A3, A4, A5, A8, A9 | Unticked in `docs/status/http-coroutines.md`: T1 (e), T13 authority, T11, authority T6 rows, commit and parse timings, T4/T6 under sanitizers (Next) |
| `m1-pins` | A3, A5 | W5's CI run 41835299429 has no terminal result and no binding check is recorded; the managed first read is 2.6% slower and below target on neither build, left to the owner |
| `reader-acl` | A1, A4 | macOS only, Linux and FreeBSD unqualified (condition C1); no mutant recorded, sanitizer run is a predicate check only |
| `legacy-wave2` | A5 | The leftover-reference grep across docs and website was deferred as a follow-up |
| `client-local-source` | A3 | B1-B6 deferred to legacy step H (`legacy-h`) |
| `post-w2` | none (A1 re-cited) | Its ledger leaves the cluster and consensus pins to Next 1; its landing commit `43d1ce9` pins them and `docs/status/m1-pins.md` records every pin at head, so A1 cites that ledger |

Open items outside these quests' criteria, unchanged: `reader-s1-s4` (its ledger's CI pending, not
a criterion), `reader-design` (owner Q1-Q6 unanswered, lead calls in force), `reader-s3` (the review
re-check awaited), `actors-docs` (owner review named as next step).

## The audit (2026-11-08)

Auditors checked the migration against everything on the laptop; their findings are in
`migration-audit-2026-11-08/` (`detent.md`, `libraries.md`). What they changed:

1. The roadmap: `PLAN.md`'s milestones follow the plans from the first (the client spec, Stages 0
   to 6, M1 to M5, after the release), with 13 history quests for the stages and components that
   finished before ledgers existed and 8 planned quests for M1b to M5 and the designs' later steps.
   The history-only milestones H0 to H4 are gone; 26 quests moved.
2. The library ledgers this campaign owns (task cancellation, reactor task sessions, http W2a and
   its steps, completion's task driver and sync files, sha256's AVX2 kernel): 9 history quests.
3. Results that lived only in `.scratch`: `docs/measurements/` (grouped sync, the reader S6 soak,
   Dense against S1/S2).
4. Fidelity: the four adoption quests carry their release-blocker briefs' criteria (16 restored);
   `post-w2` A1 is open again; D28 and D29 record two owner rulings from the lead's memory;
   `namespace-s1-s2` carries the overseer's open TLS-fixture item.
5. Safety: uncommitted work, detached heads and stashes across the laptop's repositories are on
   `wip/laptop/*` branches on GitHub.
