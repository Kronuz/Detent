# namespace-s3b: Namespace S3b, repair of a damaged pack

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status namespace-s3b`.

## Outcome

A damaged pack heals with no operator action. The scrub reads pack files off the coordinator
thread, retries a failed open or read once, and reports the damaged set; the coordinator asks
upstream for a Replace and publishes a repair at the same cursor that shares no pack with the
damaged generation; every snapshot recovers cold; the collector removes the damaged packs once no
reader leases them, retrying by itself when the lease ends, and a leased damaged root no longer
stops the rest of a collection. A verified page rewritten under the mapping never crashes a read:
header fields are bounds-checked and exceptions map to `SourceFailure`.

## Acceptance criteria

Numbering cites `namespace/s3b:docs/status/namespace-s3b.md` ("criterion n" for the lead's first
seven, K1-K6 for the fix round) and `docs/reviews/namespace-s3b-architect-2026-11-04.md` (L1, C1-C5, AR1-AR5).

- A1 T5's damage (100 flips over the 100K store) heals with no operator action: scrub reports, repair publishes, every read correct, damaged packs collected, next scrub clean (criterion 1): `test_delivery_repair heal` and `test_testing_trusted_restart heal`
- A2 A delta that crosses a damaged page heals and is offered once (criterion 2): `test_delivery_repair delta`
- A3 An unchanged-content repair publishes; a same-cursor ordinary snapshot does not; the relay sink receives `selected_repaired` once and no staging (criterion 3): `test_delivery_repair unchanged`, `relay`, `test_paged_snapshot_plan`
- A4 Other namespaces keep serving: no read that worked fails afterwards and the coordinator never fences (criterion 4): `test_delivery_repair heal`
- A5 A mutant per claim (M1-M12, M8 and M10 by the relay call counts) and a revert check per fix (criterion 5): mutant jobs at the landed code
- A6 [artifact] The ledger with the criteria verbatim and evidence for each (criterion 6): `docs/status/namespace-s3b.md`
- A7 [artifact] The how-it-works update, "Repair of a damaged pack" and the scrub that reads files (criterion 7): `docs/mechanisms/trusted-restart.md`
- A8 K1 (L1, AR3): with the damaged generation leased, a collection removes the other unreachable packs and keeps the leased root's, the failure is visible (not swallowed), and after release one bounded automatic retry removes them, with no retry loop in the test; `leaseclean` attributes the cause: `heal`, `lease-close`, collection variants 14-16, the architect's `lease` and `leaseclean` probes
- A9 K2 (C1, AR1; D17): read-time header bounds and exception mapping in `try_get` and `try_eval`; mapped child-count and body-length rewrites give `SourceFailure`, never a signal or a throw; the architect's 150-trial `q1` probe gives no signal or throw: the `mapped` case and the probe
- A10 K3 (C2, AR2; D18): every snapshot recovers cold, deltas and compactions warm; `t5` updated; publication over a damaged prior recovers; `q2pub` warm and cold numbers recorded: `t5`, the snapshot/delta/progress/compaction cases, the trusted/full-recovery oracle, `q2pub` under `exclusive`
- A11 K4 (C3, AR4): a failed open or read is retried once, a directory-open failure marks no pack, scrub I/O runs off the coordinator thread: `scrub-worker`, the eight syscall-fault cases and real `RLIMIT_NOFILE` exhaustion
- A12 K5 (C4, AR5): same-cursor repair semantics documented; a pending repair clears, or its stall is recorded, when a non-`Repair` publication replaces the selected generation: `repair_reset` and its pending-clear mutant
- A13 K6: each fix is confirmed by the reviewer's original probe and a revert or mutant check (the 17 negative checks die) at the landed code: jobs
- A14 C5: the touched tests pass in separate ASan/UBSan and TSan builds: the `asan` and `tsan` gates

CI on every job, sanitizers included (criterion 5, K6), is the landing's CI gate (REPORT O2).

## Contracts

Provides `PagedPlanIntent::Repair`, `CoordinatorAccess::request_repair`, `PublicationSink::selected_repaired`,
the status fields `repair.pending` and the visible collection error, and the cold recovery of every
snapshot plan. Consumes S3's trusted restart, `.live` and `VerifiedPages`. `dense-pages` A9 and
A13 carry these bounds and the byte-flip regressions onto DTPAGE03.

## Decisions

1. Owner ruling D10: build the repair now. Lead defaults D17 and D18 (handoff owner questions 1
   and 2); the architect calls Q1 the owner's because read results are user-visible (REPORT O4).
2. History: the architect's review is APPROVE WITH CONDITIONS at `Detent:d4692ba` (code
   `Detent:ea74c9f`); L1 and C1-C5 are A8-A14. The new `durability` review is scoped to what
   changed since, above all the deferred-collection scheduling and the asynchronous scrub ordering
   that the original verdict does not cover (ledger Next 3).
3. Not criteria, for the reviewer: AR6's collect-window crash coverage is weak; `request_repair`
   is coordinator memory and a restart re-detects damage at the first scrub pass.
4. Open (lead): this quest and `namespace-s1-s2` change `disk_store.cc`, `coordinator.cc` and
   `paged_segment_collection.cc`; they run one after the other, and the later one merges the other
   (D25).

## May land early

None.
