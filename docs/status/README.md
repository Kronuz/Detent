# Active qualification work

**Updated:** 2026-10-23. Detent remains in Stage 6, release qualification. No new supported operating ceiling is declared.

| Workstream | State | Evidence or next gate |
| --- | --- | --- |
| Startup runtime capacity | Landed and pushed | `5721d53`; 217 product tests and 54 Python tests pass |
| Managed CLI reader capacity | Full suite and ownership check pass; macOS permission recheck approves with platform conditions | 218 product tests; flat/paged real-process checks; SR1 macOS ACL refusal fixed; Linux rich/NFSv4 ACL and native FreeBSD qualification remain open |
| System cardinality | Functional trials through 130K complete | Raw source-identified trials; published report through 100K; 1M → 5M → 10M remains open |
| Ingestion waits | Explorer profiling | Serial sync completions account for substantial measured time; separate worker time from queue delay and other request phases |
| Scaling architecture | Release-scale qualification rejected | SC-1 relay full-snapshot rebuilding; SC-2/3 segment and compaction costs; see review |
| Relay snapshot lifecycle | Integrated; functional and scoped sanitizer gates pass | 219/219 candidate product tests; delayed raw/compressed TLS tests; ASan/UBSan; sustained capacity envelope remains open |
| Reader ACL trust | MacOS fix qualified | Same descriptor predicate protects public reader and private managed configuration; unsupported filesystem scope documented |
| Btree/container selection | Unresolved | Current pin remains in production; complete comparison and structural safety gate before choosing |
| Online capacity increases | Blueprint parked | Shared API design recorded; implementation follows resolution of current scaling concerns |

Self-contained [blocker handoffs](../handoffs/README.md) define the storage, container and ingestion asks. The lead retains relay integration.

The current priority is diagnosis of ingestion and bootstrap costs. Passing larger configured bounds does not close that gate. Avoid speculative tuning and further activation machinery until the measured paths are understood.

## Diagnostic done criteria

1. Reproduce the seed behavior at a small cardinality using unchanged durable semantics; attribute its major elapsed-time components with profiles or telemetry.
2. Document actual work and ownership in authority, relay, agent and SDK paths; identify cold/full work separately from small incremental updates and warm reads.
3. Prove any proposed optimization with a controlled before/after and result-identity checks; preserve failures and losing experiments.
4. Keep the complete product suite, permission review and real-process reader checks green before pushing the reader component.
5. Continue the original release gates after the diagnostic component; do not replace capacity, recovery, extraction or deployment qualification with a profiling result.

## Session quota policy

Work proceeds serially. Reuse existing binaries and captured evidence before building or measuring again. Use at most one agent at a time, only for a narrow independent review or a measured task that earns its cost. Give that call a small token and experiment budget; do not repeat broad repository surveys. Full suites run at component boundaries, with focused checks for subsequent isolated fixes. Preserve interrupted work and ledgers before resuming it.
