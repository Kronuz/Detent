# Validated Architectural Blueprint: shared resource primitives

**Date:** 2026-10-18. **Status:** Isolated implementation and consolidated source review complete except for one permit-metadata correction; application and platform qualification active.

Extend the existing `allocators` library with optional resource targets. Preserve two accounting mechanisms: concurrent allocation charges and single-executor reservations. Keep I/O lifecycle and durable-proof rules in their domain wrappers. No new ledger repository is justified by the inspected catalog and source.

## Targets and dependency boundaries

Add header-only `allocators::resources` and optional compiled `allocators::retirement`. Resource-only consumption must neither build nor link `allocators.cc` or enable global allocation interception. Keep the existing allocator target and tracking behavior unchanged. Retirement adds thread and platform dependencies only when selected.

```
allocators::resources
  ├─ Allocation ownership → completion, HTTP, Detent
  ├─ Reservation book    → Journal, DiskLedger
  └─ allocators::retirement → Caller-owned cleanup
```

## Shared mechanisms

| Primitive | Contract |
| --- | --- |
| Fixed-size resource vector | Checked componentwise fit, addition and subtraction; no allocation or domain policy |
| `ByteAccount` and move-only `ByteCharge` | Concurrent byte admission, immutable parent hierarchy, rollback on parent refusal and successful-admission peak accounting; charges retain account state |
| `ReservationBook<N>` and move-only `Reservation<N>` | Single-executor committed/reserved accounting; explicit reserve, grow, cancel, settle and reclaim; unresolved destruction preserves debt and marks abandonment |
| Allocation adapters | Owning allocation context, STL allocator and charged allocator/PMR resource; shared admission and physical-free ordering |

Keep the reservation API limited to `try_reserve(peak, spendable)`, `try_grow(reservation, minimum_peak, spendable_delta)`, `cancel`, `settle(reservation, additions, proved_credit)` and `reclaim(proved_credit)`. Spendable capacity lets Journal enforce its protected pools without encoding admission classes in the shared book. Compute domain-side changes before committing. Accounting invokes no policy callbacks.

## Domain wrappers

Journal retains protected pools, ticket-slot protection, admission classes, `mark_started`, uncertainty and fencing. Its existing replacement-growth operation delegates accounting while preserving replacement identity and durable settlement.

DiskLedger retains its named-space census, generation footprint calculations, serial/slot registry, original-operation counts and proof of durable deletion. Its opaque handles reference slots containing shared reservations. Completion retains I/O ownership and completion correlation. Detent retains configured limits, pressure diagnostics and application lifecycle policy.

Wrappers consume the shared accounting transitions rather than copy their arithmetic. Avoid a universal ledger template parameterized by concurrency, refund and settlement policies.

## Allocation lifetime

An allocation context strongly retains its PMR resource, and each charged allocation retains its account independently of the initiating facade. Admit the full upstream extent before allocation, including headers, capacity and alignment slack. Roll back charges on allocation or construction failure. Move the charge out before physically freeing its block, then release it afterward.

Preserve weak-control-block accounting: object destruction cannot refund an allocation still retained by a weak pointer. Bootstrap a resource's control block through the account-backed allocator, avoiding a self-retaining cycle. Preserve existing admitted extents initially; header unification is not required. An owning allocator forwarding into an already charged PMR resource must not charge twice.

## Retirement

Relocate the existing fixed runtime, registry, domains, factories and intrusive handles. Preserve resident admission, rollback protection, readiness publication and periodic retry. Final handle release only enqueues and publishes readiness; it cannot allocate, destroy payloads, free control blocks or join threads. The admitted resident anchors its domain until cleanup finishes, and the application retains the runtime until all domains drain.

HTTP receives the caller's allocation context and any required retirement capability. It creates no cleanup runtime. Accepted I/O retains its owner through authentic settlement. TaskGroup ownership does not replace that obligation.

First qualify the canonical consensus retirement-progress correction in the [retained-state component](RETAINED_STATE_ARCHITECTURE.md), then reuse its budget contract. A fresh nonzero byte budget permits one oversized first item, consumes its unit and saturates bytes to zero. Preserve allowance state across containers. Zero-byte predecessors consume work, and explicit zero byte or unit capacity grants no oversized allowance. Distinguish terminal retirement from separate bounded active-service quanta.

## Migration and acceptance

1. Establish the optional shared targets and accounting contracts.
2. Move allocation ownership, heap accounting and retirement, migrating Detent/completion together and deleting their generic local bodies. This foundation precedes HTTP client extraction.
3. Complete the canonical retained-state component before migrating Journal and DiskLedger reservations underneath their existing domain APIs. Preserve its growth and settlement semantics.
4. Complete reservation consolidation before release, with no maintained parallel implementation.

Require concurrent hierarchy-pressure rollback, allocation/constructor failure, alignment/overflow, weak-control lifetime and surviving allocations after facade destruction. Reservation tests cover over-quota census, protected pools, failed growth with unchanged counters, abandonment, stale handles, original-operation settlement and proved reclaim. Retirement tests cover final-release handoff races, close/admission races, pinned owners, retry fairness, finite historical/configuration drainage and shared budget allowances.

Resource-only linkage contains no global allocation overrides. Run focused application/library tests on the supported platforms and sanitizers. Measure allocation overhead, accounting cost and retirement work against the preserved baseline. Update the cpp-libraries index with the implementation series. This blueprint establishes no runtime result.

## Implementation qualification (October 18)

The isolated implementation moves owning byte accounts, page/buffer capacity, PMR allocation contexts and fixed retirement into optional `allocators` targets. Completion forwards its public allocation names; Detent removes the generic bodies. Journal and DiskLedger delegate committed/reserved arithmetic to one embedded reservation book while retaining their I/O and durable-proof rules. Alias canonical types and some layouts change, so C++ dependents require a coordinated rebuild; persisted bytes are unchanged.

Native allocator tracking OFF and ON each pass all four suites. The migrated completion passes 12/12 and Journal passes 8/8, including original-I/O and grown-reservation cases. New allocator modules pass macOS ASan/UBSan and Linux; the FreeBSD suite passes after removing an unnecessary experimental `std::jthread` dependency from its test fixture. Successful byte admission and release allocate no bookkeeping storage; exception runtime allocation is outside that claim.

The corrected implementation preserves `StoragePermit::peak()` after explicit abandonment through the reservation capability's existing preserve-debt operation, without a shadow counter. Its regression covers abandonment after the initiating facade is destroyed. The corrected allocator, completion and Journal native suites pass; the shared-library allocator profile passes 4/4. Final allocator preflight passes hardened macOS, macOS ASan/UBSan, Linux and FreeBSD.

Resource-only consumers pass with legacy tracking OFF and ON: neither compiles the legacy backend or retirement runtime, and neither defines global allocation overrides. TSan passes all three applicable allocator suites. The corrected combination passes 12/12 focused Detent tests in 39.44 seconds.

The [five-pair ownership comparison](../benchmarks/resource-extraction-ownership-20261019.json) records the explicit lifetime cost. Median root admission adds 7.15 ns per operation (+48.63%); depth-eight admission adds 4.61 ns (+5.39%); one-page arenas add 19.04 ns (+27.78%). Shared-control allocation changes by -0.20%, over-aligned PMR by +4.69%, and 100,000-owner retirement process CPU by -0.24%. These short measurements establish no efficiency improvement or statistical equivalence. Page and charged-allocation headers stay at 48 and 32 bytes; the arena grows from 72 to 80 bytes. Upstream request counts and total bytes match.

Independent account construction adds about 53.74 ns per account and one fixed shared-state allocation outside its own quota. That synthetic construction ratio is not a daemon throughput ratio. Root admission's added CPU corresponds to about 0.00715 core at one million admissions per second; actual application rates and the fixed state heap remain macro qualification inputs. Complete-component review accepts this cost for integration with no unresolved P1/P2 source finding.

The [publication record](../benchmarks/resource-extraction-qualification-20261019.json) identifies allocator `b7daa52`, completion `21809c7` and Journal `dbf753f`. All three eight-job CI runs pass. Clean public dependency builds without source overrides pass completion 12/12 and Journal 8/8. Xapiand's clean default Release/LTO build and version smoke check pass against the new allocator layout; its legacy allocator source remains unchanged. The catalog is updated in the same series. Detent's integration remains isolated pending final transport closure. Final composed platform/sanitizer coverage and integrated capacity/default footprint remain release gates.

## Validated Architectural Blueprint: EX-1 reconciliation

**Date:** 2026-10-27. **Status:** Source reconciliation validated against Detent:479c4bc and public library commits; consumer qualification pending. This addendum governs EX-1 adoption order. The shared mechanisms above remain accepted.

Adopt the published resource foundation serially, preserving the qualified prepared-reader default. Recover the resource hunks from Detent:33d36bf; exclude its authority funding changes and unrelated dependency edits. Published Journal reservation growth supports this extraction independently of consensus retained-state policy adoption.

### Public dependency closure

GitHub commit responses verify these immutable sources. Their CMake declarations form the resource closure; Detent's manifest declares them before transitive dependencies.

| Dependency | Revision and action |
| --- | --- |
| allocators | Add [allocators:b7daa52f1b6a316e04bba2817f0f527d64d704eb](https://github.com/Kronuz/allocators/commit/b7daa52f1b6a316e04bba2817f0f527d64d704eb). Provides `resources`, `retirement`, `RetirementFactory::uses_account` and `RetirementTesting::before_ready`. |
| completion | Replace completion:5251cf9 with [completion:21809c7ae35d19ee7707692e2d8bae6d8a7a92a8](https://github.com/Kronuz/completion/commit/21809c7ae35d19ee7707692e2d8bae6d8a7a92a8). Its allocation names forward to allocators. |
| journal | Replace journal:57e6250 with [journal:dbf753f60d1a74c9a036bf797406aeeb4b7998cc](https://github.com/Kronuz/journal/commit/dbf753f60d1a74c9a036bf797406aeeb4b7998cc). Its admission wrapper uses the shared reservation book. |

Keep consensus:b7073bbe4d94e66dce526f193b0dd81a57cdb661 and every other manifest revision. Journal's added shared backend is opt-in through separate constructors; existing Worker construction retains per-group storage. Preserve consensus wire bytes, funding configuration, synced acknowledgements and checkpoint behavior. All C++ consumers require rebuilding because canonical types and layouts change.

### Ownership, scale and failure checks

| Boundary | Required invariant and acceptance evidence |
| --- | --- |
| Heap admission | Charges retain account state through facade destruction. Exercise depth-eight parent refusal, overflow, allocation/constructor failure, alignment and weak controls; refund follows physical free. Admission/release introduces no bookkeeping allocation. Account construction's fixed state allocation remains outside its own quota and must appear in process-footprint accounting. |
| Retirement | Keep the caller-owned runtime, fixed registry and resident limits. Final release only hands off work. Test close/admission and readiness races, pinned generations, retry fairness and eventual drainage. Give pressure/conservation cases dedicated child accounts; inspect aggregate cleanup after shutdown and caller-handle release. Payload drainage alone does not prove metadata release. |
| Reservations | Keep Journal's protected pools and uncertainty fencing. DiskLedger retains its two slots, serial identities, original-operation counts and durable deletion proofs. The embedded book outlives its reservations; unresolved destruction preserves debt. Test over-quota census, failed growth, abandonment, stale handles and repeated/forged completion or reclaim attempts. |
| Current readers | Preserve `paged_peak()`'s prepared-metadata charge and current generation formulas. Keep prepared-reader account matching and nonmovable holders with exported member addresses. Exercise held old views, replacement, cancellation and runtime shutdown through production entry points. |
| Storage security | Preserve root/symlink refusals, owner-file bootstrap accounting and refusal without durable mutation. Malformed or unsupported storage cannot earn reclaim credit or trigger destructive recovery. No allocation alias changes authorization or transport settlement. |

### Executable stages

1. **Freeze and check dependencies.** Save the current manifest and baseline results. Create a fresh scratch candidate with only the three dependency changes above. Resolve public sources without `FETCHCONTENT_SOURCE_DIR_*` overrides, record every resolved revision and run authority/storage smoke checks. This separates dependency failures and costs from extraction. Sibling worktree HEADs are not adoption evidence.
2. **Apply resource bodies.** Reconcile `include/detent_distribution/{storage,allocator,buffer,memory_resource,retirement}.hh` to aliases or thin forwarding functions. Convert `detent_resources` to an interface target linking `allocators::resources` and `allocators::retirement`; retain `PROJECT_SOURCE_DIR` include paths. Remove `src/detent_distribution/retirement.cc`. Replace DiskLedger arithmetic with `ReservationBook<3>` while retaining its domain checks and current footprint formulas. Exclude HTTP, MessagePack and authority funding hunks.
3. **Qualify the composed consumer.** Run the commands below from the candidate checkout, using the selected supported compiler. Inspect build/link commands: no legacy `allocators.cc` or global allocation overrides. Run focused storage, retirement, prepared-reader and capacity tests first, then the complete suite. Qualify the final immutable closure through hosted Linux/macOS architectures and ASan/UBSan, plus FreeBSD; retain the documented FreeBSD LeakSanitizer exclusion. Run relevant concurrency tests with TSan on a supported host.

```sh
# Fresh public-source consumer build; configure also runs compiler probes.
quiet check
quiet light detent-ex1-configure 15 "EX-1 configure" -- cmake -S . -B .scratch/ex1-build \
  -DCMAKE_BUILD_TYPE=Release -DDETENT_BUILD_AUTHORITY=ON -DDETENT_BUILD_RELAY=ON \
  -DDETENT_BUILD_MANAGED_AGENT=ON -DDETENT_BUILD_BENCHMARKS=ON
quiet light detent-ex1-build 30 "EX-1 build" -- cmake --build .scratch/ex1-build -j4
quiet light detent-ex1-focused 15 "EX-1 focused tests" -- \
  ctest --test-dir .scratch/ex1-build --output-on-failure \
  -R '(distribution_(storage|retirement)|local_(disk|storage_session|managed|prepared)|reader_capacity)'
quiet light detent-ex1-tests 45 "EX-1 complete tests" -- \
  ctest --test-dir .scratch/ex1-build --output-on-failure -j4
```

4. **Measure and close.** Restore the preserved ownership/retirement harnesses identified by [ownership evidence](../benchmarks/resource-extraction-ownership-20261019.json) into a reproducible repository path before measurement. Their current source is under `.scratch/library-adoption-20261017/resume/`; the committed record alone is not a runnable harness. Compare dependency-only and migrated candidates with identical compilers and inputs under `quiet exclusive`; retain current-main measurements separately. Run existing paged recovery and collection benchmarks at 100,000 keys, then 500,000 if results warrant confirmation. Record CPU, wall time, requested/admitted bytes, fixed account/runtime heap, process RSS and pinned-view backlog/drainage separately. Check default startup capacity and exact allocation conservation. Report regressions against the accepted ownership costs above; claim no efficiency improvement without measurements. One component review checks lifetime/failure evidence, exact bytes and deleted engines before adoption. Update extraction/release ledgers and verify post-merge CI to completion.

### Limits and separate ownership activation

This inspection runs no builds or benchmarks. Published component results do not qualify the current composed consumer; integrated footprint and sanitizer results remain acceptance gates.

[CL-2](CONSOLIDATION_RESPONSE.md#cl-2-one-ownership-protocol) remains a separate correctness boundary. Public completion:ca0889d supplies `OwnerLockOptions` with `file`, `directory` and `both`, but [journal:d564dfea](https://github.com/Kronuz/journal/blob/d564dfea4ebbded1505c702ce11708548fda69f9/include/journal/journal.h) still calls `acquire_owner(bool)`, and [consensus:a4c28e09](https://github.com/Kronuz/consensus/blob/a4c28e090434d3b823531a31c7b748afb85e96ef/include/consensus/worker.h) exposes no Worker forwarding. A completion bump alone silently leaves Journal file-only. An immutable forwarding closure remains unresolved. Add mode forwarding through Journal/Store/Worker with existing defaults preserved, publish and qualify that closure separately, then select `both` at every mutable Detent entry point. Use completion's single lock implementation; no custom IO wrapper or invented revision. Retain the required multiprocess mode matrix, partial-acquisition rollback, bootstrap/trust tests and original-completion lifetime checks. Resource adoption does not close CL-2 or authorize mixed legacy owners on one directory.
