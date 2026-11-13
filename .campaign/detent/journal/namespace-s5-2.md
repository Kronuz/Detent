# namespace-s5-2: journal

State: working
Quest: .campaign/detent/quests/namespace-s5-2.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Step 1: the layout policy in `Limits`, the set-limits entry (`AdministrativeKind::SetLayout`), and
the seal in apply (ordinal, counters, accumulator, last seal index) carried by the image.

## Proposed split (for the lead)

The quest splits cleanly at the disk:

1. **s5-2a, the layout in memory** (steps 1-3): policy and set-limits entry, seals with apply's
   accumulator and ordinal, the seal-file / block / base / table formats, roll-up, squash, cut,
   carry, first bases at the enabling entry, and the three answers. Criteria A3, A6, A7 and A1's
   voter identity with a capture failure and a policy change. Nothing consumes it until S6.
2. **s5-2b, the layout on disk** (steps 4-7): files under the commit protocol, collector,
   recovery, checkpoint at the seal persisted after a durable layout, the ring keyed by namespace
   position with lazy eviction and no reset, install and repair from a peer, the crash matrix.
   Criteria A2, A4, A5 and A1's restart and install cases.

I keep going on both in this branch unless the lead splits it; each step is its own commit.

## Next

1. Step 2: history formats (record, section, block, base, seal file, table), SHA-256 names.
2. Step 3: the layout engine and answers, with an oracle over every position (A3, A6).
3. Step 4: history files over `journal::IO`, commit, collector, recovery; checkpoint at the seal.
4. Step 5: the ring keyed by namespace position, lazy eviction, no reset at restart (A2).
5. Step 6: install and repair from a peer (A4).
6. Step 7: crash matrix (A5) and the cluster identity job (A1).

## Blocked on

Nothing.

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
