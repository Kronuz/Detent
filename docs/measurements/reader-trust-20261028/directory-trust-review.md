# Detent directory-trust component review

Date: 2026-10-28. Reviewed **Detent:aaa37f0**, its implementation/test diff, [blueprint](directory-trust-blueprint.md), [Linux GCC](trust-linux-gcc.log) and [macOS ARM](trust-macos-arm.log) CI logs, and the complete [four-arm diagnostic](trust-four-arm/result.json), including its builder/controller. Source and binary hashes match the recorded artifacts. No builds, tests, or benchmarks run during this review.

**Ruling: conditional security GO; performance NO-GO. Discard this optimization from the current adoption attempt. Uninstrumented acceptance is not justified by the agreed diagnostic condition.**

1. **Security and implementation.** `generation_file_access.hh:69-112` implements the proposed ordering and saves open failure immediately. Only `ELOOP`, `ENOTDIR`, or `EMLINK` enter alias classification. A non-link rejects without retry. Initial disappearance remains `GenerationUnavailable`; disappearance during named verification remains a trust error. Held-descriptor type, owner, mode, ACL, and device/inode checks remain intact. The bounded root-owned alias resolver and its before/after checks are unchanged. I found no introduced trust bypass.

2. **Edge cases and state.** The implementation retains root handling, dot/dotdot behavior, absolute and relative alias expansion, component/link limits, and `RecoveryFile` unwinding. It introduces no cache, shared-library dependency, or resource-adoption change. Ordinary traversal removes one preliminary stat; aliases incur an additional failed open. Existing depth-dependent descriptor retention and ACL work remain.

3. **Test evidence and remaining condition.** The replacement hook now requires a successful open before changing the directory (`test_trusted_paths.cc:28-50`); both directory and alias races assert that injection occurred. New cases exercise error classification, all three candidate errnos, descriptor identity/CLOEXEC, and failure cleanup. The inspected CI logs show both ordinary and privileged fixtures passing on Linux and macOS, with 228 tests passing on each platform. **C1: native FreeBSD execution remains open.** Injected `EMLINK` covers dispatch, but does not establish native kernel/filesystem and ACL behavior. Local skip 77 is not a pass.

4. **Performance evidence.** Recalculation from all 80 measured processes gives these paired proposed-minus-current medians. Negative values favor the proposal; wins count negative pairs among 20 keys.

   | Metric | Private resources | Shared resources |
   | --- | ---: | ---: |
   | Directory caller CPU | −11.600 µs; 18/20 wins | −17.658 µs; 19/20 wins |
   | Attachment caller CPU | −5.410 µs; 15/20 wins | −18.062 µs; 17/20 wins |
   | Total first-read wall | −1.904 µs; 10/20 wins | −18.226 µs; 16/20 wins |
   | Total first-read caller CPU | −1.884 µs; 10/20 wins | −18.366 µs; 15/20 wins |

   Directory cost decreases consistently in this diagnostic. The private arm does not establish an end-to-end benefit: its wall differences span −132.591 to +89.739 µs and divide evenly between wins and losses. The shared arm has a favorable observed signal. Neither independent arm medians nor pooling the arms resolves the private result.

5. **Disposition.** The blueprint requires consistent end-to-end benefit in both resource arms after one bounded diagnostic. That condition is unmet; uncertainty is not proof of improvement or nonregression. Preserve the branch and measurements as evidence, but do not extend attribution or begin acceptance for this candidate. General regression coverage can be retained independently after removing expectations specific to the discarded optimization. The existing uninstrumented sub-ms first-read gate remains unchanged and open.
