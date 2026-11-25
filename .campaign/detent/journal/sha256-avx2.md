# sha256-avx2: journal

State: landed
Quest: .campaign/detent/quests/sha256-avx2.md (the lead's; never edit it)

## Doing

Nothing: landed at sha256:07cdc83 on 2026-11-07 under the earlier campaign skill.

## Next

1. Nothing: landed.

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | `docs/perf/README.md`; `docs/API.md`: "else AVX2 when CPUID reports AVX2 and BMI2 and the OS saves YMM state" |
| A2 | `docs/perf/README.md`: "the AVX2 kernel is 1.3x to 1.6x the portable one and 1.2x to 1.4x behind EVP" |
| A3 | `README.md`: "A host without SHA instructions tests the portable kernel, plus AVX2 where it has it" |

## Result

Landed at sha256:07cdc83 on 2026-11-07. One commit; CI runs 42376213584 and 42484559964 green on sha256:07cdc83.

- No ledger and no review; the commit message carries the numbers.
- The first-read finding it answers is in Detent's laptop-local branch (Detent auditor's scope).
- Docs: `docs/perf/README.md`, `docs/API.md`.
- Left open: none.

## Rulings I made

None beyond the quest file's Decisions (the ledger is the record).
