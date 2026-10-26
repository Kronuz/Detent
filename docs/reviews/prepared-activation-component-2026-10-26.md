# Prepared activation component review

**Date:** 2026-10-26. **Scope:** Current uncommitted activation source against [the validated blueprint](../PREPARED_ACTIVATION_ARCHITECTURE.md). **Provisional verdict:** NO-GO for default activation: three P2 findings remain. No P1 defect identified. This review changes no production source and runs no builds or tests.

## Open findings

1. **P2: Readiness is detached from the currently selected head.** [coordinator.cc:1216](../../src/detent_delivery/coordinator.cc#L1216) combines cached `diagnostic.prepared_ready` with the current store selection. That flag is assigned only by `bind_base` at line 481; publication, fencing and closing do not invalidate it. After prepared startup, accepting selected replacement can make selection uncertain while status still reports prepared-ready; after replacement settles, it can describe a different, not-yet-confirmed head. A failed final barrier can retain the flag through fencing. Bind readiness to an owner-held exact confirmed descriptor, Known selection and an eligible lifecycle state. Test held replacement/barrier, post-replacement failure and the interval before recovery confirmation, including same-cursor maintenance.

2. **P2: Publisher readiness uses weaker ancestry checks than client attachment.** [prepared_reader.cc:70](../../src/detent_local/prepared_reader.cc#L70) opens the configured path directly and validates only its final directory. A valid prepared store reached through an intermediate publisher-owned symlink or writable ancestor can pass this probe and be announced ready, while every ordinary prepared client rejects that path. Open through the shared protected walker, then retain the existing recovered-directory identity comparison. Test the same path through publisher readiness and fresh client attachment, including rejection without readiness and accepted root-controlled aliases.

3. **P2: The preparation deadline is absent in foreground/system operation.** [daemon.cc:77](../../src/detent_cli/daemon.cc#L77) bypasses the startup timer in foreground mode; [managed_host.cc:22](../../src/detent_agent/managed_host.cc#L22) monitors indefinitely. The 180-second constant only limits the detached launcher and a later CLI retry window. An old head whose upgrade repeatedly hits disk/heap admission pressure therefore retries indefinitely under the normal service-manager invocation, contrary to the bounded preparation contract. Apply one validated finite startup deadline to managed preparation in both modes, cancel further work at expiry and drain accepted IO. Distinguish empty-store liveness from data readiness; do not restart the entire allowance after launcher settlement.

## Closed finding and source assessment

The review's idle-republication finding is closed in source: `recover(false)` preserves preparation evidence. The updated startup fixture requires unchanged readiness and selection across two failed source rounds.

Same-cursor republish uses the existing root copy/hash/seal engine, preserves actual recovered dependencies, and mints maintenance evidence only after unchanged-root checks. Caller caps remain enforced, UID trust is explicit, and prepared-mode metadata/page failures do not become CLI defaults. The new walker retains descriptor ancestry and bounds alias expansion; its platform/race qualification remains incomplete.

## Required qualification

Supplied evidence reports eight relevant native checks passing, including startup and fresh CLI corruption refusal. It does not close the open findings. Require their regressions, restart/no-repeat upgrade, flat migration and explicit full compatibility, identity reset, cancellation/crash cuts around selection and confirmation, and foreground timeout/drain tests. Add relative/absolute root-alias, parent traversal, loop, ACL, replacement-race and descriptor-cleanup cases on macOS/Linux/FreeBSD. Qualify the chosen startup allowance on preserved large state and run ordinary managed entry-point latency checks. Prior hosted matrix successes do not qualify this uncommitted activation diff; release acceptance remains separate.
