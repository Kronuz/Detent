# reactor-on-wheel: journal

State: waiting on jobs 261112-115020-3a3dea 261112-115026-65553f
Quest: .campaign/detent/quests/reactor-on-wheel.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Code done at reactor:99aff1b (quest branch): accept retry and LagProbe are wheel entries; docs written.
Waiting on the exclusive A/B (261112-115020-3a3dea) and the suite (261112-115026-65553f).

## Next

1. Read the A/B lines; fill A2 numbers; record Evidence and Result.
2. A4 stays open until Xapiand's manager.cc moves (see Blocked on).

## Blocked on

A4 (remove PeriodicTimer): its last user is Xapiand's src/manager.cc (try_shutdown_timer, `reactor_events.h` include), moved by the Xapiand campaign. Not started here; A1-A3 may land early.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| <A1> | <from `campaign brief`> |

## Result

<When ready for review: what changed, its measured effect with numbers and the jobs behind them,
and what it leaves open. Results and how it works go in the docs of the repository it changed
(docs/perf/, docs/mechanisms/, a library's own); name them here. `campaign map` shows this.>

## Rulings I made

<Decisions inside the quest's scope, so the reviewer can check them.>
