# delta-plan-fixed-cost: journal

State: waiting on jobs 261117-075622-1f24d9 261117-075635-2d7760
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

1. Read the profiles; name the cause by function and per-page work (A1 row).
2. If the rewrite dominates: Blocked on the lead (A2's bound against the fixed page format), with the
   per-page constants the profile shows as the options. Otherwise counting probe (A4) and the fix.

## Blocked on

<What, and who decides. "Nothing" when nothing.>

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| <A1> | <from `campaign brief`> |

## Result

<When ready for review: what changed, its measured effect with numbers and the jobs behind them,
and what it leaves open. Results and how it works go in the docs of the repository it changed
(docs/perf/, docs/mechanisms/, a library's own); name them here. `campaign map` shows this.>

## Rulings I made

<Decisions inside the quest's scope, so the reviewer can check them.>
