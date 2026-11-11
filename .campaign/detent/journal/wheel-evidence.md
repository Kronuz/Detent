# wheel-evidence: journal

State: waiting on jobs 261111-120919-26a86b 261111-121548-d1f618 261111-121549-8cd0f8 261111-121551-0e58b1 261111-121553-d6c80e
Quest: .campaign/detent/quests/wheel-evidence.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Rows queued at 22a10ab7 (candidates a-e), stash pinned at stash:d249352: tests
261111-120919-26a86b (light, A1), smoke 261111-121548-d1f618 (strict), then single
261111-121549-8cd0f8, cross 261111-121551-0e58b1, mem 261111-121553-d6c80e (exclusive, A2).
Superseded: tests 261111-120050-51af98 and smoke 261111-120056-0484f0 at 67f3f4c3 (a-d).

## Next

1. Read the rows' summaries; rerun once with R=10 any row the rule calls inconclusive.
2. Results, numbers and the pick by the rule into `docs/design/wheel-evidence.md`.
3. Optional experiment 5 if (d) loses cross-thread only on inbox contention: a striped inbox.

## Blocked on

Nothing.

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

- Candidate (e) added after the smoke 261111-120056-0484f0 (strict, noisy, not a decision row):
  (d)'s 56-byte entry put its single-loop score 14-31% over (a)'s. (e) keeps (a)'s 32-byte entry
  and links the inbox through `next`, with (c)'s contract (other threads arm unarmed entries).
  The rule's thresholds are unchanged; its unified list grows to (b)-(e); the rows queued at
  67f3f4c3 were cancelled before running and every row runs with (e).
- Single-loop passes visit entries in one random order (D4 went in index order), so a 1M row
  pays the cache misses a real loop does; the numbers are not comparable with D4's table.
- Cross-thread producers read steady_clock every 64 arms, the same for every candidate.
