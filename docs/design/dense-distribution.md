> Status: DRAFT (2026-11-09), a measured design record. The owner withdrew the direction that receivers should build nothing (`.campaign/detent/PLAN.md`, D33), so this document sets no goal: it records what each encoding costs, the receiver that verifies pages against one root digest, and the rule the numbers support. No implementation quest starts from it until a goal asks for one. D1 to D9 are the designer's. The full-copy wire format is reserved to the owner (D20), so D2 and D3 are recommendations until Q2 is ruled, and the delta encoding is Q1. Every byte figure comes from the [model](dense-distribution-model.py) in the campaign job named beside it. Page bytes are exact for the 5M corpus. Record bytes are modeled and calibrated against the measured record copy. No time is measured here.

# Dense distribution

A full copy as verified pages costs what records cost on the wire, and its receiver checks and writes what it is sent instead of building. A scattered delta as pages costs 38 to 123 times its records; a clustered one about the same. The rule the numbers support is to send each update in whichever encoding is smaller.

- Author: [Germán Méndez Bravo](mailto:german.mb@gmail.com). Created 2026-11-08. Quest `dense-distribution`.
- Base: `Detent:8874c71` (`main`); owner direction D33 at `Detent:75c6c99`. The page format is [dense-pages.md](dense-pages.md) as implemented on `quest/dense-pages` at `Detent:28d3ed9`, cited as dense-pages D*n* with line numbers at that commit. [namespace-delivery.md](namespace-delivery.md) is cited as namespace D*n*.
- Depends on dense-pages D1 (one digest, placement outside it, :46), D5 (canonical bottom-up sealing, :110), D7 (occurrence reuse, :162-168), D8 (relocation computes no digest, :170) and D9 (canonicity, :174). On the namespace side it depends on D6 (records history and the cut), D7 (relays), D8 (derived ids, `namespace-delivery.md:233-239`) and D10 (first touch, scrub, repair, `:261-267`).
- Changes two things:
  - namespace W6 (`:344`): a frame names its target's root digest and carries pages or records.
  - dense-pages Q2 (:268): answered by D2 and Q2 here.
- Keeps namespace D10.3 (`:267`): repair is a full copy, which D2 makes a 25.7 MB pages copy with no build.
- Model: [dense-distribution-model.py](dense-distribution-model.py) imports [dense-pages-model.py](dense-pages-model.py) at `Detent:28d3ed9` (SHA-256 `96dfd0a7…3906bd1`), passed as the job ref `DENSE`.
  - Corpus: the r12 5M corpus (SHA-256 `bb87ae5d…52bc835`).
  - Its receiver replays every transfer. It must reproduce the target's root digest, and for a full copy the build's files byte for byte.
  - Jobs, all at `Detent:29b5e53` with `--ref DENSE=28d3ed9` and the corpus and its SHA-256 in `--env`, each exit 0: full copy `261109-030634-8606dc`, deltas `261109-030635-4e209f`, crossover sweep `261109-030636-4ce079`, a day of polls `261109-030637-311a76`, the 100K self-check and refusals `261109-030632-2d5f94`, and the design review's probes `261109-030633-20ab89`.

| Review | State |
| --- | --- |
| Design | CHANGES at `Detent:d874004` (DD-1 to DD-5 blocking); revised here |

## Abstract

Today a cold agent receives 25.6 MB of records and builds its store: at 5M a DTPAGE02 build hashes 3.77 GB and writes 1.51 GB. Dense pages make the build canonical, and a canonical build can be shipped instead of repeated.

This design ships a **transfer**: the pages the receiver lacks, back to back in the target's seal order. It leaves out what the receiver can derive itself: the segment layout, each page's digest, the digests of children inside the transfer, and placement in the sender's segments.

The receiver handles a transfer in five steps:

1. It lays the pages out by the build's rule.
2. It checks every page as a first touch does, and reruns the packing decision at every node as the validators do, so a tree packed any other way, or sent in any other order, is refused.
3. It resolves children it already holds by their trie path in its base.
4. It hashes every page bottom-up, which it would do anyway to verify the pages, and fills in the fields that were left out.
5. It publishes only if the root digest equals the one the frame names.

A full copy is the transfer from the empty base. At 5M it is 25.7 MB as sent, against 25.6 MB of records, 1.002 times. The receiver's output is byte-identical to a full build, with the same 622.7 MB of SHA-256 input as the dense build. It does not decode records, build a trie or pack.

A delta is the transfer from the receiver's base. Scattered updates cost far more as pages than as records: 6.7 MB against 108 KB for 10,000 uniform updates, 62 times. Clustered updates are near parity: 63 KB against 55 KB for 10,000 consecutive keys. The numbers support sending each update as whichever encoding is smaller (D6). Under that rule, pages win only for clustered changes of about 100,000 keys and more, and for full copies. Sending every delta as pages would cost SWEEP_RANGE times the records' bytes per scattered update, and 1.5 GB per agent-day at namespace D6's moderate churn polled every minute, against 12 MB of records (Q1).

## What the measurements say

| At 5M | Bytes as sent | Raw | Receiver SHA-256 input | Receiver writes |
| --- | --- | --- | --- | --- |
| Records full copy, measured (`docs/perf/cold-bootstrap-5m.md:118-123`) | 25,646,713 | 261,337,255 | builds: see below | builds |
| Records full copy, modeled | 25,911,641 (calibration ×0.990) | 291,340,194 (×0.897) | | |
| Today's DTPAGE02 build from records | | | 3,774,996,569 B in 14,001,455 digests | 1,509,994,805 B, 1,441 packs |
| Dense build from records | | | 622,748,454 B in 162,669 digests | 315,759,038 B, 302 packs |
| Transfer, "stored" form (packs as built) | 36,719,840 | 315,432,480 | | |
| Transfer, "digests" form (D3, digests kept) | 36,800,022 | 315,432,480 | | |
| **Transfer, "wire" form (D3)** | **25,705,725** (1.002× records) | 315,432,480 | 306,989,416 page + 315,759,038 pack = 622,748,454 B in 162,669 digests | 315,759,038 B, identical to the build |

Source: job `261109-030634-8606dc`. "As sent" is today's framing: 64 KiB fragments, each zstd-3, with a 96-byte header each (`include/detent_distribution/framing.hh:7`). A transfer's raw bytes are its pages back to back; the receiver's 302 segments add the 8-byte alignment padding. Compressing per 1 MiB segment instead gives 23,102,008 B for the wire form at zstd-3 and 20,441,963 at zstd-9 (Q3).

The modeled records stream comes within 1% of the measured one as sent (×0.990) and within 11% raw (×0.897); at 100K the same model comes within 2% as sent (×0.979, job `261109-030632-2d5f94`). Delta record sizes below are modeled and scaled by the 5M factors.

**A1, the receiver's work.**

1. **Hashing and writes.** The receiver of a full copy hashes what the dense build hashes: 622.7 MB in 162,669 digests, about 2.6 s of one core at dense-pages' rate (an estimate from `dense-pages-model.py:81-82`, not a measurement). It writes the same 316 MB. Against today's build that is 6.06 times less SHA-256 input and 4.78 times fewer bytes written.
2. **What it skips.** It decompresses 25.7 MB to 315 MB where the build decompresses 25.6 MB to 261 MB. Then it skips the build itself: decoding 5,000,004 records, the trie, the packer and the node encoder. That skipped work has no C++ counter today. The r12 agent spent 125 s of CPU on its DTPAGE02 build (`docs/perf/cold-bootstrap-5m.md`, phase table); a C++ receiver would have to be timed against the dense build to price the difference.
3. **What would cut the pack pass.** Half the hashing is the pack checksum `.live` needs (dense-pages Q1, ruled (a) for S1 to S5). Q1's (b) or (c) would remove those 316 MB.

| At 5M, one update (wire form for pages) | Occurrence writes | Pages as sent | Pages raw | Records as sent (modeled) | Records raw (modeled) | Pages / records as sent |
| --- | --- | --- | --- | --- | --- | --- |
| 1 key, uniform | 5 pages | 6,024 | 15,212 | 160 | 194 | 37.6 |
| 100 keys, uniform | 334 | 203,514 | 913,634 | 1,507 | 6,143 | 135 |
| 694 keys, uniform (a minute of namespace D6's moderate churn) | 1,847 | 1,038,489 | 5,258,725 | 8,464 | 40,357 | 123 |
| 1,000 keys, uniform | 2,540 | 1,403,314 | 7,236,393 | 12,037 | 57,869 | 117 |
| 6,944 keys, uniform (ten minutes of that churn) | 12,434 | 5,428,894 | 32,867,132 | 76,842 | 400,785 | 70.7 |
| **10,000 keys, uniform** | 16,489 | 6,710,310 | 42,435,495 | 108,240 | 575,208 | **62.0** |
| **T4 churn: 9,800 updates, 100 inserts, 100 deletes** | 16,616 | 6,792,600 | 42,821,363 | 108,472 | 570,114 | **62.6** |
| 100,000 keys, uniform | 79,978 | 18,436,352 | 168,787,212 | 861,766 | 5,759,212 | 21.4 |
| 300,000 keys, uniform | 136,337 | 24,642,024 | 269,527,690 | 2,341,715 | 17,276,902 | 10.5 |
| 1,000,000 keys, uniform | 161,904 | 28,102,893 | 315,256,189 | 7,355,195 | 57,627,380 | 3.82 |
| 2,000,000 keys, uniform | 162,216 | 28,807,525 | 315,737,295 | 13,883,436 | 115,250,486 | 2.07 |
| 100 keys, clustered | 10 | 6,579 | 28,314 | 860 | 8,129 | 7.65 |
| 1,000 keys, clustered | 41 | 10,635 | 103,202 | 5,559 | 79,893 | 1.91 |
| **10,000 keys, clustered** | 369 | 62,986 | 901,414 | 55,246 | 797,501 | **1.14** |
| 100,000 keys, clustered | 3,621 | 556,402 | 8,828,306 | 549,849 | 7,973,642 | 1.01 |
| 1,000,000 keys, clustered | 28,824 | 4,713,346 | 38,233,520 | 5,185,423 | 35,760,514 | 0.91 |

Sources: jobs `261109-030635-4e209f` (the bold rows and the 1-key row), `261109-030637-311a76` (the 694- and 6,944-key rows) and `261109-030636-4ce079` (the sweep). The occurrence writes reproduce dense-pages' projections: 5 pages for one update, and 16,616 pages of 42,821,363 page bytes for T4 (dense-pages T4).

In every scenario the pages with content the base lacks are exactly the occurrence writes, so dense-pages' path-based reuse sends nothing a content-addressed scheme would avoid.

**A2, the crossover.** Under uniform churn, pages never beat records. The ratio falls from 135 times at 100 keys to 2.07 times at 2,000,000 keys (40% of the store), where the pages are already nearly the whole store: 162,216 of 162,367. Clustered changes cross at about 100,000 consecutive keys (1.01 times), and pages win at 1,000,000 (0.91 times).

Raw bytes do not predict the winner near parity: at 1,000,000 clustered keys the raw ratio is 1.07 and the ratio as sent is 0.91. D6's rule therefore compresses both encodings when they are close.

## Design

### D1. One state, one root digest, two encodings

Every answer carries a frame header with the target: namespace, incarnation, position, and the state commitment, which is the root page's digest, or the empty commitment for an empty state (dense-pages D3). The body is one of two encodings:

1. **Pages**: a transfer (D2).
2. **Records**: today's frame body (namespace D6 and W6).

A receiver publishes a generation only when its own state commitment equals the header's. For pages the receiver computes the commitment while hashing (D4). For records it is the root of the receiver's D7 result. This is the quest's Decision 3, one verification whichever encoding arrives. The records path gains an end-to-end check it lacks today: a receiver whose incremental build diverges from the sender's refuses the result and asks for pages (D9).

The sender is the relay that built the target from records (D7). The authority builds no pages, so the digest proves agreement between builder and receiver, not the authority's account (Limit 1).

**Alternatives.**
- No target digest, as today: a divergent delta publishes silently.
- An authority-computed digest: the authority would have to build pages for every namespace at every position, inside the consensus process.

### D2. The transfer: what the receiver lacks, in seal order

A transfer is the target's pages that the receiver's base lacks under dense-pages D7's identity: the same trie path before the top node's prefix, with equal digest, length, keys and height. It sends them back to back in the target's seal order (dense-pages D5.1), which puts every page after its children. It has no padding and no segment table.

The receiver lays the pages out by the build's rule: each at the next 8-byte offset, in a new segment of at most 1 MiB when it does not fit, segments numbered from 1. It refuses a transfer whose order is not the seal order it recomputes from the pages, and placement that names any other layout (D4), so the layout is a function of the target's pages alone. It writes those segments under its own segment ids.

- **Full copy** = the transfer from the empty base: every page, in a full build's layout. With namespace D8's derived ids at build ordinal 0, the receiver's packs are the build's packs. The model's replay of the 5M full copy reproduces all 302 packs byte for byte and the root `67c70ad3…ab7f07` (`8606dc`). An image built by `detent-image` from the same transfer is the same files (namespace T6 by construction).
- **Delta** = the transfer from base R to target h: dense-pages D7's occurrence writes, including off-path seals.

Transfers are immutable and canonical. Any relay produces the same bytes for the same base and target, so a transfer is named and cached like namespace D6's blocks:

- a full copy as (namespace, incarnation, position, root digest);
- a delta as (namespace, incarnation, base root digest, target root digest).

**Alternatives.**
- Ship the sender's files as stored. After deltas a relay's files are not canonical (dense-pages D5.3) and hold dead pages.
- Use a depth-first page order. It would make received copies differ from built ones: two layouts.
- Ship content the base lacks anywhere, by digest. It saves nothing on these corpora (D2's equality above), and it needs a digest index on the receiver.

### D3. The wire form: derived fields zero

A transfer page is the stored page with three fields rewritten:

1. **The header digest** [32, 64) is zero.
2. **A child inside the transfer** has a zero content digest. Its placement entry holds (12 zero bytes, segment number from 1, offset) in the layout D2 derives, and must name a page start of that layout.
3. **A child the receiver already holds** (a base child) keeps its content digest. Its placement entry is all zero.

Keys, length and height stay in every content entry, so the receiver checks each against the child it reaches.

**Alternatives.**
- **The "digests" form**: the same, with every digest kept. A damaged page then fails at that page instead of at the root. At 5M it costs 43% more on a full copy (36.8 MB against 25.7 MB) and 14% more on a 10,000-key delta (7.62 MB against 6.71 MB). Digests are SHA-256 output and do not compress, which is most of the gap.
- **Pages as stored**: 36.7 MB.

Damage in transit is a fragment's problem (D8). A wrong page from a correct transport means the sender is wrong, and that is handled per sender (D9).

### D4. The receiver verifies; it never builds

For each transfer, in order:

1. **Split and lay out.** Pages follow one another, each with magic `DTPAGE03` and a length of 79 to 65,536 (dense-pages D2). The receiver places them by D2's rule, and every in-transfer placement entry must name a page start of that layout.
2. **Parse each page as a first touch does** (dense-pages D4.3): N nodes exactly fill [64, L − 68E); tags and lengths are valid; each branch has at least two labels, strictly ascending; in-page slots hold their preorder offsets; external slots name content entries 0 to E − 1 in order; header keys equal the page's leaves plus its content keys; header height is one more than the greatest content height, or 1. Leaf bodies are checked against the frame's position as dense-pages D4.4 checks them against the root's revision.
3. **Walk from the root**, the last page, in trie order. Each external slot resolves to an earlier page of the transfer, referenced once, or, when its placement is zero, to the base page at the slot's trie path with the stated digest, keys, length and height ("base lacks a page" otherwise). At every node the receiver reruns dense-pages D5 from what the page holds, as D9's validators do: the children it seals must be exactly the node's external slots. The walk yields the seal order those pages imply, and the transfer's order must equal it. These are the validators, not the packer.
4. **Hash bottom-up**, in transfer order. Each child's keys, length and height must match its content entry. The receiver fills the content digest, rewrites placement to its own segment id (or the base page's placement), hashes dense-pages D1's ranges and fills the header digest.
5. **Check the root.** Every page but the last must be referenced, and the last page's digest must equal the frame's. Then the receiver writes the segments, computes their pack checksums for `.live`, and publishes as namespace D8 and D11 publish.

A transfer with no pages is a full copy of the empty state when the frame names the empty commitment (dense-pages D3), and the receiver publishes an empty generation; over a base whose root the frame names, it writes nothing. Any other empty transfer is refused.

Pages verified here are verified by the tier that writes them, as namespace D10 asks of a build. Readers still verify on first touch (reader.md D3); nothing changes for them.

Steps 2 and 3 are the only defense against a wrong tree, since the root in the frame is the sending relay's (Limit 1): a relay that builds a malformed or non-canonical tree names that tree's root. With them, a transfer that passes is the full build of the records it holds, by dense-pages D9's induction, so its layout and files are the build's.

Delta receiver costs at 5M (job `261109-030635-4e209f`):

| Update | SHA-256 page input | Base-child resolutions | Base pages walked (unshared bound) |
| --- | --- | --- | --- |
| One key | 12,752 B | 111 | 324 |
| 10,000 uniform | 40,166,047 B | 70,602 | 360,979 |
| T4 | 40,525,331 B | 71,601 | 366,659 |

The walks run over pages already verified on the receiver, and walks that share a prefix share their pages. The SHA-256 input is what the records path would hash for the same new pages, since D7 writes them either way.

The model's refusals (100K, job `261109-030632-2d5f94`). Each is refused with every check on, for the reason shown. Where a check is named, the input is a forgery sealed as its sender would seal it: every digest matches and the frame names the forgery's own root, so only that check stands between it and publication, and the model with that check off (a mutant) accepts it, with files that differ from the build.

| Input | Refused because | Check |
| --- | --- | --- |
| A leaf body byte flipped, wire form | root digest mismatch | |
| A leaf body byte flipped, digests form | page digest mismatch | |
| The root's first placement redirected to the first page | a page no parent references | |
| The root sent first | placement names no page of the derived layout | |
| An extra, unreferenced copy of a leaf | a page no parent references | |
| One byte short | page length | |
| The root dropped | a page no parent references | |
| Segments broken at 512 KiB by the sender (review P1), handed over as segments | placement names no page of the derived layout | layout |
| The same transfer as a stream | placement names no page of the derived layout | |
| Children first, not in seal order (P2) | transfer is not in seal order | order |
| Root header keys +1 under a forged root, wire form (P4) | header keys disagree with the page | keys |
| Root header keys +1 under a forged root, digests form (P4) | header keys disagree with the page | keys |
| Root header height +1 under a forged root | header height disagrees with the page | height |
| The root's second label equal to its first, under a forged root | branch labels: fewer than two, or not strictly ascending | labels |
| Root header node count +1, under a forged root | page structure: nodes do not fill [64, L − 68E) | parse |
| The tree packed largest-first (3,241 pages against 3,245), in the order its pages imply, under its own root | page not packed by D5: its sealed children differ | canonical |
| An empty stream over a base at another root | empty transfer | |

An empty stream that names the empty commitment publishes the empty state, and over a base at the named root it writes nothing (same job). The review's probes, rerun at this design (`261109-030633-20ab89`), now refuse P1, P2 and P4, and still refuse P7 and P8c: repair is a full copy (D9).

### D5. Paths, not names: generic keys

A base child is named by its trie path: the raw bytes from the root to the child's top node. The receiver computes that path from the target's own pages (branch prefixes and labels), so it is never sent. No step parses a key, splits on NUL or assumes text, which meets the quest's Decision 4 and PLAN.md's "Candidate uses" (binary keys through an order-preserving escape).

No path crosses the wire. Every receiver at a position holds the same canonical page tree, so the ordered lookups PLAN.md lists (floor, ceiling, prefix scan, longest prefix) answer identically on every host once the reader has them. The transfer constrains none of them.

**Alternatives.**
- A digest index on the receiver: building it reads every page.
- Placement supplied by the sender: the sender does not know the receiver's placement, since files differ after deltas.

### D6. Pages or records per update

The sender knows both encodings' sizes: the records answer from namespace D6, and the pages from D7's diff. It applies this rule:

1. **Raw pages above twice the raw records: send records.** RULE1_EVIDENCE
2. **Otherwise, compress both and send the smaller as sent.**
3. **The cut still decides between an answer and a full copy** (namespace D6). The full copy goes as pages: 25.7 MB against 25.6 MB of records, with no build.

At 5M the rule sends records for every uniform update, and for clustered updates below about 100,000 keys. It sends pages for larger clustered changes and for every full copy. A receiver of a full copy then skips the 5M build; a receiver of a scattered delta runs dense-pages D7, whose work is proportional to the change: the same pages hashed as with pages, plus the packer on those paths.

**What a warm receiver relies on.** A receiver that applies record deltas ends with the same pages as every other holder of that position, because pages are canonical after deltas: dense-pages D7's result is the full build's page tree (dense-pages D5 and Limit 3, `dense-pages.md:256`; files are not canonical, pages are). Three results stand behind it:

1. The model's replay of the 5M full copy reproduces the build's 302 packs byte for byte (`8606dc`): the pages of a position are a function of its records.
2. The review's P9 chains three deltas over received bases and ends with a full build's page set (`20ab89`).
3. D1's root check makes a receiver whose incremental result diverged refuse it and ask for pages (D9), so a divergence is a refusal, never a silent fork.

### D7. What a relay stores and serves

A relay keeps four things per namespace:

1. **Records history**: the ring, blocks and base mirrored from upstream (namespace D7, unchanged). It serves record answers and feeds item 2.
2. **Its prepared generation at head**, built from the records it mirrors: a full build at its own cold start, dense-pages D7 after that. Relays are the build tier: one build per relay serves every agent below it. A relay that took pages from upstream would pay for both encodings, since it must mirror records anyway.
3. **A page window**: its generations at recent positions, which share all unchanged pages, so a window costs only superseded pages. It is bounded like the record ring, by 64 positions and by the generation's own size in bytes. For R in the window, the delta transfer is a top-down Merkle diff of h against R: descend while the page at a path differs, prune where digests match, and emit in seal order. D9's canonicity walk yields seal order from the pages alone. The relay indexes the window's pages by digest for D10's pulls. For R outside the window it keeps nothing: D6 answers with records, and a stale receiver pulls (D10).
4. **A canonical full copy**: the transfer from empty at a position s, made by relocating its generation into seal order. That is dense-pages D8's relocation, which computes no page digest, with D9's walk giving the order. Its first instance is the relay's own cold build, whose files are already canonical. It is replaced when the delta s→h stops being smaller than a fresh copy, and counted in namespace D6's disk bound (one cached copy per namespace). A cold agent gets the copy at s, then the D6 answer from s to h.

The relay serves only immutable objects (copies, deltas, blocks), so any HTTP cache that authorizes per namespace can serve them.

### D8. Framing and resume

A transfer travels in today's fragments: at most 64 KiB, a 96-byte header with offset and total, and zstd per fragment (`framing.hh:7-14`). The transfer is canonical and immutable, so a partial fetch resumes at the last complete fragment's offset, from the same relay or another one. The receiver keeps the prefix it holds.

A fragment that fails its frame checksum (namespace D12.2: frame SHA-256 stays) is fetched again. Fragmenting costs pages 11% against compressing per segment (25.7 MB against 23.1 MB); Q3 asks whether to change it for both encodings.

### D9. Failure, fallback and repair

| Case | What happens |
| --- | --- |
| Partial fetch, connection lost | Resume at the fragment offset (D8); the transfer's name pins its bytes |
| Fragment damaged in transit | Its frame checksum fails; that fragment is fetched again |
| Root mismatch, or any D4 refusal | The transfer is discarded. The receiver asks one other source for the same name, then falls back to records (above the cut) or a records full copy and builds, as today. Health counts refusals by source |
| Relay lacks R's generation | D6 sends records; below the cut, the full copy |
| Relay lacks or has damaged its canonical copy (scrub) | It relocates a new one (D7.4); meanwhile it serves records |
| Agent's base lacks a page the delta reuses, or a base page fails first touch or scrub | The base is damaged: namespace D10.3 applies unchanged. The namespace fails alone and the agent asks for a full copy, now the 25.7 MB pages copy with no build, published at build ordinal r + 1 |

Repair stays a full copy. A repair by page would have to fetch or relocate every live page of the damaged pack, not only the damaged page, since the pack's `.live` checksum keeps failing scrub otherwise (dense-pages D10), and a transfer carries no way to add a reused page below the delta's frontier. A full copy at 5M costs 25.7 MB once per damaged namespace.

### D10. The stale receiver: the pages it lacks, or the full copy

A receiver is in one of three states (the quest's Decision 6):

| State | Base | What it gets | Its work |
| --- | --- | --- | --- |
| Empty | none, or another incarnation | the full copy as pages (D2) | verifies and writes; no build |
| Warm | R inside the relay's record window | records (D6), or pages where smaller | dense-pages D7 on the changed paths; same pages as every holder (D6) |
| Stale | R older than the record window: a stalled agent, or one started from an old image | the pages it lacks, pulled by digest, or the full copy, whichever is smaller | verifies and writes the pulled pages; no build |

**The pull.** The relay keeps no generation for R. The receiver compares, the relay only serves pages by digest:

1. The receiver pins the head: the frame header names h and its root digest (D1).
2. **Level 1**: it asks for the root page by that digest, and checks the page hashes to it.
3. **Level k + 1**: for every page of level k it resolves each external slot's trie path (D5) and compares the slot's content entry (digest, keys, length, height) with its base page at that path. Equal: the subtree is held, by dense-pages D7's identity. Otherwise the child's digest joins the next request. One request per level carries every digest of that level, so the round trips are the depth of the deepest missing page.
4. Every answered page must hash to the digest its parent states. Pages travel in the "pull" form: content digests kept, since the receiver compares them before it holds the children, and the header digest and placement zero.
5. When no digest is left, the pulled pages in seal order are exactly the delta transfer from R to h in the digests form, and the receiver runs D4 on them: parse, canonicity, seal order, layout, root. Its files are those a warm receiver of the same delta writes.

The relay answers a pull from its page window (D7.3), indexed by digest, while h is in it. If h leaves the window mid-pull, the relay says so, and the receiver pins the new head and keeps comparing, counting the pages it already verified as base pages at their paths.

**The stop rule.** Before each request the receiver knows the raw length of every page it is about to ask for, from the content entries. It switches to the full copy as soon as what it has been sent, plus the next level at the ratio of sent to raw bytes so far, would pass the full copy's size as sent, which the relay publishes with the copy's name (D2). What it already fetched is spent; the copy then arrives as a full copy (D4).

STALE_RESULTS

### Limits

1. **The root digest is the builder relay's.** It proves that every receiver holds what that relay built, not that the relay built what the authority holds: a relay that mis-applies records sends a consistent wrong state, as today. A second relay's transfer for the same position must name the same root, so a cross-check is available, but nothing in this design runs it.
2. **Scattered deltas are built.** Under D6 agents apply them as records (dense-pages D7). Q1 (b) removes that at the byte cost in the tables above.
3. **A wire-form failure is found at the root, not at the page.** Damage in transit is caught per fragment; a sender's wrong page costs a whole retry (D3, D9).
4. **The records sizes are modeled**: MessagePack rows with the stored body as a bin. They are calibrated by one factor per corpus size against the measured full copies. Delta answers are smaller and less regular than full copies, and their factor is the full copy's.
5. **The corpus is synthetic** (dense-pages Limit 7). Bodies compress about 10 times and keys are decimal counters; a real key set reruns the model in minutes.
6. **Deployments without a relay** get records from the authority and build (Q4).
7. **Bytes per namespace are unmeasured.** The 5M figures come from one tree holding the corpus's four namespaces, where namespace W10 gives each namespace its own root. The receiver's replay is exact per namespace at 100K (the review's P11, `261109-030633-20ab89`); DD3 would measure the bytes.
8. **D6's rule 1 is fitted to the synthetic corpus**, whose bodies compress about 10 times. It is only a shortcut: dropping it changes the bytes sent, never correctness. DD4 would measure real record answers at the crossover.
9. **The model checks no leaf body** against the position (D4.2's last check, dense-pages D4.4). A C++ receiver would, and DD1 would test it.

## Open questions for the owner

**Q1. Deltas: pages, records, or the smaller?** D33 withdrew the goal that asked for pages only, so this question is open only if a later goal asks receivers never to build. Its cost per update:

| At 5M, as sent | Records (D6 picks) | Pages only | Extra per agent |
| --- | --- | --- | --- |
| 1 key | 160 B | 6,024 B | 5.9 KB |
Q1_SWEEP_100| 10,000 keys | 108 KB | 6.71 MB | 6.60 MB |
| T4 churn | 108 KB | 6.79 MB | 6.68 MB |
Q1_SWEEP_100K
Its cost per agent-day, for agents that poll the head and take each poll's changes as one delta (job `261109-030637-311a76`). Dollars are for 10,000 agents at an assumed cross-region rate of $0.02 per GB; in-region traffic is cheaper. Moderate churn is namespace D6's 1M writes a day at 5M (`namespace-delivery.md:181`), modeled as uniform updates.

| Poll, changes per poll | Records per agent-day | Pages only per agent-day | Pages / records | $ a day: records, pages only |
| --- | --- | --- | --- | --- |
| Every minute, moderate churn (694 keys) | 12.2 MB | 1.50 GB | 123 | $2.44, $299 |
| Every 10 minutes, moderate churn (6,944 keys) | 11.1 MB | 782 MB | 70.6 | $2.21, $156 |
| Every minute, one key | 231 KB | 8.67 MB | 37.6 | $0.05, $1.73 |
| Every 10 minutes, one key | 23.1 KB | 867 KB | 37.6 | under $0.01, $0.17 |
| Every minute, T4 churn | 156 MB | 9.78 GB | 62.6 | $31, $1,956 |
| Every 10 minutes, T4 churn | 15.6 MB | 978 MB | 62.6 | $3.12, $196 |

Pages only also needs R's generation for every R it answers. Either the page window grows to cover the record history, or agents outside the window pull as D10's stale receiver does, at D10's bytes.

- (a) The smaller per update (D6): agents apply scattered deltas.
- (b) Pages only: agents never build, at the bytes above, plus window disk or pull round trips.
- (c) Records for every delta, pages for full copies only. This differs from (a) only for large clustered changes, which are 1.0 to 0.91 times the records.

*Recommendation: (a)*: it never sends more than the smaller encoding, and the build it lets agents skip is the largest one, the full copy.

**Q2. The full-copy wire format (reserved, D20).**

- (a) Records, as today: 25.6 MB and a build per cold agent.
- (b) The transfer in wire form (D2, D3): 25.7 MB, no build, packs identical to a full build.
- (c) The transfer with digests kept: 36.8 MB, and a bad page found where it is.

*Recommendation: (b)*.

**Q3. Compression context.**

- (a) Per 64 KiB fragment, as today.
- (b) Per 1 MiB segment, with fragments carrying slices of the compressed stream: 10% fewer pages bytes at 5M (23.1 MB), records unmeasured.

*Recommendation: (a)* now, with (b) measured for both encodings in DD3 before any change: one framing serves both.

**Q4. Deployments with no relay.** The authority serves records only.

- (a) Agents there build, as today.
- (b) The authority's host runs a relay.
- (c) The authority builds pages.

*Recommendation: (b)*: page serving is the relay's role, and (c) puts builds in the consensus process.

## Candidate steps

These are candidates a later quest may take when a goal asks for one (D33), not a commitment. Each would be a quest with its own branch, CI green, and the model as oracle. DD1 and DD2 are local to the store and could run beside namespace S5 and S6; DD3 waits for namespace S7, DD4 for S8 and DD5 for S9.

| Step | Needs | Risk |
| --- | --- | --- |
| DD1 Transfer codec and receiver | dense-pages S1 to S5 landed, which land together (codec, validators with D9, deltas, relocation) | high: a decoder of untrusted bytes |
| DD2 Seal-order relocation (canonical copy) | dense-pages S4 (relocation) | normal |
| DD3 Full copy as pages | DD1, DD2; namespace S6 (W6 frames, namespace and incarnation, one root per namespace, W10) and S7 (derived ids); Q2 ruled | high: wire and publication |
| DD4 Delta transfers and the choice rule | DD3; namespace S8 (the relay's mirror of blocks, base and ring, whose block answer is D6's records size); Q1 ruled | high |
| DD5 Images from transfers | DD3; namespace S9 | normal |

Reader S6 is needed by none: the receiver writes files and readers keep reader.md D3's first touch. Namespace S4 (no re-validation of a tier's own build) would apply to the receiver: D4 is the tier's verification, done once. Namespace S10 is not needed.

**DD1.**
- Files: `src/detent_local/paged_transfer.{hh,cc}` (make a transfer from a generation and a page list; receive per D4, calling dense-pages S2's page and canonicity validators), `tests/detent_local/test_paged_transfer.cc`, a libFuzzer target beside `tests/detent_testing/live_record_fuzz.cc`, and a `--out` writer added to this model.
- Done when:
  - The C++ receiver takes the model's 100K and 5M full-copy transfers and reproduces the root and every pack byte for byte, with the model's segment ids.
  - Its page and pack hash counters equal 306,989,416 and 315,759,038 bytes at 5M (dense-pages' execution contract 2).
  - The four delta transfers of `261109-030635-4e209f` give their roots.
  - Each refusal of D4's table is a test: the review's P1 (segment breaks the sender chose) and P2 (children first, not in seal order); the sealed forged-root cases for header keys (P4), header height, node count, a duplicate label and a tree packed against D5, each refused by its own check with every digest matching; and the damage, reuse and truncation rows. A mutant without each check accepts its case and fails the test.
  - One CPU-hour of fuzzing finds nothing.

**DD2.**
- Files: `src/detent_local/paged_canonical_copy.{hh,cc}` and tests.
- Done when, after the T4 churn applied through dense-pages D7, relocating into seal order gives the full build's pages in the full build's order and offsets, every byte outside placement equal, with zero page digests computed (dense-pages T7's counter). Pack SHA-256 equality needs namespace S7's derived ids, and DD3 checks it.

**DD3.**
- Files: the transfer frame (`src/detent_distribution/framing.cc`, `protocol.cc`): namespace W6's frame header gains the encoding and the 32-byte target commitment beside the namespace, incarnation and position S6 adds, and the 96-byte fragment header is unchanged. The relay's copy service (`src/detent_relay/host.cc`), the agent's full-copy path (`src/detent_delivery/coordinator.cc`, `src/detent_agent/managed_host.cc`), and a system test in `tests/detent_system`.
- Done when, at 5M cold bootstrap:
  - The agent runs no packer (a counter at zero).
  - It receives 25,705,725 B as sent within 1%, and its packs equal the relay's copy, with derived ids at ordinal 0.
  - The bytes per namespace are measured for each of the corpus's four namespaces, against their record copies.
  - A root mismatch falls back to records, and a resumed fetch continues from its offset.
  - The lead measures the agent's CPU and wall against the records arm under `quiet exclusive`, and Q3's two framings for both encodings.

**DD4.**
- Files: the page window and Merkle diff on the relay, D6's rule, and the delta receiver.
- Done when, at 5M:
  - The transfers for the four scenarios of `261109-030635-4e209f` have exactly the model's pages (5; 16,489; 369; 16,616) and bytes.
  - Against S8's block answers, the rule picks records for every `u:` row and pages for `c:1000000`, and rule 1's shortcut holds on the measured record sizes at the crossover.
  - A base page that fails first touch during a delta leads to namespace D10.3's full copy as pages, and reads recover.
  - A refused transfer falls back without a wrong read.

**DD5.**
- Files: `detent-image` writes through the receiver.
- Done when namespace T6 holds by per-file SHA-256 between an image and an agent's received copy.

**Rerunning this design's numbers.**

```sh
C5=/Users/kronuz/code/Kronuz/Detent/.scratch/dense/corpus-5m.bin      # SHA-256 bb87ae5d…52bc835
C1=/Users/kronuz/code/Kronuz/Detent/.scratch/dense/corpus-100k.bin    # SHA-256 3214561e…1a5740
M=docs/design/dense-distribution-model.py
job() { campaign job submit --quest dense-distribution --ref DENSE=28d3ed9 --level light "$@"; }
job --minutes 90  --env CORPUS=$C5 --env CORPUS_SHA256=<sha> -- /usr/local/bin/python3 -I $M full $C5
job --minutes 120 --env CORPUS=$C5 --env CORPUS_SHA256=<sha> -- /usr/local/bin/python3 -I $M delta $C5 \
  u:1 u:10000 c:10000 m:9800:100:100
job --minutes 180 --env CORPUS=$C5 --env CORPUS_SHA256=<sha> -- /usr/local/bin/python3 -I $M delta $C5 \
  u:100 u:1000 u:100000 u:300000 u:1000000 u:2000000 c:100 c:1000 c:100000 c:1000000
job --minutes 120 --env CORPUS=$C5 --env CORPUS_SHA256=<sha> -- /usr/local/bin/python3 -I $M day $C5
job --minutes 30  --env CORPUS=$C1 --env CORPUS_SHA256=<sha> -- /usr/local/bin/python3 -I $M check $C1
```
