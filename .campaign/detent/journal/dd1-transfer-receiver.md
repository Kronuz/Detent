# dd1-transfer-receiver: journal

State: working
Quest: .campaign/detent/quests/dd1-transfer-receiver.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## API

`src/detent_local/paged_transfer.hh` (namespace `detent::local::detail`), pushed for DD2-DD4 at the
commit that adds it (see `git log -- src/detent_local/paged_transfer.hh`). Entry points:
`transfer_layout`, `make_transfer` (wire form of a page list in seal order), `receive_transfer`
(D4; returns `ReceivedTransfer`: segments with ownership and pack checksum, the tree reference,
counters), `TransferRefused` with `TransferRefusal`. Stable; any change is noted here with its reason.

## Doing

Receiver implementation (paged_transfer.cc), then the model's `--out` fixtures at 100K.

## Next

1. paged_transfer.cc; hermetic test; model `--out` writer; fixture replay mode.

## Blocked on

<What, and who decides. "Nothing" when nothing.>

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
