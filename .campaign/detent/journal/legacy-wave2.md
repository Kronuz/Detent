# legacy-wave2: journal

State: landed
Quest: .campaign/detent/quests/legacy-wave2.md (the lead's; never edit it)

History: landed at `Detent:72ed8ee` on 2026-11-04, before this campaign; recorded with
`campaign land --record --earlier`.

## Doing

Nothing.

## Next

1. Nothing; landed.

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | `docs/status/legacy-wave2.md` |
| A2 | `docs/status/legacy-wave2.md`: "Release configure and build of every target: clean." |
| A3 | `docs/status/legacy-wave2.md`: "Full ctest: 215 of 216 pass; `test_local_trusted_paths` is skipped on this host (as before)." |
| A4 | `docs/status/legacy-wave2.md`: "run 41812264929, every job green (ASan/UBSan and TSan included)" |
| A5 | open: deferred: the leftover-reference grep across docs and website was not done; the lead made it a follow-up, not a landing condition (Evidence) in `docs/status/legacy-wave2.md` |
| A6 | `docs/status/legacy-wave2.md`; `CHANGELOG.md`: "Legacy removal (wave 2)" |
| A7 | `docs/status/legacy-wave2.md`: "Step G on merged main, head `Detent:8ec47f3`: run 41898318055, every job green" |
| A8 | `docs/status/legacy-wave2.md`: "Removed: `src/detent_http`, `include/detent_http`, `tests/detent_http`" |
| A9 | `docs/status/legacy-wave2.md`: "`detent-agent` is the managed profile only: `run_agent` throws `no source configured;" |

## Result

B-E landed `Detent:7988bc7` to `Detent:8340b53` (CI 41812264929 green, rebased on the M1 pins); F `Detent:5092933` (CI 41886875818 green).
G `Detent:4f49f4b`, merged with main and recorded at `Detent:72ed8ee` on 2026-11-04: CI 41898318055 green on every job.
F: full ctest 218/219 then fixed; G: 215/216 with one environmental skip; a mutant per step fails `test_agent_cli`.
Sanitizers came from the hosted ASan/UBSan and TSan jobs. No review is recorded in the ledger.
Docs: `CHANGELOG.md`, `docs/mechanisms/status-probe.md`, `docs/SERVICES.md`, `docs/USAGE.md`, `docs/ACTORS.md`, website pages.
Step H continues as `legacy-h`; step I (flat-v1) is a PLAN follow-up.
Left open: A5.

## Rulings I made

Decisions 1-5 of the ledger.
