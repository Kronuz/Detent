# Relay snapshot lifecycle

**State:** Integrated. The full candidate suite, delayed TLS regression and scoped ASan/UBSan checks pass. Release-scale capacity remains unqualified.

SC-1 replaces eager snapshot invalidation with a pinned snapshot and a finite retained catch-up path. Before admitting a snapshot session, the relay funds and captures the complete path from snapshot B through open-time head C. Exact acknowledgment and transport settlement release each retained item. After C, ordinary live-cache continuity applies; an explicit reset is possible. Admission still requires a complete path within finite budgets; unlimited write rates are not guaranteed.

## Qualification evidence

1. The earlier raw/compressed delayed-consumer regression fails against frozen baseline libraries and passes against the candidate. Source and result remain in `.scratch/relay-retention-baseline/`.
2. Native tests cover live-history eviction, snapshot building during upstream staging, early and wrong acknowledgments, unchanged-cursor snapshots, cache and metadata admission pressure, expiry with outstanding transport, and final heap/mapping conservation.
3. The candidate full-product suite passes 219/219 tests in 749.14 seconds. Logs: `.scratch/relay-full-{configure,build,tests}.log`. The frozen source is `652688b` in `.scratch/reader-capacity`; it includes the relay implementation through `36addc1` and the reader ACL fix.
4. The added real TLS delayed-consumer test passes for raw and compressed snapshots. It opens at B=42/C=48, advances writers past live-history eviction before polling and between polls, decodes the retained 42→45→48 path, then observes explicit history loss after C. The TLS fixture and native cases pass 3/3 in 1.22 seconds; logs: `.scratch/relay-delayed-tls-{configure,build,tests}.log`.
5. Independent code review approves with condition CR1: the combined delayed TLS case. Item 4 supplies that missing evidence; the original review remains unchanged in `docs/reviews/relay-bootstrap-code-2026-10-23.md`.
6. The 500-session sharing fixture and 100K TLS fixture pass after correcting plan-owner funding (4/4, 11.80 seconds). The paged authority→relay→agent→reader process check passes progress, deletion defaults, relay restart and upstream revocation.

## Integration and sanitizers

The scoped LLVM 23 ASan/UBSan build passes the native and delayed TLS regressions (3/3 including certificate setup, 1.48 seconds). Logs: `.scratch/relay-lifecycle-sanitizer-{configure,build,tests}.log`. Leak detection is disabled on macOS; this is not LeakSanitizer qualification. Explicit heap/mapping conservation assertions remain enabled.

The integrated source matches all 539 tracked build/source/test files in the frozen full-product checkout, including the reader ACL fix. Its final TLS test update passes 3/3 in 1.16 seconds. Evidence: `.scratch/relay-integrated-source-equivalence.log` and `.scratch/relay-integrated-{configure,build,tests}.log`. The earlier 219-test run covers the same production code; only the additional TLS test changes afterward. No performance improvement is inferred from functional checks.

## Introduced regressions

The initial implementation underfunded retirement residents for retained session plans and broke the fixture's zero-metadata-allocation assertion. Funding now includes the configured session bound, and the assertion accounts for plan metadata while retaining the shared-payload check. Completed plans release at their final acknowledgment. These failures remain recorded rather than being counted as pre-existing defects.

## Remaining gates

Qualify the retained bootstrap envelope under sustained load and at the declared capacity tiers. No measured performance improvement or release-scale operating point is established. SC-2/3 storage costs and the btree safety/selection gate remain separate release blockers.
