> Status: CURRENT (2026-11-03) Ledger for the namespace delivery design workstream.

# namespace-design

- Design: docs/design/namespace-delivery.md; review: docs/reviews/namespace-delivery-preliminary.md (APPROVE WITH CONDITIONS, `Detent:eae3f6c`).
- Branch / worktree: `design/namespace-delivery` at `~/code/Kronuz/Detent-wt/namespace-design`; base `Detent:d4abd4a`.
- State: reviewed; S1-S4 may start under L1 and L2; D3, D6, D8, D9 and the wire list revise (L3-L7) before S5-S9.
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
- Doing: nothing; stopped at preliminary review.
- Next:
  1. Architect preliminary review; fold findings in by ID.
  2. Owner answers to Q1-Q5; P1-P3 agreed or refused by strata's owner and the Xapiand lead.
  3. S1-S4 can start once approved (no open question blocks them).
- Blocked on:
  - S5 on Q5 and Q2 (owner, with the Xapiand lead) and P1 (consensus/journal owners).
  - S6 on Q1 and Q4 (owner) and P2.
  - S9 on Q3 (owner).
  - P3 (compacted blocks) on whether Xapiand wants it; Detent-only otherwise, so out.
- Findings folded in: none.
- Design vs code: not applicable (nothing built).
- Last verified: every file:line re-read on 2026-11-03 at `Detent:d4abd4a`, `completion:576618d`, `consensus:61149a2`, `strata:9d52824`; nothing built, run or measured.

## Log
- 2026-11-03: Worktree created from `github/main` at `Detent:d4abd4a`. Read the brief, the startup attribution (`explore/agent-startup` at `Detent:e8d4e59`), the reader draft (`Detent:a3bb22f`), ACTORS, RELAY, AUTHORITY_EXPORT, DELIVERY_INTEGRATION_ARCHITECTURE, SCALE_TARGETS, STORAGE_BARRIER_ARCHITECTURE, the legacy inventory and the cited code.
- 2026-11-03: First draft used state-plus-tombstones history; replaced by the namespace journal (D6) after the lead relayed the owner's one-replication-mechanism rule and the Xapiand lead's directions. Strata evaluated: replication shape adopted, core and segments not used for delivery or the agent store.
