> Status: CURRENT (2026-11-03) Ledger for the namespace delivery design workstream.

# namespace-design

- Design: docs/design/namespace-delivery.md; review: docs/reviews/namespace-delivery-preliminary.md (APPROVE WITH CONDITIONS, `Detent:eae3f6c`).
- Branch / worktree: `design/namespace-delivery` at `~/code/Kronuz/Detent-wt/namespace-design`; base `Detent:d4abd4a`. Revision: `design/namespace-rev` at `~/code/Kronuz/Detent-wt/namespace-rev`; base `Detent:633f6fb`.
- State: designing (preliminary review folded in; ready for re-check of L3-L7). S1-S4 may start under L1 and L2.
- Lead rulings (2026-11-03):
  - L1: one store directory. D8's per-namespace directories go; the startup barrier stays one directory sync on every platform, so S1 needs no primitive spanning directories. Completion's sync and order primitives accept read-only descriptors (ND-R6).
  - C2: S2 and S3 keep today's random file ids and single directory until L6 closes.
- Done criteria (set by the lead; contributors tick, never edit):
  - S1 (completion `SyncFiles` and `OrderFiles`):
    - [ ] 1. The asynchronous driver runs `sync_files_and_directory` and `order_files_and_directory` as one primitive each, over a list of read-only or read-write descriptors and one directory.
    - [ ] 2. Seam tests on every CI platform (Linux, macOS arm64 and x86_64, FreeBSD where completion runs it) record the exact call sequence per platform: fsync or fdatasync per file, then one device flush (`F_FULLFSYNC` on macOS) and one directory sync; the recorded sequences are committed as expected values, not counts.
    - [ ] 3. Each platform's semantics stated in completion's docs: what reaches stable storage after the call returns, and what an error on one file means for the rest.
    - [ ] 4. A mutant that drops the device flush or the directory sync fails a seam test; sanitizers (ASan+UBSan, TSan) in separate builds; completion CI green; the pin moves in Detent with CI green.
  - S2 (D11 on today's store):
    - [ ] 1. Publication syncs in the grouped order owner records, barrier; payloads, barrier; table and change word, barrier (ND-R9), through S1's primitives.
    - [ ] 2. The census issues no per-file sync (`disk_store.cc:133`, `:171` gone) and one startup barrier; `ready()` stays after that barrier.
    - [ ] 3. Sync counters (files synced, device flushes, directory syncs) per publication and per startup, exposed to tests and the bootstrap harness.
    - [ ] 4. T7 on today's store: an interruption at every primitive of publication and of the startup barrier, plus restart-then-republish cycles, each followed by simulated power loss through completion's seam; every file the durable table reaches exists and verifies. Revert check: restoring a per-file order that breaks the invariant fails T7.
    - [ ] 5. Restart and bootstrap timed at 10K and 100K under `quiet exclusive`, before and after, against the attribution's numbers.
  - S3 (D10 on today's store):
    - [ ] 1. Restart reads `storage.meta`, the table and the roots and `.read` files against their digests, with no pack read and no page hash; a counter proves zero page hashes at startup.
    - [ ] 2. First-touch checks: every page the agent's update path reads from a base generation is hash-checked against its parent reference before use.
    - [ ] 3. The scrub hashes every pack at `scrub_bytes_per_second`, after start and every 24 h, and a test proves its pace within 10%.
    - [ ] 4. T5 over a 100K store: 100 single-byte flips in random pages; restart is ready; first touch fails that read with a source failure and never returns a wrong value; the scrub reports every untouched flip within one pass; reads recover after repair. A corrupt table or `.read` refuses the store.
    - [ ] 5. A mutant that skips the first-touch check fails T5; sanitizers in separate builds.
  - S4 (D12.2):
    - [ ] 1. No tier re-validates its own build, and the relay does not re-hash on recovery; frame SHA-256 stays.
    - [ ] 2. Hash counters per tier; at 10K and 100K, bytes hashed per tier at most 1.2 times bytes built.
    - [ ] 3. A test proves a corrupted pack is still caught downstream (by S3's first touch or scrub), so dropping the re-validation leaves no silent gap.
  - All four: CI green on every job; a how-it-works note per changed mechanism (sync groups, trusted restart and scrub); measured rows only on main.
- Done: the full draft of docs/design/namespace-delivery.md (D3-D12, strata fit with proposals P1-P3, reader alignment, T1-T8, W1-W12, alternatives, limits, Q1-Q5, S1-S9), owner rulings D1 and D2 kept verbatim; folds in the lead's three mid-task inputs (the owner's compacted blocks, the strata evaluation and its bar, the Xapiand lead's replication directions) (`Detent:bb1d31f`).
- Done (revision): preliminary review folded in on `design/namespace-rev`: L1 one directory (D8, D11); L3 D3 and T8 under Q5 (d), group count deferred to a new S10; L4 D6 (result records per entry, sliding window with a floor and range-bounded Replace, history on disk as strata proposal H1, merging P1 and P2; P3 out); L5 wire list W4-W8 (producers, one session endpoint per Q4 (c), range boundaries and live tail, incarnations, relays as readers); L6 file ids (layout version, profile and build ordinal in the id, `linkat` from a temporary name, verified reuse); L7 read-only adoption and uid handling (D9); ND-R8, ND-R9, ND-R11; Q1-Q5 record the lead calls; a "Review findings" table (`Detent:c4a1a6e`).
- Doing: nothing; stopped at re-check.
- Next:
  1. Architect re-check of L3-L7 (L1 and L2 are the lead's ruling and S2's criteria).
  2. H1 to strata's owner and the Xapiand lead; owner confirms or reverses the lead calls on Q1-Q5.
  3. reader.md adds the new-incarnation case (Reader alignment A4, W7).
  4. Lead: S3 criterion 3 sets a 24 h scrub; ND-R8 estimates a one-hour pass at about 0.2% of a core. Tighten if wanted (D10).
- Blocked on:
  - S5.2 on H1 (strata's owner and the Xapiand lead).
  - S6 on S5; Q1 and Q4 are lead calls the owner may reverse.
  - S10 on strata Track C2 D12 built, the shared turn log, and T8 measured on Linux.
- Findings folded in: ND-R1 to ND-R11, L1-L7, C1-C2 (design's "Review findings" table).
- Design vs code: not applicable (nothing built).
- Last verified: every new file:line re-read on 2026-11-03 at `Detent:d4abd4a` (`src/` unchanged at `Detent:633f6fb`), `completion:576618d`, `consensus:61149a2`, `strata:9d52824`; nothing built, run or measured.


## Lead calls under "move fast" (2026-11-03; the owner may reverse any)
- Q1 (b) limited to exact names, whole-component prefixes and `*`; the security reviewer reviews the matcher.
- Q2 (a) with lagged retention and a floor (ND-R2); Q3 (a) with the residual in Limits.
- Q4 (c): one session endpoint with a wait time; `poll` is a zero wait. One path, no second mode.
- Q5 (d): each namespace's position is its own counter assigned at apply; S6-S9 build on today's single group; the group count is chosen once strata C2 exists and T8 is measured on Linux.
- D8 takes the reader's `change` shape (reader D4, RD-5).

## Log
- 2026-11-03: Worktree created from `github/main` at `Detent:d4abd4a`. Read the brief, the startup attribution (`explore/agent-startup` at `Detent:e8d4e59`), the reader draft (`Detent:a3bb22f`), ACTORS, RELAY, AUTHORITY_EXPORT, DELIVERY_INTEGRATION_ARCHITECTURE, SCALE_TARGETS, STORAGE_BARRIER_ARCHITECTURE, the legacy inventory and the cited code.
- 2026-11-03: First draft used state-plus-tombstones history; replaced by the namespace journal (D6) after the lead relayed the owner's one-replication-mechanism rule and the Xapiand lead's directions. Strata evaluated: replication shape adopted, core and segments not used for delivery or the agent store.
- 2026-11-03: Revision on `design/namespace-rev` from `Detent:633f6fb`: folded L1-L7, ND-R1 to ND-R11 and C1-C2 into the design; status line and review table updated; the review file untouched.
