# dense-distribution: journal

State: ready for review
Quest: .campaign/detent/quests/dense-distribution.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State, the first step under Next, and the Evidence table. State is one of: working,
waiting on jobs <ids>, ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Round 4 (lead, 2026-11-09): stale results read, stop rule fixed, document finished. Model
`Detent:7ba0734`: the stop rule samples a sixteenth of the first pull level of 256 pages (by
digest), pulls their subtrees, scales by content-entry keys, and takes the full copy when the
estimated rest passes it; the one-level-ahead rule still prints, marked superseded. 5M job
261109-092232-0a7ab9 at that commit. Final document commit `Detent:791570b`: D10 restated (empty, warm,
stale above the cut gets records from blocks, stale below the cut pulls or takes the copy), the
stale tables, Q2-Q4 ruled (D34), Q1 moot (D33), the sweep rows, D6 rule 1 at three times (range
0.48 to 2.2), DD6 as a candidate, Limits 10-11. Every model job rerun at that head (below).

Reruns at `Detent:791570b` (queued; the lead reads them): 261109-093826-07e6d1 check, 261109-093827-d34c29
probe, 261109-093827-46d766 full, 261109-093827-efe4c3 deltas, 261109-093827-a93501 sweep,
261109-093827-ed3811 day, 261109-093828-cd4dc2 stale.

New scope since the design review at `Detent:d874004`: quest A7 and D10 (the stale receiver),
D6's warm-path property, D6 rule 1's threshold, DD6.

## Next

1. Design review of the new scope (A7, D10, D6's warm-path property and rule 1, DD6), then
   `campaign accept dense-distribution design --confirm` with the finding map below.

## Blocked on

Nothing. Finding for the lead: with namespace D6's blocks on relays the cut is 32 to 44 days back
at moderate churn (`namespace-delivery.md:181`), and the pull loses to the copy from about 24 h,
so below that cut the stop rule takes the copy; the pull wins only where a relay keeps records
hours deep (ring only, before namespace S8) or the disk cap cut early. DD6 is worth building only
there (D10, "Where the cut falls").

## Evidence

Jobs at `Detent:791570b` with `--ref DENSE=28d3ed9`, the corpus and its SHA-256 in `--env`, light level
(reruns of the 29b5e53 jobs and of 261109-092232-0a7ab9).

| Criterion | Evidence |
|---|---|
| A1 | 261109-093827-46d766: wire form 25,705,725 B as sent against 25,646,713 B of measured records (1.002×); receiver SHA-256 622,748,454 B in 162,669 digests and 315,759,038 B written, equal to the dense build, against today's 3,774,996,569 B in 14,001,455 digests and 1,509,994,805 B; replay reproduces all 302 packs and the root. Path: `docs/design/dense-distribution.md`, "What the measurements say" |
| A2 | 261109-093827-efe4c3 (u:1, u:10000, c:10000, T4), 261109-093827-a93501 (sweep) and 261109-093827-ed3811 (694 and 6,944 keys, per agent-day). 10K uniform 6,710,310 B of pages against 108,240 B of records (62.0×); 10K clustered 62,986 against 55,246 (1.14×); T4 6,792,600 against 108,472 (62.6×); uniform 2.07× to 135×; clustered cross at about 100K keys. Rule in D6. Path: `docs/design/dense-distribution.md`, the delta table and D6 |
| A3 | `docs/design/dense-distribution.md` D1-D5 and D7, and D4's refusal table from 261109-093826-07e6d1 (layout, order, keys, height, labels, parse and canonical each refused by its own check on a sealed forgery, a mutant per check accepting) |
| A4 | `docs/design/dense-distribution.md` D8, D9 and Limits; 261109-093827-d34c29 (P6 truncation refused; P7 and P8c refused, repair is a full copy) |
| A5 | `docs/design/dense-distribution.md`, "Candidate steps": DD1-DD6 with files, criteria, risk and needs, as candidates (D33) |
| A6 | 261109-093827-46d766, 261109-093827-efe4c3, 261109-093827-a93501, 261109-093827-ed3811, 261109-093828-cd4dc2, 261109-093826-07e6d1, 261109-093827-d34c29; the document cites their numbers beside the job that first produced them |
| A7 | 261109-093828-cd4dc2 (5M, seven gaps: 1, 6, 24, 168 h moderate, 1 h hot range, 1 h and 24 h uniform): pull, round trips, records from blocks, full copy and the sampled stop rule per gap; 261109-093826-07e6d1 (the stale mode at 100K, asserting the pulled set is the occurrence writes and replaying it to the head's root). Path: `docs/design/dense-distribution.md` D10 |

## Review findings

| Finding | Fix | Confirming job |
|---|---|---|
| DD-1 | Wire is pages back to back; receiver derives the layout and requires seal order (D2-D4); P1, P2 in D4's table and DD1's criteria | 261109-093826-07e6d1 (P1 'layout', P2 'order' mutants); 261109-093827-d34c29 (P1, P2 refused) |
| DD-2 | D4 refusal rows for structure and canonicity under a forged root; DD1 criteria per check | 261109-093826-07e6d1 (sealed forgeries: keys both forms, height, labels, parse, canonical) |
| DD-3 | Ruled (b) by the lead: namespace D10.3's full copy kept, now a pages copy (D9) | 261109-093827-d34c29 (P7, P8c refused, as the design expects) |
| DD-4 | Q1 per-agent-day rows and dollars from the model's `day` mode; Q1 moot under D33, rows kept as the record | 261109-093827-ed3811 |
| DD-5 | Candidate steps: DD3 needs S6 and S7, DD4 needs S8, D6's records size from S8's block answer | 261109-093827-46d766 (DD3's 25,705,725 B criterion); the document at `Detent:791570b` |
| DD-6 | Summary restated (and D33) | document |
| DD-7 | D4: empty commitment publishes an empty state, equal root writes nothing | 261109-093826-07e6d1 |
| DD-8 | DD3 names where the root, namespace and incarnation go (frame header; fragment header unchanged) | document |
| DD-9 | Not folded: damage lists are gone with DD-3 (b) | none needed |
| DD-10 | Limit 7; DD3 measures per namespace | document |
| DD-11 | DD1 needs "dense-pages S1 to S5 landed" | document |
| DD-12 | Limit 8; DD4 measures real record answers | document |
| DD-13 | The document cites the job ids beside their numbers | document |

## Result

`docs/design/dense-distribution.md` and its model `docs/design/dense-distribution-model.py`.

- **Full copy.** The canonical packs as a seal-order transfer with derived fields zeroed: 25.7 MB as sent at 5M against 25.6 MB of records; the receiver hashes 622.7 MB in 162,669 digests with no build, and its packs are byte-identical to a full build. Q2 (b) ruled (D34).
- **Deltas.** Pages cost 2.07 to 135 times the records for uniform updates and 1.14 times for 10K clustered; pages win only for clustered changes above about 100K keys. D6 sends the smaller (rule 1 shortcut at three times raw).
- **Stale.** Records from blocks beat the pull at every gap (pull 4.6 to 45.6 times the records); below the cut the pull wins up to about 6 h of moderate churn (0.27 to 0.86 of the copy) and loses from about 24 h. The sampled stop rule ends within 1.079 of the smaller of pull and copy at every 5M gap, where the one-level-ahead rule reached 1.67.

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
   The authority builds no pages (Q4 (c) rejected; the owner ruled (b), D34).
6. Lead message (2026-11-08, jobs done): the document is finished from the four jobs. The model is
   unchanged since `Detent:4fa06bd`, so no rerun.
7. Corpora: `/Users/kronuz/code/Kronuz/Detent/.scratch/dense/corpus-100k.bin` (SHA-256
   `3214561e…1a5740`, equal to the dense-pages-series copy) and `corpus-5m.bin` (`bb87ae5d…52bc835`),
   each named with its hash in the job's `--env`; the model also prints the hash it read.
8. A forged-root case is valid only when sealed: the digest checks must pass, so the targeted
   structure or canonicity check is the one that refuses (lead's direction on job 261109-014058-00ad3c).
9. Lead note D33 (owner withdrew receivers-build-nothing): folded into the same document round, no
   extra rerun; Outcome and criteria unchanged; the build plan is "Candidate steps".
10. Stale gaps (A7): "T4 churn" for a time gap is T4's mix (98% updates, 1% inserts, 1%
   deletes) at namespace D6's moderate rate, 1M writes a day at 5M (`namespace-delivery.md:181`),
   with its skew (80% of writes to a hot 1% of keys); the hot set is a random 1% ("moderate", the
   harder case for pages) or a contiguous 1% ("hotrange"); "uniform" drops the hot set. Each key
   keeps its last write (net state); writes to inserted keys are not modeled.
11. The pull names pages by digest under a pinned head root; the relay indexes its page window by
   digest; the pull form keeps content digests (the receiver compares them before it holds the
   children) and zeroes header digest and placement; requests are modeled as 96 B plus 32 B per
   digest.
12. Stop rule (lead, round 4: decide before overspending): sample, then decide. At the first pull
   level of at least 256 pages, the pages whose digest starts below 0x10 and their subtrees are
   pulled first; the rest is estimated as the sample's bytes times the other pages' keys over the
   sample's keys (a ratio estimator: the count-scaled one erred up to 2.7× at 100K, the
   key-weighted one within 15%); the full copy is taken when the estimate passes the copy, since
   spent bytes are sunk. No backstop after the decision: the bound is the pull of every page
   (1.33× the copy at 5M). 256 over 1,024 because at 100K a 1,024 threshold decided after a third
   of the copy (1.30× of the smaller against 1.09× with 256). Smoke runs at 100K under `quiet
   light`, not evidence.
13. D6 rule 1's shortcut moves from twice to three times the raw records: the sent/raw factor
   ranges 0.48 (1-key row) to 2.2, so twice could be 0.96 as sent; three gives at least 1.44. The
   only row between, 2M uniform at 2.74 raw, goes to rule 2 and still sends records.
