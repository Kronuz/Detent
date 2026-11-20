# Paged transfer

A transfer ships the dense pages a receiver lacks, and the receiver verifies and writes them instead of building. `src/detent_local/paged_transfer.{hh,cc}` implements [dense-distribution](../design/dense-distribution.md) D2 to D4; the [model](../design/dense-distribution-model.py) is its oracle.

## The transfer

`make_transfer(pages, read, limits)` takes the target's pages that the receiver lacks, in the target's seal order (every child before its parent, the root last), and returns them back to back: no padding, no segment table. Each page is the stored page with three fields rewritten (D3):

1. The header digest [32, 64) is zero.
2. A child that is in the transfer has a zero content digest, and its placement entry holds 12 zero bytes, its segment number from 1 and its offset in the derived layout.
3. Any other child is a base child: it keeps its content digest, and its placement entry is zero.

Keys, length and height stay in every content entry. The derived layout is `transfer_layout`, the build's rule: each page at the next 8-byte offset, in a new segment of at most 1 MiB when it does not fit. A full copy is the transfer of every page of a generation; a delta, the transfer of the target's pages whose summary differs from the base page at the same trie path.

## The receiver's checks, in order

`receive_transfer(account, stream, base, target, segment_id, limits)` takes the stream, the base generation (null for the empty base), and what the frame names: the root digest and the position's revision. It returns the segments to write, numbered from 1 with ids from `segment_id`, each with its pack checksum, and the root's reference. It throws `TransferRefused`, whose `reason()` names the check:

1. **Split and lay out** (`Framing`, `Limits`). Every page starts with `DTPAGE03` and a length of 79 to 65,536 that ends inside the stream; pages, bytes and segments stay within the limits. The pages are copied into their derived segments. An empty stream is the empty state when there is no base and the frame names the empty commitment, or the base itself when the frame names the base's root; any other empty stream is refused (`Empty`).
2. **Headers** (`Page`). Every header decodes as dense-pages' `decode_page_header` decodes it.
3. **Walk from the root** (`Layout`, `ParentFirst`, `Twice`, `BaseLacks`, `Page`). From the last page, in trie order, through `NodeAt` steps that must follow preorder: each external slot names the next content entry; a nonzero placement must name a page start of the derived layout, an earlier page, referenced once; a zero placement resolves the base page at the slot's trie path, walking the base from its root by prefixes and labels. The walk emits D5's seal order: after a node's subtrees, the transfer children it seals in `seals_before` order, and a page over 4,096 bytes after its own top node.
4. **References and order** (`Unreferenced`, `Order`). Every page but the last is referenced, and the transfer's order equals the seal order the walk emitted.
5. **Hash bottom-up**, in transfer order (`ContentDigest`, `ContentSummary`, `PageDigest`, `Page`, `Leaf`). Each content entry's keys, length and height equal its child's; a stated content digest is zero or the child's; the receiver writes the child's digest and its placement (its own segment id and offset, or the base page's). It hashes the page once (`page_commitment`), compares a stated header digest, writes the digest, then runs dense-pages' `PageValidator` with digests not compared: preorder structure, node count, labels, keys, height and D5's sealing at every node. Every leaf is checked as a first touch checks it: a definition by `validate_definition` at the frame's revision, a watermark at most that revision.
6. **The root** (`RootDigest`). The last page's digest equals the frame's. Then each segment is hashed once for its pack checksum.

Placement is outside the page digest, so the layout and order checks (1, 3, 4) are what make the receiver's files a function of the target's pages alone: a sender cannot choose segment breaks or page order. Steps 3 and 5 are the defense against a wrong tree under a root the sender names, since that root is the sending relay's (D4).

## Cost

A full copy hashes each page once and each segment once: at 5M, 306,989,416 page bytes and 315,759,038 pack bytes, as the dense build. `checked_hash_counts()` counts both.

## Tests

- `test_local_paged_transfer` builds generations with the packer and checks the round trip (pages equal outside placement, the re-made transfer of the received generation equal to the stream, the hash counters), a delta over a base, and a refusal per check.
- `test_local_paged_transfer --fixtures DIR [--only SLUG]` replays the model's fixtures (`dense-distribution-model.py check|full|delta CORPUS --out=DIR`): the full copy's root, packs byte for byte with the model's segment ids and the hash counters; the deltas' roots; and each refusal case of D4's table.
- The fixtures come from a corpus in today's definition encoding (`corpus-100k-varint.bin`, SHA-256 `39a0751b…2acf821`): the design's r12 corpora (`bb87ae5d…`, `3214561e…`) store definitions in an older body encoding, which the leaf check refuses. On that corpus largest-first packs as D5 does, so the model forges the non-canonical tree densest-first.
- `tests/detent_testing/paged_transfer_fuzz.cc` damages honest full and delta transfers: an accepted transfer must equal the honest result byte for byte. `test_testing_paged_transfer_fuzz_replay` replays seeded inputs; `-DDETENT_TRANSFER_FUZZ=ON` builds the libFuzzer binary.
