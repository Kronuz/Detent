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

The codec target is `detent::local_codec`; the packer target is `detent::local_packer`. Full builders, validators, readers, deltas and compaction use the same version-3 format. The test postorder generator accepts the projection model's raw structural keys; production `TrieCursor` preserves the public typed-key checks.

The structural codec checks page framing and reference summaries. Definition revision/body semantics require the generation context; graph canonicity and child-to-header binding across the complete graph belong to the S2 validators. Read-time mutation containment and bitmap-hit behavior belong to S3. The S1 fuzzer covers the codec and bounded test walk, with corpus replay in CTest.

## Refusal and reclamation

Managed startup holds the stable `owner.lock`. Recovery authenticates `selected`, the owned generation lease, `storage.meta` and the root checksum before it recognizes the exact DTROOT02/version-2/320-byte framing. Only ownership version 2 can produce `ObsoleteGeneration`, which retains the generation's shared lease through census (`src/detent_local/generation.cc:333`). Ownership encoding emits the current paged version; decoding recognizes version 2 solely for refusal and cleanup.

Startup counts all named files, validates the refused selection against that census and removes `selected` through a completion request. It credits that descriptor only after a directory sync, then reports Ready with an absent selection. The agent's ordinary bootstrap path requests a complete record copy. A crash before removal repeats refusal; a crash after removal starts without a selection. A partial replacement copy remains unselected and follows ordinary artifact cleanup (`src/detent_local/disk_store.cc:383`).

The generation collector takes an exclusive lease before removing owned unselected artifacts. Its version-2 root-prefix checks supply cleanup framing and cursor bindings; they expose no reader. Payload removal, directory sync, lease removal and another directory sync precede quota credit. The segment sweep reclaims owned packs after remaining roots permit a complete mark pass. An external reader holding the version-2 lease keeps the root and its packs charged; releasing the lease and requesting collection permits their reclamation. Unowned files remain charged and untouched.

DTPAGE02 framing raises `UnsupportedFormat` when a full validator or first touch reaches it. Bytes under version-3 ownership are damage and never authorize automatic selection removal. Trusted version-3 restart maps packs without scanning them; its first-touch and scrub checks retain that boundary. Unknown root framing, checksum failure and conflicting ownership fence startup while preserving `selected`.

`test_local_dense_refetch` uses a frozen nonempty version-2 fixture generated and value-checked against Detent:785fb34. It exercises process death before removal, between removal and its barrier, after durable absence, and during a short replacement-root write. Each restart takes a full copy, checks exact values, collects owned debris and compares the disk ledger with a fresh named-file census. Separate cases hold the old lease, preserve foreign files, refuse embedded old pages and retain damaged selections.
