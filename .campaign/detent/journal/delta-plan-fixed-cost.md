# delta-plan-fixed-cost: journal

State: blocked on lead: A2 uniform cut 4.1%, not 30% (waiting on jobs 261117-121529-6da30a 261117-121537-35314f 261117-121549-7a86f2 261117-121549-83f289)
Quest: .campaign/detent/quests/delta-plan-fixed-cost.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Session 5 (2026-11-17, lead's message: report the uniform miss, fix the probe, write A5). The 600K probe
hit the test's 100M-turn guard with one-byte validation steps; the probe now validates with the default budget
(Detent:0b759d10, mutant Detent:268feced); smoke 261117-121521-315097 passed (600K: 2 leaf reads per changed
key, 570,000 moved unread). A5 written into docs/perf/delta-plan-scaling.md "The fixed part".

Session 4 (2026-11-17): the evidence jobs at Detent:c3bba179 failed in the harness. Merged main
(Detent:1a98a291, authority and relay only). build.sh now creates each side's .scratch/ (the ref-base
checkout has none): smoke 261117-093217-e1c42f passed (both sides built, x86_64; one ab round and identity at
100K). The probe aborted past 500K on default limits twice: the wire decode (Detent:15b44583) and the
fixture's install (Detent:bd51ade2) now take format-wide limits; smoke 261117-093230-64daa8 failed on the
second. A4 mutant re-cut on the new head: Detent:afeb7a64. Early signal, not evidence: the 100K smoke cut
per rewritten byte is 4.6% (one light round, 99.8 to 95.2 ns/byte) and 11.7% in the identity run; A2 may
miss 30%, and the head profile (e06b99) says where the rest goes.

Session 3 (2026-11-17): D107 answered Blocked on (option 2 with 3's restatement; DTPAGE03 and the bytes
stay); A2 restated in the quest file. Merged main (Detent:a69811b1).

1. T9 harness port committed (docs/perf/namespace-measure-t9.cc, now in the file set); build job
   261117-080826-96b70f. The harness also reports plan_cpu_s and plan_bytes, and an arm `cF` is a
   clustered delta (one run of adjacent keys, a fraction F of the store).
2. Cuts, each inside the file set:
   - TrieOverlay: a change read every old node twice (walk down, then convert on unwind) and counted an
     inline node's keys on both reads by walking its subtree; frames now keep the walk's view and only
     conversion counts keys.
   - checked_page_bytes: a page already verified on first touch is matched to its reference by raw header
     fields, not a full decode_page_header + validate.
   - WarmGraphValidator: the old walk runs beside the new one in the same descending order, gated by the
     new walk's leaf (a node above the bound is past the new walk, so its reused pages are frontiers
     already). A new definition whose old leaf has the same key and bytes is matched and neither side is
     read; everything else is validated as before. Counters: definitions_validated, old_definition_headers,
     moved_definitions.
   - Old pages: first-touch verification is once per mapping already (VerifiedPages is shared by the
     overlay, the assembler and the validator); the remaining repeated reads were the overlay's double
     inspect and full header decodes, cut above.
3. Tests: test_delta_plan_moved_definitions (probe: validated == removed == changed keys, moved >= 8x;
   a one-page store whose moved definition is rewritten to an invalid revision is rejected, a valid
   rewrite is validated); its mutant drops the byte comparison.
4. Scripts (worktree .scratch/dpfc/): build.sh (T9 harness of this checkout against each side's library),
   ab.sh (interleaved base/fix runs at one prepared store), ab_digest.py (ns of plan CPU per rewritten
   byte, cut, clustered bound), identity.sh (A3).

## Next

1. Jobs at Detent:0b759d10: A4 probe 261117-121529-6da30a, its --fails-without 261117-121537-35314f,
   suite 261117-121549-83f289, A2 mutant 261117-121549-7a86f2 (pins 261117-121554-a6db7d passed). The A2,
   A3 and 5M jobs ran at Detent:bd51ade2; the head adds only the probe's test change.
2. The lead's ruling on A2's uniform 30% (Blocked on).

## Blocked on

A2's uniform 30% is missed. Strict A/B 261117-094335-a15eaa (beside 7 light holds): at 1M a uniform f 0.05
plan's CPU per rewritten byte fell 4.1% (68.1 to 65.3 ns/byte; 2.823 to 2.706 s), 25.9 points short; at 100K
the pairs are inconclusive (median -20%, fix/main per round 1.34, 1.34, 0.89, 0.99, 1.20). At 5M (exclusive
261117-094351-8b7ee4) apply CPU fell 3.5% against main's round (12.14 to 11.72 s), and the delta now applies
in 0.88x the full copy (13.95 against 15.83 s). The clustered half holds: 30.2% at 1M (199 to 139 ns/byte) and
within the 2x bound at every size (0.064, 0.396, 1.961 s against 0.138, 1.380, 6.90 s).
Where the uniform plan's CPU goes now (profile 261117-094355-e06b99, 1M, 8,684 samples): SHA-256 24.6%
(kronuz::sha256 compress_portable, the scalar path: old-page verify 9.5%, new-page commitments 7.9%, pack
checksums 7.2%); old-page first touch besides SHA 21% (verify_page_structure 9.8%, the first header read about
10%, likely faults on the mapped base); packing besides SHA about 14%; warm validation of new pages 22.1%;
overlay walk about 9%. What remains outside the file set or the format:
1. SHA-256 speed (Kronuz/sha256): a quarter of the plan; a SIMD or SHA-NI/ARMv8 backend is that library's change.
2. The page commitments and pack checksums DTPAGE03 requires (about 15%), and re-packing the unchanged
   clusters a uniform delta touches (D107 reserves the format change).
3. First-touch verification of the touched old pages (about 20%) runs once per mapping. Each T9 run is a fresh
   process with fresh mappings, so every plan pays it; an agent that keeps its base mapping across plans may
   pay it once (not measured; VerifiedPages lives in verified_pages.hh, outside the file set).
Options: (a) restate A2's uniform target to the measured 4% and land on the clustered result, A3 and A4;
(b) measure a warm second plan on one mapping to see how much of the 20% a long-lived agent already skips;
(c) a SHA-256 quest in Kronuz/sha256. Recommendation: (a) now, then (c), which pays every paged path.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| A1 | 261117-075622-1f24d9 (profile, f 0.05 at 1M, main's code; 100K: 261117-075635-2d7760): the delta rewrites 81-82% of the store's bytes; OverlayAssembler re-packs every touched page (39-44%), WarmGraphValidator re-validates every new page (23-28%), TrieOverlay first-touch verify_page per old page (~15-20%). Per store page, not per changed key. The fix's profile at the head: 261117-094355-e06b99. |
| A2 | Strict 261117-094335-a15eaa: uniform 4.1% at 1M (MISSED, Blocked on), clustered 30.2% at 1M and within 2x at 100K and 1M; 5M exclusive 261117-094351-8b7ee4 (clustered 1.961 s against 6.90 s); mutant 261117-121549-7a86f2 (at bd51ade2: 261117-094323-69e26d killed). |
| A3 | 261117-094340-baa902: identical at 100K (0.05, c0.05, 0.5) and 1M (0.05, c0.05). |
| A4 | Suite 261117-121549-83f289 (at bd51ade2: 268/268, 261117-094318-dc96be), pins 261117-121554-a6db7d, probe 261117-121529-6da30a, fails without 261117-121537-35314f; smoke 261117-121521-315097 (600K: 2 leaf reads per key). |
| A5 | docs/perf/delta-plan-scaling.md "The fixed part: moved definitions and repeated page reads". |

## Result

<When ready for review: what changed, its measured effect with numbers and the jobs behind them,
and what it leaves open. Results and how it works go in the docs of the repository it changed
(docs/perf/, docs/mechanisms/, a library's own); name them here. `campaign map` shows this.>

## Rulings I made

- The A4 probe takes format-wide limits (ViewLimits::prepared(), wire records 8M) everywhere, as the T9
  harness does: one shape for every size, and the default 500K-record limits are not what it tests.
- The A1 profile rerun at the head (261117-094355-e06b99, campaign's freshness rule) profiles the fix, not
  main; A1's cause stays cited to main's profile 261117-075622-1f24d9, the rerun says where the rest goes.

- Profiled with a scratch port of main's T9 harness (.scratch/dpfc/t9/), since main's copy does not build;
  the harness is outside the file set, so it is the lead's to fix on main.
- Profile jobs are light: sampling shares; shares and bytes decide A1, not wall times.
