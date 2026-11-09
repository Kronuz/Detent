# resource-adoption: EX-1, one resource-accounting implementation in allocators

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status resource-adoption`.

## Outcome

Detent's byte accounts and charges, allocation adapters, deferred retirement and `DiskLedger`
bodies are deleted; `allocators` at its head, the revision completion declares, is the one
implementation behind Detent's existing names. Storage bytes and failure behavior are unchanged,
and the sub-millisecond resident-data first read holds: a measured cost is fixed in `allocators`,
where it lives.

## Acceptance criteria

Sources: `docs/handoffs/resource-primitives.md` (the release-blocker brief, 2026-10-23); `phase/resource-adoption:docs/status/resource-adoption.md`; `docs/EXTRACTION_CHECKLIST.md`
EX-1 and "Each item requires"; `docs/RELEASE_PLAN.md` RC-1; D6.

- A1 Detent's private accounting, allocation, retirement and disk-ledger bodies are gone and the `allocators` types serve every caller (D6; EX-1): a build-and-grep job
- A2 `allocators` is pinned at its head, the same revision completion declares, and `tools/check_pins.py` reports 0 mismatches (D6): the `pins` gate
- A3 [manual] `allocators`' tests and supported-platform CI are green at the pinned revision (EXTRACTION_CHECKLIST, "Each item requires"): the lead names the run
- A4 The full native suite, ASan/UBSan and TSan pass on immutable public sources with no local override (EXTRACTION_CHECKLIST): the `native`, `asan` and `tsan` gates
- A5 Storage bytes are unchanged: the `DiskLedger` census and serialization, and captured payloads, equal the baseline's (RC-1 "compatible bytes"): a job
- A6 Failure and lifetime checks: the `DiskLedger::settle` mutant dies; retirement drains under pressure (resource-adoption ledger, Evidence; RC-1): jobs
- A7 Resident-data first reads of migrated and baseline agree within the A/A band at 200 samples per arm under `exclusive`, and both keep the sub-millisecond target (EX-1 gate; D6): jobs
- A8 [artifact] Measured CPU and memory costs, before and after, with the A/A (EXTRACTION_CHECKLIST): `docs/perf/resource-adoption.md`
- A9 [manual] The `macos-arm64` failure of `test_local_paged_segment_collection` variant 13 is explained and fixed in the test (resource-adoption ledger, Blocked on; `fix/variant13`, `Detent:febdef6`): the lead names the hosted runs
- A10 [artifact] EX-1's row records the library revision, consumer commit, deleted mechanism, results and exceptions (EXTRACTION_CHECKLIST, Resume 6): `docs/EXTRACTION_CHECKLIST.md`
- A11 [manual] Library side (handoff, Acceptance): parent refusal, overflow, allocation failure, unresolved reservations, cross-owner release where supported, cancellation, shutdown and final conservation; admitted heap kept apart from RSS, mappings and allocator overhead: the lead names the allocators commit and CI run
- A12 Optional resource targets enable no unrelated allocator interception and fetch no unrelated dependencies (handoff): a configure job
- A13 Default footprint and sustained retirement costs are measured before and after, losing cases included (handoff): `exclusive` jobs
- A14 Acknowledgment timing is unchanged with the durable and wire bytes (handoff): a job

## Contracts

Consumes `allocators`' `ByteAccount`, `ByteCharge`, charged allocator and PMR resource,
`OwnedBuffer`, retirement and `ReservationBook<3>`. Keeps `include/detent_distribution/{storage,allocator,buffer,memory_resource,retirement}.hh`
as aliases only if one shape allows it (D7); deletes the private engines.

## Decisions

1. D5 and D6: required before release; the gap is a regression to fix, not a reason to keep a copy.
2. Not taken here (resource-adoption ledger, Decisions 1): the authority retained-state funding of
   `Detent:33d36bf`, landed separately as `retained-state` (D9).
3. Open (lead, with `xapiand-74`): the ledger's proposed `allocators` change (1), one non-template
   out-of-line body for the hot paths; `allocators` is shared.
4. The branch `phase/resource-adoption` (`Detent:6d2cb2a`) was reconciled on `Detent:6a31280`
   (2026-11-02) and is far behind `main`; it is adopted, and its first step is to merge `main`.

## May land early

None.
