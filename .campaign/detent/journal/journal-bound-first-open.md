# journal-bound-first-open: journal

State: working
Quest: .campaign/detent/quests/journal-bound-first-open.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Fix committed (journal:870382d): `Host::settle_damage` reads each log with `TurnLog::read` (writes
nothing) and stores bounds (`mark_bounds`) and losses (`mark_lost`) before the drivers write their
open records. Drivers recover against the pre-settle table, so `recovered()` is unchanged (the
`bounds` case checks `recovered().bounded`). Test journal:900a11a, `lost_bound_test first_open`:
4 rule-5 layouts; locally 66 of 138 checks fail at main's host.h, 0 of 251 with the fix.
Measuring the extra read's open cost (scratch bench `.scratch/detent/journal-bound-first-open/`).

## Next

1. A2 job 261123-110049-af725e (queued, --fails-without at 900a11a).
2. Open-cost numbers main vs fix; doc sentence with them; commit doc (A3).
3. `campaign verify --for A1`; push; ci:ci.

## Blocked on

Nothing.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), name every job you examined, failures included (`campaign brief` lists failed
jobs no journal names), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| <A1> | <from `campaign brief`> |

## Result

<When ready for review: what changed, its measured effect with numbers and the jobs behind them,
and what it leaves open. Results and how it works go in the docs of the repository it changed
(docs/perf/, docs/mechanisms/, a library's own); name them here. `campaign map` shows this.>

## Rulings I made

<Decisions inside the quest's scope, so the reviewer can check them.>
