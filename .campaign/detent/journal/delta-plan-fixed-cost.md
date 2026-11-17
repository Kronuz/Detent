# delta-plan-fixed-cost: journal

State: blocked on the lead: A2's 2x bound needs a change to what a delta rewrites (published bytes), see Blocked on
Quest: .campaign/detent/quests/delta-plan-fixed-cost.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Session 2 (2026-11-17): the first profile jobs (261116-213146-06b640, 261116-213150-f47767) failed in the
build, not the script: main's T9 harness (docs/perf/namespace-measure-t9.cc) does not compile since
namespace-s7-ids landed (storage identity, DiskStore::operation/needs_work and the builders' GenerationId
arguments are gone). Pre-existing on main; the harness is outside this file set. Ported copy in
.scratch/dpfc/t9/ (owner.lock decides fresh vs existing, the store drives with work()->run(), no generation
ids); profile.sh and identity.sh build it. Resubmitted: 1M 261117-075622-1f24d9, 100K 261117-075635-2d7760.

A1: profile the f 0.05 plan (macOS `sample`, T9 harness on main's code) at 1M (job 261116-213146-06b640, 3 runs)
and 100K (261116-213150-f47767, 5 runs). Scripts: .scratch/dpfc/profile.sh (build, prepare, sampled delta runs,
new files/bytes the delta wrote) and .scratch/dpfc/digest.py (inclusive/self samples under
PagedDeltaPlanBuilder::step); output in ~/.scratch/dpfc/profile-{1m,100k}. Corpora: ~/.scratch/dps/corpus-{100k,1m}.bin.

Hypothesis to test: at f 0.05 nearly every leaf page holds a changed key (64 KiB pages), so the plan rewrites and
the warm validator re-walks about the whole store (new walk: PageValidator + validate_definition per new leaf; old
walk: every node of every removed page; first-touch verify_page of every removed page). The new-bytes figure
says how much of the store the delta rewrites.

Reading so far (no numbers yet): pages carry at most page_capacity (4 KiB) of nodes, and the test store holds
about 100 small definitions per page, so a uniform 5% answer touches about 1 - 0.95^k of the pages (87% at
k = 40, 99% at k = 100). The overlay assembler re-packs every removed page's unchanged clusters
(push_cluster); old pages above 4 KiB are pushed by reference. Fitting namespace-measure's plan times
(f 0.05 and f 0.5) gives a marginal 8.8-9.4 us per changed key and a fixed part of about 2.3 us per store key
(0.24 s at 100K, about 2.2 s at 1M, 11.5 s at 5M), the snapshot build's per-key cost. If the profile confirms
the rewrite is the fixed part, the published bytes (contract) fix how many pages a delta rewrites, so only
the per-page constant can move: that bears on A2's 2x bound and is a question for the lead.

Prepared: .scratch/dpfc/identity.sh (A3: builds main via --ref base and the head, publishes each fraction
from clones of one prepared store, compares every file's SHA-256).

## Next

1. Lead's ruling on Blocked on. Then, for option 2: counting probe (A4), the cuts, before/after exclusive
   paired rounds with an A/A band, A3 identity (.scratch/dpfc/identity.sh, ported harness).

## Blocked on

The lead (owner: page format and published bytes are a contract). Profile 261117-075622-1f24d9 (1M, 3 runs)
and 261117-075635-2d7760 (100K, 5 runs), main's code, light jobs (wall times noisy; CPU and bytes are not):

- A uniform f 0.05 delta rewrites 81-82% of the store's bytes at both sizes: 1M 41,478,579 B in 84 new files
  of a 49,784 KiB store; 100K 4,097,650 B of 4,880 KiB. Apply CPU 5.22-5.73 s at 1M vs 0.55-0.57 s at
  100K (9.8x for 10x the store at the same f): the "fixed part" is the rewrite, proportional to the store.
- At 1M the f 0.05 plan (7.4-9.7 s wall) costs more than the full copy's plan (5.38 s wall, same job).
- A2 target at 1M: 50,208 keys x 13.8 us = 0.69 s, 2x = 1.39 s.
- Where the plan goes (samples under PagedDeltaPlanBuilder::Work::step, runs 2 and 3), all per rewritten page:
  1. OverlayAssembler (re-pack every removed page's unchanged clusters, PagedPacker SHA-256 per page,
     SegmentBuilder SHA-256 per pack, verify_page of old pages): 44% / 39%.
  2. WarmGraphValidator on the new pages (PageValidator + decode_node per node, validate_definition per leaf,
     though most are unchanged definitions moved to new pages): 28% / 23%.
  3. TrieOverlay change walk: 25% / 35%, of which 61% (run 2) is first-touch verify_page (SHA-256 +
     structure) and decode_page_header of each old page; the per-key part is the rest.
  SHA-256 (portable compress, no SHA-NI on this Intel host) is about 21% of the plan.
- Floor inside the contract: the assembly alone is about 40% of about 4.5 s plan CPU, about 1.8 s > 1.39 s.
  Dropping validation and old-page verification entirely would still miss A2's 2x.

Options:
1. Format change: let a delta keep unchanged sub-page clusters in place (reference them, or smaller leaf
   pages) so the rewrite is proportional to changed keys. Reaches A2; changes the published bytes (contract).
2. Keep the bytes, cut the per-page constants: carry the base's validation proofs for moved-but-unchanged
   definitions instead of re-validating them, and reuse the first-touch verification. Estimate 30-45% off the
   plan at f 0.05 (1M about 4.5 -> 2.5-3 s CPU), store-proportional still; A2 restated per rewritten byte.
3. Restate A2 only: the bound holds for deltas whose keys cluster in pages (the rewrite is per touched page),
   and measure a clustered f 0.05 alongside the uniform one.
Recommendation: 2 with 3's restatement now (inside the contract, A3 byte identity holds); 1 as a separate
format quest if T9 at 5M needs a delta to beat the full copy.

Also, pre-existing on main and outside this file set: docs/perf/namespace-measure-t9.cc does not compile
since namespace-s7-ids (jobs 261116-213146-06b640, 261116-213150-f47767 failed in its build). The port is in
.scratch/dpfc/t9/ (owner.lock, DiskStore::work()->run(), no GenerationId arguments).

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
