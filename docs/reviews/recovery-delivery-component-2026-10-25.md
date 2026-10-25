# Recovery and delivery component review

**Date:** 2026-10-25. **Reviewed source:** `d0c6d27`. **Verdict:** Accepted in the tested component scope. Runtime and matrix conditions below are satisfied; sustained capacity and fast client attachment remain open.

## Source findings

1. Image decoding drains pending reclamation before admitting another slice and before completion. State transfer is single-use, and queries after transfer avoid moved ownership. Fragmented-input and retirement-bound fixtures cover both one-row and default 128-row steps.
2. Subscription idle expiry exempts accepted requests and active transfers. Maximum lifetime and explicit closing remain unconditional. Settlement/release starts a fresh idle interval only after activity finishes; stale tokens cannot refresh it. Fake-clock tests cover feedback before settlement and active-phase expiry.
3. The optional delivery allowance is finite and validated. Omitting it preserves timeout inheritance, while publisher and control timing remain separate. Snapshot progress reads existing counters without allocation or traversal. Diagnostic changes preserve authorization and transport behavior.
4. Active-export counters exclude queue, authorization, quorum and `SnapshotRequired` failures. Zero values do not establish complete delivery success.

## Acceptance conditions

1. Finish the composed matrix for the reviewed production changes, including sanitizers and both macOS architectures.
2. Verify every record in the preserved 5M corpus through the independent SDK. A reduced topology is reported as such.
3. Keep the resource-limited two-relay/two-agent trial unqualified. Successful first transfers do not establish final convergence or its memory requirement.

The [scheduling blueprint](../AUTHORITY_SCHEDULING_ARCHITECTURE.md) is a separate performance component. This review introduces no scheduling or dependency changes. Execution evidence is supplied by the component's qualification runs; the review independently inspects source and edge tests.

## Qualification closure

Candidate `b7584c4` passes all six enabled profiles in [run 40207536973](https://github.com/Kronuz/Detent/actions/runs/40207536973): Linux GCC x86/ARM, Linux Clang, macOS x86/ARM and ASan/UBSan/LSan. The production idle/reclamation changes previously pass 224 native checks. The [5M report](../../benchmarks/system-capacity-5m-20261024.json) records successful verification of every persisted knob through the independent SDK in 83.61 seconds, with no daemon running. This closes conditions 1 and 2. Condition 3 remains the scope restriction: the two-relay/two-agent control is not qualified.

The [Intel deadline investigation](coordinator-feedback-deadline-2026-10-25.md) records 24 passing exact-profile controls and the phase timings supporting the isolated 500K fixture allowance correction. Subsequent production changes are scalar cleanup diagnostics; no scheduling optimization or dependency adoption is included. The SDK's repeated full validation on fresh client attachment is a separate measured performance blocker, not closed by this component.
