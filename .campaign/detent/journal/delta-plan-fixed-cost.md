# delta-plan-fixed-cost: journal

State: waiting on jobs 261116-213146-06b640 261116-213150-f47767
Quest: .campaign/detent/quests/delta-plan-fixed-cost.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

A1: profile the f 0.05 plan (macOS `sample`, T9 harness on main's code) at 1M (job 261116-213146-06b640, 3 runs)
and 100K (261116-213150-f47767, 5 runs). Scripts: .scratch/dpfc/profile.sh (build, prepare, sampled delta runs,
new files/bytes the delta wrote) and .scratch/dpfc/digest.py (inclusive/self samples under
PagedDeltaPlanBuilder::step); output in ~/.scratch/dpfc/profile-{1m,100k}. Corpora: ~/.scratch/dps/corpus-{100k,1m}.bin.

Hypothesis to test: at f 0.05 nearly every leaf page holds a changed key (64 KiB pages), so the plan rewrites and
the warm validator re-walks about the whole store (new walk: PageValidator + validate_definition per new leaf; old
walk: every node of every removed page; first-touch verify_page of every removed page). The new-bytes figure
says how much of the store the delta rewrites.

## Next

1. Read the profiles; name the cause by function and per-page work (A1 row).
2. Counting probe (A4) and the fix inside the file set.

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
