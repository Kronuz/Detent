# Prepared activation component review

**Date:** 2026-10-26. **Scope:** Core `Detent:f27dd2d`, activation `Detent:276bc58` (merged at `Detent:205ece1`), and the inspected uncommitted owner-held readiness correction against [the validated blueprint](../PREPARED_ACTIVATION_ARCHITECTURE.md). **Provisional verdict:** Source GO; no concrete P1/P2 blocker remains in the reviewed implementation. Qualification of the final correction remains pending. This recheck runs no builds or tests and changes only this review.

## Readiness correction

The first correction compared selection against `base->generation`, which the recovery worker replaces before checking metadata. That introduced a P1 data race and allowed readiness before authentic worker settlement. The inspected final correction removes that access and duplicate cached readiness evidence.

[coordinator.cc:482](../../src/detent_delivery/coordinator.cc#L482) assigns a separate owner-held `confirmed_prepared` descriptor only in `bind_base`, after worker settlement and applicable publication confirmation. [Status at line 1223](../../src/detent_delivery/coordinator.cc#L1223) reads that evidence, requires exact Known selection, and excludes closing/fenced lifecycle states. It never inspects worker-mutated recovery results. This closes the original stale-head finding and the correction's concurrency defect in source.

Require a deterministic changed-selection regression that queries status while recovery is held in flight, then verifies readiness only after confirmation. Existing manual-original fixtures deliberately stop before this worker window. A focused race-detector check is relevant; the listed sanitizer results do not detect C++ data races.

## Source closures

1. The ancestry finding is closed: [prepared_reader.cc:70](../../src/detent_local/prepared_reader.cc#L70) uses the shared protected walker and retains the actual recovered-directory identity check.
2. Foreground preparation now has a finite monitor deadline, closes the coordinator on expiry, waits for authentic drainage, and then reports timeout. The CLI allowance begins before launching rather than restarting afterward. The inspected deadline fixture holds actual worker settlement beyond expiry and verifies no readiness and complete drainage.
3. Idle reconciliation preserves preparation evidence. The startup fixture checks stable selection/readiness across two failed source rounds.

Same-cursor republish, explicit UID trust, caller caps and corruption refusal remain within the previously reviewed mechanism.

## Evidence and remaining qualification

Supplied results report 164/164 native managed-profile checks in 1115.72 seconds; five ASan/UBSan/object-size/pointer-overflow/leak checks in 14 seconds; three targeted native status/CLI checks in 6.91 seconds; and their three sanitizer variants in 15.39 seconds. These precede the final owner-held correction. Their real DiskStore replacement/barrier operations are manually driven on the owner without yielding before reconciliation; they do not establish concurrent-recovery or complete network-path fault coverage. Targeted reruns and the Intel hosted job remain pending at this recheck.

Require final-correction regression results, remaining platform alias/ACL/race cases, restart and rollback compatibility, cancellation/crash cuts through confirmation, and preserved-large-state preparation/ordinary-entry-point measurements. A 180-second allowance is finite, not a guarantee that every supported store prepares within it. This provisional implementation verdict is not release acceptance or a universal first-read latency claim.
