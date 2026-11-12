# wheel-evidence: journal

State: waiting on jobs 261111-182307-a3c694 261111-182314-ea4b17
Quest: .campaign/detent/quests/wheel-evidence.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Rows, doc and pick done (D70, 9b5bc959). Waiting on the VM test rerun 261111-182307-a3c694 (TSan
under `setarch -R`; 261111-182307-a3c694 (ASan+UBSan and TSan under setarch -R, all ok) passed ASan+UBSan on the VM, TSan aborted with
"unexpected memory mapping", the 6.x kernel's mmap ASLR against TSan) and the suite gate
261111-182314-ea4b17.

## Next

1. Both jobs pass: paste the brief's table, State "ready for review", push.

## Blocked on

Nothing. D70 (owner): (f) is the one wheel; task-wheel closes the macOS re-arm gap first.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | 261111-130750-f599af (passed at 512798d349d2; only merges from main since) |
| A2 | 261111-121549-8cd0f8, 261111-121551-0e58b1 (passed at 40a5988ed6d4), 261111-130753-1687cf, 261111-130755-91093a, 261111-130756-004da8 (passed at 512798d349d2), 261111-143146-cf9183 (passed at 5ecbeb4c7693) |
| A3 | D70 (PLAN.md: (f) adopted on these rows; D69 applied the rule to A5/A6); rule pre-registered at 7a3682c4 before the first measured job |
| A4 | `docs/design/wheel-evidence.md`: "## The pick" |
| A5 | 261111-143146-cf9183 (passed at 5ecbeb4c7693; ignores docs-only wheel_vs_stash.sh, wheel_vs_stash_vm.sh, wheel-evidence.md) |
| A6 | 261111-144149-2a64b9, 261111-144151-4c40f1, 261111-144152-d8a935 (passed at 7c62c5dca353; ignores docs-only wheel-evidence.md), 261111-182307-a3c694 (ASan+UBSan and TSan under setarch -R, all ok) (VM tests) |

## Result

`docs/design/wheel-evidence.md` (Results: A2 rows, "Tighter single-loop rows (A5)", "Linux VM
(A6)"; The pick per host). A/A band at n = 1,000: macOS 31.1% (from 48.3%; one outlier sample),
VM 10.8%; 5% not reached. At 1M: 24.9% / 29.7%.
- macOS A5 score n=1k / 1M: (a) 37.3 / 352.7; (b) 1,106 / 1,201; (c) 857 / 594; (d) 39.4 /
  406.9 (1.06 / 1.22); (e) 41.1 / 340.7 (1.101 / 1.02); (f) 44.4 / 333.3 (1.19 / 1.00, best at 1M).
  No pick.
- VM single n=1k / 1M: (a) 40.1 / 470.4; (b) 1,001 / 1,322; (c) 847 / 634; (f) 41.6 / 465.4
  (1.04 / 1.00). Cross P=1/4/7: (b) 207/307/424; (c) 147/220/267; (f) 15.7/13.6/20.7. Mem 1M:
  (a), (f) 32 B, 0 heap per arm; (b) 51.5 B, 35 B per re-arm. Pick (f).

## Rulings I made

- Candidate (e) added after the smoke 261111-120056-0484f0 (strict, noisy, not a decision row):
  (d)'s 56-byte entry put its single-loop score 14-31% over (a)'s. (e) keeps (a)'s 32-byte entry
  and links the inbox through `next`, with (c)'s contract (other threads arm unarmed entries).
  The rule's thresholds are unchanged; its unified list grows to (b)-(e); the rows queued at
  67f3f4c3 were cancelled before running and every row runs with (e).
- Single-loop passes visit entries in one random order (D4 went in index order), so a 1M row
  pays the cache misses a real loop does; the numbers are not comparable with D4's table.
- Cross-thread producers read steady_clock every 64 arms, the same for every candidate.
- After the first rows (wheel-evidence.md, "Changes after the first rows"): candidate (f), a
  striped inbox; the cross workload 2M due in 1-50 ms -> 1M due in 1-1,000 ms, since no owner
  (stash's included) kept up; rule 3's R=10 rerun decides by its median ratio. Thresholds
  unchanged; every row reruns.
- A5/A6 (D69): the A3 rule and thresholds unchanged; the R = 10 single rows decide by median ratio
  (change 3). The minimum sample is 100 ms of timed passes on top of 1,000,000 / n repetitions.
- A6 adds a Linux run of the model, manual-clock and cross-thread tests (ASan+UBSan, TSan, g++)
  261111-182307-a3c694 (ASan+UBSan and TSan under setarch -R, all ok), since rule 2 needs the manual-clock test on the host it picks for.
- VM cross row: (c) at P = 7 prints "inconclusive" (0.63x against a 53% band); not rerun, since (c)
  fails the VM single loop by 21x at n = 1,000 whatever that row says.
- VM TSan runs under `setarch $(uname -m) -R` (ASLR off for that one process); the VM's sysctl is
  unchanged. macOS runs TSan as before.
