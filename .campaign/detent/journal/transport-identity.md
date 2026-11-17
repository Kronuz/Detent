# transport-identity: journal

State: waiting on jobs 261117-083329-29d3d7 261117-083341-09b068 261117-083354-74485c 261117-083409-90840d and the 12 mutant/control jobs (261117-083434-aa0196 .. 261117-083704-f48fcf)
Quest: .campaign/detent/quests/transport-identity.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Evidence jobs at consensus `1b94858` (the whole change: `include/consensus/transport/host_transport.h`,
`test/transport/admission_test.cc`, `groups_test.cc` cases `identity`, `joiner`, `lagging`,
`relayout`, mutants in `test/transport/mutant_commit.py`, docs
`docs/mechanisms/host-transport.md` Identity). A probe in the worktree passed all nine
(admission, groups identity/joiner/lagging/relayout/driver/refill/threads, 261117-082953-4f19e5).

Jobs: suite 261117-083329-29d3d7; cases A1 261117-083341-09b068; relayout A2
261117-083354-74485c; TSan 261117-083409-90840d. Mutants (`mutant_commit.py 1b94858 NAME`,
`--fails-without` on `^consensus_transport_(admission|groups_identity)$`): admit-unstored
aa0196, admit-unopened 824101, admit-misrouted cc7d19, admit-spoofed 3b8076, open-cluster
6cfcce, open-group 54ea6f, open-epoch 36b07f, open-profile 3d1151, closed-ignored 0b9d69,
open-once e936b2 (ids 261117-0834xx..0836xx). Control voters-only on joiner 261117-083655-e2029f,
on lagging 261117-083704-f48fcf.

## Next

1. Read the jobs' values into Evidence; set "ready for review".
2. Lead's note (transport-groups-ci lands first): merge main into this branch when told, then
   re-run the gates; a red groups_driver or groups_threads under TSan is that quest's, so the
   TSan job here runs only admission and groups identity/joiner/lagging/relayout.

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

- The open exchange is one frame per direction (kind 4), checked on the group's loop in order
  with the group's frames; a refusal is answered with `closed` (kind 5), after which the
  sender opens again before its next frame. No acknowledgment of a match: frames follow the
  open in TCP order, so a sender never waits on the exchange.
- The receiver keeps per stored group the connections (node, lane, generation) whose open
  matched; a sender keeps per lane the groups opened by store serial (16 B each). A store
  changes the serial, so new values open again everywhere.
- `member_of` is a host function in `HostTransportOptions` (required), called on the group's
  loop; the transport owns the stored/opened/destination checks.
- "A joiner installs": the joiner's host stores and creates the group (empty membership) and
  catches up by append; a snapshot install over Bulk is consensus-transport's (rpc-lane).
- The voter-only control is a mutant of the real rule (`voters-only`); under it `lagging`
  fails at its joiner step too, since that rule refuses every joiner.
- Tests use incarnation k on node k; the Driver tests keep journals on disk (`PosixIO`,
  `groups-<case>/` in the build dir) since a shared in-memory disk is not thread-safe and a
  re-partition needs one journal per host.
