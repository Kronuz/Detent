> Status: CURRENT (2026-11-04) Execution ledger for the [November handoff](../handoffs/detent-codex-2026-11.md). Maintainer: Detent campaign lead.

# November campaign

State: recovery and implementation. No branch has landed under this takeover.

## Done criteria

1. [ ] Reader S6 lands with its final mutants, sanitizer gates, independent security review and green merged-head CI.
2. [ ] Legacy H incorporates S6, proves the two outstanding CI fixes and lands with green CI.
3. [ ] Namespace S3b closes the architect's leased-generation collection blocker and conditions, with probes, revert checks and green CI.
4. [ ] Namespace S1/S2 closes proof gaps, publishes measured bootstrap and filesystem results, passes architect review and lands with green CI.
5. [ ] Dense pages completes S1-S5, the damaged-page regressions, 100K/5M gate, decoder-safety and architect reviews, then lands with green CI.
6. [ ] The handoff's follow-ups receive evidence-backed dispositions; the shared-library pin wave lands after upstream CI and coordination.
7. [ ] Main's required CI is green; ledgers, mechanisms and qualification limits match the landed tree; safe worktree cleanup completes.

## Active ownership

| Workstream | Branch / worktree | State | Next |
| --- | --- | --- | --- |
| Reader recovery | `reader/s6`, `.claude/worktrees/agent-a1fbf8344cbf48ecb` | Contributor diagnosing fixed-head concurrent failure | Final proof, audit, security review, CI |
| Sync groups | `namespace/s1-s2`, `.claude/worktrees/agent-af2c08dd24001ec58` | Contributor diagnosing power-cut invariant failure | Finish bounded proof and measurement jobs |
| Dense design | Design on `main` | Independent architect reviewing format and cross-design assumptions | Fold required changes into design and criteria |
| Lead | `main` | State reconciliation and landing coordination | Prepare remaining branch recovery and review queue |

## Evidence at takeover

1. Main and `github/main` are `Detent:2ffd63f`; working tree is clean before campaign records change.
2. Reader CI run 41933091120 at `Detent:47470ea` is cancelled overall. The TSan test step is cancelled; all other executed jobs succeed. This is not a green gate.
3. The fixed-head reader soak reports zero failures in 12 isolated runs and one failure in 24 concurrent runs (`.scratch/overnight/done/detent-s6-soak-fixed.log`). The failing child log reaches the fork reply timeout after the switch stress passes. Cause remains unverified. The contributor merges main and dispatches CI run 41973979278 on `Detent:a08386d`.
4. Grouped syncs pass six targeted tests. `test_local_publication_power_cut` aborts with `snapshot invariant failed`; full `ctest` returns 8. The overnight driver is alive (PID 60892), with sanitizer configuration queued behind the exclusive hold. A narrow initial process query misses it; no duplicate jobs are queued. Sanitizers, bootstrap pairs and ext4 evidence remain owed.
5. Legacy CI run 41926685309 passes both binding jobs and macOS jobs; both Linux GCC jobs fail and TSan is cancelled. Final merged-head CI remains owed.
6. The host has an Xapiand exclusive benchmark hold at takeover. No local build or test starts while it remains exclusive. Free space is 181 GiB; load averages are 2.45, 2.88 and 3.00 at 21:24 PST.
7. The dense architect identifies a builds-on question: a digest set cannot distinguish equal pages at distinct key paths while physical extents must remain disjoint. The preliminary ruling and the occurrence-aware reuse contract are pending.

## Decisions in force

The handoff's defaults apply: read-time bounds and failure containment, cold recovery for every snapshot, the retained pack checksum pass, records for full copy, temporary removal of `detent get --defaults`, build-wide position-independent code, and leader-aware daemon readiness. The Homebrew tap remains pinned. No AWS is authorized.

## Next

1. Close the reader's concurrent failure and outstanding proof before its security gate.
2. Start S3b's fix round when a contributor slot frees. Its worktree exists despite the handoff's stale claim that it is gone; inspect status before reuse.
3. Incorporate the dense architect's builds-on findings and set numbered S1-S5 criteria before implementation.
4. Finish sync-group proof and bounded queued runs without duplicating the shared queue's work.
5. Land in the handoff's dependency order, verifying each composed tree and CI.

## Monitoring

The lead keeps the sidebar, this ledger and the status index current. A local `blockers --watch 10` process watches host contention. The tool catalog offers no session scheduling tool. `.scratch/campaign-monitor.sh` records host, branch and CI snapshots and sends a sidebar notification at odd hours, minute 17, for at most four days. Its stop file is `.scratch/campaign-monitor/STOP`. The active lead reads contributor ledgers for the narrative report; a notification alone does not continue campaign work.

## Log

1. 2026-11-04: Owner starts the handoff campaign. Reader recovery, sync-group recovery and independent dense-format review start in separate contexts. No AWS, tap update or release publication is authorized.
