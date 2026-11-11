# namespace-s4: journal

State: waiting on jobs 261111-075504-ec3c00 261111-075504-e24863 261111-075545-8516fc 261111-075547-031cc4 261111-075550-246c94 261111-075554-690969 261111-075558-73943c 261111-075601-6f02c9 261111-080104-4bc749 261111-080106-08759e
Quest: .campaign/detent/quests/namespace-s4.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Code and tests are at `quest/detent/namespace-s4` (head fcf37aff plus journal commits); the jobs above run at it
(they are chained on one build slot, `--after`). The branch is pushed for CI (ASan, TSan, platforms).

## Next

1. When the jobs finish: paste `campaign brief`'s Evidence table below, fill the A2 numbers into the Result and
   `docs/status/namespace-s4.md`, set State to "ready for review".
2. Read CI on the pushed head; fix what it finds.

## Blocked on

Nothing. Lead rulings in: file set widened (note: `include/detent_local/paged_layout.hh` is also edited and is not
in the list; please add it); A2 is D64 (layout hashing at most 1.2 times the build's, frames reported beside it).

Relay-delta shutdown hang: attributed by two probe jobs, `relay_delta_shutdown.cc` run by
`tests/detent_delivery/relay_delta_probe.sh`: at main 261111-080104-4bc749, at the quest head 261111-080106-08759e.
Exit 124 means "did not drain". If main hangs, it is pre-existing: the probe stays unregistered in ctest and the
lead opens a follow-up. If main passes, it is this quest's to fix. (Jobs 261111-075517-c6c3c1, 075520-d8c977,
075522-766e74, 075525-3a75d9, 075528-283102 were submitted with a quoting error and failed at once; 075514-e91fbc
was cancelled; none is evidence.)

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
