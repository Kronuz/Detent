# transport-idle-budget: journal

State: waiting on jobs 261118-075717-797949 261118-075718-7593b8 261118-075719-d29c58
Quest: .campaign/detent/quests/transport-idle-budget.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Head consensus:33f8540 (pushed; CI run 44341580130). Jobs: 797949 (fits at head, Release vs
none, x3), 7593b8 (A2 mutant, both builds, --fails-without), d29c58 (A3 suite).

## Next

1. Read the three jobs; write the slopes before/after in docs/perf/transport-idle-cost.md; fill
   Evidence; check CI 44341580130; push; State "ready for review".

## Blocked on

Nothing.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| A1 [manual] | Cause: the loops=4 slope sits 2-8 B over the 48 KB cap (49,152 B), and the build type tips it. Job 261118-073615-da90ae at main consensus:5923b6c: Release passes 3/3 (the f0eff6 suite built Release), no build type fails 3/3 with slope 49,154 B (1fb596 and e7e9cf built without one). Both link OpenSSL 4.1.0. The slope carries each loop's OpenSSL thread-local DRBGs: connections sit on loop_of_node(peer), so at L=4 the loops running TLS grow with P. Job 261118-074432-acdca6 at consensus:d2eeb0d (DRBGs drawn per loop first): passes 3/3 in both builds, L=4 slope 46,735 B. |

## Result

<When ready for review: what changed, its measured effect with numbers and the jobs behind them,
and what it leaves open. Results and how it works go in the docs of the repository it changed
(docs/perf/, docs/mechanisms/, a library's own); name them here. `campaign map` shows this.>

## Rulings I made

- Docs go under docs/perf/transport-idle* only (lead, mid-task).
- Per-thread OpenSSL state is a per-loop cost, set apart and printed per loop like the timer
  wheels (consensus:33f8540), not counted per host: counting it per host (d2eeb0d) charges all 4
  loops' DRBGs, about 30 KB, and puts L=4 hosts 11-26% over 52 KB + 2P x 44 KB, failing the full
  (non --quick) mode's 10% check that passed at 108-109% on main.
