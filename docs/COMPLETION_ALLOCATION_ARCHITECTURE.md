# Validated Architectural Blueprint: completion allocation

**Status:** Lead Software Architect design GO (frontier model, max effort), October 7, 2026. Source inspection establishes the plan below; implementation and component qualification establish its behavior. This extends the Stage 4 resource component, whose disk inventory and generation reclamation gates remain open.

Use an optional owned PMR allocation context in the generic completion module. Detent supplies the charged resource adapter. Preserve existing constructors, canonical journal aliases and wire formats. Implement the complete extension autonomously through its commits, then review the finished resource/storage component once.

## Allocation contract

Add `completion/allocation.h` with `AllocationContext` and `OwnedAllocator<T>`, independent of Detent, models, codecs and Asio. A context owns a shared `std::pmr::memory_resource`. An empty context selects the process-lifetime `new_delete_resource()` without creating another shared control. A borrowed resource or mutable global default cannot provide the required lifetime.

The standard allocator retains this context through rebinding, container ownership and shared-control destruction, including weak retention. Define equality and propagation explicitly. Check multiplication before forwarding the requested bytes and `alignof(T)`; deallocation cannot throw. Nested owning names need the allocator too. Allocating their enclosing object alone does not account for their strings.

Detent creates its adapter and shared control through `StorageAllocator`, avoiding recursive self-allocation. The adapter owns its ledger; the ledger owns no clients or resources. Each request admits all upstream bytes, including headers and alignment slack, before allocating. Allocation and constructor failures release reservations. Actual release precedes refund. Support over-alignment or reject it before allocation; never silently misalign it or refund pooled storage that remains allocated.

## Factories and original ownership

Account outer jobs and backend/queue objects with factories or already-admitted parent storage. Add context-first constructors accepting borrowed names to preserve legacy overloads and avoid owning argument temporaries in the managed path. Prepared state retains the context; publication inherits it. Set the prepared capability’s selected flag only after fallible publication construction succeeds.

Preserve unique-return `IO` and completion file/cursor types. Managed concrete POSIX controls can use resource-backed class-specific allocation with owned metadata and matching constructor-failure deletion. Default deletion through the virtual base must recover the original resource. Restrict this mechanism to the managed concrete types. Allocate controls before native handles where practical, and close acquired handles on failure. An allocation resource does not replace the owner-lock/backend lifetime contract.

Prepared image state owns its file uniquely. Request file handles alias the admitted state instead of allocating another shared control at Create completion. Recovery and scan operations may use separate admitted holders; aliasing an operation into its own request would create a cycle.

Finish fallible request preparation before submission. The driver retains the operation, request views and required owners through authentic original reaping and result consumption. Cancellation, facade destruction, rejection and notification failure cannot refund live originals or fabricate completion. Linux submission uncertainty retains existing reconciliation and cannot trigger duplicate fallback. Allocation failure after a filesystem effect does not establish rollback or release disk credit.

## Native capacity and scanning

Use inline NUL-terminated buffers for Linux’s existing bounded native basenames and fixed probe data, retaining them through reaping. POSIX syscall name scratch uses checked buffers for its existing 128/255-byte limits. Keep one original and one result slot per queue; bound queue instances separately.

| Resource | Account or boundary |
| --- | --- |
| Image jobs/state/names, managed POSIX controls and explicit queue storage | Actual allocation requests through the context |
| Fallback thread and thread-runtime allocations | Fixed native/runtime baseline and bounded instance count |
| Linux ring mappings and kernel resources | Separately reported native capacity |
| `fdopendir`, libc and exception-runtime storage | Explicit external-runtime boundary |
| Asio services, handlers and coroutine frames | Adapter boundary until separately integrated |
| Legacy Journal graphs, caller allocation and third-party IO | Legacy/external boundary |

The retirement runtime’s configured stack does not describe completion fallback threads. These boundaries cannot be replaced with guessed reservations or a process RSS claim. Allocation ownership also does not defer destruction: SDK release relies on intrusive retirement, while completion controls retain their established driver/worker disposal path.

Legacy cursor and completion scan names use owning `std::string`; a context cannot redirect their characters while preserving those types. Add an opt-in caller-buffer scan operation before claiming fully accounted inventory output. Unsupported managed scanning fails explicitly and never silently uses the allocating legacy path.

## Detent integration and qualification

Charge publisher implementation/owner, recovery state and outer jobs. Replace generated owning-name scratch with bounded construction. Share one aggregate heap account across decoding, building and publication. Keep heap, mapped virtual bytes, logical disk commitments and native baselines separate. Disk credit follows actual generation reclamation and its namespace barriers; uncertain selections remain protected.

1. Qualify observed allocations under exhaustion, constructor failure, growth overlap, short/long names, alignment, overflow, weak retention and cross-thread final release.
2. Preserve resource lifetime with surviving file/cursor controls after backend destruction. Owner-lock lifetime remains separately constrained by its backend.
3. Inject at every preparation/publication allocation. Exhaust capacity with Create pending and verify adoption needs no new shared control.
4. Hold authentic native/fallback originals through cancellation and facade destruction, including uncertain publication.
5. Run standalone completion, publication and journal compatibility suites on macOS, Linux and FreeBSD, with ASan/UBSan and focused concurrency instrumentation.
6. Record actual managed capacity and native baselines separately. Do not infer latency, power or whole-process memory improvement from source inspection.

The scalability review keeps queue state constant and controls proportional to admitted work. Edge-case analysis requires genuine settlement and complete rollback before exposure. The dependency/state audit preserves the generic boundary and single-owner operation model. Security review retains confined names, no-follow/exclusive creation, ownership checks and protection of possibly selected generations. Consolidated resource acceptance still needs bounded disk inventory and original/lease-protected cleanup; Stage 4 also needs production agent integration and its crash/interoperability/scale gates.
