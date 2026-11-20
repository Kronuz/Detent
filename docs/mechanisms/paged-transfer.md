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

## The agent's plans

The agent (`src/detent_delivery/coordinator.cc`) opens with `"pages":true` and takes a W6 pages frame two ways, neither running the packer:

- **A full copy** (`after` 0): `receive_image` names the generation as a full copy over the selected base (`next_generation(..., true)`), receives over the empty base with `transfer_segment_ids` (the segment builder's ids, from ordinal 0) and returns the build's image: its packs are byte for byte the ones a records build of the same state writes. `PagedSnapshotPlanBuilder` takes that image in place of the records encoder.
- **A delta** (`after` the store's position): `receive_delta` over the selected base, whose pages are read with first touch, then `PagedTransferPlanBuilder`: the new packs are the received ones, named by `next_generation` over the base; a walk of the base from its root that stops at the pages the transfer reuses gives the removed base pages per pack and the new root's counts, and `SparsePlanFinalizer`'s warm validator must find the same removal.

A refusal of either fails the response; the next open lists the namespace in `"records"` until records publish it (D9). A base that lacks a reused page, or fails first touch, asks a full copy (namespace D10.3).

## The relay's builds

The relay (`src/detent_relay/namespace_relay.cc`, `page_copy.cc`) answers a session that opened with `"pages":true` from full builds of the records it mirrors (D7), named as the agent names a full copy, so a cold build is its own canonical copy:

- **A full copy** goes as the newest build a delta still follows: same incarnation, not below the cut. When that build is older than the head, the build at the head starts; with no such build the position gets no frame, as a Current answer does, while one is in flight, and records when the build failed.
- **A delta** from a base the page window holds (its last 4 builds) goes as pages when D6 finds them smaller against the records answer from that base. Below the newest build it waits for the build at the head, but only when D6 rule 1 could pick pages: when the records answer is at least a third of the newest build's root page, which every delta carries one of.

`MirrorService` runs one build at a time on a second thread. The mirror thread reads the files the full copy needs and copies the layout and ring; decoding, the records encode and the build run on the builder, and a build that lands wakes the mirror thread to answer the sessions that wait for it. At 100K keys a records answer goes in 3 ms while the build runs, which takes 250 ms. `/v1/health` reports `mirror.pages`: builds, build failures, and full copies and deltas sent as pages.

## Cost

A full copy hashes each page once and each segment once: at 5M, 306,989,416 page bytes and 315,759,038 pack bytes, as the dense build. `checked_hash_counts()` counts both.

## Tests

- `test_pages_coordinator` runs an agent against a test server that sends DD1 transfers: a 100K-key cold agent boots from pages with no build hash, its descriptor and packs equal to the server's build; a clustered change of 10,000 keys comes as a delta transfer (251 KB against 2.84 MB for the full copy) and publishes with no build hash; a wrong root, for a full copy and for a delta, falls back to records.
- `test_relay_pages` runs the relay over a scripted upstream that serves real history files: the full copy reproduces the relay's build under the agent's derived ids, D6 picks records for 5 scattered keys and pages for a rewrite of all 3,000, a base outside the window keeps records, the mirror refuses a pages frame from its upstream, and in the service a records answer goes while a 100K build runs (`mutants/build-on-mirror-thread.patch` builds inline and fails it).
- `test_system_relay_pages` runs three authority voters, a relay and two agents, one on the authority (records) and one on the relay: the relayed agent boots 100K keys from one page transfer with zero bytes hashed to build, to the records agent's root, and a clustered rewrite of 55,555 keys made while the relay is paused reaches it as one pages delta (1.2 MB). `forcing/relay_pages_forced.sh` under `forcing/relay-root-forced.patch` makes every full copy name a wrong root: the agent refuses it and boots from records, to the same root, and its session then takes the delta as records.
- `test_local_paged_transfer` builds generations with the packer and checks the round trip (pages equal outside placement, the re-made transfer of the received generation equal to the stream, the hash counters), a delta over a base, and a refusal per check.
- `test_local_paged_transfer --fixtures DIR [--only SLUG]` replays the model's fixtures (`dense-distribution-model.py check|full|delta CORPUS --out=DIR`): the full copy's root, packs byte for byte with the model's segment ids and the hash counters; the deltas' roots; and each refusal case of D4's table.
- The fixtures come from a corpus in today's definition encoding (`corpus-100k-varint.bin`, SHA-256 `39a0751b…2acf821`): the design's r12 corpora (`bb87ae5d…`, `3214561e…`) store definitions in an older body encoding, which the leaf check refuses. On that corpus largest-first packs as D5 does, so the model forges the non-canonical tree densest-first.
- `tests/detent_testing/paged_transfer_fuzz.cc` damages honest full and delta transfers: an accepted transfer must equal the honest result byte for byte. `test_testing_paged_transfer_fuzz_replay` replays seeded inputs; `-DDETENT_TRANSFER_FUZZ=ON` builds the libFuzzer binary.
