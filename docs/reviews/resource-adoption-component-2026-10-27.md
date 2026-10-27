# EX-1 resource adoption component review

**Date:** 2026-10-27. **Verdict:** Source GO, conditional on G1–G5 below. No P1/P2 source findings. This ruling covers Detent:39f19a4 against Detent:9d8d2c3, with the public dependency closure from Detent:ac58292. Passing these gates permits landing unchanged source without another approval turn. A failed gate requires attribution and correction; it cannot be waived by this verdict.

**Not run:** Builds, tests, benchmarks or sanitizer jobs during this review. The published canonical implementations remain accepted; inspection covers their forwarding contracts, the nine-file migration and direct reader/storage callers. Context includes the [reconciliation blueprint](../RESOURCE_EXTRACTION_ARCHITECTURE.md#validated-architectural-blueprint-ex-1-reconciliation), calibration, Detent:48d1cab and Detent:b547429.

## Source assessment

1. **Ownership and refunds.** The five distribution headers alias canonical types or forward construction without retaining another engine. `make_storage_owner` forwards both upstream hooks and constructor arguments. `make_storage_resource` preserves account-backed control allocation. The canonical charge retains account state; physical free precedes refund. PagedStorage retains shared state rather than a borrowed facade. Buffer capacity and weak-control lifetime remain charged. The private retirement implementation is deleted, and `detent_resources` propagates the resource and retirement targets with `PROJECT_SOURCE_DIR` includes.
2. **Reservation lifetime and policy.** [disk_ledger.hh:159](../../include/detent_local/disk_ledger.hh#L159) declares the embedded book before its two slots, so reservations die first. Deleted copying also prevents an implicit move that could leave reservations pointing into another object. Reserve checks registry/serial availability before booking; its remaining moves cannot throw. Cancel and settle detach the canonical reservation before slot reset. Settlement computes credit subtraction, additions and reserved subtraction before mutation. Over-quota census, original counts, stale-handle rejection, uncertainty fencing and explicit reclaim remain domain policies. Invalid credit leaves counters unchanged. The book adds no dynamic registry.
3. **Prepared readers and durable state.** [disk_ledger.hh:56](../../include/detent_local/disk_ledger.hh#L56) retains the exact current `prepared_metadata_bytes` and four-entry base formula. ReaderCapacity remains nonmovable and destroys its domain before its runtime. Canonical `uses_account` and `before_ready` satisfy current caller/test contracts. Disk publication still settles after the selection barrier; collection credits after namespace barriers and closure. The migration does not modify file trust, authorization, original-completion matching, authority funding or serialized bytes.
4. **Dependencies and scale.** The closure is exactly allocators:b7daa52f1b6a316e04bba2817f0f527d64d704eb, completion:21809c7ae35d19ee7707692e2d8bae6d8a7a92a8 and journal:dbf753f60d1a74c9a036bf797406aeeb4b7998cc. Consensus:b7073bbe and other pins remain unchanged. Retirement still uses the application-owned fixed runtime and resident limit. Each ByteAccount adds shared-state heap outside its own quota; admitted bytes alone therefore cannot establish process footprint. Rebuilding every consumer is required for changed C++ types/layouts.

## Qualification gates

| Gate | Required terminal evidence |
| --- | --- |
| G1: Functional integration | On the exact migration source and public closure, pass the complete native CTest suite with authority, relay and managed agent enabled. Explicitly include distribution storage/memory-resource/retirement/factory, DiskLedger/disk-store/publication-retirement, storage-session, managed recovery, prepared collection/republish, lazy-page access, reader-capacity, prepared-startup and authority completion cases. Preserve allocation-failure/parent-pressure rollback, stale/repeated completion refusal, weak ownership, pinned views and configured-capacity refusal. Require byte-identical valid protocol/storage fixtures. |
| G2: Platform and races | Pass the six hosted Linux/macOS architecture/compiler/sanitizer jobs for the candidate, plus FreeBSD native managed coverage and focused ASan/UBSan coverage. Record FreeBSD's unsupported LeakSanitizer explicitly. On a supported host, pass TSan for distribution storage, retirement and retirement-factory tests, with no race suppressions introduced. Confirm final-release/close races and retry drainage. |
| G3: Closure and linkage | Record resolved public revisions for every populated dependency, with no local source overrides. Verify consumer compile/link commands contain the canonical retirement translation unit once and no legacy `allocators.cc` or allocation-interception symbols. Include a reader-only build with distributor, authority, relay and managed agent disabled. Confirm the SDK probe's output/behavior under matching flags/link closure. |
| G4: Costs and conservation | Run matched dependency-only/migration ownership and 100,000-owner retirement pairs using the restored identical consumer-named harnesses under `quiet exclusive`. Record CPU/wall distributions, header/arena sizes, upstream calls/bytes and process RSS. Separately account for fixed account/runtime heap and stack. Verify zero residual aggregate charges after runtime shutdown and release of caller handles; use dedicated child accounts for pressure cases. Run paged recovery/collection at 100,000 keys. Repeat the qualified 5M prepared-reader run: retain 200/200 attaches below 1 ms, zero major faults and report steady-read changes. Attribute regressions outside baseline variability; accepted primitive lifetime costs do not establish macro equivalence. |
| G5: Adoption | Commit reproducible raw evidence, exact source identities and private-code deletion inventory; update extraction/release ledgers. Verify the actual main merge's hosted CI to terminal success. |

Inspected local logs show dependency-only smoke suites passing 6/6 and 9/9, and the migration product build completing. These establish neither G1 nor G2. Migration performance evidence remains pending.

CL-2 owner-mode forwarding and `both` activation remain separate. This resource closure retains existing file-only ownership; it does not qualify mixed legacy writers or close the release obligation. HTTP, MessagePack, persistent-map and consensus funding adoption are outside this ruling.

## G4 performance ruling, 2026-10-27

**Verdict: G4 remains NO-GO; source GO is unchanged.** Detent:29bc779 preserves the reviewed production code. The supplied native/platform/sanitizer results close the reported functional work, but they cannot close this performance gate. No builds or measurements run during this ruling.

The counterbalanced `sdk-pairs.json` provides stronger attribution than the sequential profiles: 40 real keys, five rounds, separate per-arm warmup and alternating order under one exclusive hold. Both arms return correct values with zero major faults.

| First verified read | Before | After |
| --- | ---: | ---: |
| Below 1 ms | 196/200 | 195/200 |
| Median | 752.395 µs | 768.515 µs |
| p99 | 1,093.883 µs | 1,056.333 µs |
| Maximum | 1,158.457 µs | 1,551.799 µs |

The median paired difference is **+21.908 µs**, with attachment and lookup contributing paired medians of +9.532 and +9.415 µs. Those medians are not additive. Paired process CPU rises 24 µs, minor faults rise four and RSS rises 16 KiB. Eight of the nine samples at or above 1 ms also record at least 1 ms process CPU; the candidate maximum records 1.612 ms CPU. Process CPU includes the retirement worker, so it neither isolates caller execution nor supports attributing these tails solely to descheduling. Extra faults are a hypothesis, not a proved cause. The paired steady-read median difference is −44.41 ns; that does not cancel first-read cost.

My original 200/200 wording carries a historical sample result into a repeatability gate. Both-arm failure demonstrates that it is not an unconditional scheduling guarantee. It does **not** establish an acceptable replacement threshold: the [source requirement](../FAST_CLIENT_ARCHITECTURE.md) still measures construction through the first verified value below 1 ms on resident data. This ruling changes neither that target nor G4 to a median-only, percentile-only or relative tolerance selected after seeing results. The migration regression is not disproved.

Other costs are explicit and explainable at the component boundary. Account heap grows from 80 bytes/one allocation to 128 bytes/two allocations per shared account, with zero residual; root admission adds 8.679 ns and arena construction 22.65 ns. Page/charged-allocation headers remain 48/32 bytes. Retirement CPU rises about 1.3% with unchanged admitted peak and fixed runtime allocation. Paged recovery preserves operation counts while its heap peaks add 208–432 bytes; collection adds 384 admitted bytes. Recovery RSS medians add 256 KiB; collection RSS medians rise from 72,122,368 to 74,657,792 bytes. These measurements support conservation and quantify cost, not global speedup or equivalence. Raw evidence is under `.scratch/resource-adoption-20261027/{sdk-pairs.json,ownership-pairs/result.json,paged-pairs/result.json}` and must be committed with the qualification record.

**Next action, bounded:**

1. Instrument the same diagnostic probe in both arms to separate source-factory construction, Client construction and first lookup. Record caller-thread CPU alongside wall time, faults and context switches; inspect allocation/runtime-startup activity separately. Preserve the public-path timing boundary. Run one fixed batch of the existing 40 keys in two counterbalanced rounds. Diagnostic instrumentation is not acceptance timing.
2. Use that evidence to locate the added work. A fix may change resource initialization/allocation or first-access integration while preserving canonical ownership, accounting, trust checks and runtime lifetime. Do not add another engine, move required initialization outside the timer, suppress tails or expand the dependency closure to chase this number.
3. After an attributed fix, run one fresh, uninstrumented 200-pair batch with the existing controller and all samples retained. Recheck changed failure/lifetime paths and the affected platform gates. If one diagnostic pass cannot identify a justified fix, report that limit and the measured tradeoff; do not repeat unchanged trials until one passes. A numeric criterion change requires an explicit performance decision with its own rationale.

## G4 diagnostic attribution ruling, 2026-10-27

**Verdict: attribution is insufficient for a justified implementation fix. G4 remains NO-GO.** Detent:dadfab7 retains the reviewed production implementation. This ruling inspects the common diagnostic probe, controller and 320 recorded samples; it runs no builds or trials. The [raw record](../measurements/resource-caller-diagnostic-20261027.json) and [probe](../../benchmarks/resource-qualification/diagnostic/probe.cc) match the inspected scratch files byte-for-byte.

The batch contains 80 before/after pairs in each observer mode. Unhooked phase measurements give these median paired differences:

| Phase | Wall time | Caller-thread CPU | Process minor faults |
| --- | ---: | ---: | ---: |
| Source factory | +1.422 µs | +1.438 µs | +1 |
| Client construction | −6.351 µs | −6.187 µs | −1 |
| First lookup | +8.944 µs | +8.966 µs | +4 |

Caller CPU tracks the lookup increase, so that observed difference is not explained solely by time off the calling thread. Faults and context switches use `RUSAGE_SELF`, however, and include worker activity. They cannot identify which thread or memory region faults. The median involuntary-switch counts are unchanged in every phase.

The observer-mode allocation counts are source factory 2→2, Client construction 75→79 and lookup 34→34. The four additional Client allocations are consistent with the shared-state allocation for each of the three default accounts in [view.cc](../../src/detent_local/view.cc) and the child verification-cache account in [lazy_page_access.cc](../../src/detent_local/lazy_page_access.cc). That matches the already measured account lifetime cost. Allocation counts do not identify stacks, requested sizes or page touches, and these counts come from the separate instrumented mode. They do not prove that account construction causes the first-lookup increase.

The paired median sum of diagnostic phase times rises 3.443 µs. It does not supersede the uninstrumented end-to-end increase of 21.908 µs. The probe inserts CPU/rusage observations between phases, warms its CPU-clock path, and changes code/layout and execution boundaries. Its allocation-logging mode additionally installs and smoke-checks a malloc observer. These are useful attribution tools, not interchangeable acceptance measurements. Summing phase medians also does not recover an end-to-end median.

The remaining plausible explanations include first-touch/code-layout effects and different allocation placement. The evidence identifies no redundant allocation, duplicated validation, incorrect cache miss, avoidable faulting region or other specific operation whose removal preserves the accepted contracts. Coalescing account state, removing the child quota or changing retirement initialization would therefore be speculative changes. Neither the original regression nor the sub-millisecond target is disproved.

**Next action:** preserve this diagnostic and stop unchanged measurement retries. Keep the candidate isolated and present an explicit performance decision with the measured costs and unresolved first-read attribution. Retaining the target means G4 stays open; further work needs a separately bounded investigation that identifies responsible allocation/fault addresses or stacks before proposing one local change. Accepting this extraction with a documented first-read exception requires an explicit owner decision that names the exception and its release consequence. This review grants no such exception and supplies no replacement numeric threshold.

No canonical library or integration edit is justified by this batch alone. Functional, lifetime and platform evidence remains valid for the unchanged candidate; it does not substitute for the unresolved performance decision.
