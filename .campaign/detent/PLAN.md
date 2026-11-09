# detent: plan

The owner writes Goals, Non-goals and Reserved decisions; the lead keeps the rest current. Quest
state is in `state.json` (`campaign status`, the quest log); this file holds what the state cannot.
Sources, oldest first: the client spec `justknobs-implementation-spec.md` (build milestones 1-9,
2026-09-29); the stage plan `docs/VALIDATED_ARCHITECTURE.md` and `docs/IMPLEMENTATION.md` (Stages 0-6,
2026-10-02); `docs/STAGE6_ARCHITECTURE.md` (Stage 6 components, 2026-10-14); `docs/RELEASE_PLAN.md`
(release gates from 2026-10-18, exits RC-1 to RC-5 from 2026-10-27) and `docs/EXTRACTION_CHECKLIST.md`;
the lead's plan `Detent:aa00b0e:docs/PLAN.md` (M1-M5, 2026-11-02) as last kept at `Detent:7c007ce`; its
handoff `docs/handoffs/detent-codex-2026-11.md` (2026-11-04); the ledger
`Detent:8c76f7e:docs/status/campaign-lead.md`; Codex's Dense campaign (`docs/campaign/PLAN.md`); and the
public roadmap `website/src/content/docs/roadmap.md`.

## Goals

Release Detent. Detent is in Stage 6, release qualification (`docs/RELEASE_PLAN.md`); the release
is the five exits of its remaining checklist, on the Kronuz libraries at their heads (lead plan
`Detent:7c007ce`, Goal). Each exit stays open until its recorded evidence passes:

- RC-1 Required integration: EX-1 resources, EX-2 HTTP clients, EX-3 bounded MessagePack, EX-4 persistent map, CL-2 owner-mode forwarding, finalized consensus transport, asynchronous authority/relay HTTP log completion. Closure: reviewed public pins, deleted private engines, compatible bytes, failure and lifetime checks, each component recorded separately.
- RC-2 Client performance and updates: first-read repeatability, incremental refresh, retained-reader behavior, daemon startup attribution. Closure: the sub-millisecond resident-data first read held, measured limits recorded, daemon preparation measured apart from attachment.
- RC-3 Sustained capacity: ingestion calibration, the raw/zstd 500/2k/10k-session ladder, traffic and retry amplification, a mixed-pressure soak, sustained 5M. Closure: a published operating envelope on the actual topology; the exploratory 10M does not raise the ceiling.
- RC-4 Failure and restoration: a fresh three-voter restore under a new epoch; combined failures, receipts and resource conservation. Closure: complete-state and durable recovery checks pass.
- RC-5 Composed release candidate: the supported-platform and sanitizer matrix, clean installation, service lifecycle, upgrade and rollback. Closure: the integrated source and exact public dependency closure qualified, installation and measured limits documented.

Targets that stand (`docs/SCALE_TARGETS.md`): 1M knobs (5M qualified, 10M exploratory), 10k+
publishers, 250k+ agents, sub-millisecond warm reads. The release blockers of 2026-10-23
(`docs/handoffs/README.md`) close inside those exits: SC-5 containers (closed by EX-0), SC-2/SC-3
segment storage scaling and SC-4 ingestion waits (RC-3), resource primitives, HTTP clients,
bounded MessagePack and the persistent map (RC-1); SC-1 relay lifecycle is integrated with its
sustained load open (RC-3).

The roadmap, in order (the Milestones table below is the same list):

1. The client library, per the JustKnobs spec's build milestones 1-9: landed 2026-09-29.
2. Stages 0 to 5 of the validated architecture: accepted 2026-10-02 to 2026-10-14.
3. Stage 6, system qualification, active since 2026-10-14. Its first components landed by
   2026-10-29; on 2026-10-27 its remaining work became the five exits RC-1 to RC-5 above. The lead's
   plan of 2026-11-02 routes Stage 6 to the release through:
   1. M1 On the tip: done 2026-11-04.
   2. N1: the reader core, namespace delivery and legacy removal, landing in the handoff's order
      (D25): reader S6, legacy H, damaged-pack repair and grouped syncs.
   3. Dense pages: the DTPAGE03/DTROOT03 format that shrinks the 5M store and its bootstrap (RC-2
      first reads, RC-3 storage and ingestion, SC-2/SC-3).
   4. N2: the handoff's follow-ups get evidence-backed dispositions; the shared-library pin wave
      lands after upstream CI and coordination (lead ledger criterion 6).
   5. M1b: coroutine drivers for journal and consensus, then Detent's owner loops on them, the old
      Worker API deleted (`docs/SHARED_LIBRARY_DECISIONS.md`, November 2).
   6. M2 to M5: the exits RC-1 to RC-5 themselves.
4. After the release: the public roadmap's open items (`website/src/content/docs/roadmap.md`), when
   the owner schedules them.

Throughout: main's required CI is green, and ledgers, mechanisms and qualification limits match the
landed tree (lead ledger criterion 7).

## Non-goals

- AWS, cloud machines or any billable resource (D16).
- Legacy modes, shims, deprecated aliases or a second way to do one thing (D7).
- Raising the supported ceiling from the exploratory 10M trial; new optimizations as release requirements without a demonstrated blocker against an exit (`docs/RELEASE_PLAN.md`).
- The rejected btree rewrite, extractions without a second consumer, new storage frameworks (`docs/RELEASE_PLAN.md`).
- A Homebrew tap bump without the owner's word (D22).

## Constraints

- No AWS: local VMs (Linux ext4, SSH port 18772; FreeBSD, 18774) under `QUIET_VM=<pid> quiet strict`,
  or hosted CI.
- `quiet` for all machine work: builds and tests `light` with at most 4 jobs; timings `strict`;
  numbers that decide or get published `exclusive`; strict and exclusive holds at most 30 minutes.
- Identity: commit as `Germán Méndez Bravo <german.mb@gmail.com>`, push as `Kronuz`, remote `github`
  (`origin` is stale). No AI or tool attribution anywhere.
- Merge `main` into branches; never force-push a shared branch. Before landing anything "as docs",
  `git diff --stat main...BRANCH` must show no code.
- CI: branch pushes do not trigger `ci.yml`; dispatch it. Done is every job green on the tree that
  lands; rerun a macOS arm64 job that never starts.
- Stay on the tip; one shape; every library consumer is ours (D7). A pin moves only after that
  library's CI is green; shared-library API or pin changes are agreed with the Xapiand lead
  (`xapiand-74`) and recorded in `docs/SHARED_LIBRARY_DECISIONS.md`.
- Builds on macOS use Apple Clang: `-DCMAKE_CXX_COMPILER=/usr/bin/clang++ -DCMAKE_C_COMPILER=/usr/bin/clang
  -DCMAKE_OSX_SYSROOT=`. Scratch in `.scratch/`, never `/tmp`.

## Candidate uses (owner's idea, 2026-11-08; not a goal until the owner says so)

Detent as a general replicated lookup table, beyond configuration: GeoIP-style range data, IP and
domain blocklists, routing and tenant-to-shard maps, revocation sets, rate-limit and pricing tables.
Staying generic needs, above the page format (DTPAGE03 and the trie are byte-generic already):
1. Binary-safe keys through an order-preserving escape (0x00 as `00 FF`, terminator `00 01`) instead of a bare NUL.
2. Ordered reader lookups: floor, ceiling, prefix scan and longest-prefix match.
3. An opaque bytes value type beside the typed knobs.
Until then, no design may assume text keys or NUL-free names.

Framing: an engine for rapidly distributing indexed lookup databases to large fleets, with local
access to every entry and incremental publication of immutable generations. Fit (a peer's table,
2026-11-08): excellent for flags, authorization policy, gateway routing and static dictionaries;
strong for localization; conditional for service discovery (publication latency is unmeasured;
RC-2) and package metadata; weak for large catalogs until namespace subscriptions (S4-S10) and
page fetch by digest (dense-distribution) deliver slices; poor for telemetry and counters.

## Reserved decisions

The owner decides these; everything else the lead decides and records below.

- Goals, non-goals and significant scope changes.
- Removing or narrowing user-visible behavior (the read path's results included: D17, D21).
- Public APIs and formats the owner reserves: the `detent_*` C ABI and the language bindings,
  DTPAGE03/DTROOT03, the full-copy wire format (D20), `detent`/`detentd` command-line behavior.
- Commitments to other teams (the Xapiand campaign's shared libraries), new external services or
  repositories, anything that costs money; the Homebrew tap.

## Documents

What the product is goes to `docs/` of the repository it describes, never to `.campaign/`: designs
(`docs/design/`), research (`docs/research/`), measured results (`docs/perf/`), mechanisms
(`docs/mechanisms/`), reviews before this campaign (`docs/reviews/`), release gates
(`docs/RELEASE_PLAN.md`, `docs/EXTRACTION_CHECKLIST.md`), shared-library agreements
(`docs/SHARED_LIBRARY_DECISIONS.md`). The ledgers in `docs/status/` and the handoffs in
`docs/handoffs/` are history from the earlier skill; journals replace ledgers. Notes about running
the campaign go to `.campaign/detent/`.

## Milestones

| Milestone | Quests | Validation (a check that runs on `main`) |
|---|---|---|
| Spec milestones 1-9: the client library | client-spec | Done 2026-09-29 (history): 14 ctest suites, Python and Rust binding suites, ASan/UBSan and TSan clean (`Detent:f789380`) |
| Stage 0: architecture and contracts | stage-0-architecture | Done 2026-10-02 (history): Architect review, CONDITIONAL GO |
| Stage 1: legacy Raft safety | stage-1-raft-safety | Done 2026-10-02 (history): each defect reproduced first; cluster CTests native and ASan/UBSan; Xapiand compatibility matrix |
| Stage 2: durable storage and strict consensus | stage-2-durable-consensus | Done 2026-10-04 (history): crash, torn-write, failed-sync, partition and recovery invariants; consolidated Architect GO |
| Stage 3: bounded transactions and authority runtime | stage-3-authority-runtime | Done 2026-10-04 (history): concurrent histories, retry, delete/recreate and restart consistency; consolidated Architect source GO |
| Stage 4: portable distribution and immutable local views | stage-4-distribution | Done 2026-10-09 (history): Architect component acceptance with platform and sanitizer qualification |
| Stage 5: subscriptions, relays and incremental publication | stage-5a-subscriptions, stage-5b-relays, stage-5c-incremental-publication | Done 2026-10-14 (history): 5A, 5B and 5C each accepted after a consolidated review |
| Stage 6: qualification components (to 2026-10-29) | managed-failover, fleet-runner, library-adoption-ae, startup-runtime-capacity, reader-acl, relay-snapshot-lifecycle, seeding-waits, container-selection, recovery-delivery, prepared-reader, prepared-activation | Done 2026-10-29 (history, Codex era): hosted runs and component reviews per quest; the remaining Stage 6 components are the exits RC-1 to RC-5 (M2-M5) |
| M1 On the tip | m1-pins, http-coroutines, post-w2, salvage, salvage-followups, retained-state | Done 2026-11-04 (history): pin-check 0 mismatches; CI run 41796189324 green on every job |
| N1 Reader core, namespace delivery, legacy removal and guides (handoff order 1-4) | reader-design, reader-s1-s4, reader-s3, hash-once, reader-s5, reader-s6, namespace-design, namespace-s3, namespace-s3b, namespace-s1-s2, client-local-source, legacy-wave2, legacy-h, actors-docs, how-it-works, quickstart-managed | Full native suite with authority, relay and bindings, plus ASan/UBSan and TSan builds, as jobs on `main`; `tools/check_pins.py` 0 mismatches; hosted `ci.yml` green on `main` |
| Dense pages | dense-pages, dense-sync-stack | Native, sanitizer/decoder, 100K/5M model and reader acceptance jobs on the complete feature; accepted results repeated on `main` (serves RC-2 and RC-3) |
| N2 Handoff follow-ups | daemon-ready-leader, pin-wave | As N1, plus the pin wave's input-class identity diff on `main`; follow-up dispositions recorded below |
| M1b Async drivers | async-drivers | Designs reviewed by the architect and the Xapiand lead; old Worker API deleted |
| M2 Integration (RC-1) | resource-adoption, http-client-adoption, msgpack-adoption, persistent-map-adoption, rc1-integration | Each lands or is rejected with evidence; composed full suite and sanitizers on `main` |
| M3 Client performance (RC-2) | rc2-client-performance, reader-s7-s9 | Sub-millisecond resident-data first read held, 200/200, exclusive, on `main` |
| M4 Capacity and restoration (RC-3, RC-4) | rc3-sustained-capacity, namespace-s4-s10, rc4-restoration | Published operating envelope; fresh three-voter restore |
| M5 Release candidate (RC-5) | rc5-release-candidate | Platform/sanitizer matrix, clean install, service lifecycle, upgrade and rollback; tagged release |
| After the release: the public roadmap | (none yet: `website/src/content/docs/roadmap.md`, Knob Model to Qualification, when the owner schedules them) | none until scheduled |

The rows follow the plans' own order. A finished Stage 6 component sits in the Stage 6 row; the
exits it feeds (RC-1 to RC-5) are M2 to M5. The work landed between M1 and the handoff (reader,
namespace and legacy designs and steps, and the guides) sits with the quests that finish it, in N1.

Not quests yet (dispositions owed under N2, lead ledger criterion 6, or work the M2-M5 quests split out):

1. Flakes 3, 4, 5 and 8 in `docs/status/http-coroutines.md` (8: TSan `test_local_paged_publication_artifacts`).
2. T9 and T4(7) measurements and the period-64 wire run (`docs/status/namespace-design.md` Next 2).
3. Legacy step I, flat-v1 removal (`explore/legacy-inventory` cut order I; legacy-wave2 ledger Next).
   It removes the `image_format` option, so it is critical risk and the owner's to confirm.
4. H's leftovers: Python on a free-threaded build, fork in the Rust tests, the unused
   `atomic-shared-ptr` link (legacy-wave2 ledger, Step H).
5. Fixtures that still cannot start on port 0 (`test_multivoter.py`, `tests/detent_system/{authority,managed}.py`,
   `test_raft_cluster.cc`; `docs/status/post-w2.md`, Blocked on 2). The daemons' port race (salvage
   item 1) is closed by post-W2's bound-port report.
6. W2 code-review follow-ups CR3-CR6 (`docs/reviews/w2-s1-s8-code-review.md`).
7. M1 W5's two open owner items: the 2.6% managed first-read loss and http-parser strict mode
   (`docs/status/m1-pins.md`, W5 Blocked on).
8. Parked candidates without a quest, for M3: the reader metadata capsule (`track/reader-capsule`,
   `track/resource-capsule`, ledgers `docs/status/{reader,resource}-capsule.md` there; 5M acceptance
   fails three of four rows; D27), the discarded ancestor-storage and directory-trust trials
   (`track/reader-ancestor-storage`, `track/reader-directory-trust`), the client-read baseline
   (`diagnostics/client-read`); the scheduler wakeup fix (upstream `scheduler:36afada`, `34631ec`;
   confirm, then retire); `explore/agent-startup` (an unmerged code probe; its note is cited by
   hash-once).
9. Later build steps of the designs: now `reader-s7-s9` (M3) and `namespace-s4-s10` (M4).
10. Online capacity increases: blueprint parked (`docs/status/README.md`, 2026-10-29), for M4.
11. Startup runtime capacity is the history quest `startup-runtime-capacity`. Managed CLI reader
    capacity landed before the 2026-10-23 index with no ledger; its security fix is `reader-acl`, its
    evidence `docs/reviews/calibration.md` and `reader-capacity-security-2026-10-23.md`.

## Decisions

A `[decision]` criterion's evidence names a row's ID here.

| ID | Date | Decision | By | Why |
|---|---|---|---|---|
| D1 | 2026-11-05 | Only Dense pages; implement before whole-feature reviews, then architect merge ruling. Scope superseded by D4 | Owner | Previous broad campaign wasted the feature budget |
| D2 | 2026-11-05 | Adopt full-build quest plus reads, deltas and integration tasks | Lead (Codex) | Components are one persistent-format change and cannot land independently |
| D3 | 2026-11-05 | Preserve conditional contract approval as scoped history | Lead (Codex) | It authorizes building, not landing |
| D4 | 2026-11-08 | The owner restores the full scope; Dense pages is one quest among the campaign's. D1's scope is superseded; its review process (whole-feature reviews, then the architect's merge ruling) stays for `dense-pages` | Owner | The lead takes back its whole plan; the handoff's landing order (D25) applies again |
| D5 | 2026-10-27 | The remaining reusable-library extractions EX-1 to EX-4 are required before release | Owner | `docs/RELEASE_PLAN.md` |
| D6 | 2026-11-02 | Resource accounting: one shared implementation in `allocators`; Detent deletes its copies; the first-read gap is fixed where the cost is | Owner | `docs/SHARED_LIBRARY_DECISIONS.md`, November 2 |
| D7 | 2026-11-02 | Stay on the tip; one shape everywhere (`task::task` the one coroutine type); every library consumer is ours | Owner | Lead plan, "Rules shared with the Xapiand campaign" |
| D8 | 2026-11-03 | Namespace history as compacted blocks, cut by cost | Owner | `docs/status/namespace-design.md` |
| D9 | 2026-11-03 | Adopt the `retained_state` object and the stricter replacement-pool funding check | Owner | `docs/status/retained-state.md` |
| D10 | 2026-11-04 | Build the damaged-pack repair now (S3b option b), ahead of S7 | Owner | "Yes, build the damaged-pack repair" |
| D11 | 2026-11-04 | Starting with no source configured is a hard error, nonzero exit | Owner, lead chose | The owner accepted this or an empty snapshot with a warning |
| D12 | 2026-11-04 | Drop the unmanaged agent and the legacy pieces with it; roadmap item 4 dropped | Owner | legacy-wave2 ledger, decision 7 |
| D13 | 2026-11-04 | Step H takes option B: remove both legacy Client sources, `source_poll` and `source_path` | Owner | legacy-wave2 ledger, decision 8 |
| D14 | 2026-11-04 | Delete `detent::Client` and `health()`; `Reader` is the one read API in every language (Q1 (a), Q2 (a)) | Owner | `docs/status/client-local-source.md` |
| D15 | 2026-11-04 | Start the dense-page design | Owner | Lead summary, owner rulings |
| D16 | 2026-11-04 | No AWS of any kind | Owner | Handoff rule 3 |
| D17 | 2026-11-04 | S3b Q1: read-time page-header bounds; any exception from a verified page except `bad_alloc` and `StoragePressure` maps to `SourceFailure` | Lead (handoff default); owner may overrule | Removes the SIGSEGVs of AR1; read results are user-visible |
| D18 | 2026-11-04 | S3b Q2: recover every snapshot plan cold; deltas and compactions stay warm | Lead (handoff default); owner may overrule | Removes AR2's fence at no extra hashing |
| D19 | 2026-11-04 | Dense Q1: keep the pack checksum pass at build | Lead (handoff default); the architect may overrule | Handoff owner question 3 |
| D20 | 2026-11-04 | Dense Q2: a full copy ships records; shipping files is a later design | Lead (handoff default); owner reserves the wire format | Files cost about 20 times the bytes |
| D21 | 2026-11-04 | Legacy H: `detent get` stays on `GenerationReader` and loses `--defaults` until reader S7; `CMAKE_POSITION_INDEPENDENT_CODE` on for the whole build | Lead (handoff default); owner may overrule | The core lacks generation-identity and capacity open options |
| D22 | 2026-11-04 | The Homebrew tap stays at `Detent:323b971` until the owner says | Owner | Handoff owner question 6 |
| D23 | 2026-11-04 | `detentd --daemonize` reports ready only once a leader exists | Lead (handoff default) | It reports ready about 2 s early |
| D24 | 2026-11-05 | Temporary ownership of the completion-declaration pin update: `journal:e5f6ff1`, `consensus:ab05507` | Owner | Closes S1/S2's pin-check; `docs/SHARED_LIBRARY_DECISIONS.md`, November 4 |
| D31 | 2026-11-08 | Revises D30: Dense pages lands right after S1/S2; reader S6 and S3b follow it and merge it (S3b's byte-flip tests run on DTPAGE03 then, instead of before Dense). Dense resumes now on its branch beside S1/S2 (`--force`: it merges S1/S2 before landing). Dense distribution (shipping pages, not records) gets its own design quest now | Lead (owner: "fastest and most efficient") | Dense is the owner's priority and the furthest-reaching change (5M agent selected 748.6 to 360.8 s measured; 316 MB of packs); landing it before S6 and S3b means each of those rebases once onto the final format instead of Dense rebasing over both |
| D32 | 2026-11-09 | namespace-s1-s2's A12 (the perf table) moves to quest sync-groups-perf, after S1/S2 lands; the mechanism note (A11) is written before the final jobs; the A9 exclusive arms run after the four gates | Lead | A docs commit changes job identity, so docs written after the 14 exclusive timings would force rerunning them all; a pending exclusive also blocks new light holds, so timings queued before the gates delay the gates |
| D33 | 2026-11-09 | The owner withdrew "agents don't have to build anything". dense-distribution finishes as a measured design record (its fixes and evidence are nearly done); no build quest starts from it without a goal | Owner; lead on finishing | Finishing costs one document round and six model jobs; dropping it would discard measured receiver-cost results the reader and namespace quests can use |
| D34 | 2026-11-09 | Dense distribution: Q2 (b), the full copy ships as the transfer in wire form (pages, no build; settles reserved D20 for that design); Q3 (a), per-fragment compression stays, per-segment measured for both encodings before any change; Q4 (b), a deployment with no relay runs a relay on the authority's host | Owner (accepted the lead's recommendations) | 25.7 MB against 25.6 MB with no receiver build; one framing serves both encodings; builds stay out of the consensus process |
| D35 | 2026-11-09 | O4: D17 and D18 stand. O5: D21 stands (`detent get` loses `--defaults` until reader S7). O18: legacy step I (flat-v1 removal, the `image_format` option) is approved, to run after N1 lands. O19: W5's 2.6% managed first-read loss is accepted without a bisect, and `detentd` keeps http-parser strict mode | Owner (accepted the lead's recommendations) | Dense pages replaces the first-read path; strict parsing is the guide's default and safer |
| D36 | 2026-11-09 | Dense distribution DD-16 (c): the stale page pull is dropped. A receiver above the cut gets records from blocks; below the cut, the full copy. A7's measurements stay in the document as the record, with the cases where a pull would pay (ring-only relays before namespace S8, early disk-cap cuts); DD-14 and DD-15 close with the pull | Owner ("drop the pull if it doesn't pay off") | Records beat the pull above the cut by 4.6 to 45.6 times; below the cut the pull loses to the copy from about 24 h, and after S8 the cut is 15 to 102 days back |
| D37 | 2026-11-09 | Knob observability (usage per knob, resolved values and ramp arms, defaults taken, view freshness, export) becomes a goal; quest observability-design designs it first, starting now beside the M1 path, since the reader's hot path (S5-S7) is where it must fit | Owner (goal); lead (order) | A design costs agents, not machine time, which is the bottleneck; designing before the reader core freezes avoids reopening it |
| D38 | 2026-11-09 | Observability Q7: exact per-knob counting is preferred if it can be made cheap; quest observability-design first searches for an exact mechanism that fits OB1's gate (≤ 5 ns at 1 thread, ≤ 10 ns at 8 and 64 threads, paired); if none does, sampled counting (option a) stands. Q1-Q6 stay open until the owner reviews them | Owner ("Exact counting might be better, if we could optimize things so they don't cost as much. Otherwise, I guess sampled could work.") | Exact counts answer dead knobs per host and ramp splits without sampling error; the measured exact arms cost +23 to +64 ns, so the mechanism, not the goal, is open |
| D30 | 2026-11-08 | Landing order revised: namespace S1/S2 first, then reader S6, namespace S3b (shares `disk_store.cc`, `coordinator.cc`, `paged_segment_collection.cc` with S1/S2), legacy H after S6, Dense pages after all four. One contributor at a time until `campaign usage` has Claude rows | Lead | S1/S2's evidence is complete (5M cold bootstrap 999-1001 s to 545 s, 222/222 ctest, ASan/TSan 10/10) and it needs only a durability review, the main merge and CI; S6 has open security findings. Dense was measured on top of S1/S2 (`docs/measurements/dense-cold-bootstrap-20261107/`). Supersedes D25's order, keeps its S6-before-Dense reason |
| D25 | 2026-11-04 | Landing order: reader S6, legacy H, namespace S3b, namespace S1/S2 (S3b and S1/S2 in either order, the later merges the other), then Dense pages | Lead | Handoff; S6 and Dense S3 both change `resolve` and `lookup` |
| D26 | 2026-11-03 | Reader Q1-Q6 all (a); RD-5: namespace delivery adopts D4's `change` shape; `change` slot 0 only until namespace-delivery S7 | Lead; owner may reverse | `docs/status/reader-design.md`, reader-s5-s6 ledger |
| D27 | 2026-10-29 | One reader capsule prototype, with coordinated rollout and changed refusal behavior | Owner | `docs/status/README.md` (2026-10-29) |
| D28 | 2026-11-04 | The lead may push and land on any Kronuz cpp-library repository, cpp-libraries included, once that library's CI is green; tell the Xapiand lead before changing shared files such as `.github/workflows/cmake.yml` | Owner | detent-d5 memory `cpp-libraries-push-authority` (2026-11-04); not in any ledger |
| D29 | 2026-11-03 | A client pays only for what it is missing; an owner proposal is rejected only on engineering grounds with numbers that beat it | Owner | detent-d5 memory `owner-rules-are-constraints`; `docs/status/namespace-design.md` owner ruling (2026-11-03) |
