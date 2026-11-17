# delta-plan-fixed-cost: journal

State: ready for review
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

1. Review. The suite at the head, 261117-121549-83f289, is still running; it passed 268/268 at Detent:bd51ade2,
   and the head adds only the probe's test change. Main moved since the merge (campaign files only).

## Blocked on

Nothing. The uniform miss was ruled by D110 (option a): A2 is restated, and there is no SHA-256 quest.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| A1 | 261117-075622-1f24d9 (profile, f 0.05 at 1M, main's code; 100K: 261117-075635-2d7760): the delta rewrites 81-82% of the store's bytes; OverlayAssembler re-packs every touched page (39-44%), WarmGraphValidator re-validates every new page (23-28%), TrieOverlay first-touch verify_page per old page (~15-20%). Per store page, not per changed key. The fix's profile at the head: 261117-094355-e06b99. |
| A2 | Strict 261117-094335-a15eaa: uniform 4.1% at 1M (stated with where its CPU goes, as D110 restates A2), clustered 30.2% at 1M and within 2x at 100K and 1M; 5M exclusive 261117-094351-8b7ee4 (clustered 1.961 s against 6.90 s); mutant 261117-121549-7a86f2 (at bd51ade2: 261117-094323-69e26d killed). |
| A3 | 261117-094340-baa902: identical at 100K (0.05, c0.05, 0.5) and 1M (0.05, c0.05). |
| A4 | Suite 261117-121549-83f289 (at bd51ade2: 268/268, 261117-094318-dc96be), pins 261117-121554-a6db7d, probe 261117-121529-6da30a (2 leaf reads per changed key at 100K and 1M; 950,000 moved unread at 1M), fails without 261117-121537-35314f (40 per key); smoke 261117-121521-315097 (600K: 2 leaf reads per key). |
| A5 | docs/perf/delta-plan-scaling.md "The fixed part: moved definitions and repeated page reads". |

## Result

Warm validation skips definitions a delta moves unchanged; the trie overlay reads each old node once per change;
a verified page is matched by its raw header. Published bytes are unchanged (A3). Clustered f 0.05: 30.2% less
plan CPU per rewritten byte at 1M, within 2x of keys x 13.8 us at 100K, 1M and 5M (7.6-10.6 us per key).
Uniform f 0.05: 4.1% at 1M (D110); at 5M a delta applies in 0.88x the full copy. Leaf reads per changed key are
2 at every size. Results: docs/perf/delta-plan-scaling.md "The fixed part". Open: the uniform delta's plan stays
per rewritten byte (DTPAGE03 re-packing, D107).

## Rulings I made

- The A4 probe takes format-wide limits (ViewLimits::prepared(), wire records 8M) everywhere, as the T9
  harness does: one shape for every size, and the default 500K-record limits are not what it tests.
- The A1 profile rerun at the head (261117-094355-e06b99, campaign's freshness rule) profiles the fix, not
  main; A1's cause stays cited to main's profile 261117-075622-1f24d9, the rerun says where the rest goes.

- Profiled with a scratch port of main's T9 harness (.scratch/dpfc/t9/), since main's copy does not build;
  the harness is outside the file set, so it is the lead's to fix on main.
- Profile jobs are light: sampling shares; shares and bytes decide A1, not wall times.
