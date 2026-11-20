# dd4-delta-transfers: journal

State: working
Quest: .campaign/detent/quests/dd4-delta-transfers.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Building and running the hermetic tests: `test_page_window` (diff against the occurrence-write
oracle, seal order, window bounds) and `test_delta_transfer` (D6 choice; receiver outcomes, which
link once DD1's `paged_transfer.cc` is on the branch).

## Next

1. Build + run test_page_window; push; hand to the lead for dense-distribution-build (owner: only
   suite and CI gate; 5M rounds and review run after the feature lands).

## Blocked on

Nothing. Files outside the set, needed and edited (lead to widen): `src/detent_relay/CMakeLists.txt`,
`src/detent_delivery/CMakeLists.txt`, `tests/detent_relay/CMakeLists.txt`,
`tests/detent_delivery/CMakeLists.txt`, `tests/detent_relay/delta_scenarios.{hh,py}`.

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

- Seal order from pages alone (D7.3, dense-pages D5): a page longer than 4096 B sealed at its own
  top node (the packer's Combine), so it follows its subtree directly; any other was chosen at its
  parent's node, after that node's subtree, by `seals_before` (height, then larger), ties in label
  order. Checked: the diff against the empty base equals a full build's file order.
- The Merkle diff descends base and target in step by entering path (equal prefix = same node), so
  base lookups cost O(1) per step; prune = equal digest, length, keys, height (dense-pages D7).
- Window cost: a generation retains the pages its successor lacks (the reverse diff); the window
  holds at most 64 generations and retained bytes at most the head's page bytes, oldest out first.
- D6 rule 2 tie goes to records. `sent_bytes` uses the real `encode_fragment` (zstd-3, 96 B header).
- D9 mapping: `BaseLacks` refusal or a base page's `ReadSourceFailure` -> FullCopy (D10.3); any
  other refusal -> OtherSource first, Records after one more refusal; nothing published unless
  Published.
- A1/A2 at corpus scale: `delta_scenarios.py` (imports the model) writes per-scenario patches and
  the model's wire streams; the tests build base and target in C++ and compare byte for byte (A1)
  and against an in-memory S8 block history encoded by `encode_session_frames` (A2).
