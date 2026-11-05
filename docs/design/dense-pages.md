> Status: DRAFT (2026-11-04) Designing (ready for preliminary review). G1 and G2 are the lead's constraints; everything else is the designer's draft, with numbers projected (not measured) by [the model](dense-pages-model.py) from the 5M trial store's records.

# Dense pages

Many trie nodes per verified page, one header and one digest per page, in place of one node per page.

- Author: [Germán Méndez Bravo](mailto:german.mb@gmail.com). Created 2026-11-04. Ledger: `docs/status/dense-pages.md`.
- Base: `Detent:bea5c43`. Line citations are at that commit unless another is named: reader S6 at `Detent:f92535f` (`reader/s6`), S3b at `Detent:d4692ba` (`namespace/s3b`).
- Depends on: [reader.md](reader.md) D3, the verified-page bitmap, whose hit path D4 changes; [namespace-delivery.md](namespace-delivery.md) D8, derived file ids, which T8 needs, and D10, first touch, the scrub and repair. Q1 and Q2 block nothing in S1.
- Changes: reader.md D3's hit path, which gains a compare again, and its Limit 5 (D4); namespace-delivery D10's scrub only under Q1 (b) or (c), and its T9 cut point and W6 frame only under Q2 (b); the page sections of `docs/HOW_IT_WORKS.md`, `docs/mechanisms/verified-page-bitmap.md` and `docs/mechanisms/recovery-digest-once.md` (S5).
- Model: [dense-pages-model.py](dense-pages-model.py). Every count and byte figure below is projected, not measured: the model's, exact for the r12 corpus, until a C++ build reproduces it. Every time is an estimate and says so. The lead measures the gate rows.

| Review | State |
| --- | --- |
| Architect, preliminary | not started |

## Given by the lead

**G1. One format.** DTPAGE03 and DTROOT03 replace DTPAGE02 and DTROOT02. No legacy reader, mode or migration: the project is pre-release.

**G2. Old stores are refused.** A store built before this format is refused, and the agent takes a full copy, through the path a store it cannot trust already takes (namespace-delivery D10.3).

## Abstract

Today every trie node is its own page: an 80-byte header, and a 96-byte reference per child that carries two 32-byte digests (`include/detent_local/paged_layout.hh:7-37`). At 5M records that is 7,000,007 pages, and headers and references are 83% of the 1.48 GB of page bytes. Dense pages hold a connected piece of the trie, up to 4 KiB: nodes in preorder linked by 2-byte in-page slots, and one 68-byte entry per edge that leaves the page. Each page has one SHA-256 digest over everything except where its children are stored. That digest is the only one a reference carries, and the root page's digest commits to the whole state. Pages are sealed bottom-up by a rule that reads only the subtree below each node, so a page's bytes, placement aside, depend only on the records under it: two full builds give the same pages (and, with namespace-delivery D8's derived ids, the same files), and a delta writes exactly the pages a full build of its result has and its base lacks. On the 5M store the model gives 162,367 pages and 63.1 bytes per record against 7,000,007 and 295.8, 5 pages per lookup at the median and 6 at p99 against 12, 0.62 GB of SHA-256 input at a full build against 3.77 GB, and packs of 31.7 MB at zstd-1 against 530 MB. The prices are scattered deltas, which write 1.7 to 1.9 times today's bytes, and the first touch of a definition's page, which hashes 2,360 bytes at the median where today's hashes 270 (D6).

## What one node per page costs

The model walks the r12 agent store (`.scratch/capacity-trials/5000000-restore-r12/agent-0/generations`), rebuilds the trie from its 5,000,004 records and reproduces the store exactly: 7,000,007 pages, 1,479,001,197 page bytes, and, packed in postorder at 8-byte offsets, 1,441 segments of 1,509,994,805 bytes, the packs' total. So its numbers for today are the store's, and its dense numbers come from the same trie.

| At 5M (`extract`, `today`) | Bytes | Share of page bytes |
| --- | --- | --- |
| Page headers (80 B each) | 560,000,560 | 37.9% |
| Child references (96 B each, 7,000,006) | 672,000,576 | 45.4% |
| Prefixes | 4,500,029 | 0.3% |
| Bodies | 242,500,032 | 16.4% |
| Page bytes | 1,479,001,197 | 295.8 B per record |
| Packs (1,441), zstd-1 / zstd-3 | 529,843,307 / 526,744,244 | 35.1% / 34.9% of 1,509,994,805 |

A build hashes every page twice, the exact bytes and the logical commitment (`paged_layout.cc:120-169`), and every segment once more for its pack checksum (`paged_segment_builder.cc:105`): 3,774,996,569 bytes in 14,001,455 digests, about 19.7 s of one core at the review's rate (estimate: 0.3 µs plus 243 bytes per µs per digest, from `sha_page` in `docs/reviews/reader-design-preliminary.md:54`). A definition lookup walks the definition key and the namespace watermark (`paged_read_state.cc:7-37`): 12 pages for every key in this store, 10 on the definition walk and 3 on the watermark walk sharing the root. A lookup whose pages are all cold hashes 11,095 bytes at the median (11,609 at p99), about 53 µs (estimate).

## Design

### D1. One digest per page, placement outside it

A page's digest is SHA-256 over its bytes [0, 32) and [64, L − 20E), where L is its length and E its count of external edges: every byte except the digest field itself (32 to 63) and the placement table at the page's end (D2). The header's digest field holds that value. A reference to a page carries the page's digest, key count, length and height (the content entry, covered by the parent's digest) and its segment and offset (the placement entry, outside every digest).

1. **One value for both jobs.** The digest covers the page's exact bytes except placement, so it is the first-touch check. Placement-free, and fixed by the records under the page (D5), it is also the logical commitment. Today's two digests per reference (`paged_layout.hh:14-19`) and the second hash pass over every page at build and at first touch (`verify_page` computes both, `paged_layout.cc:170-186`) go.
2. **The root page's digest is the state's commitment.** Every reader of a logical digest outside the codec and its builders reads the root's: state equivalence (`paged_state_equivalence.hh:4-9`, `state_equivalence.cc:12-27`), `StateCursor::logical_commitment` (`state_cursor.cc:165-171`) and the coordinator through it (`src/detent_delivery/coordinator.cc:211,391,523,545`), `applied_base_source.hh:129`, compaction's invariant (`paged_compaction_plan.cc:44`, `sparse_plan_finalizer.cc:59-60`), the same-cursor snapshot check (`paged_snapshot_plan.cc:75-84`) and the prepared view (`prepared_reader.cc:183`). They read the root page's digest instead; no reference needs a second digest.
   - "Nothing changed" is equal root digests, as today.
   - Namespace equality: namespace-delivery D8 keeps one generation per namespace, so a generation's root digest is its namespace's commitment. Until then a generation holds several namespaces, and nothing compares namespaces inside one (the list above is complete).
   - Byte identity (T8): the digest excludes placement, so it is equal on every host even with today's random segment ids; the files are equal once namespace-delivery D8's derived ids make placement equal too.
3. **Placement is unauthenticated.** A wrong segment or offset is caught when the page it reaches fails its digest at first touch, or fails D4's compare on a hit. As today, the digests detect damage, not the publisher's account, which can rewrite any page it prepared (reader.md Limit 5).
4. **Moving a page changes no digest** (D8).

### D2. The page

```
offset     bytes  field
0          8      magic "DTPAGE03"
8          4      length L, 79 to 65,536
12         2      external edges E
14         2      nodes N
16         8      keys under the top node, the pages below included
24         1      height: pages on the longest path down, this one included
25         7      zero
32         32     digest (D1)
64         ...    N nodes in preorder, the top node first
L - 68E    48 E   content entries in slot preorder: digest 32, keys 8, length 4, height 1, zero 3
L - 20E    20 E   placement entries, same order: segment 16, offset 4
```

Nodes, integers big-endian as today, tagged with today's `PageKind` values (`paged_layout.hh:9`), so a zero byte is never a node:

- **Branch:** tag 1, prefix length (1 byte), children − 1 (1 byte), the prefix, the labels in ascending order, then one 2-byte slot per child. A slot below 0x8000 is the child's offset in the page; 0x8000 | j is content entry j. 3 + p + 3c bytes, against 80 + p + 96c today.
- **Definition and namespace leaves:** tag 2 or 3, suffix length (1 byte), body length (2 bytes), the suffix, the body. 4 + s + b bytes, against 80 + s + b.

Prefix and suffix lengths fit a byte because a key is at most 202 bytes (`paged_layout.cc:98-109`). The body bound stays today's, page_bytes − 80 − suffix (`paged_snapshot_builder.cc:195,218`, `paged_trie_overlay.cc:240`, `paged_delta_leaves.cc:193`), so every definition that fits today fits; at that bound the leaf is a page of its own of at most 65,524 bytes. Pages start at 8-byte offsets in segments of at most 1 MiB, as today (`paged_layout.cc:27-32`). The smallest page, a store's lone watermark leaf, is 64 + 4 + 3 + 8 = 79 bytes, so page starts stay at least 80 bytes apart.

An edge inside a page costs its 2-byte slot; an edge to another page costs 68 bytes, against 96 for every edge today. Only edges cross pages: every node, a leaf's whole body included, lies in one page.

### D3. References and the root

`paged::Reference` becomes segment 16, offset 4, length 4, keys 8, height 1 and digest 32: what D2's two entries hold. DTROOT03 keeps 320 bytes and DTROOT02's fields up to offset 160 (`paged_layout.cc:259-282`); the tree reference at 160 is the same 65-byte `Reference`, its keys equal to records plus namespaces, and the separate root logical digest at 248 goes (it was always the tree's, `paged_layout.cc:73`). `Root::pages`, `bytes` and `segments` keep their meaning and their limits (`:63-64`). Of the root checks that assume a node per page (`:69-74`), the page floor becomes 79 bytes, and `pages >= records + namespaces` becomes `pages < 2 × (records + namespaces)`: a page holds at least one node, and a trie of n leaves has fewer than 2n nodes. `Limits` (`paged_layout.hh:10-13`) keeps its fields and defaults; a store of the same records has about 43 times fewer pages, so the `pages` cap binds later, and the `records` cap where it does today.

### D4. Reading a page: first touch and hit

A **first touch** (the miss path, `lazy_page_access.cc:203-215`; `checked_page_bytes`, `paged_navigation.hh:20-37`):

1. Bounds from the reference as today (`:196-198`), with lengths 79 to 65,536.
2. SHA-256 over D1's two ranges, equal to the reference's digest and to the header's digest field.
3. Structure, in one preorder parse: N nodes exactly fill [64, L − 68E); every tag, length and label order is valid; each in-page slot holds exactly the offset where preorder puts that child, so one layout is accepted per page; external slots name content entries 0 to E − 1 in order; the header's keys equal the page's leaves plus its content entries' keys; content lengths are 79 to 65,536; the header's height is one more than the greatest content height, or 1 with no content entries.
4. Every leaf's body is checked as today's single leaf is (`lazy_page_access.cc:206-211`): a definition's header against the root's revision, a watermark at most the revision.
5. The page's bit is set.

A **hit** (the bit is set) compares the reference's digest with the header's digest field and the reference's length with the header's, then uses the page. reader.md D3 dropped this compare (reader.md:133): a validated generation references every page once, and today a damaged reference also fails its parent's first touch, since the parent's physical digest covers placement. D1 leaves placement outside the parent's digest, so without the compare a damaged placement entry could steer a hit to another verified page unnoticed. With it, a hit checks what today's first touch of the parent checked, and reader.md Limit 5's weakening is undone. Cost: the page's first 64 bytes, fetched beside the first node it walks (estimate; T5 measures).

**Walking a page:** at a branch, compare the prefix, find the label among the ascending labels, read the slot; an in-page slot jumps to its node, an external slot reads the content and placement entries and resolves the next page through the page table. No per-node header or digest.

The bitmap keeps its 80-byte granule (`verified_pages.hh:9-14`): pages are disjoint, start 8-aligned and are at least 79 bytes, so no two starts share a granule. Its comment's reason changes from "span at least one header" to that.

### D5. Packing: bottom-up, canonical

The packer takes the trie in postorder as `TrieCursor` emits it (`paged_trie_cursor.cc:155-170`). Each node yields a cluster, the node plus the clusters of the children it keeps, sized as its node bytes, its kept clusters, and 68 bytes per child sealed into its own page. With budget B = C − 64 for capacity C:

1. A leaf is a cluster of 4 + s + b bytes; over B, it is a page alone.
2. At a branch, children arrive as clusters or sealed pages. While its cluster exceeds B, seal one more child cluster: the shallowest first (fewest pages below it), then the largest, then the lowest label. Only clusters above 68 bytes are sealed, since sealing a smaller one adds bytes.
3. A branch still over B is a page alone, above C and below 64 KiB: at most 256 kept children of at most 68 bytes each.
4. The root's cluster is sealed last.

Every decision reads only the node's subtree: its children's cluster sizes and heights, and the length and height of its sealed children. So the page holding a node, and its bytes apart from placement, depend only on the records under that node:

1. **Full builds are identical.** Two builds of the same records give the same pages. Pages are written as they are sealed (a leaf over B when it arrives, a branch's children in the order the rule seals them, then the branch if it is a page alone, the root last), each at the next 8-byte offset and in a new segment when it does not fit in 1 MiB, so with namespace-delivery D8's derived segment ids two builds also give the same files (T2 counts them, T8 compares them).
2. **Deltas land on the full build.** After any sequence of deltas every reachable page has the bytes, placement aside, and the digest a full build of the same records gives it (T1).
3. **Not canonical after deltas:** which segment holds which page, offsets, the segment ids inside placement entries, padding, and dead bytes in older segments. These are file bytes, so a store that took deltas has a full build's pages but not its files (Q2 prices it).

The rule is the model's best of three on the 5M store ("Alternatives considered"): at 4 KiB, height-first holds a lookup to 5 pages at the median and 6 at p99, where largest-first gives 6 and 7 and keys-per-byte 5 and 7, for the same bytes within 0.02%. The content entry's height byte exists for it: a delta and a validator decide a node with no read of its sealed children.

### D6. Capacity: 4 KiB

At 5M under the height rule (`pack CORPUS 2048 4096 8192 16384 32768 65536 --rule=height`):

| Capacity | Pages | Page bytes per record | Segments | Pages per lookup, median / p99 | Cold lookup hashes, median / p99 | Definition page first touch, median / p99 |
| --- | --- | --- | --- | --- | --- | --- |
| Today | 7,000,007 | 295.8 | 1,441 | 12 / 12 | 11,095 / 11,609 B | 270 / 272 B |
| 2 KiB | 372,497 | 68.6 | 329 | 6 / 8 | 6,216 / 7,623 B | 988 / 1,964 B |
| 4 KiB | 162,367 | 63.1 | 302 | 5 / 6 | 13,151 / 13,958 B | 2,360 / 3,641 B |
| 8 KiB | 99,348 | 61.4 | 294 | 4 / 6 | 22,741 / 29,501 B | 7,145 / 7,747 B |
| 16 KiB | 48,655 | 60.1 | 288 | 5 / 6 | 54,768 / 75,616 B | 9,563 / 16,265 B |
| 32 KiB | 16,225 | 59.2 | 286 | 5 / 6 | 123,271 / 153,164 B | 23,185 / 29,366 B |
| 64 KiB | 12,625 | 60.5 | 294 | 5 / 6 | 230,674 / 300,566 B | 23,573 / 60,160 B |

Bytes written per delta, the mean over seeded trials of same-size updates to definitions and their namespaces' watermarks (`delta CORPUS 2048 4096 8192 16384 65536 --rule=height`; 2,000 trials of one key, 200 and 50 of 100, 10 of 10,000):

| Delta | Today | 2 KiB | 4 KiB | 8 KiB | 16 KiB | 64 KiB |
| --- | --- | --- | --- | --- | --- | --- |
| 1 key | 7,936 | 8,124 | 14,811 | 28,609 | 59,820 | 238,328 |
| 100 keys, uniform | 468,916 | 472,090 | 900,581 | 1,591,179 | 2,962,312 | 10,530,689 |
| 100 keys, clustered | 37,851 | 15,681 | 21,904 | 35,897 | 69,351 | 237,139 |
| 10,000 keys, uniform | 25,504,788 | 25,522,789 | 42,812,457 | 72,509,117 | 109,914,760 | 184,232,009 |
| 10,000 keys, clustered | 2,916,424 | 635,933 | 595,962 | 588,385 | 613,696 | 803,732 |
| Generation reused after 10,000 uniform | 98.3% | 92.6% | 86.4% | 76.4% | 63.4% | 39.1% |

4 KiB is where density stops paying:

1. **Above it,** the bytes fall at most 6.2% more (32 KiB), and the pages per lookup stay 5 and 6 at the median and p99; 8 KiB saves one page at the median, not at p99. Each doubling roughly doubles what a cold lookup hashes and what a changed key writes: at 8 KiB 1.7 and 1.9 times 4 KiB's, at 64 KiB 17.5 and 16 times, and 64 KiB's p99 cold lookup, about 1.24 ms of SHA-256 (estimate), misses reader T2's 1 ms on its own.
2. **Below it,** 2 KiB writes what today writes per scattered delta, but costs 8.8% more bytes, 2.3 times the pages and digests, and 8 pages at p99.
3. **At 4 KiB** a cold lookup hashes 13,151 bytes at the median against today's 11,095, about 56 µs against 53 (estimates), in 5 digests where today computes 24. Once the pages above it are verified, a lookup's first touch is its definition's page: 2,360 bytes hashed at the median and 3,641 at p99, about 10 and 15 µs, against today's leaf page, 270 bytes in two digests at 1.7 µs (estimates). That page verifies 31 records on average, so verifying the whole store hashes 306,989,416 bytes against today's 2,265,001,764.

The price is write amplification on scattered changes. A uniform delta writes 1.7 to 1.9 times today's bytes (14,811 against 7,936 for one key, 42.8 MB against 25.5 MB for 10,000), because a changed key rewrites up to 4 KiB per page on its path where today it rewrites 12 small pages. Clustered changes share pages and write 0.2 to 0.6 times today's. The generation is 4.7 times smaller, so the 10,000-key delta rewrites 13.6% of 315 MB where today it rewrites 1.7% of 1.48 GB.

C is a constant of DTPAGE03, not a build parameter: D9's canonicity check and T8's identity depend on it, so another capacity is a format change.

### D7. Deltas repack the changed paths

The overlay marks changed leaves and their ancestors as today (`TrieOverlay::change`, `paged_trie_overlay.hh:31`). The assembler (`paged_overlay_assembler.cc`) reruns D5 at each dirty node, bottom-up, with each clean child supplied from the base: a child sealed in the base by its content and placement entries, copied as they are; a child kept inline in the base by its cluster, read from the base page. When the rule now seals a clean child that was inline, the assembler writes that cluster as a new page; when it now keeps a clean child that was sealed, it reads that page and inlines its cluster. The result is D5's either way, so T1(3) holds by construction, and a clean page is never copied: reuse is by placement. A dirty page that comes out with its base page's digest, as after a change and its reversal, reuses the base page too, so a delta writes only pages its base lacks.

The write cost is whole pages: a changed key rewrites every page on its path, against every node on its path today. D6's table gives the bytes. Inserts and deletes can also move a sealing decision at a dirty node, so a clean cluster becomes a page of its own. A page is sealed by its top node's parent, and a clean node decides as before, so every such page is a child of a page on a changed path, where the assembler reaches it (the model asserts it). Diffing two full builds by digest (`churn`), 99 of the 16,616 pages that 9,800 updates, 100 inserts and 100 deletes write at 5M (220,903 of 42,821,363 bytes) are of this kind.

### D8. Compaction and relocation compute no digest

Relocation copies the pages of chosen segments and the paths above them, as today (`TrieOverlay::begin_relocation`, `paged_trie_overlay.cc:150-190`). A moved page keeps its digest, so its parent's content entry is unchanged and only placement entries are rewritten: relocation computes no page digest, and only the new segments' pack checksum hashes. The root digest cannot change, so compaction's check (`sparse_plan_finalizer.cc:59-60`) holds by construction. Today every copied page is rehashed, because each reference's placement is inside its parent's physical digest.

### D9. Validation checks canonicity

The page, graph and warm validators (`paged_page_validator.cc`, `paged_graph_validator.cc`, `paged_warm_validator.cc`) check D4's structure, the key counts, and the 8-byte cell map that keeps pages disjoint (`paged_graph_validator.cc:265-275`), and one thing more: canonicity. At each node of a page they rerun D5's decision from what the page holds (inline clusters' sizes and heights from its bytes, sealed children's lengths and heights from content entries; a length above C marks a page sealed alone) and require that the children it seals are exactly the node's external slots. By induction over pages, a generation that validates is the full build of its records, so a builder defect that packs differently is refused instead of silently losing T8 identity and delta reuse. The warm validator reads reused children's key counts from content entries; today it reads their headers (`paged_warm_validator.cc:338-355`).

### D10. What the store's other files and designs see

- **Segments and pack checksums:** unchanged. The whole-file SHA-256 per segment (`paged_segment_builder.cc:105`) is checked by segment reads (`paged_segment_read.cc:215-216`) and the scrub (`pack_scrub.cc:54`) and listed in `.live` (`live_record.cc:46`). Q1 asks whether to keep its pass at build.
- **`.live`:** the same rows (segment id, length, pack SHA-256, live pages, live bytes) and the same checks against `root.pages` and `root.bytes` (`docs/mechanisms/trusted-restart.md:44-46`), with 302 rows at 5M instead of 1,441.
- **`.read`:** unchanged (`reader_metadata.hh:7-13`: storage, generation, root checksum, counts, physical bytes).
- **Recovery** (`docs/mechanisms/recovery-digest-once.md`): discovery compares each segment's checksum against its `.owner` record (`ownership.hh:13`) and each page's one digest; the logical compare goes, so a cold recovery hashes a pack twice instead of three times. A flipped placement byte, which no page digest covers, fails the segment checksum, or, with the `.owner` record rewritten to match, the digest of the page it misdirects to.
- **Scrub:** at the default 512 KiB/s (namespace-s3 ledger, criterion 3) a pass over the 5M store's packs takes 10.0 minutes (315,759,038 bytes) instead of 48.0 (1,509,994,805).
- **Repair (S3b):** how a damaged pack is repaired is open on S3b (`docs/status/namespace-s3.md`, "Blocked on", at `Detent:d4692ba`); under either of its options the repair publishes a generation that reuses no page of the damaged pack, and neither depends on the page format. D1 makes that publication cheaper than today: a rebuilt page keeps its digest, so the pages above it are rewritten for their placement entries alone, as relocation does (D8), and no digest above it is recomputed. An opportunity, not a decision here.
- **Blocks (namespace-delivery D6):** record streams; unaffected.
- **Images (namespace-delivery D9):** `detent-image` runs the agent's full-copy build at ordinal 0, so D5 makes images byte-identical to an agent's build once namespace-delivery D8's ids land (T8).
- **Reader S6** (`reader/s6`): the core is unchanged; it holds a `PageTable` and a `PagedReadState` per generation (`src/detent_reader/core.cc:96-106` at `Detent:f92535f`). Below it, `PageTable::resolve` (`lazy_page_access.cc:204-242` there) gains D4's hit compare (`:214-220`) and D4's first touch in place of `verify_page` and the one-leaf check (`:221-229`); `PagedReadState::lookup` (`paged_read_state.cc:38-105`) walks in-page nodes. `reader_bench` and reader T1 are rerun; "pages verified per lookup" keeps its meaning. S6 lands first and waits on nothing here.

## Projected 5M totals

Counts and bytes are projected, not measured: the model's, exact for the r12 corpus. Every time is an estimate except the measured build CPU, which is the cold-bootstrap note's (`docs/perf/cold-bootstrap-5m.md` at `Detent:d995e86`).

| At 5M | Today | Dense pages, 4 KiB |
| --- | --- | --- |
| Pages | 7,000,007 | 162,367 |
| Page bytes | 1,479,001,197 (295.8 B per record) | 315,432,480 (63.1 B per record) |
| Packs, raw | 1,441 segments, 1,509,994,805 B | 302 segments, 315,759,038 B |
| Packs, zstd-1 / zstd-3, each pack whole | 529,843,307 / 526,744,244 B | 31,671,463 / 30,775,854 B |
| SHA-256 input at a full build | 3,774,996,569 B in 14,001,455 digests, about 19.7 s (estimate) | 622,748,454 B in 162,669 digests, about 2.6 s (estimate) |
| Pages per lookup, median / p99 | 12 / 12 | 5 / 6 |
| Cold lookup hashes, median / p99 | 11,095 / 11,609 B, about 53 / 55 µs (estimate) | 13,151 / 13,958 B, about 56 / 59 µs (estimate) |
| First touch of a definition's page, median / p99 | 270 / 272 B, about 1.7 µs (estimate) | 2,360 / 3,641 B, about 10 / 15 µs (estimate) |
| Bytes hashed to verify every page | 2,265,001,764 | 306,989,416 |
| Bytes written per delta of 1 / 100 / 10,000 keys, uniform | 7,936 / 468,916 / 25,504,788 | 14,811 / 900,581 / 42,812,457 |
| Bytes written per delta of 100 / 10,000 keys, clustered | 37,851 / 2,916,424 | 21,904 / 595,962 |
| Agent CPU for a cold build | 125 s (r12 binaries, measured) | at most about 108 s (estimate: 17 s less SHA-256; encoding and writing 1.19 GB fewer bytes not estimated) |
| Full copy on the wire | records, about 28 MB (estimate: the note's 100K measurement scaled) | the same records, or files of 31,671,463 B at zstd-1 |
| Link speed above which files beat records | 1.8 MB/s today, 4.3 MB/s after namespace-delivery D11 (the note's estimates) | about 13 to 32 KB/s (estimate, below) |

Files beat records above (F − R) / S, with F the files' bytes, R the records' and S the agent time a copy of files saves: 277 s today and about 115 s after namespace-delivery D11 (the note's estimates). Dense pages take F − R from about 500 MB to about 3.7 MB. S shrinks too, since the build gets cheaper, by what T6 measures; even at S = 1 s the crossover would be 3.7 MB/s. The corpus's bodies are synthetic and compress unusually well, but files and records carry the same bodies, so it is F − R, the digests and trie structure against the record framing, that sets the crossover (Q2).

## Gate rows

The packer and the codec are deterministic, so the rows on counts and bytes are equalities with the model, not ranges. The lead measures every timed row on `main`.

| Row | Target | Procedure |
| --- | --- | --- |
| T1 Round trip and canonicity | (1) Every key's body comes back exact through encoded pages and every absent key misses, including a 256-way branch, a body at today's bound, a page sealed alone and a store holding only a watermark. (2) Two full builds of the same records give identical files. (3) After any sequence of deltas (updates, inserts, deletes, a namespace added and removed), the reachable pages' digests and the root digest equal a full build's of the same records. (4) The validators refuse a page packed against D5, a slot off preorder and a content entry with a wrong height. | Unit and property tests in `tests/detent_local` with the model as oracle: the C++ build of the synthetic corpus (`dense-pages-model.py synthetic OUT`) and of the 100K store's records (`extract .scratch/capacity-trials/100000-r3/agent-0/generations OUT`) gives the root digest and page count that `bytes CORPUS 4096 --rule=height` prints, and the delta from a corpus to the one `churn CORPUS 4096 --rule=height --out=AFTER` writes gives AFTER's root digest; a seeded test of 1,000 rounds of 1 to 500 random changes, each round against a full rebuild. Mutants, each failing a test: the packer sealing one child too many, the hit compare removed, the canonicity check removed. |
| T2 Size | At 5M: 162,367 pages, 315,432,480 page bytes and 302 segments of 315,759,038 bytes, the model's figures; zstd-1 of each pack recorded, against the model's 31,671,463 bytes (Python 3.15 `compression.zstd`, so not an equality) | Build the 5M store from the r12 store's records (`extract .scratch/capacity-trials/5000000-restore-r12/agent-0/generations OUT`) with the new build; count pages and sum pack sizes |
| T3 Pages per lookup | At 5M, over every definition, the definition and watermark walks touch 5 pages at the median and 6 at p99 and at most, the model's figures | A test walk over all 5,000,000 definitions of the T2 store counting distinct pages per lookup |
| T4 Delta bytes | A delta writes exactly the pages a full build of its result has and its base lacks. At 5M, for the model's two seeded deltas: one update writes 5 pages of 15,691 bytes; 9,800 updates, 100 inserts and 100 deletes write 16,616 pages of 42,821,363 bytes, 99 of them (220,903 bytes) off every changed path. New segment bytes recorded beside them | `dense-pages-model.py churn CORPUS 4096 U I D --rule=height --out=AFTER` prints the counts and writes the changed corpus; the agent's delta path takes the T2 store from CORPUS's records to AFTER's, and its new pages are counted by digest against the base's |
| T5 Reader | reader.md T1 (warm p50 and p99, hot and scattered) and T2 (first read below 1 ms) on dense pages, against `main`'s numbers just before S3; pages verified per lookup recorded | `benchmarks/reader_bench` and reader.md's T2 procedure over the T2 store, four interleaved repetitions under `quiet exclusive` (the lead) |
| T6 Build | SHA-256 input at a 5M full build equals the model's 622,748,454 bytes in 162,669 digests; build CPU and wall recorded against today's (no target: the projection is a bound) | `DETENT_READ_COUNTERS` `bytes_hashed` and `paged::page_digests()` around the T2 build; CPU and wall under `quiet strict` (the lead) |
| T7 Relocation | Relocating every segment of a 100K store computes no page digest (`paged::page_digests()`, `paged_layout.hh:47-48`, unchanged), hashes only the new segments, and leaves the root digest unchanged | A unit test over `TrieOverlay::begin_relocation` |
| T8 Identity | namespace-delivery T6 holds on DTPAGE03 files | namespace-delivery T6's procedure, after its S7 derived ids |
| T9 Refusal | A DTPAGE02 store is refused at cold start; the agent takes a full copy and serves from it | A fixture store from today's build; a system test |
| T10 Decoder safety | First touch and the validators on arbitrary bytes: one CPU-hour of fuzzing with zero findings and a hang timeout, the corpus replayed in CI; the security reviewer's decoder-safety review | A libFuzzer target over D4's first touch and walk; the harness rewrites the digest field so inputs reach the structure checks |

## Format changes

DTPAGE03 and DTROOT03 (D2, D3). The generation id's layout version (namespace-delivery D8.1) moves with them. A DTPAGE02 or DTROOT02 file is refused (G2). The logical-commitment domain (`paged_layout.cc:147`), the two-digest `Reference` and `Child` (`paged_layout.hh:14-23`), the 80-byte `PageHeader` and the reserved `Overflow` kind, which no tree holds today (`docs/HOW_IT_WORKS.md:89`), go.

## Alternatives considered

**Fixed-size pages, padded.** Every page C bytes, as a B-tree. At 4 KiB the mean page is 1,943 bytes, so padding would take the packs from 315 MB to 665 MB (162,367 × 4,096). Variable lengths cost nothing: segments already pack pages at 8-byte offsets.

**Two digests per reference, as today.** Placement inside the physical digest, a logical digest beside it: 32 more bytes per edge, a second hash of every page at build and at first touch, and relocation rehashes every copied page. It buys what D4's 32-byte compare buys.

**Placement inside the one digest.** No hit compare, but the digest then depends on placement: compaction rehashes every copied page, and the root digest moves when nothing changed, so "nothing changed" and T8 need a second commitment, which is the alternative above.

**Content-addressed pages behind an index.** References carry only digests and a per-generation table maps digest to place. Moving a page touches nothing above it, but every page hop on the warm path adds a probe of that table, and the table is one more file to publish, verify and keep in memory.

**Largest-first and keys-per-byte sealing.** At 4 KiB they pack within 0.02% of the height rule's bytes (315,397,368 and 315,436,836 page bytes against 315,432,480) and need no height byte, but give 6 and 7 pages per lookup at median and p99 (largest-first) and 5 and 7 (keys per byte), against 5 and 6 (`pack CORPUS 4096 --rule=largest,density,height`).

**Optimal packing.** Lukes' and Gil and Itai's dynamic programs over subtree sizes are also bottom-up, so also canonical and delta-local, and can beat the greedy's page hops: a counting bound gives at least 4 pages for the deepest key at 4 KiB (a page holds at most 56 edges out, at 71 bytes each with label and slot, and at most 126 of these leaves, and 126 × (1 + 56 + 56²) < 5,000,000), against the greedy's 6. They cost a table of costs per node in the builder and in D9's check. Not taken until T5 shows page hops dominate.

**Larger pages.** From 8 to 64 KiB the bytes fall by at most 6.2% (59.2 to 61.4 B per record against 63.1), the pages per lookup stay 5 and 6 at median and p99 (8 KiB: 4 and 6), and a cold lookup hashes 1.7 to 17.5 times as much (D6). At 64 KiB the p99 cold lookup is about 1.24 ms of hashing (estimate), past reader T2's 1 ms on its own.

**A 2-byte segment index in placement** in place of the 16-byte id: 14 bytes per edge, 2.3 MB of 315 MB at 5M, for a segment table that must stay stable while generations share segments. Not worth a second table.

## Limits

1. A changed key rewrites every page on its path, whole (D7; D6's table gives the bytes).
2. A first touch hashes and checks a whole page to read one leaf: at 4 KiB a definition's page hashes 2,360 bytes at the median, against today's 270 (D6). A definition at the semantic limit of 33,816 bytes (`docs/HOW_IT_WORKS.md:89`) is a page alone of at most 34,086 bytes; the format's bound is 65,524.
3. Pages are canonical after deltas, files are not (D5).
4. A hit reads its page's header for D4's compare.
5. A branch whose children are many and small makes a page above C, at most 18,445 bytes (a 202-byte prefix and 256 children of at most 68 bytes each, with the header); none occurs in this corpus.
6. One-byte prefix and suffix lengths rely on keys of at most 202 bytes (`paged_layout.cc:99`); a longer key limit is a format change.
7. **The corpus is synthetic:** decimal counters in four namespaces, with 73-byte bool and 24-byte int bodies. Two reshapings bracket it at 4 KiB (`--namespace=load0`, `--shape=hex`). One namespace alone, 1,250,001 records with 73-byte bodies, packs to 88.1 B per record, 15.1 beyond its bodies against the store's 14.6, with 5 and 6 pages per lookup. Hashed 16-character local names pack to 74.8 B per record, 26.3 beyond the bodies, with 5 and 6 pages per lookup, where today's format takes 299.2 B and 11 and 12 pages on the same names. The model takes any DTPAGE02 store, so a real key set reruns in minutes.

## Open questions

Neither blocks S1 or S2; the format does not depend on either answer.

**Q1. Keep the pack checksum's pass at build?** It hashes every segment byte once more after the page digests have hashed all but padding and placement: 315,759,038 of the 622,748,454 bytes a 5M build hashes, about 1.3 s of one core (estimate). (a) Keep it: the scrub (`pack_scrub.cc:54`), segment reads (`paged_segment_read.cc:215-216`), `.live` rows, recovery's discovery and S3b's trigger use it unchanged, and it is the one check over placement and padding before a walk reaches them. (b) Replace it with a digest of the segment's page digests in order, 32 bytes per page hashed (about 5.2 MB at 5M); the scrub then checks page by page against each header and finds damaged placement only through references, and `.live`, the scrub, segment reads and S3b change with it. (c) Drop it and scrub by walking pages from the root. *Recommendation: (a)*, until T6 shows the pass matters; it is the lead's or the architect's call, since (b) and (c) change namespace-delivery D10's scrub and S3b.

**Q2. Ship files, or records, for a full copy?** Dense packs compress to 31,671,463 bytes at 5M, against today's 529,843,307 and the records' estimated 28 MB, so files now cost about what records cost on the wire ("Projected 5M totals"). With D5 and namespace-delivery D8's derived ids, a full build's files are canonical: a server that builds a copy's generation once ships every cold agent the files it would have built, and the server already builds and caches each full copy by name (namespace-delivery.md:145).

- (a) Records, as today; every cold agent builds (125 s of agent CPU at 5M today, less with dense pages).
- (b) Files for a full copy: the server builds the copy at ordinal 0 once, and the agent writes the files, validates them (D9) and serves. About 3.7 MB more per copy (estimate) and no build on the agent. A store that took deltas has a full build's pages but not its files (D5), so the server builds the copy's files apart from its own store: one full build per position it serves, shared by every agent that asks. It changes the full-copy frame (namespace-delivery W6, :344) and T9's cut point, whose full-copy time includes the agent's build (:333).
- (c) Pages by digest: a store asks for the pages whose digests it lacks. Pages are canonical after deltas where files are not, so (c) serves a store that took deltas, a repair (D10) and a cold copy alike, paying only for what is missing; it is a new protocol beside the blocks the owner chose for catch-up (namespace-delivery D6).

*Recommendation: (a) for S1 to S5, then (b) as its own design* once the lead has measured the records' wire bytes at 5M (the note's open item 3) and T6's build: on these numbers (b) saves each cold agent its build for about 13% more bytes. (c) waits for a repair or copy cost that needs it. The full-copy wire format is the owner's call.

## Build plan

This plan is written to be executed without the designer. Every count and byte figure in it is projected (the model's, for the r12 corpus), not measured: a step meets a count or byte figure when the C++ build reproduces it exactly, and the lead measures every time.

**Shape.** The work is one series on one branch under `phase*/` or `design/` (CI runs on both: `.github/workflows/ci.yml:4`), with one commit or more per step and CI green at every step. The format switches once (G1), so S1 to S5 land on `main` together, after S5 and the series' reviews: the code reviewer on the series, and the security reviewer on S1's decoder and S3's walk (T10). S2's and S4's tests can be written beside S1 against its interfaces.

**The oracle.** The oracle is `docs/design/dense-pages-model.py`, run as `/usr/local/bin/python3 -I` (3.15.2 here) under a light hold.

- `synthetic OUT` writes the synthetic corpus.
- `extract STORE OUT` writes a store's records.
- `bytes CORPUS 4096 --rule=height` prints the pages, page bytes, segments, segment bytes, page-digest input and root digest that a correct full build reproduces.
- `churn CORPUS 4096 U I D --rule=height --out=AFTER` writes a delta's result and prints the pages and bytes that the delta writes.
- `check DIR` runs the model's self-check.

Take each root digest from a fresh `bytes` run, never from a sweep log: the node tags changed after the 5M sweep (D2), which moves every digest but no count.

**Landing order against the work in flight.**

| Work | What it shares with this series | Order |
| --- | --- | --- |
| Reader S6 (`reader/s6`, `Detent:f92535f`) | `PageTable::resolve` (`lazy_page_access.cc:204-242` there) and `PagedReadState::lookup` (`paged_read_state.cc:38-105`) | S6 lands first. S3 is written against S6's `resolve` (D10) |
| S3b (`namespace/s3b`, `Detent:d4692ba`) | The scrub, `.live` and trusted restart, and their byte-flip tests: `test_testing_trusted_restart` (`tests/detent_testing/test_trusted_restart.cc` there) and `tests/detent_local/test_paged_hash_once.cc` | S3b lands first, and its tests pass on DTPAGE03 before the series lands. Its open repair ruling does not depend on the page format |
| Namespace S1 and S2 (completion's file syncs and D11's grouped publication syncs, `namespace-delivery.md:427-436`) | Publication and syncs, not the codec | Either order: the later one rebases. The lead records T6's wall on a `main` that has both |
| Namespace S7 (derived ids) | Segment and generation names | Independent. T8 runs once both have landed |

**S1. Codec and packer.** Depends on nothing.

- Files:
  - `include/detent_local/paged_layout.hh` and `src/detent_local/paged_layout.cc` (D2, D3): DTPAGE03 and DTROOT03 encode and decode, D1's one digest, the 65-byte `Reference`, and D4's first-touch structure check as a function.
  - A new `src/detent_local/paged_packer.{hh,cc}` (D5): a pure function from the cursor's postorder events (`paged_trie_cursor.cc:155-170`) to sealed pages, in seal order.
  - `tests/detent_local/test_paged_layout.cc` and a new `tests/detent_local/test_paged_packer.cc`.
  - A libFuzzer target over D4's first touch and the walk, built the way `tests/detent_testing/live_record_fuzz.cc` is.
- Done:
  - T1(1) passes on the synthetic corpus and on the 100K store's records, including a 256-way branch, a body at today's bound, a page sealed alone and a store holding only a watermark.
  - For both corpora, the page count, page bytes and root digest equal a fresh `bytes` run's.
  - A test kills each of these mutants: sealing one child too many; largest-first in place of shallowest-first; sealing clusters of 64 bytes in place of only those above 68; and the height byte left out of the digest.
  - T10's fuzzing runs for one CPU-hour (for example 4 jobs for 15 minutes) with zero findings, and CI replays its corpus.

**S2. Full build and validation.** Depends on S1.

- Files:
  - `paged_page_builder.cc`, `paged_segment_builder.cc` and `paged_snapshot_builder.cc` (all in `src/detent_local`) emit through the packer: seal order, 8-byte offsets, 1 MiB segments.
  - `paged_page_validator.cc`, `paged_graph_validator.cc` and `paged_warm_validator.cc` check D4's structure and D9's canonicity.
  - Recovery's discovery takes D10's digest count, and every consumer in D1.2 reads the root page's digest.
  - The root check becomes D3's (it replaces `paged_layout.cc:74`), and DTPAGE02's build path is deleted.
- Done:
  - T1(2) and T1(4) pass.
  - For the 100K store, the segment count and segment bytes equal the model's.
  - At 5M, from the r12 records:
    - T2: 162,367 pages, 315,432,480 page bytes, and 302 segments of 315,759,038 bytes.
    - T3: 5 pages at the median, and 6 at p99 and at most.
    - T6's hash input: 622,748,454 bytes in 162,669 digests, from `bytes_hashed` and `paged::page_digests()`.
  - `test_paged_hash_once.cc` refuses each flip inside a page and inside placement.
  - A test kills each of these mutants: the canonicity check removed, the height check removed, and pages written out of seal order (a two-segment build's placement offsets must follow seal order).

**S3. Reading.** Depends on S2, and on reader S6 having landed.

- Files:
  - `lazy_page_access.cc`: in `resolve`, D4's first touch replaces `verify_page` and the one-leaf check, and the hit compare is added.
  - `paged_navigation.hh` (`checked_page_bytes`).
  - `paged_read_state.cc`: the walk through in-page nodes.
  - `verified_pages.hh`: the granule's comment.
- Done:
  - The reader suites and `test_testing_trusted_restart` pass on DTPAGE03: a damaged read fails, and none returns a wrong value.
  - A test redirects a placement entry to another verified page and gets a source failure. With the hit compare removed (the mutant), the test fails.
  - First touch counts one digest per page (`pages_verified`, `bytes_hashed`).
  - T5 is the lead's, after landing.

**S4. Deltas and relocation.** Depends on S2.

- Files: `paged_trie_overlay.{hh,cc}`, `paged_overlay_assembler.cc`, `paged_delta_leaves.cc`, `paged_reuse.cc` and `paged_contextual_reuse.cc` (D7, D8).
- Done:
  - T1(3) passes: 1,000 seeded rounds of 1 to 500 changes, each against a full rebuild.
  - T4 at 5M:
    - One update writes 5 pages of 15,691 bytes.
    - 9,800 updates, 100 inserts and 100 deletes write 16,616 pages of 42,821,363 bytes, 99 of them (220,903 bytes) off every changed path.
  - T7 passes.
  - A test kills each of these mutants:
    - Skipping the off-path seals fails T1(3) and T4.
    - A relocation that rehashes fails T7's zero-digest count.
    - Rewriting a dirty page whose digest is unchanged fails a test that a delta with no net change writes no page.

**S5. Refusal, removal and notes.** Depends on S2 to S4.

- Files:
  - The cold-start path that decodes the root: it refuses a DTPAGE02 or DTROOT02 store, and the agent then takes a full copy (T9).
  - Deleted: the logical commitment and its domain strings (`paged_layout.cc:147`, `:189`), the two-digest `Reference` and `Child`, and `PageKind::Overflow` (`paged_layout.hh:9`).
  - Updated: `docs/HOW_IT_WORKS.md`'s page section, `docs/mechanisms/verified-page-bitmap.md` and `docs/mechanisms/recovery-digest-once.md`.
  - A new `docs/mechanisms/dense-pages.md`.
- Done:
  - T9 passes.
  - A search of the tree finds no logical digest field, no `detent.local.logical.v2` string and no `Overflow` page kind.
  - The code review and the security reviewer's decoder-safety review (T10) clear.

**After landing (the lead).**

- T5, and T6's CPU and wall, on `main` under quiet holds.
- T8, once namespace S7 has landed.
- The calls on Q1 and Q2.
