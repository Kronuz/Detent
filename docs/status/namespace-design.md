> Status: CURRENT (2026-11-04) Ledger for the namespace delivery design workstream.

# namespace-design

- Design: docs/design/namespace-delivery.md; review: docs/reviews/namespace-delivery-preliminary.md (APPROVE WITH CONDITIONS, `Detent:eae3f6c`).
- Branch / worktree: `design/namespace-delivery` at `~/code/Kronuz/Detent-wt/namespace-design`; base `Detent:d4abd4a`. Revision: `design/namespace-rev` at `~/code/Kronuz/Detent-wt/namespace-rev`; base `Detent:633f6fb`. Second revision: `design/namespace-rev2` at `~/code/Kronuz/Detent-wt/namespace-rev2`; base `Detent:35e4b82`. Third revision: `design/namespace-rev3` at `~/code/Kronuz/Detent-wt/namespace-rev3`; base `Detent:e9ae326`. Blocks revision: `design/compacted-blocks` in the agent worktree `.claude/worktrees/agent-a297f0637e0378f0f`; base `Detent:9b24fb3`.
- State: ready to land: the architect approved the roll-up with conditions (RU-C1 to RU-C5), all folded in with the lead's calls on RU-C3, RU-3 and RU-C4; nothing new is open beyond α on T9 and the measurements T4(7) and `wire2.out` owe. S1-S4 may start under L1 and L2.
- Lead rulings (2026-11-03):
  - L1: one store directory. D8's per-namespace directories go; the startup barrier stays one directory sync on every platform, so S1 needs no primitive spanning directories. Completion's sync and order primitives accept read-only descriptors (ND-R6).
  - C2: S2 and S3 keep today's random file ids and single directory until L6 closes.
- Done criteria (set by the lead; contributors tick, never edit):
  - S1 (completion `SyncFiles` and `OrderFiles`):
    - [ ] 1. The asynchronous driver runs `sync_files_and_directory` and `order_files_and_directory` as one primitive each, over a list of read-only or read-write descriptors and one directory.
    - [ ] 1a. Opening an existing file gains a read-only mode (today `posix.h:731` opens `O_RDWR`), so the census and collector never open a seeded file for writing (N4a).
    - [ ] 2. Seam tests on every CI platform (Linux, macOS arm64 and x86_64, FreeBSD where completion runs it) record the exact call sequence per platform: fsync or fdatasync per file, then one device flush (`F_FULLFSYNC` on macOS) and one directory sync; the recorded sequences are committed as expected values, not counts.
    - [ ] 3. Each platform's semantics stated in completion's docs: what reaches stable storage after the call returns, and what an error on one file means for the rest.
    - [ ] 4. A mutant that drops the device flush or the directory sync fails a seam test; sanitizers (ASan+UBSan, TSan) in separate builds; completion CI green; the pin moves in Detent with CI green.
  - S2 (D11 on today's store):
    - [ ] 1. Publication syncs in the grouped order owner records, barrier; payloads, barrier; the new table written under a temporary name and fsynced, renamed over `selected`, directory synced, then `change` incremented with no sync (D11, N6), through S1's primitives. Revert check: renaming before the fsync fails T7.
    - [ ] 2. The census issues no per-file sync (`disk_store.cc:133`, `:171` gone) and one startup barrier; `ready()` stays after that barrier.
    - [ ] 3. Sync counters (files synced, device flushes, directory syncs) per publication and per startup, exposed to tests and the bootstrap harness.
    - [ ] 4. T7 on today's store: an interruption at every primitive of publication and of the startup barrier, plus restart-then-republish cycles, each followed by simulated power loss through completion's seam; every file the durable table reaches exists and verifies. Revert check: restoring a per-file order that breaks the invariant fails T7.
    - [ ] 5. Restart and bootstrap timed at 10K and 100K under `quiet exclusive`, before and after, against the attribution's numbers.
  - S3 (D10 on today's store):
    - [ ] 1. Restart reads `storage.meta`, the table and the roots and `.read` files against their digests, with no pack read and no page hash; a counter proves zero page hashes at startup.
    - [ ] 2. First-touch checks: every page the agent's update path reads from a base generation is hash-checked against its parent reference before use.
    - [ ] 3. The scrub hashes every pack at `scrub_bytes_per_second`, continuously at that pace (ND-R8: an hourly pass at 1.51 GB costs about 0.2% of a core), and a test proves its pace within 10%.
    - [ ] 4. T5 over a 100K store: 100 single-byte flips in random pages; restart is ready; first touch fails that read with a source failure and never returns a wrong value; the scrub reports every untouched flip within one pass; reads recover after repair. A corrupt table or `.read` refuses the store.
    - [ ] 5. A mutant that skips the first-touch check fails T5; sanitizers in separate builds.
  - S4 (D12.2):
    - [ ] 1. No tier re-validates its own build, and the relay does not re-hash on recovery; frame SHA-256 stays.
    - [ ] 2. Hash counters per tier; at 10K and 100K, bytes hashed per tier at most 1.2 times bytes built.
    - [ ] 3. A test proves a corrupted pack is still caught downstream (by S3's first touch or scrub), so dropping the re-validation leaves no silent gap.
  - All four: CI green on every job; a how-it-works note per changed mechanism (sync groups, trusted restart and scrub); measured rows only on main.
- Done: the full draft of docs/design/namespace-delivery.md (D3-D12, strata fit with proposals P1-P3, reader alignment, T1-T8, W1-W12, alternatives, limits, Q1-Q5, S1-S9), owner rulings D1 and D2 kept verbatim; folds in the lead's three mid-task inputs (the owner's compacted blocks, the strata evaluation and its bar, the Xapiand lead's replication directions) (`Detent:bb1d31f`).
- Done (revision): preliminary review folded in on `design/namespace-rev`: L1 one directory (D8, D11); L3 D3 and T8 under Q5 (d), group count deferred to a new S10; L4 D6 (result records per entry, sliding window with a floor and range-bounded Replace, history on disk as strata proposal H1, merging P1 and P2; P3 out); L5 wire list W4-W8 (producers, one session endpoint per Q4 (c), range boundaries and live tail, incarnations, relays as readers); L6 file ids (layout version, profile and build ordinal in the id, `linkat` from a temporary name, verified reuse); L7 read-only adoption and uid handling (D9); ND-R8, ND-R9, ND-R11; Q1-Q5 record the lead calls; a "Review findings" table (`Detent:c4a1a6e`).
- Done (second revision): re-check N1-N6 folded in on `design/namespace-rev2`: N1 orphan names unlinked and rebuilt (D8.4, T7); N2 result records per command (D6); N3 one held Replace per namespace per server, built at head on demand, replacing the Replace at each boundary (D6, D7, W6, T4's bound, alternatives); N4 no `.lease` in an image, a read-only existing-file open in completion, owners checked first and the fallback at the next ordinal (D9, S1, S9); N5 the head piece served from R + 1 (D6, T3, W6); N6 table fsync before rename (D11, T7) and a new incarnation as a generation switch on the same namespace (A4, W7, reader.md A4, the authorized cross-document edit); H1's three asks in "Strata fit" (`Detent:6ac5ae2`).
- Done (third revision): re-check 2 folded in on `design/namespace-rev3`: N7 Replace builds queue into cycles through the one distribution capture, capped by `replace_cycle_bytes`, pinned memory at most one cycle's writes, a 4,096-namespace bootstrap in ⌈ΣC / `replace_cycle_bytes`⌉ cycles (D6); N8 an open cycle pins the horizon at its capture until its answers are composed, rebuild bound per effective window (D6, Limit 3); N9 T4 bound of W plus S − 1 entries plus records and index; N10 foreign table and files never read, bootstrap at ordinal 1 with a fresh table, foreign `change` replaced as D8's one exception (D8, D9); N11's six clarifications (D6 "Replace", "Holding", "Selection", relay horizon, T4, T6) (`Detent:22f54a3`).
- Done (blocks revision): the owner's ruling folded in on `design/compacted-blocks`: P1 ("a client pays only for what it is missing") with a table of how each decision serves it; D6 rewritten as three tiers (ring, compacted blocks, base) with seals at half the ring, squash by ratio 4 and a merge limit, a cut by cost and by disk with α from T9, derived-block answers, the proof of trimmed and merged blocks, model numbers, checkpoints at a durable seal, recovery that refills the ring; D7 relays mirror blocks, base and ring; H1 withdrawn and the Strata fit rows on segments marked open; T3, T4 rewritten, T9 added; Q2 recorded as the owner's ruling; Limits, Alternatives, W3, W6-W8, S5, S7, S8 and the findings table updated. Wording forced elsewhere: D8 (the absent-key tombstone as a no-op; "a build from a full copy"), D9 and D10 ("full copy" for "Replace"), T6, A4. The arithmetic is `docs/design/namespace-delivery-blocks-model.py`, committed with the revision.
- Done (blocks fold): the blocks review folded in on `design/compacted-blocks`. D6: seals on changes or log progress (8,192 entries or 8 MiB), counted from the entries, empty seals as checkpoint points (NB-L1); every layout input canonical or replicated: canonical sizes, the policy in `Limits` changed by the "set limits" entry and carried in each seal file, apply's own accumulator feeding seals instead of the ring, the first base at the enabling or restore entry (NB-L2); squash with a deduplicating pass past L, the cut by cost over whole-block answers against |st(s)| and a separate disk cap at 2α (NB-L3); one seal file per group seal, block and base files at 1 MiB and more, the `layout` table, the commit, collector, recovery and install on D8's rules, the gate as a durable tiling (NB-L4, NB-C4); lazy ring eviction (NB-C5). D3: positions start at the stored revision (NB-C1). D7: relays adopt the upstream's layout (NB-C5). T3, T4 (crash matrix, liveness, capture failure, shapes, disk with caches and base rewrite), T9, W3, W6, W8, Limits 2 and 5, alternatives, S5 and the findings log updated. The model script is now a key-level simulation (`seal` and `layout`), sealing by size alone (NB-C6) (`Detent:c4922ed`).
- Done (re-check fold): NR-1 log-progress thresholds derived from the group's consensus limits in force, set in the configuration at the enabling entry, with a retained-bound check (D6 "Seal", T4.6); NR-2 α as a ratio of integers, the "set limits" entry as an S5.2 step, placement a function of the replicated policy; NB-L3 the cap "apart from the newest block", 1.25 α typical and about 2α plus the newest block as the ceiling, blocks bounded against L when built, 40,000-key figures labelled, 1,200-key rows in D6 and T4(8); NR-3 first table at the enabling entry, roll-up digests in seal files, peer fetch or install for rotted files, no re-fold below W, install acknowledged after its files; NB-C2 disk-figure line; the checkpoint captured in apply at the seal (S5, `node.cc:519-530`). NR-4: the packing is gone; a namespace rolls up its level-0 sections every 16 seals or at L, so seal files die whole. Model: roll-up in `layout`, small namespaces, and a new `files` mode (`.scratch/blocks-fold/layout2.out`, `files.out`) (`Detent:59a024e`).
- Done (roll-up conditions): RU-C1 seal ordinal in apply's state, checkpoint, snapshot, seal files and table; RU-C2 table only at period seals, collector checks the table reaches none of a seal file's sections; RU-3 outputs durable before the table, not the checkpoint; RU-C3 staggered roll-ups and a carry under 4 KiB for at most 2 periods, files and flushes per platform, T4(7) with `completion_sync_probe` on APFS and ext4 in a local VM; RU-C4 level-0 merged at answer time, hot-key worst restated for stores 2-32 seals behind, T4(8) samples them; RU-C5 the five wording fixes. Model: `files` mode compares the shapes with dead bytes and files sampled every seal; `layout` merges level-0 at answer time, carries, and samples 2-32 seals behind (`.scratch/blocks-fold/files2.out`, `layout3.out`). Commit: see the log.
- Doing: nothing; stopped at ready to land.
- Next:
  1. The lead lands `design/compacted-blocks`.
  2. The lead runs T9 to set α from time, and T4(7)'s `completion_sync_probe` on APFS and in a local Linux VM, which decide whether the roll-up period moves from 16 to 64 (with `wire2.out`).
  3. The owner confirms or reverses the lead calls on Q1, Q3, Q4, Q5, B1-B3, RU-C3, RU-3 and RU-C4.
- Blocked on:
  - α on T9 (the lead measures; designers run no benchmarks).
  - S6 on S5; Q1 and Q4 are lead calls the owner may reverse.
  - S10 on strata Track C2 D12 built, the shared turn log, and T8 measured on Linux.
- Findings folded in: ND-R1 to ND-R11, L1-L7, C1-C2, N1-N11, the re-check's H1 points, NB-L1 to NB-L4, NB-C1 to NB-C6, NR-1 to NR-4 and RU-C1 to RU-C5 (design's "Review findings" table).
- Design vs code: not applicable (nothing built).
- Last verified: the re-check fold's new file:line (`node.cc:22-25,519-530`; `consensus/include/consensus/core.h:163,316-318,726-727`) re-read on 2026-11-04 at `Detent:2b9208d` and `consensus:61149a2`; the blocks fold's at `Detent:bd52cc3` (`src/` unchanged since `Detent:9b24fb3`) and `consensus:61149a2`; earlier: the blocks revision at `Detent:9b24fb3`, and every new file:line on 2026-11-03 at `Detent:d4abd4a` (`src/` unchanged at `Detent:633f6fb`), `completion:576618d`, `consensus:61149a2`, `strata:9d52824`; nothing built, run or measured; the model is a simulation of stated shapes.


## Lead calls under "move fast" (2026-11-03; the owner may reverse any)
- Q1 (b) limited to exact names, whole-component prefixes and `*`; the security reviewer reviews the matcher.
- Q2 (a) with lagged retention and a floor (ND-R2): superseded by the owner's ruling of 2026-11-03 (compacted blocks, cut by cost). Q3 (a) with the residual in Limits.
- Q4 (c): one session endpoint with a wait time; `poll` is a zero wait. One path, no second mode.
- Q5 (d): each namespace's position is its own counter assigned at apply; S6-S9 build on today's single group; the group count is chosen once strata C2 exists and T8 is measured on Linux.
- D8 takes the reader's `change` shape (reader D4, RD-5).

## Owner ruling (2026-11-03)
- P1: a client pays only for what it is missing. An agent with a store of any age gets the blocks it lacks; a full copy only for a client with no store or one so far behind that the copy is cheaper.
- Mechanism: compacted blocks per namespace (last write per key, tombstones), sealed by size, squashed coarser with age and size; the in-memory ring (4,096 transactions, 16 MiB) and the relay's exact edges stay the hot path; history cut by cost (α · base, α from time parity), with a cap on blocks per answer. Q2 superseded.
- Strata parks its own evaluation of the same principle; the segment rows in "Strata fit" are deferred: the owner holds strata's evaluation, and D6 decides on its own model (Xapiand lead's note: per-segment delete bitmaps in a manifest-relative install).

## Strata H1 (2026-11-03; withdrawn by the blocks revision)
- Withdrawn: Detent needs no apply-output interface in consensus (its recorder exists), no compaction gate (the checkpoint is taken at a durable seal) and no journal reads by non-members. The Xapiand lead held the agreement below pending the revised D6.
- Agreed in shape by the Xapiand lead: Xapiand uses H1 for change capture (resume by log index) and for lagging replicas (replay within the horizon, checkpoint install beyond it).
- Conditions: the journal itself, no parallel delta store; horizon and byte budget explicit, inside D13's node budget; a reader past the horizon gets a typed "too old" answer, never a gap; no reader holds back compaction (a slow reader loses its place).
- P3 refused by Xapiand, so it stays out. The strata architect reviews H1's design before it is built; S5.2 waits for it.

## Log
- 2026-11-03: Worktree created from `github/main` at `Detent:d4abd4a`. Read the brief, the startup attribution (`explore/agent-startup` at `Detent:e8d4e59`), the reader draft (`Detent:a3bb22f`), ACTORS, RELAY, AUTHORITY_EXPORT, DELIVERY_INTEGRATION_ARCHITECTURE, SCALE_TARGETS, STORAGE_BARRIER_ARCHITECTURE, the legacy inventory and the cited code.
- 2026-11-03: First draft used state-plus-tombstones history; replaced by the namespace journal (D6) after the lead relayed the owner's one-replication-mechanism rule and the Xapiand lead's directions. Strata evaluated: replication shape adopted, core and segments not used for delivery or the agent store.
- 2026-11-03: Revision on `design/namespace-rev` from `Detent:633f6fb`: folded L1-L7, ND-R1 to ND-R11 and C1-C2 into the design; status line and review table updated; the review file untouched.
- 2026-11-03: Second revision on `design/namespace-rev2` from `Detent:35e4b82`: folded the re-check's N1-N6 and H1 points; edited reader.md A4 (the lead's call on the incarnation case); the review file untouched.
- 2026-11-03: Third revision on `design/namespace-rev3` from `Detent:e9ae326`: folded re-check 2's N7-N11; the review file untouched.
- 2026-11-03: Blocks revision on `design/compacted-blocks` from `Detent:9b24fb3`: owner ruling (P1, compacted blocks, ring as hot path, cut by cost and time parity) folded into D6, D7, Strata fit, T3, T4, T9, Q2, Limits, Alternatives, S5-S8; model script added; the review file untouched; nothing built, run or measured beyond the arithmetic model.
- 2026-11-04: Blocks fold on `design/compacted-blocks` from `Detent:bd52cc3`: folded the blocks review's NB-L1 to NB-L4 and NB-C1 to NB-C6, re-decided B1 as whole blocks, replaced the model with a key-level simulation (run under `nice -n 19`; output in `.scratch/blocks-fold/layout.out`); the review file untouched (`Detent:c4922ed`).
- 2026-11-04: Re-check fold on `design/compacted-blocks` from `Detent:2b9208d`: folded NR-1 to NR-3, NB-L3's wording and NB-C2's line; replaced the seal-file packing with a roll-up after the model's `files` mode showed it writes 1.8 to 3.4 times the raw changes against the packing's 5.0 to 11.6, at 121 to 245 files per seal over 4,096 namespaces; the review file untouched (`Detent:59a024e`).
- 2026-11-04: Roll-up conditions on `design/compacted-blocks` from `Detent:5a69d9c`: folded RU-C1 to RU-C5 with the lead's calls on RU-C3, RU-3 and RU-C4; model `files` and `layout` modes extended; `wire2.out` had not landed (the reviewer's queue), so period 64 stays an open option; marked ready to land; the review file untouched.

## Lead calls, 2026-11-04 (owner: "move fast"; the owner may reverse each)

1. B1: an answer below the ring is one block derived for [R + 1, s] and cached per (namespace, incarnation, R, s), not the persisted blocks served whole: each changed key once, one request; images share R.
2. B2: the disk rule stays beside the cost cut (blocks above the base at most α · base), accepting that a store cut by it pays up to 1/α of its answer.
3. B3: seals trigger on size only; age squashing follows positions. Revisit if T9 shows quiet namespaces holding an unsealed ring too long.

Revised by the blocks fold (2026-11-04), for the lead to confirm:

1. B1 becomes persisted blocks served whole: the block holding R + 1 trimmed to revisions above R, then every younger block as stored. On the review's numbers (NB-6) B1 read 1.51 times its answer on average (11.6 at worst) with a merge and a cache entry per R; whole blocks send 1.25 times (1.45) with no merge, shared across R. The owner's original picture; T3's bound (at most the changes counted per transaction) still holds.
2. B2 is restated: the cut is by cost over the whole-block answer against |st(s)|, so a store cut by it pays at most 1/α of its answer at the cut (model: 0.98 to 1.02 at α = 1); the disk rule becomes a separate cap at 2α · |st(s)|, which fired in none of the model's runs. The old "up to 1/α" was about 4/α (NB-3).
3. B3 keeps size-only seals for blocks and adds log progress (8,192 entries or 8 MiB), so a group with few positioned entries still checkpoints (NB-1).

## Lead calls, 2026-11-04 (after the blocks re-check)

1. B1 revised: persisted blocks served whole replace derived per-R blocks (NB-C3, the reviewer's numbers: 1.25x wire with no merge against 1.51x mean and 11.6x worst server reads). This matches the owner's original picture.

## Lead calls, 2026-11-04 (the roll-up pass; the owner may reverse each)

1. RU-C3: staggered roll-ups (namespace N at (ordinal + d(N)) mod 16 = 0) plus a carry of roll-ups under 4 KiB, at most 2 periods in a row: the architect's 1.9-2.9 times fewer files, no extra writes, dead at most 2.8%. Files and flushes stated per platform: Linux fsyncs every file, macOS issues one flush per group. T4(7) measures with `completion_sync_probe` at 256-4,096 files of 1 KiB on APFS locally and on ext4 in a local Linux VM; no AWS.
2. RU-3: roll-up outputs must be durable before the table that reaches them, not before the checkpoint.
3. RU-C4: option (b), level-0 sections merged at answer time: no writes or files; hot-heavy 1.32 times mean and 2.31 worst against 1.54 and 3.21 served whole, for stores 2-32 seals behind, which T4(8) samples.
