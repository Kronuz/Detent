# stash-niche: journal

State: waiting on jobs 261111-213553-ca2652 261111-213556-6fd40d
Quest: .campaign/detent/quests/stash-niche.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

D74 (lead): wheel arm = Kronuz/wheel wheel.hh at wheel:50665c8 (vendored as stash_niche_wheel.hh,
arm "w"), memory gate on total bytes per key; recorded in docs/design/stash-niche.md (107fe6fb).
Cancelled the earlier rows, memory jobs and probe (they ran the prototype (f)); main merged.
The prototype smoke 261111-200152-81602e is kept as the reason (82.7M empty wakes of 82.8M).
Submitted: checks 261111-213553-ca2652 (ASan+UBSan, TSan), then smoke 261111-213556-6fd40d on the
library (strict, after the checks). Rows are not submitted until the smoke shows the 24 h wakes
near the occupied slots plus cascades.

## Next

1. Read the smoke: 24 h wakes (calls) vs occupied slots + cascade wakes, its drain time at 1M keys.
2. Submit the 12 rows (exclusive, <= 10 min each, --for A1): 1 s x 1,000; 1 s x 1M; 24 h x 1,000
   (6 pairs, P 1/4/14 each); 24 h x 1M per P (PAIR_START split if a run is long); memory jobs
   (strict) for the four cells.
3. Summarize all logs with stash_niche_summary.py, write the doc's Results and verdict.

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
