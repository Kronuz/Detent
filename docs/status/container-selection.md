# Container selection and extraction

**Updated:** 2026-10-25. EX-0 qualification passes and is landed on main at `4f1a086`.

1. Contributor tip `19cffc5` is reconciled. Strong copy assignment from `1234d08`, allocation-refusal tests and publication atomicity remain intact. The [comparison report](../CONTAINER_SELECTION_REPORT.md) preserves source attribution and baseline CI results.
2. The [flat-map library](https://github.com/Kronuz/flat-map) at `a9a52b02e387ab054ff38647d3f141f9f8c00593` passes its seven-job matrix. The candidate uses this immutable pin and a forwarding header, with no private engine. C++ consumers require rebuilding; canonical JSON and mapped payload bytes match.
3. A clean public-pin native build passes 224/224 checks. Rust and Python each pass five binding checks. The embedded SDK consumer fixture passes. The [report](../CONTAINER_SELECTION_REPORT.md#reconciliation-qualification) identifies source revisions and scope.
4. [Run 40125876413](https://github.com/Kronuz/Detent/actions/runs/40125876413), source `a686fe1`, passes all six enabled jobs: Linux GCC x86/ARM, Linux Clang, macOS x86/ARM and ASan/UBSan/LSan. Each registers 216 checks.
5. The corrected hardened lifecycle [stress run 40125771247](https://github.com/Kronuz/Detent/actions/runs/40125771247) passes 400 repetitions across four concurrent drivers. Controls reproduce reset shutdown before readiness; the correction waits for readiness and strengthens deletion and fatal-exit checks. The [report](../CONTAINER_SELECTION_REPORT.md#managed-process-readiness-race) preserves attribution.
6. Two isolated-account fixture corrections pass 1,000 cleanup repetitions and 100 controlled-refund repetitions, plus the composed matrix. Deadlines and production limits are unchanged.
7. The 5M exploratory trial completes replicated seeding and authority verification, then times out during agent convergence. Daemon shutdown precedes the first sleep by nine minutes, so sleep does not explain the failure. The [trial report](../../benchmarks/system-capacity-5m-20261024.json) preserves sampled memory and explicit limits; a clean repeat remains required.

The [architecture recheck](../reviews/container-extraction-architect-2026-10-24.md) rejects btree `44cb140` as a portable lifetime repair; that branch remains experimental. Continue the remaining release scope and scaling gates. Container qualification does not establish release-scale capacity.
