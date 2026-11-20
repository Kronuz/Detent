# dd1-transfer-receiver: journal

State: working
Quest: .campaign/detent/quests/dd1-transfer-receiver.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## API

`src/detent_local/paged_transfer.hh` (namespace `detent::local::detail`), first pushed at
`Detent:1e410613`. Changed once, at `Detent:c5fdfdbe`: `receive_transfer` takes a `TransferTarget`
(root digest, revision, maximum overrides) instead of a bare digest, and `TransferRefusal::Leaf` is
new, because D4.2 checks every leaf body against the frame's position (dense-pages D4.4). Entry points:
`transfer_layout`, `make_transfer` (wire form of a page list in seal order), `receive_transfer`
(D4; returns `ReceivedTransfer`: segments with ownership and pack checksum, the tree reference,
counters), `TransferRefused` with `TransferRefusal`. Stable; any change is noted here with its reason.

## Doing

Owner folded DD1 into dense-distribution-build: gates are the suite and CI; mutants, the fuzzing
hour, review and 5M run after landing. Doing: build, hermetic test, the model's 100K fixtures
(job `261120-044550-64acb6`, in `.scratch/detent/dd1-transfer-receiver/fx100k`) byte for byte.

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

1. The library target `detent::local_transfer` is declared in `src/CMakeLists.txt` (the file set's
   CMake file), after `add_subdirectory(detent_local)`, not in `src/detent_local/CMakeLists.txt`.
2. The receiver takes a stream only (D2: no padding, no segment table), so the model's "P1 handed
   over as segments" row has no C++ input; P1's test is the stream row, refused by the layout check.
   Its mutant lays pages out where the stream's placements name them (the sender's layout).
3. Mutant patches and the runner live in `.scratch/detent/dd1-transfer-receiver/` (outside the file
   set: they patch dense-pages' validators); the job names them by absolute path.
4. The leaf check covers the transfer's pages only; base pages were verified when they were written.
