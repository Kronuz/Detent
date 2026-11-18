# transport-identity: journal

State: waiting on jobs 261117-121758-d594d8 261117-121804-c9cf0c 261117-121816-b25730 261117-121846-795592 and the 14 mutant/control jobs 261117-121906-918deb .. 261117-122127-8bcba2
Quest: .campaign/detent/quests/transport-identity.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Security review (Detent:1f91509c, `.campaign/detent/reviews/transport-identity-security.md`):
CHANGES on S1. Folded in at consensus `9fe84c9` (merge of main `0ed76ef` at `23e87e5`, clean):

- S1 (fixed, `fe42cc2`): `Connection::reader` runs `on_read` for the frames a read handed over
  before it rethrows a failure (oversized length, a throwing handler), so a batch always leaves
  under the connection its frames came on. Chose the flush over dropping: the frames are
  authenticated and checked per frame like any other; only their attribution was wrong. New
  groups case `leftover` (the reviewer's probe, asserting): node 1 never opens g, sends g's
  frame and then an oversized one; host 2 must refuse it `unopened`. Mutant
  `read-fails-unflushed` (on_read after the rethrow) on `groups_leftover`.
- N2 (fixed): `store()` refuses a zero cluster, group or epoch; `identity` checks all three throw;
  mutant `store-zero`.
- N3 (fixed): host-transport.md Identity says to store a group once `member_of` answers; the test
  `Node` now stores after create/recover completes.
- N4 (fixed, by wording): `forget()`'s comment and the doc say frames already queued still go.
- N1 (follow-up): `lane.closes` (like `grants`) grows per distinct refused group id while a peer
  does not read; bounded by the peer's own bytes. A cap or a refusal bound that ends the
  connection is left for a follow-up quest.

Jobs at `9fe84c9`: suite 261117-121758-d594d8; cases A1 (admission, identity, joiner, lagging,
leftover) 261117-121804-c9cf0c; relayout A2 261117-121816-b25730; TSan admission + every groups
case, `--output-on-failure` 261117-121846-795592. Mutants re-made at `9fe84c9`
(`mutant_commit.py 9fe84c9 NAME`, `--fails-without`, admission+groups_identity): admit-unstored
261117-121906-918deb, admit-unopened 261117-121920-e422e4, admit-misrouted 261117-121929-694e4a,
admit-spoofed 261117-121939-c2d42f, open-cluster 261117-121953-0f5f8b, open-group
261117-122010-ffc421, open-epoch 261117-122026-10df2c, open-profile 261117-122035-0f8c5e,
closed-ignored 261117-122045-dc679e, open-once 261117-122053-2dc66a, store-zero
261117-122101-b62389; read-fails-unflushed on groups_leftover 261117-122111-4dfd63; control
voters-only on joiner 261117-122119-6a7f7a, on lagging 261117-122127-8bcba2.

Earlier: TSan 261117-083409-90840d's `lagging` abort was the test's race with core D3 (c), fixed
in `c84e64e` (NotReady-only retry); its jobs passed (suite 261117-093903-345ea0, TSan
261117-093923-dd6ee7, 0 NotReady retries), per the review.

## Next

1. When the jobs pass, paste `campaign brief`'s table into Evidence; set "ready for review"
   for the same security reviewer's re-check.

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
- The lagging abort under TSan was the test's race with D3 (c), so the fix is in the test: retry
  only NotReady, the documented transient refusal, not a longer sleep.
- S1's fix is in `Connection` (on_read also after a failed read), the one place every frame
  passes; every mutant and control is re-made at `9fe84c9` since the merge and S1 touch
  host_transport.h and connection.h.
