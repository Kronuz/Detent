# dense-distribution: journal

State: fixing DD-1, DD-2, DD-3, DD-4, DD-5 (design review CHANGES at d874004, msg from the lead)
Quest: .campaign/detent/quests/dense-distribution.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State, the first step under Next, and the Evidence table. State is one of: working,
waiting on jobs <ids>, ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Design review CHANGES at `Detent:d874004`
(`.campaign/detent/reviews/dense-distribution-design.md`, probe job 261109-012437-1ff8e3).
Model fixed at `Detent:4d66fe1`: the wire is the pages back to back; the receiver derives the
layout (DD-1), parses each page as a first touch does and reruns D5 at every node, refusing a
non-seal order or a non-canonical tree (DD-1, DD-2); check mode adds P1, P2, P4 and one forged-root
case per check with a mutant each; `day` mode prices pages per agent-day (DD-4). Check job
261109-014058-00ad3c queued. Document: D2-D4, D9, Limits and the build plan rewritten for DD-1 to
DD-3, DD-5 and the folds; numbers wait for the reruns.

## Next

1. Read check job 261109-014058-00ad3c; fix the model if a case fails.
2. Submit at the final head: full, delta (4), sweep, day (5M), check (100K) and the reviewer's
   probe; fill D4's refusal table, Q1's per-day rows and every number and job id in the doc.
3. Answer DD-1 to DD-13 under "Review findings", Evidence, then State "ready for review".

## Blocked on

Nothing for this quest. The document's owner questions are Q1 (deltas: smaller encoding / pages
only / records only; recommend smaller), Q2 (full-copy wire format, reserved D20; recommend the
wire-form transfer), Q3 (compression context; recommend today's fragments now) and Q4 (no-relay
deployments; recommend a relay on the authority's host). None of them blocks the review.

## Evidence

All jobs are at `Detent:4fa06bd` with `--ref DENSE=28d3ed9`, light level, exit 0. The model
(`docs/design/dense-distribution-model.py`) is unchanged from that commit to the head; later
commits touch only the document and this journal.

| Criterion | Evidence |
|---|---|
| A1 | 261108-230300-5e2827: full copy at 5M. Wire form 25,728,032 B as sent against 25,646,713 B of measured records (1.003×), 315,759,038 B raw against 261,337,255. Receiver SHA-256 622,748,454 B in 162,669 digests and 315,759,038 B written, the same as the dense build, against today's 3,774,996,569 B in 14,001,455 digests and 1,509,994,805 B. The replay reproduces all 302 packs and the root. Path: `docs/design/dense-distribution.md`, "What the measurements say" |
| A2 | 261108-230300-2bec9b (u:1, u:10000, c:10000, m:9800:100:100) and 261108-230300-c7df19 (the sweep). 10K uniform: 6,715,150 B of pages against 108,240 B of records (62×). 10K clustered: 62,961 against 55,246 (1.14×). T4: 6,801,139 against 108,472 (62.7×). Crossover: clustered at about 100K keys (1.01×, 0.91× at 1M); uniform never (2.07× at 2M). Rule in D6. Path: `docs/design/dense-distribution.md`, A2 table, D6 and Q1 |
| A3 | `docs/design/dense-distribution.md` D1-D5 and D7 (frame header digest, transfer, wire form, receiver, path naming, relay storage). Refusals from 261108-230300-28e052: root digest mismatch, page digest mismatch, page referenced twice, child not before its parent |
| A4 | `docs/design/dense-distribution.md` D8 and D9 (resume, damaged fragment, root mismatch, relay lacks base or copy, base page missing, repair), plus Limits |
| A5 | `docs/design/dense-distribution.md`, "Build plan": DD1-DD5 with files, criteria, risk and needs from dense-pages, reader S6 and namespace S4-S10 |
| A6 | 261108-230300-5e2827, 261108-230300-2bec9b, 261108-230300-c7df19 and 261108-230300-28e052 at b99d3a37ebb1 (the head minus journal commits), cited in the document beside their numbers |

Examined and not evidence: probe 261108-214633-1b1530 (unversioned, 100K).

## Result

`docs/design/dense-distribution.md` (331 lines) and its model `docs/design/dense-distribution-model.py`.

- **Full copy.** It is the canonical packs as a seal-order transfer with derived fields zeroed: 25.7 MB as sent at 5M against 25.6 MB of records. The receiver hashes 622.7 MB in 162,669 digests with no build, and its packs are byte-identical to a full build.
- **Deltas.** Pages cost 21 to 135 times the records for uniform updates and 1.14 times for 10K clustered. Pages win only for clustered changes of about 100K keys and more. The rule is to send the smaller encoding per update, with "pages only" left as owner question Q1 at its byte cost.
- **Wire and resume.** Keeping digests on the wire would cost 43% more on a full copy. Today's 64 KiB fragment framing costs pages 11% against compressing per segment.

Open: no C++ CPU number for the skipped build (DD3 measures it); record delta sizes are modeled
(calibrated ×0.990 as sent on the full copy).

## Rulings I made

1. The dense model is the one at `Detent:28d3ed9` (unchanged in `docs/design/` through
   `Detent:be49a1d`), loaded by path from the job's `DENSE` ref, not copied into this file set.
2. Lead message (2026-11-08, quest Decision 4 at `Detent:a08818f`): keys stay generic. The wire
   names pages by digest and base children by trie path computed from the pages (length-framed
   bytes only in repair requests); nothing parses keys or splits on NUL. "Pages only" is Q1 (b)
   with its byte cost. Only the record model parses today's store key grammar, to size today's wire.
3. Record wire sizes are modeled (MessagePack rows, stored body as bin) and calibrated by one
   factor per corpus size against the measured full copies (5M ×0.897 raw, ×0.990 as sent; 100K
   ×0.891, ×0.979).
4. Wire form "wire" (derived fields zero) over "digests": measured 43% cheaper on a full copy, at
   the cost of root-only failure detection (Limit 3).
5. Relays stay the build tier (D7.2), and the frame's root digest is the builder relay's (Limit 1).
   The authority builds no pages (Q4 (c) rejected in the recommendation).
6. Lead message (2026-11-08, jobs done): the document is finished from the four jobs. The model is
   unchanged since `Detent:4fa06bd`, so no rerun.
