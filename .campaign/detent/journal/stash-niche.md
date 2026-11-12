# stash-niche: journal

State: waiting on jobs 261111-213243-cb4908 261111-213246-cecef8 261111-213249-1b4870 261111-213224-87c403 261111-213226-5f60b5 261111-213229-d88bf1 261111-213232-d87453 261111-213251-5ff0a1
Quest: .campaign/detent/quests/stash-niche.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Smoke 261111-200152-81602e and check 261111-200145-53e785 passed. Smoke read: valid runs; 24 h x
100k keys: stash cost 2,375 ns/key, (f) 141,619 ns/key (drain 14 s: 82.8M wakes, 82.7M empty);
1 s: (f) 0.13x of stash's cost. Stash heap 470 B/key vs (f) 0.7: rule 2's gate fails stash
wherever stash allocates (noted in the doc, rule unchanged; summary also prints time-only).
Exclusive jobs last at most 10 min here, so rows are split. Submitted (A1):
- 1 s x 1,000 keys, P 1/4/14, 6 pairs: 261111-213243-cb4908
- 1 s x 1,000,000: 261111-213246-cecef8
- 24 h x 1,000: 261111-213249-1b4870
- memory (strict, count build) 1 s x 1,000 / 1 s x 1M / 24 h x 1,000 / 24 h x 1M: 261111-213224-87c403, -213226-5f60b5, -213229-d88bf1, -213232-d87453
- timing probe (strict, not evidence): one pair, 24 h x 1M, P=4: 261111-213251-5ff0a1. It sizes the
  24 h x 1M rows (the (f) drain may take minutes): then submit them per P and pair range
  (--env PRODUCERS=p --env PAIR_START=k, "row 86400000 1000000 N"), 10 min each.

## Next

1. Read the probe; submit the 24 h x 1M rows (exclusive, <= 10 min, tagged --for A1).
2. Summarize all row logs with stash_niche_summary.py (rows split over jobs: pass all their logs).
3. Fill the Results and verdict in docs/design/stash-niche.md by the rule.

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
