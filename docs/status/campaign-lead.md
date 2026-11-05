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
| Reader recovery | `reader/s6`, `.claude/worktrees/agent-a1fbf8344cbf48ecb` | Checkpointed at `Detent:78195dd`; queued proof and hosted CI pending | Read results, resume contributor, audit and security review |
| Sync groups | `namespace/s1-s2`, `.claude/worktrees/agent-af2c08dd24001ec58` | Contributor diagnosing power-cut invariant failure | Finish bounded proof and measurement jobs |
| Dense design | `phase/dense-pages`, `.claude/worktrees/dense-pages-series` | Designer correcting preliminary findings DP-1 through DP-6 | Settle occurrence/root/summary contracts and run corrected model |
| Damaged-pack repair | `namespace/s3b`, `.claude/worktrees/agent-a70bf4a502ea488db` | Contributor closes architect findings after merging main | Original probes, revert checks, final CI |
| Lead | `main` | State reconciliation and landing coordination | Prepare remaining branch recovery and review queue |

## Evidence at takeover

1. Main and `github/main` are `Detent:2ffd63f`; working tree is clean before campaign records change.
2. Reader CI run 41933091120 at `Detent:47470ea` is cancelled overall. The TSan test step is cancelled; all other executed jobs succeed. This is not a green gate.
3. The fixed-head reader soak reports zero failures in 12 isolated runs and one failure in 24 concurrent runs (`.scratch/overnight/done/detent-s6-soak-fixed.log`). The failing child log reaches the fork reply timeout after the switch stress passes. Cause remains unverified. The contributor merges main and dispatches CI run 41973979278 on `Detent:a08386d`.
4. Grouped syncs pass six targeted tests. `test_local_publication_power_cut` aborts with `snapshot invariant failed`; full `ctest` returns 8. The overnight driver is alive at takeover (PID 60892), with sanitizer configuration queued behind the exclusive hold. The lead stops it and queued child 26601 at 21:36 before either starts CPU work: its remaining build/test/timing holds exceed 30 minutes. Both PIDs and the waiting sleep child are gone. Replacement bounded jobs preserve the remaining sanitizer, bootstrap and ext4 obligations.
5. Legacy CI run 41926685309 passes both binding jobs and macOS jobs; both Linux GCC jobs fail and TSan is cancelled. Final merged-head CI remains owed.
6. The host has an Xapiand exclusive benchmark hold at takeover. No local build or test starts while it remains exclusive. Free space is 181 GiB; load averages are 2.45, 2.88 and 3.00 at 21:24 PST.
7. The dense preliminary review requires DP-1 through DP-6. The lead records the review and K1 through K15 in `Detent:d5fa05d`. Occurrence-aware reuse, empty roots and child-summary binding need explicit contracts before affected implementation.
8. Reader candidate `Detent:31ac047` adds deterministic selected-replacement proof and fork diagnostics; no local verification runs under the exclusive hold. Hosted run 41974132576 covers that code. The queue holds `detent-s6-final-proof.sh` and `detent-s6-t3-gate.sh`; the latter requires a successful preflight and runs each sanitizer trial in a separate bounded hold. Ledger-only head is `Detent:78195dd`.
9. Sync candidate `Detent:630b2f3` has hosted run 41974346242. Its session-counter lifecycle fix is `Detent:0cd1527`; the identical baseline patch is `Detent:327ca58` over `Detent:7a4425a`. T7 accounting fix `Detent:522452a` separates startup counters. All runtime proof remains pending. The proof job mutates an isolated source archive, never the live overnight driver's source.
10. Sync CI pin-check rejects its newer completion pin against journal's declaration; [the shared decision record](../SHARED_LIBRARY_DECISIONS.md#grouped-file-sync-dependency-closure-november-4) records the closure request to the peer. Main pins remain consistent. No shared library is edited.
11. Dense contract corrections are `Detent:d379f69` on `phase/dense-pages`. The independent proof auditor grades them thin: tiny model checks are queued, root-byte fixtures and actual external-entry mutation remain future C++ proof. A fresh architect at explicit maximum effort checks the changed foundational contracts, with no implementation approval inferred from source inspection.

## Decisions in force

The handoff's defaults apply: read-time bounds and failure containment, cold recovery for every snapshot, the retained pack checksum pass, records for full copy, temporary removal of `detent get --defaults`, build-wide position-independent code, and leader-aware daemon readiness. The Homebrew tap remains pinned. No AWS is authorized.

## Next

1. Read the reader's hosted and queued results, resume its contributor for any failures, then run an independent proof audit and security review. The fork timeout remains open until attributed.
2. Finish S3b's active fix round; verify original reviewer probes and dying reverts. Its existing worktree is retained.
3. Verify dense contract corrections against DP-1 through DP-6. Use a fresh architect at explicit maximum effort for unresolved foundational changes, before the affected implementation; avoid another broad design survey.
4. Finish sync-group proof and bounded replacement runs. The contributor bounds the bootstrap selection loop and helper cleanup; require smoke before long confirmation. Read final hosted results, then close the library dependency gate with upstream CI.
5. Land in the handoff's dependency order, verifying each composed tree and CI.

## Monitoring

The lead keeps the sidebar, this ledger and the status index current. A local `blockers --watch 10` process watches host contention. The tool catalog offers no session scheduling tool. `.scratch/campaign-monitor.sh` records host, branch and CI snapshots and sends a sidebar notification at odd hours, minute 17, for at most four days. Its stop file is `.scratch/campaign-monitor/STOP`. The active lead reads contributor ledgers for the narrative report; a notification alone does not continue campaign work.

## Log

1. 2026-11-04: Owner starts the handoff campaign. Reader recovery, sync-group recovery and independent dense-format review start in separate contexts. No AWS, tap update or release publication is authorized.
