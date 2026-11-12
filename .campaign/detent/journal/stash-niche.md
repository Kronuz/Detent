# stash-niche: journal

State: waiting on jobs 261111-231503-9f6729 261111-231504-ab1b64 261111-231504-105c4b 261111-231505-db629f 261111-231503-c6e0f3 261111-231504-d0ab1d 261111-231504-a7001e 261111-231505-46ce45
Quest: .campaign/detent/quests/stash-niche.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

The first rows (261111-225202-d8a30c, -225206-c080d7, -225211-0eb5ec, -225215-abef30; memory
-225204-6df5aa, -225208-e62bf7, -225213-02c592, -225217-6c3043) finished, all 144 runs valid, and
the doc's Results and verdict (899c8bd7, 90309fe6) quote them: no use is kept, retirement stands;
stash wins time only at 24 h x 1,000 keys for P = 1 and 4; the memory gate fails stash everywhere;
no row inconclusive. `campaign brief` flagged those jobs as "ran other code" (only the doc and
journal changed after them), so all eight were rerun at 90309fe6:
- rows: 261111-231503-9f6729 (1 s x 1,000), 261111-231504-ab1b64 (1 s x 1M),
  261111-231504-105c4b (24 h x 1,000), 261111-231505-db629f (24 h x 1M)
- memory: 261111-231503-c6e0f3, 261111-231504-d0ab1d, 261111-231504-a7001e, 261111-231505-46ce45

## Next

1. When the reruns finish: summarize them (stash_niche_summary.py over the eight logs); replace the
   doc's tables and job ids with the rerun's numbers; check the verdict still follows the rule
   (a row inconclusive reruns with 12 pairs).
2. Fill the Evidence table from `campaign brief`; State "ready for review".

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
