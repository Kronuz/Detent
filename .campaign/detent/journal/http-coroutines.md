# http-coroutines: journal

State: landed
Quest: .campaign/detent/quests/http-coroutines.md (the lead's; never edit it)

History: landed at `Detent:d0a5cd8` on 2026-11-03, before this campaign; recorded with
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
| A1 | `docs/status/http-coroutines.md`: "[x] PLAN M1 done criterion 3 (no `workers`, `should_offload`, `DeferredReply`" |
| A2 | open: left unticked in the ledger; T1 (a)-(d) pass, T1 (e) is owed to S4 (Evidence S1, Next) in `docs/status/http-coroutines.md` |
| A3 | open: left unticked in the ledger; T12 runs in CI, T13 has no authority teardown test (Next) in `docs/status/http-coroutines.md` |
| A4 | open: left unticked in the ledger; T11 rows open (Next); post-W2 checks the write-deadline margin on the harness only in `docs/status/http-coroutines.md` |
| A5 | open: left unticked in the ledger; T6 passes for the classic owner, the authority rows are open (Next) in `docs/status/http-coroutines.md` |
| A6 | `docs/status/http-coroutines.md`: "[x] Classic GET and health never wait on a commit, in both modes; T2 passes deterministically" |
| A7 | `docs/status/http-coroutines.md`: "[x] `detent::assert_off_loop` guards Detent's blocking points, and moving the classic commit back inline fails T3" |
| A8 | open: left unticked in the ledger; the 560 KB and 16 MiB commit timings and the authority parse timing are owed (Next) in `docs/status/http-coroutines.md` |
| A9 | open: left unticked in the ledger; T1 and the touched suites pass under both sanitizers, T4 and the authority T6 rows are open (Next) in `docs/status/http-coroutines.md` |

## Result

S1, S6-S9 landed from `phase/w2-s1` (CI 41635725007 green) and S2-S5 through `Detent:d0a5cd8` on 2026-11-03.
Native 238/238 on Linux clang 18; T10 and T12 greps empty in CI `source-checks`; mutants per fix die.
Reviews: `docs/reviews/http-coroutines-design.md` REJECT, then re-check APPROVE WITH CONDITIONS (N1-N6 folded);
`docs/reviews/w2-s1-s8-code-review.md` APPROVE WITH CONDITIONS (CR1, CR2 closed; CR3-CR6 follow-ups);
`docs/reviews/w2-s2-code-review.md` APPROVE WITH CONDITIONS.
Flakes 3, 4, 5 and 8 remain open in the ledger's log.
Docs: `docs/design/http-coroutines.md`, `docs/mechanisms/owner-loop.md`, `classic-publish.md`, `authority-reply.md`.
Left open: A2, A3, A4, A5, A8, A9.

## Rulings I made

See Decisions 1.
