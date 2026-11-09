# dense-distribution review: design

Commit: d874004f12f83a8fdc9caea88b8571d1e8d013b5
Verdict: CHANGES
Date: 2026-11-09

APPROVE: lands as is, at this commit's code. CHANGES: lands after each blocking finding is fixed
and confirmed by a job at the fixed commit. REJECT: the approach is wrong; the quest is re-planned.
An APPROVE lists no blocking finding; a CHANGES lists at least one.

The shape is right: one root digest and two encodings, seal-order transfers with derived fields
zeroed, and paths in place of names. The 5M numbers are exact and reproducible. What has to change
is the receiver's contract: as specified and modeled, it accepts transfers whose files are not the
build's (DD-1) and trees the sender forged under its own root (DD-2). It also has no procedure for
the repair the design promises (DD-3). The owner's Q1 lacks its daily cost (DD-4), and the build
plan's order cannot meet its own criteria (DD-5).

## Evidence I relied on

- `261108-230300-5e2827` (full copy, 5M), `261108-230300-2bec9b` (four deltas, 5M),
  `261108-230300-c7df19` (sweep, 5M) and `261108-230300-28e052` (100K check and refusals), all at
  `Detent:b99d3a3` with `DENSE=28d3ed9`, exit 0. `git diff b99d3a3 d874004 -- docs/design/` is
  empty, so they stand for this commit. I spot-checked the logs against the document: 25,728,032 B
  as sent, 622,748,454 B in 162,669 digests, 302 identical packs, 6,026 / 6,715,150 / 62,961 /
  6,801,139 B of delta pages and their ratios. All match.
- These establish A1, A2 and A6, plus the four refusals in D4's table. I did not rerun them.

## What I checked myself

Probe job `261109-012437-1ff8e3` ran at this commit with `DENSE=28d3ed9` on the 100K corpus and
exited 0. Its script is `.scratch/review-dense-distribution-design/probe.py`, which drives the
quest's own `receive()` and `make_transfer()`. Rerun it with:

```sh
campaign job submit --quest dense-distribution --ref DENSE=28d3ed9 --level light --minutes 30 -- \
  /usr/local/bin/python3 -I /Users/kronuz/code/Kronuz/Detent/.scratch/review-dense-distribution-design/probe.py \
  /Users/kronuz/code/Kronuz/Detent/.claude/worktrees/dense-pages-series/.scratch/dense-s1/corpus-100k.bin
```

| Probe | Input | Result |
| --- | --- | --- |
| P0 | Full copy as the design sends it | accepted; files identical to the build |
| P1 | The same pages, with segments broken at 512 KiB | **accepted; root equal; 12 segments against 6, files differ** |
| P2 | Every page children-first, but not in seal order | **accepted; root equal; files differ** |
| P3 | A zero-page transfer: an empty state, and equal roots | refused, "empty transfer" |
| P4 | Root header keys +1, with the frame naming the recomputed root | **accepted** |
| P5 | An extra, unreferenced copy of a leaf | refused, "a page no parent references" |
| P6 | Last segment dropped; root dropped at a page boundary; one byte short | all refused |
| P7 | D9's repair at an unchanged head: one damaged page, target = base | **refused, "root digest mismatch"** |
| P8 | Delta u:1, plus a reused frontier page, plus a reused page below the frontier | accepted; accepted; **refused** |
| P9 | Three chained deltas, each received over a base that was itself received (u:500, m:300:20:20, c:2000) | accepted; every placement reaches the page its content entry names; the page set equals a full build's |
| P10 | 40 pages of 65,501 B: full copy, and a delta changing one of them | accepted; files identical |
| P11 | Each of the four namespaces alone (namespace W10) | accepted; files identical |

Answers to the named risks:

1. **Trust.** The root commits to every byte the receiver keeps. The receiver derives the header
   digest and every placement itself, and trie paths come from hashed bytes. Reuse, unreferenced
   pages and truncation are all refused (model refusals, P5, P6). Two gaps remain. The receiver
   does not enforce the layout (DD-1), and it does not run the structure checks D4.3 claims (DD-2).
   The second matters because the frame's root is the sending relay's (Limit 1).
2. **Determinism.** It holds for an honest sender across large pages, single namespaces and
   received bases (P9 to P11). It does not hold for every accepted input (P1, P2). Zero-page
   transfers are refused (P3, DD-7).
3. **D6.** The choice is well defined at the sender. Both sizes are functions of (R, h) and of the
   relay's window, and no receiver state enters. The records path does not keep "builds nothing":
   it runs dense-pages D7. D6 and Limit 2 say so plainly. "Pages only" is costed exactly per update
   but not per agent-day (DD-4).
4. **Fit with namespace-delivery.** The fit holds for D8 at ordinal 0 and for the 64 KiB fragments
   (offset, total and per-fragment SHA-256, `src/detent_distribution/framing.cc:50-67`). Nothing on
   the wire or in the receiver assumes text keys: `external_paths` is byte-generic, and
   `split_key` sizes only the record model. D10's repair does not fit (DD-3).
5. **Build plan.** The steps are close to minimal, but the order is wrong (DD-5).

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| DD-1 | yes | defect | The receiver accepts any children-first order and any segment breaks. It publishes the right root in files that are not the build's. That breaks D2 ("its new files are a function of the transfer", "packs are the build's packs"), DD3's "packs equal the relay's copy" and DD5/namespace T6. A relay bug would go unnoticed until D8.4's reused-name check reports corruption. The wire carries no segment boundaries at all: the model passes a list of segments, but measures `b"".join(segments)`. | P1, P2; `dense-distribution-model.py:256-273` | In D3/D4, the stream is the pages back to back in seal order with no segment table. The receiver derives every segment break and offset from page lengths by the build's rule (dense-pages D5.1), and it requires transfer order to equal the seal order that D9's walk computes from the pages (D7.3 already relies on that walk). Add P1 and P2 to D4's refusal table and to DD1's done-when, each with a mutant. |
| DD-2 | yes | gap | D4.3 says the receiver runs dense-pages D4's structure checks and D9's canonicity checks. The model does not, and DD1's criteria cover only D4's four refusals. Because the frame's root is the sender's, those checks are the only defense against a relay that builds a malformed tree. A false key count under a self-consistent root is accepted. | P4; `dense-distribution-model.py:305-343`; DD1 criteria | Add a D4 refusal row: structure or canonicity broken under a root the sender chose. Make DD1 refuse forged-root cases for a header key count, a height, a page packed against D5 and a duplicate label, with a mutant per check. A Python check in the model is optional; the C++ validators of dense-pages S2 are the oracle. |
| DD-3 | yes | defect | D9's repair rows have no receive procedure. A repair transfer of the damaged pages alone carries no root, so D4 refuses it (P7). A reused page below the delta's frontier cannot be added to a delta either (P8c). Repair must also stop reaching the whole damaged pack, not only the damaged page: its `.live` checksum keeps failing scrub, and dense-pages D10 says a repair "reuses no page of the damaged pack". As written, the design changes namespace D10.3 without saying how. | P7, P8c; D9 last two rows; DD4's repair criterion | (a) Specify it: each page is verified against its parent's content entry at its path in the selected generation; every live page of the damaged pack is fetched or relocated and verified; ancestors are relocated (dense-pages D8); the result publishes at r + 1; a model case covers it. Or (b), which I recommend: keep namespace D10.3's full copy, now a 25.7 MB pages copy with no build, and drop "Changes namespace D10.3". Under (a), split repair out of DD4. |
| DD-4 | yes | gap | Q1 costs "pages only" per update, using a 10,000-key update at 62x ("$1.3 per such update"). Agents following the head take small, frequent deltas, where the doc's own table gives 38x to 135x. My estimate, from the doc's 1/100/1,000-key rows and namespace D6's moderate churn (1M writes a day at 5M), is arithmetic, not a measurement: about 1.4 GB per agent-day at 1-minute polls and about 4 GB at 1-second polls, against 12 to 28 MB of records. At 10,000 agents and $0.02/GB that is about $280 to $860 a day. The owner needs the per-day cost to rule Q1. | Q1 table; A2 table; namespace-delivery.md:181 | Add a Q1 row for bytes and dollars per agent-day at a stated poll cadence and churn, computed by the model (for example `delta` at the per-poll key counts). |
| DD-5 | yes | defect | The plan's order cannot meet its own criteria. (1) DD3 runs "before namespace S6 and S7", but until S7 placement entries hold per-host segment ids (namespace D8.1), so "its packs equal the relay's copy" cannot hold. (2) D1's header names a namespace and an incarnation, which arrive with S5.1/S6 (W3, W7), and the 5M tree holds four namespaces where W10 roots hold one, so DD3 would build a whole-store frame that S6 replaces. (3) The plan says "S8 is not needed", but DD4 extends S8's mirror, and D6's records size is namespace D6's block answer. | Build plan table; namespace-delivery.md:235,431-434 | DD3 needs S6 and S7 (or restate its criterion as root plus placement-free page bytes, with a pre-S6 header defined). DD4 needs S8, or D6 uses today's relay ring for the records size until S8. |
| DD-6 | no | nit | The one-line summary says state "reaches relays and agents as verified pages ... and builds nothing". Under D7.2 relays never take pages, and under Q1 (a) agents apply scattered deltas as records. | D7.2, D6, Limit 2 | Say "full copies reach agents as verified pages". |
| DD-7 | no | nit | Zero-page transfers (an empty state, or equal roots) are refused, and D4 does not say what they do. Both are rare: a watermark-only namespace is nonempty. | P3; `dense-distribution-model.py:271` | One line in D4: the empty commitment publishes an empty state, and an equal root writes nothing. |
| DD-8 | no | gap | The 96-byte fragment header has no field for D1's 32-byte root, namespace or incarnation. | `framing.cc:50-67` | DD3 names where they go. |
| DD-9 | no | nit | A transfer with a damage list is not the canonical (base, target) object, so its name cannot "pin its bytes" for resume or caching (D2, D8). Adding a frontier page works (P8b). | P8b; D2, D8 | Name damage-list transfers by their list too, or never cache them. |
| DD-10 | no | gap | The 5M figures come from one tree holding four namespaces. W10 gives each namespace its own root. Replay is exact per namespace at 100K (P11), but per-namespace bytes are unmeasured. | P11 | DD3 measures them per namespace. |
| DD-11 | no | nit | DD1 says it needs "dense-pages S1 and S2 landed", but dense-pages S1 to S5 land together. | dense-pages.md:283 at `Detent:28d3ed9` | Say "dense-pages landed", or build DD1 on that branch. |
| DD-12 | no | gap | D6 rule 1 (raw pages above twice raw records means records) is fitted to a synthetic corpus whose bodies compress about 10x. It is only a shortcut: dropping it never changes correctness. Delta record sizes are modeled (Limit 4). | D6, Limit 4 | DD4 measures real record answers at the crossover. |
| DD-13 | no | nit | The document cites the `Detent:4fa06bd` job ids. A6 asks for runs at the branch head; the journal records the reruns at `b99d3a3`, which has identical code. | doc header, journal Evidence | Cite the `261108-230300-*` ids. |

## Not run

- Nothing at 5M. Every probe ran on the 100K corpus, and the receiver logic is size-independent.
- No C++ code exists, so there is no fuzzing, sanitizer or timing evidence. Those are DD1's and
  DD3's.
- I did not check the record model's calibration beyond the logged factors, the relay page
  window's disk bound (D7.3), or the 64-position window choice.
- DD-4's daily figures are my arithmetic from the document's table, not a model run.
- I did not model a real binary-key corpus under the order-preserving escape. The receiver's path
  code is byte-generic by inspection (`dense-distribution-model.py:181-204`).
