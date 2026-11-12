# stash-niche: journal

State: waiting on jobs 261111-225202-d8a30c 261111-225206-c080d7 261111-225211-0eb5ec 261111-225215-abef30 261111-225204-6df5aa 261111-225208-e62bf7 261111-225213-02c592 261111-225217-6c3043
Quest: .campaign/detent/quests/stash-niche.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Checks 261111-213900-917751 and smoke 261111-213902-d798e3 on wheel:50665c8 passed (valid; 24 h:
120,808 wakes, 20,875 empty, for 100,000 keys; w/b cost 0.238; 1 s: 0.137). Rows submitted (A1,
specs checked with `campaign job show`; a first batch with the wrong arguments was cancelled):
- exclusive rows, 6 D51 b/w pairs at P = 1, 4, 14 each (3 rows per job):
  1 s x 1,000: 261111-225202-d8a30c; 1 s x 1,000,000: 261111-225206-c080d7;
  24 h x 1,000: 261111-225211-0eb5ec; 24 h x 1,000,000: 261111-225215-abef30
- strict memory (bytes per key, count build), same cells in the same order:
  261111-225204-6df5aa, 261111-225208-e62bf7, 261111-225213-02c592, 261111-225217-6c3043

## Next

1. When the jobs finish: `python3 docs/design/async-drivers-bench/stash_niche_summary.py` over the
   four row logs and four memory logs (job logs under ~/.scratch/campaign/jobs/<id>/a1/log).
2. A row that comes out inconclusive reruns with 12 pairs (rule 3).
3. Write the doc's Results and verdict by the rule; fill the Evidence table from `campaign brief`.

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

1. The niche stash arm uses a top level of 4,801 x 18 s (not 4,800): harness choice so 24 h fits from any base; wheel_vs_stash.cc untouched.

<Decisions inside the quest's scope, so the reviewer can check them.>
