# dense-distribution review: design

Commit: 791570b7ef9597c3ad5f63b2e0805795ec9b7612
Verdict: CHANGES
Date: 2026-11-09

APPROVE: lands as is, at this commit's code. CHANGES: lands after each blocking finding is fixed
and confirmed by a job at the fixed commit. REJECT: the approach is wrong; the quest is re-planned.
An APPROVE lists no blocking finding; a CHANGES lists at least one.

This re-review covers `Detent:d874004..791570b` in `docs/design/`. The five blocking findings of
the first round (DD-1 to DD-5) are fixed, each confirmed by a job at this commit. The non-blocking
ones are folded in. Three things in the new scope block:

1. **D10's mid-pull re-pin loses pages (DD-14).** It counts pulled pages as base pages before
   their subtrees arrive. D4 then refuses the result, so nothing wrong is published, but the pull
   fails.
2. **The stop rule's estimate breaks under value-size skew (DD-15).** When one sampled subtree
   holds most of the pull's bytes, the rule takes the full copy at 6.4 times the cheaper answer.
   The document states its accuracy from one corpus whose values are all about the same size.
3. **DD6's dependencies put it after namespace S8 (DD-16).** D10's own numbers show the pull
   stops paying once S8 is in.

D6 rule 1 at three times is supported, and the cut finding is stated honestly.

## Evidence I relied on

All jobs ran with `--ref DENSE=28d3ed9` and the corpus SHA-256 in `--env`, and exited 0.

- **Reruns at `Detent:791570b`:** `261109-093826-07e6d1` (check and refusals, 100K),
  `261109-093827-d34c29` (my first-round probe), `261109-093827-46d766` (full copy, 5M),
  `261109-093827-efe4c3` (deltas, 5M) and `261109-093827-ed3811` (agent-day, 5M). I compared each
  log with the document: 25,705,725 B as sent; 306,989,416 + 315,759,038 B hashed; files identical
  to the build; 6,024 / 6,710,310 / 62,986 / 6,792,600 B of delta pages; 1,495,424,160 B and
  $299.08 per agent-day. All match.
- **Runs at earlier commits that stand for this one:**
  - `261109-030636-4ce079` (sweep, at `Detent:29b5e53`). Its rerun `261109-093827-a93501` was
    still running. `delta_main` and the receiver are unchanged between the two commits:
    `git diff 29b5e53 791570b` touches only `make_transfer`'s new "pull" branch and adds the stale
    mode.
  - `261109-092232-0a7ab9` (stale gaps, at `Detent:7ba0734`). Its log reproduces both D10 tables
    row by row: estimates −5.2% to +2.9%, worst 1.079 times the cheaper side, 10 and 7 round
    trips. Its rerun at this commit, `261109-093828-cd4dc2`, has the same stop-rule lines, and
    `git diff 7ba0734 791570b -- docs/design/dense-distribution-model.py` is empty.
- **What these establish:** A1, A2 and A6, D4's refusal table with a mutant per check, the stale
  tables of A7 on the model's three churn shapes, and the warm path's root check. I did not rerun
  them.

## What I checked myself

The probe scripts are in `.scratch/review-dense-distribution-design/`: `probe.py` (first round)
and `stale_probe.py` (this round). Both drive the quest's own model at `$JOB_SRC`.

| Job | Probe | Result |
| --- | --- | --- |
| `261109-094830-64ab20` | `stale_probe.py repin` at 100K. h1 is 24 h of moderate churn after the base. The pull of h1 stops after level k, then re-pins to h2, which is 0.1, 0.5 or 2 h of uniform churn after h1. It is run twice: first counting every page pulled so far as a base page at its path (D10 as written), then counting only pages whose h1 subtree is complete | **As written:** 4 to 849 of the R→h2 delta's pages are never fetched (k = 2 to 4), and D4 refuses with "base lacks a page the delta reuses" or "content digest mismatch". **Complete subtrees only:** 0 missing, D4 accepts with the root equal, in all 12 cases |
| `261109-094731-e7d7c1` | `stale_probe.py repin` with h2 = 6 h after h1 (100K), and `skew` at 100K: the model's 24 h gap, a 400-key run of 1 KiB values plus 2,000 scattered updates, and a 60/40 split | Repin: no page missed, because a 6 h head move rewrites every upper page. Skew: every outcome below was computed for each of the 16 samples the rule could draw (digest high nibble 0 to 15; the design's sample is nibble 0). The 24 h gap's estimates range from −10.9% to +12.3%, 5 of 16 choices are wrong and the worst is 1.16 times the cheaper side (pull / copy 1.10). The blob run ranges from −49% to +171%, 6 of 16 are wrong and the worst is 1.36 times. The split ranges from −8.7% to +9.9% with no wrong choice |
| `261109-094738-064abd` | `stale_probe.py skew` at 5M, over the same 16 samples: the model's 24 h moderate gap as a control, a contiguous run of 2,000 keys rewritten with 1 KiB random values plus 2,000 or 20,000 scattered updates, and two contiguous splits near the copy's size | **Control:** sample 0 equals the model's `stop_rule` (30,096,430 B, 1.064). The 16 samples range from −1.8% to +1.9%, with no wrong choice and a worst of 1.100. **Blob + 2,000:** pull 4,685,721 B against a 27,792,113 B copy (0.169). Fifteen samples underestimate by 38% to 53% and pull, which is right. Sample 11 holds the blob's subtree, 47.9% of the pull below it: it estimates the rest at 43,445,167 B (+1,665%), takes the copy and pays 30,096,077 B, **6.42 times the cheaper side**. **Blob + 20,000:** estimates −20.7% to +34.9%, with no wrong choice at pull / copy 0.478. **Splits:** 600,000 or 750,000 updates inside the first 60% or 75% of keys, plus 1,000 in the rest. At pull / copy 0.889 the estimates range from −25.1% to +30.3%, with 1 wrong choice of 16 and a worst of 1.209. At pull / copy 1.097 they range from −24.8% to +16.5%, with 6 wrong choices of 16 and a worst of 1.100 |

I used separately compressed bucket costs as an approximation of the rest. It is 1 to 5% above the
exact one-pass cost: for sample 0 at 100K, 673,908 B against the model's 639,623. The 6.42 times
does not depend on that approximation: it is the sample plus the copy, against the model's own
pull total.

Answers to the four questions:

1. **DD-1 to DD-5 are fixed.**
   - **DD-1 and DD-2.** `07e6d1` refuses P1 for "layout" and P2 for "order". It refuses sealed
     forgeries for keys in both forms, height, labels, parse and canonical, each by its own check,
     and a mutant without each check accepts. `d34c29` refuses P1, P2 and P4.
   - **DD-3.** It took option (b): D9 keeps namespace D10.3's full copy, and `d34c29` refuses P7
     and P8c as the document now expects.
   - **DD-4.** `ed3811` gives the per-agent-day rows. Q1 is moot under D33.
   - **DD-5.** DD3 needs S6 and S7, DD4 needs S8, and `46d766` reproduces DD3's 25,705,725 B.
   - **The rest.** P3 is accepted (DD-7), and DD-6 and DD-8 to DD-13 are folded in, except one
     stale citation (DD-17).
2. **New scope.**
   - **The four states hold.** The pulled set equals the occurrence writes (the model asserts it),
     so records beat the pull above the cut at 4.6 to 45.6 times.
   - **The warm path's claim is safe.** It rests on dense-pages D5 and D9 and on D1's root check.
     The two measurements it cites do not test locally built deltas (DD-18).
   - **The estimator.** It is a ratio estimator over a sample chosen by digest. That choice is
     independent of key order, so a skewed key distribution does not bias it. Its error grows with
     how unevenly bytes per key spread across the sampled level's subtrees.
     - Uneven change density widens it from the documented −5.2% to +2.9% to −25% to +30% at 5M.
       The loss then stays within the error, at most 1.21 times the cheaper side, as Limit 11
       says.
     - Uneven value size breaks it, because the receiver scales by keys and cannot see how large
       a subtree's values are. A run of large values lands in the sample one time in 16, and then
       the rule pays 6.4 times the cheaper side (DD-15).
   - **The bound.** In units of the copy, the bounds the document states hold: a wrong copy costs
     the spend plus the copy, and a wrong pull costs at most about the pull of every page (1.28 to
     1.33 times the copy at 5M). Relative to the cheaper side there is no bound. "About a
     sixteenth of the pull" holds only when bytes spread evenly: the blob sample held 47.9%.
   - **The mid-pull re-pin is wrong as written (DD-14).** D4 catches it, so the result is
     liveness and bytes, never a wrong publish.
3. **Rule 1.** I checked every row of `4ce079` and `4e209f`. The ratio as sent is 0.48 to 2.20
   times the raw ratio. Every row above three times raw (all uniform rows up to 1,000,000 keys,
   and 100 clustered keys at 3.48) has a ratio as sent of 3.82 or more. The 2,000,000-key row, at
   2.74 raw, falls through to rule 2 and still sends records. At two times, the 1-key row's factor
   (0.48) would allow 0.96 as sent, so three times is the threshold the data support. It remains
   only a shortcut: either encoding is verified against the same root (D1), so rule 1 changes
   bytes, never what publishes. Limit 8 says so.
4. **The cut finding is honest and placed where a reader sees it:** D10, "Where the cut falls",
   the abstract, and the journal's note to the lead.
   - It matches `namespace-delivery.md:181`: 32 to 44 days at α = 1, and 15 to 20 days at α = 0.5.
     That is at least 15 days for every α the namespace model ran, against a pull that loses from
     about 24 h.
   - DD6 is not scoped to where it pays (DD-16).
   - D10 also routes "stale above the cut" to records where the owner's Decision 6 says pages or
     the copy. The numbers support it, and the owner should see it named as a refinement (DD-20).

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| DD-1 | no | defect | Fixed. The wire is pages back to back. The receiver derives the layout and requires seal order (D2 to D4), and P1 and P2 are D4 rows and DD1 criteria, each with a mutant. | `07e6d1` (P1 'layout', P2 'order', mutants accept); `d34c29` (P1, P2 refused) | None. |
| DD-2 | no | gap | Fixed. D4 has refusal rows for structure and canonicity under a sealed forged root, with a mutant per check, and DD1's criteria name each. | `07e6d1` (keys in both forms, height, labels, parse, canonical) | None. |
| DD-3 | no | defect | Fixed by option (b): repair stays namespace D10.3's full copy, now a 25.7 MB pages copy (D9; the header "Keeps namespace D10.3"). | `d34c29` (P7, P8c refused, as D9 now expects) | None. |
| DD-4 | no | gap | Fixed. Q1 has per-agent-day bytes and dollars: 1.50 GB against 12.2 MB, $299 against $2.44 at 1-minute polls. Q1 is moot under D33. | `ed3811` | None. |
| DD-5 | no | defect | Fixed. DD3 needs S6 and S7, DD4 needs S8, and D6's records size is S8's block answer. | Candidate steps; `46d766` | None. |
| DD-6 to DD-13 | no | nit, gap | Folded in, as the journal maps them. DD-9 is moot under DD-3 (b). DD-13 is partly done: see DD-17. | Document at `Detent:791570b` | None. |
| DD-14 | yes | defect | D10's re-pin, "counting the pages it already verified as base pages at their paths", treats a pulled page as a held subtree before its children have arrived. When the new head keeps that page, the pull never descends below it. In the probe, 4 to 849 of the delta's pages are never fetched, and D4 refuses the result ("base lacks a page the delta reuses", "content digest mismatch"). The pull fails exactly when the head moves a little, which is what crossing the window's 64 positions means. It is safe, because D4 catches it, but the procedure does not reach DD6's criterion that "the pull still ends at the new head's root". | `64ab20`; D10, "The pull", last paragraph | Count a pulled page as a base page only once its whole subtree is held. A page pulled without its subtree saves only its own fetch, and its children are compared again. With that rule the probe misses 0 pages and D4 accepts with the root equal (`64ab20`). Add an interruption after each level to DD6's re-pin criterion. Rerun `stale_probe.py repin CORPUS-100K 0.1 0.5 2`; it must report 0 missing for the rule the document states. |
| DD-15 | yes | gap | The stop rule scales the sample by keys, but the pull's bytes follow value sizes, which the receiver cannot see before it pulls. When one subtree holds most of the pull, a sample that contains it overestimates by up to +1,665% and takes the copy at **6.42 times** the cheaper side: 30.1 MB against a 4.7 MB pull. That happens one time in 16, with a 2,000-key run of 1 KiB values beside 2,000 scattered updates at 5M. Samples that miss it underestimate by 38 to 53%, which is harmless here. The bounds D10 states hold in units of the copy: spend plus copy, or the pull of every page. Three statements read as bounds against the cheaper side, and each fails under value skew: "about a sixteenth of the pull below it", "that spend is its whole overshoot: at most 1.079" and Limit 11's "by at most the error". At 5M the sample held 47.9% of the pull, and the error was 16.6 times. The design's own sample (nibble 0) happened to miss the run. Uneven change density alone also widens the error well past "−5.2% to +2.9%": −25% to +30% at 5M, with losses up to 1.21 times. | `064abd` (blob:2000:2000:1024, sample 11; the two splits); `e7d7c1` (100K: −49% to +171%, worst 1.36); D10, "The stop rule"; abstract; Limit 11 | Either: (a) state the bound as it is: never more than the pull so far plus the copy, nor more than the pull of every page (about 1.33 times the copy at 5M); relative to the cheaper side, unbounded under value skew. Put this probe's 6.42 times, and its −25% to +30% under density skew, in Limit 11. Or (b) make the estimator robust and keep (a)'s statement. A wrong copy is the unbounded error, while a wrong pull costs at most the pull of every page. So the rule could add each sampled subtree above a set share of the sample's bytes once, at its measured cost, instead of scaling it, or require the estimate without the heaviest sampled subtree to pass the copy. Rerun `stale_probe.py skew CORPUS-5M gap:24:moderate blob:2000:2000:1024` to show the worst case under (b). The choice is the designer's; (a) is the minimum. |
| DD-16 | yes | defect | DD6 needs DD4, and DD4 needs namespace S8. D10 shows the pull pays only where a relay's records stop hours back, meaning the ring alone before S8 mirrors blocks, or an early disk-cap cut. After S8 the cut is 32 to 44 days back at moderate churn, and at least 15 days at any α the namespace model ran, while the pull loses from about 24 h. As written, DD6 becomes buildable when it stops paying, apart from the rare disk-cap cut in a small namespace after a mass delete (`namespace-delivery.md:139`). | Candidate steps (DD4 "Needs: S8", DD6 "Needs: DD4"); D10, "Where the cut falls"; `namespace-delivery.md:181` | The owner's call, since Decision 6 asks for the pull. (a) Keep DD6 but make it need only DD3 and the relay's page window indexed by digest (D7.3), not DD4's choice rule or S8. State that it pays only on relays without blocks, before S8, and on disk-cap cuts after S8. (b) Keep DD6 as written and state that after S8 it pays only on disk-cap cuts. (c) Drop DD6, so that a receiver below the cut takes the full copy; this narrows Decision 6 and needs the owner. Whichever is chosen, the step table and D10 must agree. |
| DD-17 | no | nit | The sweep rows of the delta table cite `4ce079` but carry the numbers of `261108-230300-c7df19` (at `Detent:b99d3a3`, before the DD-1 fix). For example, 100 uniform keys show 203,514 against 203,518, and 1,000,000 clustered keys show 4,713,346 against 4,705,091. The ratios agree to the second decimal. | `4ce079` and `c7df19` logs; "What the measurements say" | Refresh the sweep rows from `4ce079` (or `a93501`). |
| DD-18 | no | gap | The warm path's "three results" cite the full copy's replay and P9. Both concern received pages, and the model has no incremental D7 (dense-pages D7: "a specification for the incremental allocator, not evidence"). That a locally built delta matches everywhere follows from dense-pages D5 (each decision reads only its subtree) and D9's induction. D1's root check makes a divergence a refusal, not a fork. | D6, "What a warm receiver relies on"; `dense-pages.md` D7 at `Detent:28d3ed9` | Cite D5 and D9 as the reason and D1 as the guard, and drop or relabel items 1 and 2. |
| DD-19 | no | nit | The stop rule compares its estimate with the copy size the relay publishes (D10 step 4). A relay that misstates the size steers the choice. Only bytes are at stake, since D4 verifies either answer. | D10, "The stop rule" step 4 | One line saying that only bytes depend on it, or take the size from the copy's first fragment header (its total). |
| DD-20 | no | nit | Decision 6 says a stale receiver gets the missing pages or the copy. D10 sends "stale above the cut" records from blocks, which is right by 4.6 to 45.6 times, but labels the table "the quest's Decision 6" as if unchanged. | D10 table; quest Decision 6 | Name it as a measured refinement of Decision 6 for the owner. |

## Not run

- **Value skew at 5M with other run sizes or value lengths.** One run of 2,000 keys of 1 KiB shows
  the failure. I did not map its envelope.
- **Re-pins at 5M, or re-pins in the middle of a level.** The model shows the defect at 100K at
  level boundaries.
- **Write-grinding adversaries.** A writer who grinds content digests could aim at the sample. The
  cost is bounded in copy units, as DD-15 states, and correctness is untouched.
- **The sweep's rerun at `Detent:791570b`** (`a93501`) was still running. I relied on `4ce079`,
  whose delta code is identical.
- **No C++ code exists**, so there is no fuzzing, sanitizer or timing evidence. Those belong to
  DD1, DD3 and DD6.
