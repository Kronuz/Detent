# Container selection and extraction

**Updated:** 2026-10-24. EX-0 adoption is implemented; main remains gated on the managed-process qualification failure.

1. Contributor tip `19cffc5` is reconciled. Strong copy assignment from `1234d08`, allocation-refusal tests and publication atomicity remain intact. The [comparison report](../CONTAINER_SELECTION_REPORT.md) preserves source attribution and baseline CI results.
2. The [flat-map library](https://github.com/Kronuz/flat-map) at `a9a52b02e387ab054ff38647d3f141f9f8c00593` passes its seven-job matrix. The candidate uses this immutable pin and a forwarding header, with no private engine. C++ consumers require rebuilding; canonical JSON and mapped payload bytes match.
3. A clean public-pin native build passes 224/224 checks. Rust and Python each pass five binding checks. The embedded SDK consumer fixture passes. The [report](../CONTAINER_SELECTION_REPORT.md#reconciliation-qualification) identifies source revisions and scope.
4. [Run 40121767475](https://github.com/Kronuz/Detent/actions/runs/40121767475), source `5c0d7d3`, passes Linux GCC x86/ARM, macOS x86/ARM and ASan/UBSan/LSan. Linux Clang fails `test_managed_delivery_process`; its original shell log does not identify the assertion. This remains open.
5. The encoded-update accounting fixture uses a dedicated child account (`0b9dbec`) and passes 1,000 repetitions. The coordinator startup-pressure fixture isolates admission and deliberately refunds unrelated parent bytes (`89d2cd6`); it passes 100 repetitions and all three 500K HTTP/migration/transition checks. These checks also pass in the composed matrix. Deadlines and production limits are unchanged.
6. [Focused run 40123378242](https://github.com/Kronuz/Detent/actions/runs/40123378242) repeats the real managed restart, credential-rejection and reset scenario. Source `dd6b0eb` adds assertion-line reporting and bounded child logs. It builds only the three required binaries on a temporary diagnostic branch, preserving the complete matrix evidence.
7. The separate 5M exploratory full-system trial is running under an exclusive host hold, using source `5c0d7d3` and the public-pin binaries. No supported capacity follows from an unfinished trial.

The [architecture recheck](../reviews/container-extraction-architect-2026-10-24.md) rejects btree `44cb140` as a portable lifetime repair; that branch remains experimental. After EX-0 lands, continue the remaining release scope and scaling gates. Container qualification does not establish release-scale capacity.
