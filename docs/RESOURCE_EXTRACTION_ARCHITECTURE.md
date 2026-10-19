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

Independent account construction adds about 53.74 ns per account and one fixed shared-state allocation outside its own quota. That synthetic construction ratio is not a daemon throughput ratio. Root admission's added CPU corresponds to about 0.00715 core at one million admissions per second; actual application rates and the fixed state heap remain macro qualification inputs. Clean Xapiand compatibility, final composed platform/sanitizer coverage, public immutable closure and integrated capacity/default footprint remain open. These source results do not accept the extraction for release.
