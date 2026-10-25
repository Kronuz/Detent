# Recovery and delivery component review

**Date:** 2026-10-25. **Reviewed source:** `d0c6d27`. **Verdict:** Conditional acceptance; no blocking production defect found. Runtime and matrix conditions remain open.

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
