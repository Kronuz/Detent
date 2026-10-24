# Container selection and extraction

**Updated:** 2026-10-24. EX-0 adoption is implemented on `track/container-adoption`; qualification remains open before landing on main.

1. Contributor tip `19cffc5` is reconciled through `c3da772`. Strong copy assignment from `1234d08`, allocation-refusal tests and publication atomicity remain intact. The [comparison report](../CONTAINER_SELECTION_REPORT.md) preserves the contributor's baseline CI results.
2. The [architecture recheck](../reviews/container-extraction-architect-2026-10-24.md) rejects btree `44cb140`'s mutable-pair/const-pair view as a portable lifetime repair. That branch remains experimental; no safe older rollback is established.
3. The [flat-map library](https://github.com/Kronuz/flat-map) at `a9a52b02e387ab054ff38647d3f141f9f8c00593` passes its [seven-job matrix](https://github.com/Kronuz/flat-map/actions/runs/40095399514). Detent's candidate uses this immutable pin and a forwarding header, with no private container engine. C++ consumers require rebuilding; canonical JSON and mapped payload bytes match.
4. The clean public-pin native build passes 224/224 checks. Rust passes five integration tests; Python 3.15 passes five binding checks. Two isolated pressure fixtures each pass 100 repetitions. The [report](../CONTAINER_SELECTION_REPORT.md#reconciliation-qualification) identifies the precise source revisions and limits.
5. Product [run 40117311416](https://github.com/Kronuz/Detent/actions/runs/40117311416) passes Linux x86 Clang and macOS ARM. Linux x86 GCC reports a Raft fixture port collision; ARM GCC times out in 500K migration; ASan/UBSan times out in selected-sink cancellation. macOS x86 reaches the 60-second publication deadline in the 500K relay HTTP fixture and rejects missing durable feedback. These failures keep the gate open.
6. The Raft fixture reserves OS-selected ports and binds each advertised endpoint; ten repeated local cluster checks pass. The migration case passes locally in 38.28 seconds, and selected-sink cancellation passes 100 repetitions. Phase checkpoints identify setup, network and drainage boundaries on a recurrence; those local passes do not resolve the hosted timeouts.
7. SDK embedding uses Detent's project source root instead of the outer application's source root. The standalone FetchContent consumer fixture passes, and standalone product compile commands remain byte-identical.

## Separate CI repairs

The shared workflow selects Homebrew LLVM and an explicit macOS SDK. TLS fixtures own their OpenSSL request configuration: hosted certificates otherwise inherit ambient CA extensions twice and fail strict verification. The ARM macOS job passes with the hermetic fixture. Empty agent startup also guards zero-byte copies, preserving production sanitizer checks.

The current composed gate is [run 40121767475](https://github.com/Kronuz/Detent/actions/runs/40121767475) at `5c0d7d3`. The preceding run passes Linux Clang, Linux ARM, Intel macOS and sanitizers. The two remaining fixture failures have isolated-account corrections (`0b9dbec`, `89d2cd6`), with 1,000 cleanup repetitions and 100 controlled-refund repetitions passing. Hosted verification remains pending. A separate 5M exploratory trial is running under an exclusive host hold.

After this component lands, continue EX-1 resource adoption and the independent scaling gates. Passing the container checks does not establish release-scale capacity.
