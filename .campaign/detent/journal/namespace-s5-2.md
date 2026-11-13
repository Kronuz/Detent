# namespace-s5-2: journal

State: working
Quest: .campaign/detent/quests/namespace-s5-2.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Steps 4-7 written, building: history files over `journal::IO` (`history.hh/.cc`), the node's
checkpoint at the seal, the ring keyed by namespace position with lazy eviction, the cluster
identity test (A1, A2, A4) and the history crash matrix (A5).

## Proposed split (for the lead)

The quest splits cleanly at the disk:

1. **s5-2a, the layout in memory** (steps 1-3): policy and set-limits entry, seals with apply's
   accumulator and ordinal, the seal-file / block / base / table formats, roll-up, squash, cut,
   carry, first bases at the enabling entry, and the three answers. Criteria A3, A6, A7 and A1's
   voter identity with a capture failure and a policy change. Nothing consumes it until S6.
2. **s5-2b, the layout on disk** (steps 4-7): files under the commit protocol, collector,
   recovery, checkpoint at the seal persisted after a durable layout, the ring keyed by namespace
   position with lazy eviction and no reset, install and repair from a peer, the crash matrix.
   Criteria A2, A4, A5 and A1's restart and install cases.

I keep going on both in this branch unless the lead splits it; each step is its own commit.

## Next

1. Build; run the authority suite; fix what breaks.
2. Docs: S5.2 section in `docs/status/namespace-s5.md`.
3. Jobs: `campaign verify` per criterion, mutants for the commit order, the checkpoint gate,
   lazy eviction and the cost cut.

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

1. Log-progress thresholds (log_entries, log_bytes) travel in the replicated policy, filled
   from the proposer's consensus limits on the enabling and SetLayout entries, instead of a new
   consensus configuration entry; a node whose retained bounds are below them refuses to start
   or keep applying (`check_retained_bounds`).
2. The layout policy is not in the semantic profile: the policy in force is replicated state
   (image `DTAIMG03` carries it, the pending one and the seal counters).
3. The enabling entry is seal 0; its first bases are its outputs and its table the first
   table. An empty base is not a file. A drop is a seal entry flagged dropped.
4. History lives in a sibling directory `<directory>.history` (the journal's inventory treats
   subdirectories as incomplete).
5. A seal commits its seal file, then its outputs as one sync group, then the table at a
   period seal; all three before `seal()` returns (stronger than RU-3). A name that exists
   with the same bytes is reused; with other bytes it is an orphan and is replaced.
6. History IO is synchronous inside apply's seal (open: move it to IO operations). A history
   failure stalls history and checkpoints, never apply.
7. The checkpoint is captured from the latest seal's state, and only when history is durable
   through it; any index stays a valid image (the accumulator is in it).
8. A table past the checkpoint's seal is adopted; seals at or below it fold nothing.
9. The ring captures every transaction with its namespace position and incarnation; the
   delta wire still serves only entries whose position is the group revision. It evicts at
   its bound only entries at or below the newest durable seal's revision (`covered`), else
   refuses the new entry (a ring gap, never a block gap). A drop raises the wire floor and no
   longer resets the ring. The service attaches the ring before recovery, so replay refills it.
10. Answer "unchanged after a restart" (A2) is checked as: the same blocks answer, byte for
   byte, and every R from the cut delivers exactly the head store; ring answers below the
   checkpoint's seal become block answers (the ring restarts at the checkpoint).
