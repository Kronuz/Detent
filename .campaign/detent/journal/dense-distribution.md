# dense-distribution: journal

State: waiting on jobs 261109-030636-4ce079 261109-080251-f17f4b 261109-080252-b7e62c (sweep; stale probe at 100K; stale at 5M)
Quest: .campaign/detent/quests/dense-distribution.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State, the first step under Next, and the Evidence table. State is one of: working,
waiting on jobs <ids>, ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Round 3 (lead, 2026-11-09; quest Decision 6 and A7 at `Detent:15649a7`): the stale tier and the
warm-path property, in the same round as the sweep rows. Model `stale` mode at `Detent:d0e8f85`
(pull per level in the "pull" form, stop rule, full copy of the head, records answer; asserts the
pulled set is the occurrence writes and replays it to the head's root; `check` runs three gaps).
Document: D6's warm-path property, D7.3 and Q1 point to D10, D10 drafted with STALE_RESULTS left
for job 261109-080252-b7e62c. Older rounds below.

Design review CHANGES at `Detent:d874004` (`.campaign/detent/reviews/dense-distribution-design.md`).
Model fixed at `Detent:4d66fe1` and `Detent:29b5e53` (each forged-root case sealed, so only its
targeted check refuses: the digests-form P4 forgery of failed job 261109-014058-00ad3c kept the
root's stale digest). Five of the six jobs at 29b5e53 passed. Document round at `Detent:854e742`:
D33 folded in (a measured record, no receivers-build-nothing goal, candidate steps), every number
and job id from the jobs, D4's refusal table, Q1's per-agent-day rows (DD-4). Left: the sweep rows
(SWEEP_RANGE, RULE1_EVIDENCE, Q1_SWEEP_100, Q1_SWEEP_100K) from 261109-030636-4ce079.

| Job (at 29b5e53) | What | Result |
|---|---|---|
| 261109-030632-2d5f94 | check, 100K | exit 0; every refusal for its reason, a mutant per check |
| 261109-030633-20ab89 | reviewer's probe, 100K | exit 0; P1, P2, P4 refused; P7, P8c refused as D9 now expects |
| 261109-030634-8606dc | full copy, 5M | exit 0; 25,705,725 B as sent, 302 packs identical |
| 261109-030635-4e209f | four deltas, 5M | exit 0 |
| 261109-030636-4ce079 | sweep, 5M | queued |
| 261109-030637-311a76 | day, 5M | exit 0; 1.50 GB pages against 12.2 MB records per agent-day at 1-minute polls |

## Next

1. Read 261109-030636-4ce079; fill the sweep rows (SWEEP_RANGE, RULE1_EVIDENCE, Q1_SWEEP_*),
   recheck D6 rule 1 on every row (sent/raw range starts at 0.48, the 1-key row).
2. Read 261109-080252-b7e62c; fill STALE_RESULTS (table, where each wins), the abstract, Limits,
   a DD6 candidate and the rerun commands; one final document commit.
3. `campaign job rerun` the six plus the 5M stale job at that head (--after keeps order); State
   "ready for review", noting A7 is new scope the reviewer has not seen.

## Blocked on

Nothing for this quest. The document's owner questions are Q1 (deltas: smaller encoding / pages
only / records only; recommend smaller), Q2 (full-copy wire format, reserved D20; recommend the
wire-form transfer), Q3 (compression context; recommend today's fragments now) and Q4 (no-relay
deployments; recommend a relay on the authority's host). None of them blocks the review.

## Evidence

Jobs at `Detent:29b5e53` with `--ref DENSE=28d3ed9`, the corpus and its SHA-256 in `--env`, light
level; their reruns at the final head are the same jobs at that code.

| Criterion | Evidence |
|---|---|
| A1 | 261109-030634-8606dc: wire form 25,705,725 B as sent against 25,646,713 B of measured records (1.002×); receiver SHA-256 622,748,454 B in 162,669 digests and 315,759,038 B written, equal to the dense build, against today's 3,774,996,569 B in 14,001,455 digests and 1,509,994,805 B; replay reproduces all 302 packs and the root. Path: `docs/design/dense-distribution.md`, "What the measurements say" |
| A2 | 261109-030635-4e209f (u:1, u:10000, c:10000, T4), 261109-030636-4ce079 (sweep) and 261109-030637-311a76 (694 and 6,944 keys, per agent-day). 10K uniform 6,710,310 B of pages against 108,240 B of records (62.0×); 10K clustered 62,986 against 55,246 (1.14×); T4 6,792,600 against 108,472 (62.6×). Rule in D6. Path: `docs/design/dense-distribution.md`, the delta table, D6 and Q1 |
| A3 | `docs/design/dense-distribution.md` D1-D5 and D7, and D4's refusal table from 261109-030632-2d5f94 (layout, order, keys, height, labels, parse and canonical each refused by its own check on a sealed forgery, a mutant per check accepting) |
| A4 | `docs/design/dense-distribution.md` D8, D9 and Limits; 261109-030633-20ab89 (P6 truncation refused; P7 and P8c refused, repair is a full copy) |
| A5 | `docs/design/dense-distribution.md`, "Candidate steps": DD1-DD5 with files, criteria, risk and needs, as candidates (D33) |
| A7 | pending: 261109-080252-b7e62c (5M stale gaps), 261109-080251-f17f4b (100K probe). Path: `docs/design/dense-distribution.md` D10 |
| A6 | 261109-030634-8606dc, 261109-030635-4e209f, 261109-030636-4ce079, 261109-030637-311a76, 261109-030632-2d5f94, cited in the document beside their numbers |

## Review findings

| Finding | Fix | Confirming job |
|---|---|---|
| DD-1 | Wire is pages back to back; receiver derives the layout and requires seal order (D2-D4); P1, P2 in D4's table and DD1's criteria | 261109-030632-2d5f94 (P1 'layout', P2 'order' mutants); 261109-030633-20ab89 (P1, P2 refused) |
| DD-2 | D4 refusal rows for structure and canonicity under a forged root; DD1 criteria per check | 261109-030632-2d5f94 (sealed forgeries: keys both forms, height, labels, parse, canonical) |
| DD-3 | Ruled (b) by the lead: namespace D10.3's full copy kept, now a pages copy; "Changes D10.3" dropped (D9) | 261109-030633-20ab89 (P7, P8c refused, as the design now expects) |
| DD-4 | Q1 per-agent-day rows and dollars from the model's `day` mode | 261109-030637-311a76 |
| DD-5 | Candidate steps: DD3 needs S6 and S7, DD4 needs S8, D6's records size from S8's block answer | document at the final head; 261109-030634-8606dc for DD3's 25,705,725 B criterion |
| DD-6 | Summary restated (and D33) | document |
| DD-7 | D4: empty commitment publishes an empty state, equal root writes nothing | 261109-030632-2d5f94 |
| DD-8 | DD3 names where the root, namespace and incarnation go (frame header; fragment header unchanged) | document |
| DD-9 | Not folded: damage lists are gone with DD-3 (b) | none needed |
| DD-10 | Limit 7; DD3 measures per namespace | document |
| DD-11 | DD1 needs "dense-pages S1 to S5 landed" | document |
| DD-12 | Limit 8; DD4 measures real record answers | document |
| DD-13 | The document cites the 29b5e53 ids | document |

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
7. Corpora: `/Users/kronuz/code/Kronuz/Detent/.scratch/dense/corpus-100k.bin` (SHA-256
   `3214561e…1a5740`, equal to the dense-pages-series copy) and `corpus-5m.bin` (`bb87ae5d…52bc835`),
   each named with its hash in the job's `--env`; the model also prints the hash it read.
8. A forged-root case is valid only when sealed: the digest checks must pass, so the targeted
   structure or canonicity check is the one that refuses (lead's direction on job 261109-014058-00ad3c).
10. Stale gaps (A7): "T4 churn" for a time gap is T4's mix (98% updates, 1% inserts, 1%
   deletes) at namespace D6's moderate rate, 1M writes a day at 5M (`namespace-delivery.md:181`),
   with its skew (80% of writes to a hot 1% of keys); the hot set is a random 1% ("moderate", the
   harder case for pages) or a contiguous 1% ("hotrange"); "uniform" drops the hot set. Each key
   keeps its last write (net state); writes to inserted keys are not modeled.
11. The pull names pages by digest under a pinned head root; the relay indexes its page window by
   digest; the pull form keeps content digests (the receiver compares them before it holds the
   children) and zeroes header digest and placement; requests are modeled as 96 B plus 32 B per
   digest.
9. Lead note D33 (owner withdrew receivers-build-nothing): folded into the same document round, no
   extra rerun; Outcome and criteria unchanged; the build plan is "Candidate steps".
