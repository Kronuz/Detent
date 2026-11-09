# quickstart-managed: journal

State: landed
Quest: .campaign/detent/quests/quickstart-managed.md (the lead's; never edit it)

History: landed at `Detent:bea5c43` on 2026-11-04, before this campaign; recorded with
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
| A1 | `docs/status/quickstart-managed.md`: "The seven pages. Each publish is one `/v2/transactions` call through a `publish KEY VALUE` shell helper" |
| A2 | `docs/status/quickstart-managed.md`: "Every read printed the value the page states." |
| A3 | `docs/status/quickstart-managed.md` |
| A4 | `docs/status/quickstart-managed.md`: "run 41905491096: `build` green" |

## Result

Landed `Detent:0593628`, `73669a9`, `f1fcb56`, `bea5c43` on 2026-11-04.
Every `sh` fence run with `sh -v` and `zsh -v`; every read printed the stated value.
Pages run 41905491096: build green; deploy refused off `main`, as expected.
The ledger names the lead's review as its next step; the lead landed it.
Docs: `QUICKSTART.md`, website `quickstart.md`, `settings-and-flags.md`, `rollouts.md`, `group-overrides.md`, `next.md`, `index.mdx`.
Left open: none.

## Rulings I made

None.
