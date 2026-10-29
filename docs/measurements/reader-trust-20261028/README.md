# Prepared-reader attachment diagnosis

The 40-process instrumented diagnostic passes lookup and cardinality checks with 20 recorded keys and counterbalanced private/shared order. Matching warmup processes precede each measured process. No production archives change. Instrumentation is diagnostic overhead and does not satisfy the first-read acceptance gate.

| Scope | Private median caller CPU | Shared median caller CPU |
| --- | --- | --- |
| Attachment total | 389.69 µs | 397.50 µs |
| Directory trust | 212.69 µs | 215.42 µs |
| Metadata records (sum per process) | 88.85 µs | 94.21 µs |
| View controls | 9.59 µs | 12.62 µs |

These are independent medians, not paired causal deltas. Fault counters cover the whole process and can include retirement-worker activity. The result identifies directory traversal as a target; it does not attribute all resource-migration cost to that traversal.

The [validated blueprint](directory-trust-blueprint.md) preserves held-descriptor ownership, ACL and named-inode checks while moving preliminary classification behind a refused no-follow open. It requires FreeBSD `EMLINK` handling and race injection at the post-open boundary. The prototype starts from Detent:f1487c2. Native ordinary-path tests pass, including error mapping, exact exception classes, CLOEXEC and FD drainage. Privileged ownership/alias fixtures skip locally (77) because noninteractive sudo is unavailable; platform and sanitizer coverage remain open.

The four-arm diagnostic passes all 80 measured processes. Paired median total first-read wall changes are −1.904 microseconds (private, 10/20 improvements) and −18.2255 microseconds (shared, 16/20 improvements). The [complete review](directory-trust-review.md) rules performance NO-GO because both resource arms do not show consistent end-to-end benefit. The prototype implementation is discarded; no uninstrumented acceptance rerun follows. Its immutable source and reusable tests remain at Detent:aaa37f0. Reproduction requires that source revision and its parent header, not the restored branch working tree. The sub-millisecond resource gate remains open. Restore `attach-diagnostic` into `.scratch/resource-adoption-20261027/` for the recorded build layout. The initial failed controller validation is preserved separately; it incorrectly expected lookup mapping events from an attachment observer and contains no accepted samples.
