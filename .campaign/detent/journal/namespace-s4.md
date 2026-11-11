# namespace-s4: journal

State: waiting on jobs 261111-075504-ec3c00 261111-075504-e24863 261111-075545-8516fc 261111-075547-031cc4 261111-075550-246c94 261111-075554-690969 261111-075558-73943c 261111-075601-6f02c9
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

Nothing blocking, one file-set mismatch for the lead: the quest's file set is `src/detent_delivery/**`,
`src/detent_relay/**`, their includes, `tests/detent_delivery/**`, `docs/status/namespace-s4.md`, but the outcome
(no tier re-validates its own build; hash counters) lives in `src/detent_local/` (`generation.cc`,
`trusted_restart.*`, `paged_layout.*`, `paged_packer.cc`, `paged_segment_builder.cc`), with frame counters in
`src/detent_distribution/` (`frame_encoder.cc`, `framing.cc`, a new header), the agent's log in
`src/detent_agent/managed_host.cc`, and docs in `docs/mechanisms/`. No change inside the set can remove the
recovery's hashing. I edited those files; `land` refuses edits outside the set, so extend the file set
(`campaign set namespace-s4 files=...`) before landing.

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
