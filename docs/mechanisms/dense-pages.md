> Status: CURRENT (2026-11-05) S1 codec and packer on `phase/dense-pages`; full integration and the sole-format switch require S2 through S5.

# Dense pages

DTPAGE03 puts connected trie nodes in a page with one digest. The digest covers its header, nodes and external-child summaries. Child placement sits outside that digest, so moving a page preserves its commitment. The [design](../design/dense-pages.md) defines the format and acceptance rows; the [ledger](../status/dense-pages.md) records executed evidence.

## How it works

`paged_layout.hh` defines one 65-byte `Reference`: segment, offset, length, keys, height and digest. `verify_page` binds the reference's summaries to the reached header, checks the digest, and parses the page in exact preorder. It requires forward inline slots and ordered external indices. A fixed 203-frame stack bounds the parse; it allocates no heap storage. `decode_node` bounds each visited field against the current page and its tables.

DTROOT03 occupies 320 bytes. Its reserved bytes are zero, and an absent tree is exactly 65 zero bytes. `state_commitment` returns the root-page digest for a nonempty tree and SHA-256 of the exact ASCII bytes `detent.local.empty.v3` for an empty tree. Empty roots retain their cursor and profile.

`TrieCursor` emits a leaf or branch in postorder. Its event carries child labels; `commit(budget)` advances the cursor after the caller accepts the event. `PagedPacker::push(event, body)` borrows immutable spans until `accepted()` becomes true. The caller drives `step(budget)` and consumes each `page()` before supplying another event.

The packer keeps one admitted cluster per open subtree. A branch combines its children's clusters while its encoded size fits 4,032 bytes. Above that size, it seals eligible children by increasing height, decreasing size, then increasing label. Only clusters larger than a 68-byte external entry are eligible. A leaf or branch that still exceeds the capacity seals alone. `finish()` seals the root last; an empty input emits no page.

`page()` exposes immutable bytes, their summary and the entering trie path. `commit(reference, budget)` verifies the summary and binds it to an admitted segment extent. The caller chooses segment identities and offsets; writing in emission order at aligned offsets reproduces the model's placements. Distinct occurrences retain distinct paths even when their page bytes and digests agree.

Retained controls, vectors and output buffers use `StorageAllocator` or `OwnedBuffer`, which admit capacity before allocation. `ValidationBudget` can suspend preparation, copying, hashing and publication. A stop token refuses further work and extent commitment. `discard(budget)` releases retained clusters and output in bounded turns; direct child-ledger tests check the final refund.

## Integration boundary

The codec target is `detent::local_codec`; the packer target is `detent::local_packer`. They contain the production version-3 implementation. Existing builder, graph validator, warm validator and reader consumers require the S2 through S5 migration before the complete repository builds. The S1 test postorder generator accepts the projection model's raw structural keys; production `TrieCursor` preserves the public typed-key checks.

The structural codec checks page framing and reference summaries. Definition revision/body semantics require the generation context; graph canonicity and child-to-header binding across the complete graph belong to the S2 validators. Read-time mutation containment and bitmap-hit behavior belong to S3. The S1 fuzzer covers the codec and bounded test walk, with corpus replay in CTest.
