# delta-plan-fixed-cost: journal

State: working
Quest: .campaign/detent/quests/delta-plan-fixed-cost.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

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

1. Smoke 261117-082340-afb229; then A2 strict, A3 identity, A4 suite + probe, mutant jobs (chained).
2. Results into docs/perf/delta-plan-scaling.md (A5); the 5M exclusive round after A2's strict job.

## Blocked on

Nothing.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| A1 | 261117-075622-1f24d9 (profile, f 0.05 at 1M, main's code; 100K: 261117-075635-2d7760): the delta rewrites 81-82% of the store's bytes; OverlayAssembler re-packs every touched page (39-44%), WarmGraphValidator re-validates every new page (23-28%), TrieOverlay first-touch verify_page per old page (~15-20%). Per store page, not per changed key. Details under Blocked on. |

## Result

<When ready for review: what changed, its measured effect with numbers and the jobs behind them,
and what it leaves open. Results and how it works go in the docs of the repository it changed
(docs/perf/, docs/mechanisms/, a library's own); name them here. `campaign map` shows this.>

## Rulings I made

- Profiled with a scratch port of main's T9 harness (.scratch/dpfc/t9/), since main's copy does not build;
  the harness is outside the file set, so it is the lead's to fix on main.
- Profile jobs are light: sampling shares; shares and bytes decide A1, not wall times.
