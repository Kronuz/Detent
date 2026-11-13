# namespace-s5-2: journal

State: waiting on jobs 261112-220407-bcf0f5 261112-220517-95cd37 261112-221626-7487a8 261112-221630-939181 261112-221634-e3740a 261112-221638-00d2bb 261112-221642-2dcc98
Quest: .campaign/detent/quests/namespace-s5-2.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Steps 1-7 done at eb37a84a (merged with main at a81ccd79); jobs queued at the branch head:

- Gate: native 261112-220407-bcf0f5, pins 261112-220407-64f2e5 (`campaign verify`).
- A1, A2, A4: `test_authority_history_cluster` 261112-220409-a2488f.
- A2: `test_authority_change_history` (lazy eviction) 261112-220413-0e34b3.
- A3, A6: `test_authority_history_layout` 261112-220417-d8a94e.
- A5: `test_authority_history_power_cut`, `test_authority_namespace_power_cut` 261112-220422-eb1426.
- Mutants (`--fails-without`, `tests/detent_authority/history_mutant.sh`): A5 table-before-outputs
  261112-220445-848938, collect-above-table 261112-220450-f5e9a0, trust-local-files
  261112-220456-df8763, checkpoint-between-seals 261112-220505-41157f; A2 eager-eviction
  261112-220512-56ad47, ring-by-group-revision 261112-220517-95cd37; A3/A6 cut-at-twice-the-cost
  261112-220521-ca11db.

Results so far: pins passed; the A1/A2/A4 cluster, A2 ring, A3/A6 layout and A5 crash jobs
passed; mutant collect-above-table killed (261112-220450-f5e9a0). Five mutants lost their
runner (stale heartbeat on a loaded host, not a test result) and were resubmitted with
`--retries 2` at e5103e4b: table-before-outputs 261112-221626-7487a8, trust-local-files
261112-221630-939181, checkpoint-between-seals 261112-221634-e3740a, eager-eviction
261112-221638-00d2bb, cut-at-twice-the-cost 261112-221642-2dcc98. Still running: native
261112-220407-bcf0f5, ring-by-group-revision 261112-220517-95cd37.

Local runs at e121df71 (quiet light): authority suite 48/49, the one failure
`test_authority_snapshot_catchup` under a loaded parallel run, passing alone (51 s); the gate
job decides whether it is pre-existing. history_power_cut: 39 seals, 7 tables, 946 storage
operations, 1,892 cuts (370 recovered below the table), 4,243 cuts in installs.
namespace_power_cut: 495 cuts over 495 operations with history on the same disk. Cluster: 23
seals, 66 history files identical on three voters, ring answers kept 17 of 17 after every
voter restarts.

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

1. Read the jobs; fix what fails; paste `campaign brief`'s evidence table; State "ready for
   review".

## Blocked on

Nothing.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| A7 | [decision] D90 in PLAN.md: α = 1/8 (`alpha_num` 1, `alpha_den` 8) and roll-up period 64 from namespace-measure, the `LayoutPolicy` defaults in `src/detent_authority/state.hh` |

## Result

Every voter persists the same compacted history per namespace (D6): apply seals on changes or
log progress, seal files carry level-0 sections and the outputs their roll-ups write, blocks
squash by ratio and L, the oldest are cut into the base by cost or the cap, and the `layout`
table is written at period seals; the commit, collector, recovery, install and peer repair run
over `journal::IO`. Checkpoints are taken at a seal whose history is durable. The ring is keyed
by namespace position, evicts lazily and is refilled by replay after a restart. How it works,
the contract and what is left open: `docs/status/namespace-s5.md` (S5.2 section). Left open
there: nothing proposes the enabling entry yet (S6 cutover), production peer fetch needs an
endpoint, and history IO runs synchronously in apply's seal.

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
11. Checkpoints are taken only at seals, so `test_authority_namespace_power_cut` seals every
   change (`seal_transactions` 1) to keep its checkpoint steps, and `test_distribution_service`
   expects a delta after a restart (replay refills the ring; it expected a snapshot before).
12. The ring's draft checks a record's revision against its namespace position (records carry
   positions after the enabling entry); the group-revision check failed every non-delete capture.
