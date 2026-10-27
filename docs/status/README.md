# Active qualification work

**Updated:** 2026-10-27. Detent remains in Stage 6, release qualification. No new supported operating ceiling is declared.

| Workstream | State | Evidence or next gate |
| --- | --- | --- |
| Startup runtime capacity | Landed and pushed | `Detent:5721d53`; 217 product tests and 54 Python tests pass |
| Managed CLI reader capacity | Full suite and ownership check pass; macOS permission recheck approves with platform conditions | Historical 218-test gate and flat/paged process checks; macOS ACL refusal and focused FreeBSD UFS checks pass; FreeBSD full managed profile (170 checks) and focused sanitizers (nine checks) pass, while Linux rich/NFSv4 ACL and FreeBSD NFS4/ZFS remain unqualified |
| System cardinality | 5M replicated seed and reduced-topology candidate SDK verification complete | [Capacity evidence](../CAPACITY_QUALIFICATION.md); original convergence fails, larger topology reaches host guard; sustained 5M and exploratory 10M remain open |
| Ingestion waits | Explorer profiling | Serial sync completions account for substantial measured time; separate worker time from queue delay and other request phases |
| Scaling architecture | Release-scale qualification rejected | SC-1 relay full-snapshot rebuilding; SC-2/3 segment and compaction costs; see review |
| Relay snapshot lifecycle | Integrated; functional and scoped sanitizer gates pass | 219/219 candidate product tests; delayed raw/compressed TLS tests; ASan/UBSan; sustained capacity envelope remains open |
| Reader ACL trust | MacOS fix qualified | Same descriptor predicate protects public reader and private managed configuration; unsupported filesystem scope documented |
| Container selection | Public flat-map adopted on main | 224 native tests and six enabled hosted jobs pass; [qualification state](container-selection.md) |
| Recovery/delivery | Qualified component merged on main | 224 native tests, complete persisted 5M SDK verification and all six enabled hosted profiles pass; sustained capacity remains open |
| Fresh client attachment | Managed prepared-reader default qualified | [Real 5M managed trial](../reviews/prepared-managed-qualification-2026-10-26.md): preparation/restart pass, live resident-data fresh reads 200/200 below 1 ms; six enabled hosted jobs, 170 FreeBSD native checks and nine FreeBSD sanitizer checks pass |
| Resource-library adoption | Functional gates pass; performance gate remains open | A later counterbalanced resident-data batch gives 196/200 baseline and 195/200 migrated first reads below 1 ms. Main retains private resource bodies; one bounded phase/CPU/allocation diagnostic precedes any adoption decision. See the [extraction checklist](../EXTRACTION_CHECKLIST.md). |
| Storage barriers | Blueprint validated; implementation pending | [Grouped preparation](../STORAGE_BARRIER_ARCHITECTURE.md), preserving file syncs and ownership-before-payload ordering |
| Online capacity increases | Blueprint parked | Shared API design recorded; implementation follows resolution of current scaling concerns |

Self-contained [blocker handoffs](../handoffs/README.md) define the storage, container and ingestion asks. The lead retains relay integration.

The current priority is diagnosis of ingestion and bootstrap costs. Passing larger configured bounds does not close that gate. Avoid speculative tuning and further activation machinery until the measured paths are understood.

The [extraction checklist](../EXTRACTION_CHECKLIST.md) preserves the remaining resource, HTTP, bounded-codec and persistent-map work, including existing worktrees and clean-session restart instructions. These are release gates; completed library publication does not imply consumer adoption.

## Diagnostic done criteria

1. Reproduce the seed behavior at a small cardinality using unchanged durable semantics; attribute its major elapsed-time components with profiles or telemetry.
2. Document actual work and ownership in authority, relay, agent and SDK paths; identify cold/full work separately from small incremental updates and warm reads.
3. Prove any proposed optimization with a controlled before/after and result-identity checks; preserve failures and losing experiments.
4. Keep the complete product suite, permission review and real-process reader checks green before pushing the reader component.
5. Continue the original release gates after the diagnostic component; do not replace capacity, recovery, extraction or deployment qualification with a profiling result.

## Session quota policy

Work proceeds serially. Reuse existing binaries and captured evidence before building or measuring again. Use at most one agent at a time, only for a narrow independent review or a measured task that earns its cost. Give that call a small token and experiment budget; do not repeat broad repository surveys. Full suites run at component boundaries, with focused checks for subsequent isolated fixes. Preserve interrupted work and ledgers before resuming it.
