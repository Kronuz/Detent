# consensus-dual-stack: journal

State: waiting on jobs 261111-100403-c63f54 261111-100400-7132ce
Quest: .campaign/detent/quests/consensus-dual-stack.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Implemented and pushed (branch quest/detent/consensus-dual-stack). Waiting on the check suite and the A1 mutant.

## Next

1. When both jobs pass: paste `campaign brief` table below, write Result, set State ready for review, check CI on the branch.

## Blocked on

Nothing.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| A1 | pending jobs |
| A2 | `CHANGELOG.md`, 2026-11-11 Changed: first entry names the `::` dual-stack acceptor |

## Result

<When ready for review: what changed, its measured effect with numbers and the jobs behind them,
and what it leaves open. Results and how it works go in the docs of the repository it changed
(docs/perf/, docs/mechanisms/, a library's own); name them here. `campaign map` shows this.>

## Rulings I made

- A1 host note: this host is macOS, whose default is dual-stack, so a plain revert of the
  `v6_only(false)` line does not fail the new `dual-stack` scenario here; FreeBSD's v6-only
  default is what CI's FreeBSD job (where enabled) checks. To still show the test detects a
  v6-only acceptor, `test/transport/dns_mutants.sh` gains a `v6-only` mutant (sets
  `v6_only(true)`, what a v6-only OS default does); the --fails-without job runs it.
- Test: voter three listens on `::`; voter one dials it at 127.0.0.1, voter two at ::1 (lower id
  dials), all three commit and hold 4 connections each. Skips if the host has no ::1.
- Only exact `::` triggers the option; other addresses, including v4-mapped, bind as before.
