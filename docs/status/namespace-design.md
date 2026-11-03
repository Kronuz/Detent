> Status: CURRENT (2026-11-03) Ledger for the namespace delivery design workstream.

# namespace-design

- Design: docs/design/namespace-delivery.md; review: docs/reviews/namespace-delivery-preliminary.md (APPROVE WITH CONDITIONS, `Detent:eae3f6c`).
- Branch / worktree: `design/namespace-delivery` at `~/code/Kronuz/Detent-wt/namespace-design`; base `Detent:d4abd4a`. Revision: `design/namespace-rev` at `~/code/Kronuz/Detent-wt/namespace-rev`; base `Detent:633f6fb`. Second revision: `design/namespace-rev2` at `~/code/Kronuz/Detent-wt/namespace-rev2`; base `Detent:35e4b82`.
- State: designing (re-check N1-N6 folded in; N3 is a new Replace mechanism, ready for its check). S1-S4 may start under L1 and L2.
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
- Doing: nothing; stopped for the check of N3.
- Next:
  1. A rechecker on N3 (new mechanism) and on N1 and N4b in the crash model; the lead confirms the rest.
  2. H1 to the strata architect with its three asks; owner confirms or reverses the lead calls on Q1-Q5.
  3. Lead: S1's criteria do not list completion's read-only existing-file open that D9 now needs (N4a); S2 criterion 1 says "table and change word, barrier", while D11 now fsyncs the temporary table, renames, syncs the directory, then increments `change` without a sync (N6). Edit the criteria if wanted.
- Blocked on:
  - S5.2 on H1 (strata architect's review, then build).
  - S6 on S5; Q1 and Q4 are lead calls the owner may reverse.
  - S10 on strata Track C2 D12 built, the shared turn log, and T8 measured on Linux.
- Findings folded in: ND-R1 to ND-R11, L1-L7, C1-C2, N1-N6 and the re-check's H1 points (design's "Review findings" table).
- Design vs code: not applicable (nothing built).
- Last verified: every new file:line re-read on 2026-11-03 at `Detent:d4abd4a` (`src/` unchanged at `Detent:633f6fb`), `completion:576618d`, `consensus:61149a2`, `strata:9d52824`; nothing built, run or measured.


## Lead calls under "move fast" (2026-11-03; the owner may reverse any)
- Q1 (b) limited to exact names, whole-component prefixes and `*`; the security reviewer reviews the matcher.
- Q2 (a) with lagged retention and a floor (ND-R2); Q3 (a) with the residual in Limits.
- Q4 (c): one session endpoint with a wait time; `poll` is a zero wait. One path, no second mode.
- Q5 (d): each namespace's position is its own counter assigned at apply; S6-S9 build on today's single group; the group count is chosen once strata C2 exists and T8 is measured on Linux.
- D8 takes the reader's `change` shape (reader D4, RD-5).

## Strata H1 (2026-11-03)
- Agreed in shape by the Xapiand lead: Xapiand uses H1 for change capture (resume by log index) and for lagging replicas (replay within the horizon, checkpoint install beyond it).
- Conditions: the journal itself, no parallel delta store; horizon and byte budget explicit, inside D13's node budget; a reader past the horizon gets a typed "too old" answer, never a gap; no reader holds back compaction (a slow reader loses its place).
- P3 refused by Xapiand, so it stays out. The strata architect reviews H1's design before it is built; S5.2 waits for it.

## Log
- 2026-11-03: Worktree created from `github/main` at `Detent:d4abd4a`. Read the brief, the startup attribution (`explore/agent-startup` at `Detent:e8d4e59`), the reader draft (`Detent:a3bb22f`), ACTORS, RELAY, AUTHORITY_EXPORT, DELIVERY_INTEGRATION_ARCHITECTURE, SCALE_TARGETS, STORAGE_BARRIER_ARCHITECTURE, the legacy inventory and the cited code.
- 2026-11-03: First draft used state-plus-tombstones history; replaced by the namespace journal (D6) after the lead relayed the owner's one-replication-mechanism rule and the Xapiand lead's directions. Strata evaluated: replication shape adopted, core and segments not used for delivery or the agent store.
- 2026-11-03: Revision on `design/namespace-rev` from `Detent:633f6fb`: folded L1-L7, ND-R1 to ND-R11 and C1-C2 into the design; status line and review table updated; the review file untouched.
- 2026-11-03: Second revision on `design/namespace-rev2` from `Detent:35e4b82`: folded the re-check's N1-N6 and H1 points; edited reader.md A4 (the lead's call on the incarnation case); the review file untouched.
