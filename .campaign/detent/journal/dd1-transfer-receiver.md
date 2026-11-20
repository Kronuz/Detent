# dd1-transfer-receiver: journal

State: ready for review
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

Done for the folded quest (dense-distribution-build): codec, receiver, tests, 100K fixtures.

## Next

1. After landing (no longer gates): mutant job (`.scratch/detent/dd1-transfer-receiver/mutants.sh FIXTURES`, `--fails-without`), one CPU-hour of libFuzzer (`-DDETENT_TRANSFER_FUZZ=ON`), the 5M round on a current-format corpus.

## Blocked on

<What, and who decides. "Nothing" when nothing.>

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| A1 (100K) | `test_local_paged_transfer --fixtures fx100k-varint` (fixtures job `261120-053017-1bd05e`): root `1f457c20…fb86534` equal, 5 packs byte for byte with the model's ids |
| A2 (100K) | same run: page bytes hashed 4,793,219, pack bytes 4,953,350, equal to the model's counters; 5M not run (no longer a gate) |
| A3 (100K) | same run: u:1, u:2000, c:2000, m:98:1:1 roots equal; pages, page bytes hashed, base children and walks equal the model's |
| A4 | the same run refuses all 16 model cases for their reasons; the hermetic test refuses one case per refusal kind. Mutants written, not run (no longer a gate) |
| A5 | `test_testing_paged_transfer_fuzz_replay` 300 inputs pass; fuzz hour and sanitizers: CI and after landing |
| A6 | `docs/mechanisms/paged-transfer.md` |

## Result

`src/detent_local/paged_transfer.{hh,cc}`: `make_transfer` (D2, D3 wire form), `receive_transfer`
(D4: framing, layout, root walk with base paths and seal order, bottom-up hash with dense-pages'
`PageValidator`, first-touch leaf checks, root, pack checksums). The model gains `--out`.
How it works: `docs/mechanisms/paged-transfer.md`.

Finding: the design's corpora (r12 5M `bb87ae5d…`, 100K `3214561e…`) store definitions in an older
body encoding; D4.2's leaf check (as today's first touch, `validate_definition`) refuses them, so
fixtures use `.scratch/okt/corpora/corpus-100k-varint.bin` (`39a0751b…2acf821`). A2's 5M figures
(306,989,416 / 315,759,038) are for the old corpus; a 5M round needs `corpus-5m-varint.bin`, whose
counters will differ.

## Rulings I made

1. The library target `detent::local_transfer` is declared in `src/CMakeLists.txt` (the file set's
   CMake file), after `add_subdirectory(detent_local)`, not in `src/detent_local/CMakeLists.txt`.
2. The receiver takes a stream only (D2: no padding, no segment table), so the model's "P1 handed
   over as segments" row has no C++ input; P1's test is the stream row, refused by the layout check.
   Its mutant lays pages out where the stream's placements name them (the sender's layout).
3. Mutant patches and the runner live in `.scratch/detent/dd1-transfer-receiver/` (outside the file
   set: they patch dense-pages' validators); the job names them by absolute path.
4. The leaf check covers the transfer's pages only; base pages were verified when they were written.
5. Fixtures from the current-format 100K corpus (see Result). On it largest-first packs as D5 does,
   so the model forges the non-canonical tree densest-first (falls back only when largest-first is
   canonical).
6. A model case "flipped leaf body byte, wire form" may be refused by the leaf check before the root
   (the model checks no leaf, its Limit 9); the replay accepts `Leaf` there. On this corpus it was
   refused for the root digest.
