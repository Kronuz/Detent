# dd2-canonical-copy: journal

State: working
Quest: .campaign/detent/quests/dd2-canonical-copy.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Built `src/detent_local/paged_canonical_copy.{hh,cc}` (`x::canonical_copy`) and
`tests/detent_local/test_paged_canonical_copy.cc`; first local Debug build running.

## Next

1. Local run of the test (synthetic churn), then commit and push.
2. Jobs: A1+A2 at 100K (`--corpus` with the model's mixed T4 churn), the ctest suite; A3 the
   `--fails-without` job that deletes the `stable_sort` line.
3. 5M: one round (D85).

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

## Messages

- msg (owner via lead, the design is the spec): building dense-distribution.md DD2 as written; no
  deviation so far.

## Rulings I made

- Seal order is recomputed from the pages, as dense-pages D9's validators do: postorder over nodes
  in label order; a page above C is written at the end of its top node (sealed alone, D5.1 and
  D5.3); every other external child is written at its parent node, those of one node ordered by
  `seals_before(height - 1, length - 64)`, ties in label order; the root last.
- The copy writes through `SegmentBuilder` under a caller-given generation id, ordinals from 0, so
  a full build under the same id gives identical segments: A1 is checked as whole-segment byte
  and ownership equality, stronger than "every byte outside placement".
- Pages are read by header compare only (`decode_page_header` against the reference); the input
  is a validated generation and no `VerifiedPages` first touch runs, so no page digest is computed.
- One synchronous call, not a budgeted stepper: DD3 decides how the relay schedules it.
- The library target lives in `src/CMakeLists.txt` (`detent::local_canonical_copy`), the file in
  my set, not `src/detent_local/CMakeLists.txt`.
- The test's full build drives the production `PagedPacker` into `SegmentBuilder`, the snapshot
  builder's page loop, from records; T4 churn is the model's `churn` corpus applied through the
  `TrieOverlay` and `OverlayAssembler` (D7).
