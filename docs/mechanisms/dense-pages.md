# Dense pages

DTPAGE03 puts connected trie nodes in a page with one digest. The digest covers its header, nodes and external-child summaries. Child placement sits outside that digest, so moving a page preserves its commitment. The [design](../design/dense-pages.md) defines the format and acceptance rows; the [ledger](../status/dense-pages.md) records executed evidence.

## How it works

`paged_layout.hh` defines one `Reference` encoding, the 68-byte child entry: a 48-byte content entry (digest, keys, length, height, 3 zero bytes) and a 20-byte placement entry (segment, offset). A page keeps the halves in its content and placement tables; the root keeps them adjacent. `decode_child` reads both, so page edges and the root share one decoder, and `validate(const Reference &)` checks a reference without encoding it. `verify_page` binds the reference's summaries to the reached header, checks the digest, and parses the page in exact preorder. It requires forward inline slots and ordered external indices. A fixed 203-frame stack bounds the parse; it allocates no heap storage. `decode_node` bounds each visited field against the current page and its tables.

**In-page walks.** Every walk through a page's node region holds its position as a `paged::NodeAt` (`paged_node_step.hh`), and `NodeAt::decode` is the only caller of `decode_node`. A walk starts at the page's first node and only moves forward: `after(node)` steps to the node that follows one it read, and `child(parent, slot)` steps to an inline child, refusing with `invalid_argument` a slot that does not start at or past the parent's end. `decode_node` keeps every node inside the node region, so each walk ends within the page. Read state, the cursor, the overlay, the assembler, contextual reuse, the validators, the packer's cluster scan and first-touch verification all step this way; preorder validators also compare each `child` step with the next position.

DTROOT03 occupies 320 bytes. The tree reference's content entry is [160,208) and its placement entry [208,228); every reserved byte, [228,320) included, is zero, and an absent tree is exactly 68 zero bytes. `state_commitment` returns the root-page digest for a nonempty tree and SHA-256 of the exact ASCII bytes `detent.local.empty.v3` for an empty tree. Empty roots retain their cursor and profile.

`TrieCursor` emits a leaf or branch in postorder. Its event carries child labels; `commit(budget)` advances the cursor after the caller accepts the event. `PagedPacker::push(event, body)` borrows immutable spans until `accepted()` becomes true. The caller drives `step(budget)` and consumes each `page()` before supplying another event.

The packer keeps one admitted cluster per open subtree. A branch combines its children's clusters while its encoded size fits 4,032 bytes. Above that size, it seals eligible children by increasing height, decreasing size, then increasing label. Only clusters larger than a 68-byte external entry are eligible. `seal_eligible` and `seals_before` in `paged_layout.hh` state that rule once; the packer and the page validator's canonicity replay both call them. A leaf or branch that still exceeds the capacity seals alone. `finish()` seals the root last; an empty input emits no page.

`page()` exposes immutable bytes, their summary and the entering trie path. `commit(reference, budget)` verifies the summary and binds it to an admitted segment extent. The caller chooses segment identities and offsets; writing in emission order at aligned offsets reproduces the model's placements. Distinct occurrences retain distinct paths even when their page bytes and digests agree.

Retained controls, vectors and output buffers use `StorageAllocator` or `OwnedBuffer`, which admit capacity before allocation. `ValidationBudget` can suspend preparation, copying, hashing and publication. A stop token refuses further work and extent commitment. `discard(budget)` releases retained clusters and output in bounded turns; direct child-ledger tests check the final refund.

## Integration boundary

The codec target is `detent::local_codec`; the packer target is `detent::local_packer`. Full builders, validators, readers, deltas and compaction use the same version-3 format. The test postorder generator accepts the projection model's raw structural keys; production `TrieCursor` preserves the public typed-key checks.

- `verify_page` and `decode_node` check one page's framing, summaries and preorder structure; definition revision and body semantics need the generation context and are checked by the walks that hold it.
- `PageValidator` adds D5's canonicity replay for one page, `GraphValidator` binds every child reference to its reached header and keeps extents disjoint across the graph, and `WarmGraphValidator` does both for a successor's new pages while skipping only proven base occurrences.
- `PagedReadState::lookup` and `LazyPageAccess::resolve` bound every visited node at read time; a bitmap hit compares the reached header with the reference ([verified-page bitmap](verified-page-bitmap.md)).
- `tests/detent_local/paged_codec_fuzz.cc` fuzzes the root and page decoders and a bounded inline walk, and its corpus replays in CTest (`test_local_paged_codec_replay`). `test_local_dense_reads --fuzz SECONDS SEED` fuzzes checksum-correct mutations through lookup, the page, graph and warm validators.

## Deltas and relocation

A delta is two steps: `TrieOverlay` (`paged_trie_overlay.cc`) records the changed trie as fresh nodes over the base generation, then `OverlayAssembler` (`paged_overlay_assembler.cc`) reruns D5 over those fresh nodes and writes the pages that differ.

**Bounded overlay walk.** `change(key, body)` walks from the root to the key and converts each node on the path into a fresh `OverlayNode`, whose untouched children stay base references or in-page offsets. Every base page it reads comes through `checked_page_bytes`, so first touch applies. An inline step is accepted only forward (a slot at or past the node's end), a key-count walk inside a page visits at most the header's node count on a 203-frame stack, and key depth never exceeds 202 bytes. Each step is charged to the `ValidationBudget`, so a long delta suspends between steps, and a damaged base page throws `invalid_argument`; the walk never loops or reads outside the page.

**Reuse at corresponding paths.** The assembler pushes fresh nodes into the shared `PagedPacker` in postorder. A clean child of a fresh node goes in whole: `push_reference` for a page sealed alone (above the capacity), `push_cluster` with its base bytes otherwise, so the packer can inline or seal it exactly as a full build would. For each page the packer emits, `corresponding(path)` walks the base along the page's entering path. When the base page there has the same digest, length, keys and height, the new generation keeps that base extent instead of writing a page. That is the only reuse: no search for equal content elsewhere and no digest deduplication.

**One extent per occurrence.** `touch` records every base extent the delta read or kept; keeping one twice fails (`require(!extent.retained)`). An extent that is read but not kept is charged as removed from its segment, and the live counts are then checked against the base's. Two equal pages at different paths therefore occupy two extents, and no extent has two parents.

**Off-path seals.** When a changed branch grows past the capacity, the packer may seal a clean inline sibling that had no page of its own in the base. That page has no corresponding base page, so it is written, even when another kept page has identical bytes. Such a page is always a direct child of a changed-path page, because the assembler pushes clean children whole and never descends into them.

**Relocation computes no page digest.** `begin_relocation(packs, maximum_recipes)` walks every page from the root, with at most 8 selected packs and a recipe limit. A page in a selected pack, or above one that moved, gets a recipe: its bytes, copied unchanged, and its old reference. The assembler writes each copy with only its placement entries rewritten (`encode_placement` of each child's new location) and keeps its reference's content entry. A moved page keeps its digest, so its parent's content entry and the root digest stay the same. Relocation hashes only the new packs' checksums; the hash counters prove it (relocation leaves `HashCounts::page_calls` at 0 in `test_local_dense_delta_assembly`).

## Refusal and reclamation

The agent store holds `owner.lock` for its life. Startup checks each table entry's `.image`, `.read` and `.live` against the entry (the root's SHA-256 and length, `.read`'s SHA-256, `.live`'s bindings). A root in another framing than DTROOT03 or DTIMAGE1, damage, or a `.lease` that is not empty fails the entry; the store fences and keeps every file, including `selected`. There is no older layout to recognize or migrate (D43): the operator bootstraps a fresh store, and the agent's ordinary bootstrap takes a full copy.

A page cannot name its own format. Bytes that are not DTPAGE03 are damage: the decoders throw `invalid_argument`, first touch reports `ReadSourceFailure`, and damage never removes a table entry. Trusted restart maps packs without scanning them; its first-touch and scrub checks keep that boundary. A reader that meets a generation of another format reports `GenerationUnavailable`.

Collection (`AgentStore::collect`) removes every generation the table does not reach, its `.lease` first under an exclusive lock, and every segment no remaining `.live` names. A generation a reader holds keeps its files and the segments its `.live` names; the collection runs again after a backoff. Names the store does not own stay untouched. `test_foreign_generation` fences a store whose entry names another format's generation and keeps its files.
