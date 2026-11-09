> Status: DRAFT (2026-11-08) Ready for design review. D1 to D9 are the designer's. The full-copy wire format is reserved to the owner (`.campaign/detent/PLAN.md`, "Reserved decisions", D20), so D2 and D3 are recommendations until Q2 is ruled, and the delta encoding is Q1. Every byte figure comes from the [model](dense-distribution-model.py) in the campaign job named beside it. Page bytes are exact for the 5M corpus. Record bytes are modeled and calibrated against the measured record copy. No time is measured here.

# Dense distribution

DTPAGE03 state reaches relays and agents as verified pages: a receiver checks and writes what it is sent, and builds nothing.

- Author: [Germán Méndez Bravo](mailto:german.mb@gmail.com). Created 2026-11-08. Quest `dense-distribution`.
- Base: `Detent:8874c71` (`main`). The page format is [dense-pages.md](dense-pages.md) as implemented on `quest/dense-pages` at `Detent:28d3ed9`, cited as dense-pages D*n* with line numbers at that commit. [namespace-delivery.md](namespace-delivery.md) is cited as namespace D*n*.
- Depends on dense-pages D1 (one digest, placement outside it, :46), D5 (canonical bottom-up sealing, :110), D7 (occurrence reuse, :162-168), D8 (relocation computes no digest, :170) and D9 (canonicity, :174). On the namespace side it depends on D6 (records history and the cut), D7 (relays), D8 (derived ids, `namespace-delivery.md:233-239`) and D10 (first touch, scrub, repair, `:261-267`).
- Changes three things:
  - namespace W6 (`:344`): a frame names its target's root digest and carries pages or records.
  - namespace D10.3 (`:267`): repair fetches the damaged pages, not a full copy.
  - dense-pages Q2 (:268): answered by D2 and Q2 here.
- Model: [dense-distribution-model.py](dense-distribution-model.py) imports [dense-pages-model.py](dense-pages-model.py) at `Detent:28d3ed9` (SHA-256 `96dfd0a7…3906bd1`), passed as the job ref `DENSE`.
  - Corpus: the r12 5M corpus (SHA-256 `bb87ae5d…52bc835`).
  - Its receiver replays every transfer. It must reproduce the target's root digest, and for a full copy the build's files byte for byte.
  - Jobs, all at `Detent:4fa06bd` with `--ref DENSE=28d3ed9`, each exit 0: full copy `261108-215112-5db839`, deltas `261108-215113-2c03ee`, crossover sweep `261108-215113-376435`, and the 100K self-check and refusals `261108-215111-57b64f`.

| Review | State |
| --- | --- |
| Design | not started |

## Abstract

Today a cold agent receives 25.6 MB of records and builds its store: at 5M a DTPAGE02 build hashes 3.77 GB and writes 1.51 GB. Dense pages make the build canonical, and a canonical build can be shipped instead of repeated.

This design ships a **transfer**: the pages the receiver lacks, in the target's seal order, laid out exactly as the receiver will write them. It leaves out the fields the receiver can derive itself: each page's digest, the digests of children inside the transfer, and placement in the sender's segments.

The receiver handles a transfer in four steps:

1. It hashes every page bottom-up, which it would do anyway to verify the pages.
2. It fills in the fields that were left out.
3. It resolves children it already holds by their trie path in its base.
4. It publishes only if the root digest equals the one the frame names.

A full copy is the transfer from the empty base. At 5M it is 25.7 MB as sent, against 25.6 MB of records, 1.003 times. The receiver's output is byte-identical to a full build, with the same 622.7 MB of SHA-256 input as the dense build. It does not decode records, build a trie or pack.

A delta is the transfer from the receiver's base. Scattered updates cost far more as pages than as records: 6.7 MB against 108 KB for 10,000 uniform updates, 62 times. Clustered updates are near parity: 63 KB against 55 KB for 10,000 consecutive keys. So each update goes out as whichever encoding is smaller. Under that rule, pages win only for clustered changes of about 100,000 keys and more, and for full copies. "Pages only" (Q1) is the owner's alternative: receivers never build, at 21 to 135 times the records' bytes per scattered update.

## What the measurements say

| At 5M | Bytes as sent | Raw | Receiver SHA-256 input | Receiver writes |
| --- | --- | --- | --- | --- |
| Records full copy, measured (`docs/perf/cold-bootstrap-5m.md:118-123`) | 25,646,713 | 261,337,255 | builds: see below | builds |
| Records full copy, modeled | 25,911,641 (calibration ×0.990) | 291,340,194 (×0.897) | | |
| Today's DTPAGE02 build from records | | | 3,774,996,569 B in 14,001,455 digests | 1,509,994,805 B, 1,441 packs |
| Dense build from records | | | 622,748,454 B in 162,669 digests | 315,759,038 B, 302 packs |
| Transfer, "stored" form (packs as built) | 36,736,371 | 315,759,038 | | |
| Transfer, "digests" form (D3, digests kept) | 36,824,459 | 315,759,038 | | |
| **Transfer, "wire" form (D3)** | **25,728,032** (1.003× records) | 315,759,038 | 306,989,416 page + 315,759,038 pack = 622,748,454 B in 162,669 digests | 315,759,038 B, identical to the build |

Source: job `261108-215112-5db839`. "As sent" is today's framing: 64 KiB fragments, each zstd-3, with a 96-byte header each (`include/detent_distribution/framing.hh:7`). Compressing per 1 MiB segment instead gives 23,099,721 B for the wire form at zstd-3 and 20,459,677 at zstd-9 (Q3).

The modeled records stream comes within 1% of the measured one as sent (×0.990) and within 11% raw (×0.897); at 100K the same model comes within 2% as sent (×0.979, job `261108-215111-57b64f`). Delta record sizes below are modeled and scaled by the 5M factors.

**A1, the receiver's work.**

1. **Hashing and writes.** The receiver of a full copy hashes what the dense build hashes: 622.7 MB in 162,669 digests, about 2.6 s of one core at dense-pages' rate (an estimate from `dense-pages-model.py:81-82`, not a measurement). It writes the same 316 MB. Against today's build that is 6.06 times less SHA-256 input and 4.78 times fewer bytes written.
2. **What it skips.** It decompresses 25.7 MB to 316 MB where the build decompresses 25.6 MB to 261 MB. Then it skips the build itself: decoding 5,000,004 records, the trie, the packer and the node encoder. That skipped work has no C++ counter today. The r12 agent spent 125 s of CPU on its DTPAGE02 build (`docs/perf/cold-bootstrap-5m.md`, phase table), and DD3's gate measures the dense difference.
3. **What would cut the pack pass.** Half the hashing is the pack checksum `.live` needs (dense-pages Q1, ruled (a) for S1 to S5). Q1's (b) or (c) would remove those 316 MB.

| At 5M, one update (wire form for pages) | Occurrence writes | Pages as sent | Pages raw | Records as sent (modeled) | Records raw (modeled) | Pages / records as sent |
| --- | --- | --- | --- | --- | --- | --- |
| 1 key, uniform | 5 pages | 6,026 | 15,234 | 160 | 194 | 37.6 |
| 100 keys, uniform | 334 | 203,514 | 913,634 | 1,507 | 6,143 | 135 |
| 1,000 keys, uniform | 2,540 | 1,403,314 | 7,236,393 | 12,037 | 57,869 | 117 |
| **10,000 keys, uniform** | 16,489 | 6,715,150 | 42,485,753 | 108,240 | 575,208 | **62.0** |
| **T4 churn: 9,800 updates, 100 inserts, 100 deletes** | 16,616 | 6,801,139 | 42,872,248 | 108,472 | 570,114 | **62.7** |
| 100,000 keys, uniform | 79,978 | 18,436,352 | 168,787,212 | 861,766 | 5,759,212 | 21.4 |
| 300,000 keys, uniform | 136,337 | 24,642,024 | 269,527,690 | 2,341,715 | 17,276,902 | 10.5 |
| 1,000,000 keys, uniform | 161,904 | 28,102,893 | 315,256,189 | 7,355,195 | 57,627,380 | 3.82 |
| 2,000,000 keys, uniform | 162,216 | 28,807,525 | 315,737,295 | 13,883,436 | 115,250,486 | 2.07 |
| 100 keys, clustered | 10 | 6,579 | 28,314 | 860 | 8,129 | 7.65 |
| 1,000 keys, clustered | 41 | 10,635 | 103,202 | 5,559 | 79,893 | 1.91 |
| **10,000 keys, clustered** | 369 | 62,961 | 901,570 | 55,246 | 797,501 | **1.14** |
| 100,000 keys, clustered | 3,621 | 556,402 | 8,828,306 | 549,849 | 7,973,642 | 1.01 |
| 1,000,000 keys, clustered | 28,824 | 4,713,346 | 38,233,520 | 5,185,423 | 35,760,514 | 0.91 |

Sources: jobs `261108-215113-2c03ee` (the bold rows and the 1-key row) and `261108-215113-376435` (the sweep). The occurrence writes reproduce dense-pages' projections: 5 pages for one update, and 16,616 pages of 42,821,363 page bytes for T4 (dense-pages T4).

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

### D2. The transfer: what the receiver lacks, in seal order, as it will be written

A transfer is the target's pages that the receiver's base lacks under dense-pages D7's identity: the same trie path before the top node's prefix, with equal digest, length, keys and height. It lists them in the target's seal order (dense-pages D5), which puts every page after its children.

The pages are laid out as a build lays them out: each at the next 8-byte offset, in segments of at most 1 MiB, numbered from 1. The receiver writes the transfer's segments under its own segment ids, so its new files are a function of the transfer alone.

- **Full copy** = the transfer from the empty base: every page, in a full build's layout. With namespace D8's derived ids at build ordinal 0, the receiver's packs are the build's packs. The model's replay of the 5M full copy reproduces all 302 packs byte for byte and the root `67c70ad3…ab7f07` (`5db839`). An image built by `detent-image` from the same transfer is the same files (namespace T6 by construction).
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
2. **A child inside the transfer** has a zero content digest. Its placement entry holds (12 zero bytes, transfer segment number from 1, offset).
3. **A child the receiver already holds** (a base child) keeps its content digest. Its placement entry is all zero.

Keys, length and height stay in every content entry, so the receiver checks each against the child it reaches.

**Alternatives.**
- **The "digests" form**: the same, with every digest kept. A damaged page then fails at that page instead of at the root. At 5M it costs 43% more on a full copy (36.8 MB against 25.7 MB) and 14% more on a 10,000-key delta (7.63 MB against 6.72 MB). Digests are SHA-256 output and do not compress, which is most of the gap.
- **Pages as stored**: 36.7 MB.

Damage in transit is a fragment's problem (D8). A wrong page from a correct transport means the sender is wrong, and that is handled per sender (D9).

### D4. The receiver verifies; it never builds

For each transfer, in order:

1. **Parse.** Pages start at 8-aligned offsets, padding is zero, and lengths are 79 to 65,536 (dense-pages D2).
2. **Resolve base children (deltas only).** Walk the transfer top-down from its last page, the root, giving each external slot its trie path. A slot whose placement is zero names a base child. The receiver walks its base along that path and requires a page with the stated digest, keys, length and height; otherwise it refuses with "base lacks a page".
3. **Hash bottom-up**, in transfer order:
   - Each in-transfer child must already be hashed and must not be referenced twice.
   - Its keys, length and height must match the content entry.
   - The receiver fills the content digest and rewrites placement to its own segment id, or to the base page's placement for a base child.
   - It then hashes dense-pages D1's ranges and fills the header digest.
   - It runs dense-pages D4's structure checks and D9's canonicity check per page. These are the validators, not the packer.
4. **Check the root.** Every page but the last must be referenced exactly once, and the last page's digest must equal the frame's. Then the receiver writes the segments, computes their pack checksums for `.live`, and publishes as namespace D8 and D11 publish.

Pages verified here are verified by the tier that writes them, as namespace D10 asks of a build. Readers still verify on first touch (reader.md D3); nothing changes for them.

Delta receiver costs at 5M (jobs `2c03ee`, `376435`):

| Update | SHA-256 page input | Base-child resolutions | Base pages walked (unshared bound) |
| --- | --- | --- | --- |
| One key | 12,752 B | 111 | 324 |
| 10,000 uniform | 40,166,047 B | 70,602 | 360,979 |
| T4 | 40,525,331 B | 71,601 | 366,659 |

The walks run over pages already verified on the receiver, and walks that share a prefix share their pages. The SHA-256 input is what the records path would hash for the same new pages, since D7 writes them either way.

The model's refusals (100K, `57b64f`), each refused and none accepted:

| Damage | Refusal |
| --- | --- |
| A flipped node byte, wire form | root digest mismatch |
| A flipped node byte, digests form | page digest mismatch, at that page |
| A placement redirected to another page | page referenced twice |
| A parent sent before its child | child not before its parent |

### D5. Paths, not names: generic keys

A base child is named by its trie path: the raw bytes from the root to the child's top node. The receiver computes that path from the target's own pages (branch prefixes and labels), so it is never sent. No step parses a key, splits on NUL or assumes text, which meets the quest's Decision 4 and PLAN.md's "Candidate uses" (binary keys through an order-preserving escape).

The one place a path crosses the wire is a repair request (D9), as length-framed bytes. Every receiver at a position holds the same canonical page tree, so the ordered lookups PLAN.md lists (floor, ceiling, prefix scan, longest prefix) answer identically on every host once the reader has them. The transfer constrains none of them.

**Alternatives.**
- A digest index on the receiver: building it reads every page.
- Placement supplied by the sender: the sender does not know the receiver's placement, since files differ after deltas.

### D6. Pages or records per update

The sender knows both encodings' sizes: the records answer from namespace D6, and the pages from D7's diff. It applies this rule:

1. **Raw pages above twice the raw records: send records.** Every measured case with a raw ratio of 2.74 or more (every uniform row, and 100 clustered keys at 3.48) had a ratio as sent of 2.07 or more; across all rows, sent over raw ratio ran from 0.76 to 2.2.
2. **Otherwise, compress both and send the smaller as sent.**
3. **The cut still decides between an answer and a full copy** (namespace D6). The full copy goes as pages: 25.7 MB against 25.6 MB of records, with no build.

At 5M the rule sends records for every uniform update, and for clustered updates below about 100,000 keys. It sends pages for larger clustered changes and for every full copy. So agents build nothing for the full copy, the 5M build that motivated dense pages. They still run dense-pages D7 for scattered deltas, which costs work proportional to the change: the same pages hashed as with pages, plus the packer on those paths.

### D7. What a relay stores and serves

A relay keeps four things per namespace:

1. **Records history**: the ring, blocks and base mirrored from upstream (namespace D7, unchanged). It serves record answers and feeds item 2.
2. **Its prepared generation at head**, built from the records it mirrors: a full build at its own cold start, dense-pages D7 after that. Relays are the build tier: one build per relay serves every agent below it. A relay that took pages from upstream would pay for both encodings, since it must mirror records anyway.
3. **A page window**: its generations at recent positions, which share all unchanged pages, so a window costs only superseded pages. It is bounded like the record ring, by 64 positions and by the generation's own size in bytes. For R in the window, the delta transfer is a top-down Merkle diff of h against R: descend while the page at a path differs, prune where digests match, and emit in seal order. D9's canonicity walk yields seal order from the pages alone. For R outside the window, D6's choice is records (or the cut's full copy).
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
| Agent's base lacks a page the delta reuses (resolution fails, or a base page fails first touch) | The agent repeats the request with a damage list of (length-framed path, digest). The relay adds those pages to the transfer; when a page is unchanged in h, h's copy is the same page |
| Repair at an unchanged head (scrub or first touch, namespace D10.3) | The same request with target = base. The transfer is the damaged pages alone; the receiver rewrites their ancestors' placement (dense-pages D8: no digest moves) and publishes at build ordinal r + 1. A full copy remains the fallback, not the first step |

### Limits

1. **The root digest is the builder relay's.** It proves that every receiver holds what that relay built, not that the relay built what the authority holds: a relay that mis-applies records sends a consistent wrong state, as today. A second relay's transfer for the same position must name the same root, so a cross-check is available, but nothing in this design runs it.
2. **Scattered deltas are not built-nothing.** Under D6 agents apply them as records (dense-pages D7). Q1 (b) removes that at the byte cost in the table above.
3. **A wire-form failure is found at the root, not at the page.** Damage in transit is caught per fragment; a sender's wrong page costs a whole retry (D3, D9).
4. **The records sizes are modeled**: MessagePack rows with the stored body as a bin. They are calibrated by one factor per corpus size against the measured full copies. Delta answers are smaller and less regular than full copies, and their factor is the full copy's.
5. **The corpus is synthetic** (dense-pages Limit 7). Bodies compress about 10 times and keys are decimal counters; a real key set reruns the model in minutes.
6. **Deployments without a relay** get records from the authority and build (Q4).

## Open questions for the owner

**Q1. Deltas: pages, records, or the smaller?** Pages only is what "agents build nothing" asks for; this table is what it costs.

| At 5M, as sent | Records (D6 picks) | Pages only | Extra per agent |
| --- | --- | --- | --- |
| 1 key | 160 B | 6,026 B | 5.9 KB |
| 100 keys | 1,507 B | 203,514 B | 202 KB |
| 10,000 keys | 108 KB | 6.72 MB | 6.61 MB |
| T4 churn | 108 KB | 6.80 MB | 6.69 MB |
| 100,000 keys | 862 KB | 18.4 MB | 17.6 MB |

Example: across 10,000 agents, one 10,000-key update costs 66 GB extra under pages only. At an assumed cross-region rate of $0.02 per GB that is about $1.3 per such update; in-region traffic is cheaper. Pages only also needs R's generation for every R it answers. Either the page window grows to cover the record history, or agents outside the window pull level by level, one round trip per level of the page tree (about 6 at 5M, dense-pages T3).

- (a) The smaller per update (D6): agents apply scattered deltas.
- (b) Pages only: agents never build, at the bytes above, plus window disk or pull round trips.
- (c) Records for every delta, pages for full copies only. This differs from (a) only for large clustered changes, which are 1.0 to 0.91 times the records.

*Recommendation: (a)*: it never sends more than the smaller encoding, and the build agents avoid entirely is the one that matters, the full copy.

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

## Build plan

Each step is a quest with its own branch, CI green, and the model as oracle.

| Step | Needs | Risk |
| --- | --- | --- |
| DD1 Transfer codec and receiver | dense-pages S1 and S2 landed (codec, validators with D9) | high: a decoder of untrusted bytes |
| DD2 Seal-order relocation (canonical copy) | dense-pages S4 (deltas, relocation); namespace S7 for derived ids, before it model-style ordinal ids in tests | normal |
| DD3 Full copy as pages | DD1, DD2; Q2 ruled; runs on today's one-generation store before namespace S6 and S7 | high: wire and publication |
| DD4 Delta transfers, choice rule, repair | DD1, DD2, DD3; namespace S6 (per-namespace frames, W6) and S3b's repair path; Q1 ruled | high |
| DD5 Images from transfers | DD3; namespace S9 | normal |

Reader S6 is needed by none: the receiver writes files and readers keep reader.md D3's first touch. Namespace S4 (no re-validation of a tier's own build) applies to the receiver: D4 is the tier's verification, done once. Namespace S5, S8 and S10 are not needed. S8's relay mirror is D7.1, which DD4 extends.

**DD1.**
- Files: `src/detent_local/paged_transfer.{hh,cc}` (make a transfer from a generation and a page list; receive per D4), `tests/detent_local/test_paged_transfer.cc`, a libFuzzer target beside `tests/detent_testing/live_record_fuzz.cc`, and a `--out` writer added to this model.
- Done when:
  - The C++ receiver takes the model's 100K and 5M full-copy transfers and reproduces the root and every pack byte for byte.
  - Its page and pack hash counters equal 306,989,416 and 315,759,038 bytes at 5M (dense-pages' execution contract 2).
  - The four delta transfers of `2c03ee` give their roots.
  - Each refusal of D4's table is a test, and a mutant without each check fails one.
  - One CPU-hour of fuzzing finds nothing.

**DD2.**
- Files: `src/detent_local/paged_canonical_copy.{hh,cc}` and tests.
- Done when, after the T4 churn applied through dense-pages D7, relocating into seal order gives packs whose SHA-256 equal a full build's of the after corpus, with zero page digests computed (dense-pages T7's counter).

**DD3.**
- Files: the frame kind and header digest (`src/detent_distribution/framing.cc`, `protocol.cc`), the relay's copy service (`src/detent_relay/host.cc`), the agent's full-copy path (`src/detent_delivery/coordinator.cc`, `src/detent_agent/managed_host.cc`), and a system test in `tests/detent_system`.
- Done when, at 5M cold bootstrap:
  - The agent runs no packer (a counter at zero).
  - It receives 25,728,032 B as sent within 1%, and its packs equal the relay's copy.
  - A root mismatch falls back to records, and a resumed fetch continues from its offset.
  - The lead measures the agent's CPU and wall against the records arm under `quiet exclusive`, and Q3's two framings for both encodings.

**DD4.**
- Files: the page window and Merkle diff on the relay, D6's rule, the delta receiver, the damage list, and the repair path.
- Done when, at 5M:
  - The transfers for the four scenarios of `2c03ee` have exactly the model's pages (5; 16,489; 369; 16,616) and bytes.
  - The rule picks records for every `u:` row and pages for `c:1000000`.
  - A flipped base page is repaired by fetching that page alone at ordinal r + 1, and reads recover.
  - A refused transfer falls back without a wrong read.

**DD5.**
- Files: `detent-image` writes through the receiver.
- Done when namespace T6 holds by per-file SHA-256 between an image and an agent's received copy.

**Rerunning this design's numbers.**

```sh
campaign job submit --quest dense-distribution --ref DENSE=28d3ed9 --level light --minutes 90 -- \
  /usr/local/bin/python3 -I docs/design/dense-distribution-model.py full CORPUS-5M
campaign job submit --quest dense-distribution --ref DENSE=28d3ed9 --level light --minutes 120 -- \
  /usr/local/bin/python3 -I docs/design/dense-distribution-model.py delta CORPUS-5M u:1 u:10000 c:10000 m:9800:100:100
campaign job submit --quest dense-distribution --ref DENSE=28d3ed9 --level light --minutes 30 -- \
  /usr/local/bin/python3 -I docs/design/dense-distribution-model.py check CORPUS-100K
```
