# consensus-driver-install review: correctness

Commit: consensus:740453b5c2ba44d956873211205a0584d8cecd03
Verdict: CHANGES
Date: 2026-11-21

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

The code at this commit is correct on every path I traced. The one blocking row covers the code
that will land: the quest lands after consensus-quiescence, and that merge removes the wake this
commit relies on to abandon an orphaned image.

## Evidence I relied on

- A1 lagging 261120-225736-8a7fed (boundary 17, 12 frames, 151,995 bytes; application state equal
  to the leader's past the boundary), groups_test install 261120-225738-b62375 and
  261120-225744-fa0a56; no-transfer-out and others fail without (5 jobs). They check values.
- A2 rejected 261120-225740-b2bdb1: two refusals keep applied index and application state, then
  a retry installs. Mutant failed-validates 261120-191749-bef99d is killed (4 failures).
- A3 crash 261120-225742-c5e61e: 26 disk operations from the heal through activation and the next
  commit, 24 cuts, 19 old and 5 new replicas, application state equal to the leader's after each.
  Mutant activate-on-validated 261120-191751-b901bb is killed ("activated an image at 17 that
  consensus did not persist (restored 4)").
- A4 simulation 261120-225748-45cc05 and 261120-225750-c12778: images are checked against the
  committed sequence at validation and activation, and only a validated image activates. Seed 15
  is the orphaned-image case: the source crashed mid-transfer.
- A6: CI run 44877138480 at 740453b, with TSan and ASan.

## What I checked myself

Read at consensus:740453b:

- Durability (Q1). Core::install_rejection (core.h:519-533) rejects StaleTerm and CaughtUp
  against the current term and commit index at cutover, not at Begin. PersistInstall carries one
  RecoveredState: hard state, base index and term, membership, retained suffix (core.h:534-566).
  The Worker encodes it into the bundle and publishes it in one `store_.publish` (worker.h:1064).
  ActivateInstall requires Phase::Completion, after publish (worker.h:1131). So the boundary,
  term and membership become durable in the same swap, and activation always follows it. A3's
  span covers activation as well as transfer: it runs through activated() and the next commit.
- Races (Q2). Validation tokens come from a Worker-wide counter (worker.h:822), so an answer for an
  earlier transfer cannot match a later one within an incarnation. The Driver also checks
  `token == in.validating` and the channel's current incoming id (driver.h:1019-1028). The
  channel holds one receiving_ at a time. A newer Begin is Busy and stays in that peer's input
  buffer, retried at each run_one (snapshot_channel.h:150-180). With no quiescence, a follower
  keeps its election deadline, so run_one sees time pass and an orphaned non-final image ends at
  snapshot_timeout (snapshot_channel.h:137).
- Reader borrow (Q3). ValidateImage and ActivateImage pass `*incoming->reader` from the Worker's
  live checkpoint_ inside a synchronous Sink call (driver.h:907-909, 1055-1060). The answers
  validated, validation_failed and activated only queue (driver.h:229-236, 987-991), so a reentrant
  answer cannot free the reader during the call. The reader is freed only at finish_snapshot,
  after the answer is processed on a later step. ASan in CI covers the tests' reads.
- Validation failure (Q4). reject_snapshot_validation cancels the replacement
  (InvalidApplication). The channel then sends Result Failed, and the leader retries after its
  timeout. `rejected` proves that the retry installs, so no state is left stuck.
- Probe job 261121-003415-2f2c90, from
  `.scratch/detent/review-consensus-driver-install-correctness/run.sh` (patch_test.py adds the
  cases). It prints each crash cut's state (activations, staged, active, restored), to show cuts
  between the durable publish and activated(). It also adds `orphan` (the leader is gone after
  Begin, Accepted and a Chunk; node 3 must install from the new leader) and `orphan-final` (the
  leader is gone while the application holds a whole image's validation; a late answer must end
  installed). The job was still queued behind 35 jobs when I wrote this review, so no result
  here rests on it. Rerun: `campaign job submit --quest consensus-driver-install --slot
  review-cdi-correctness -- <that run.sh>`.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| R1 | yes | gap | An orphaned incoming image ends only when the group steps: Driver::arm wakes on `w.next_deadline()` and queued frames only (driver.h:737-743). The channel's receive timeout runs inside run_one (snapshot_channel.h:137). On quest/detent/consensus-quiescence a resting follower may have no Core deadline (its arm is unchanged, driver.h:757-765 there), so an image whose leader crashed can hold repair, checkpoint and every newer Begin indefinitely. The quest lands after that quest, so the landed code has this risk. The journal's Next 3 names it, but nothing tests it. | driver.h:737-743; snapshot_channel.h:137; consensus-quiescence driver.h:757 | At the merge, while install state holds a non-final incoming image, either arm the group at the channel's receive deadline or keep the group from resting. Test: a quiescent follower whose leader is gone after Begin abandons the image within snapshot_timeout, then installs from the new leader or repairs. |
| R2 | no | gap | A3 does not report whether any cut lands after the durable publish and before activated(), the window where recovery must finish a staged activation (`staged == base`). The kill of activate-on-validated proves the opposite ordering is caught. The 5 new replicas do not show which window they came from. | driver_install_test.cc crash; probe 261121-003415-2f2c90 (queued) | Print or count the cuts that recover the new replica with activations == 0, as the probe does; if none exists, add a cut that holds activated() across the restart. |
| R3 | no | defect | DRIVER.md Install says the follower ends its side after its own snapshot_timeout without a frame. The channel skips that timeout once the final chunk arrived (`!receiving_->final`, snapshot_channel.h:137), so a whole image waits for the application's answer however long it takes, with repair and checkpoint behind it. | snapshot_channel.h:137; DRIVER.md "Frames" | Say that after the final chunk, the image waits for validated or validation_failed. |
| R4 | no | nit | Validation and activation tokens restart with each Worker. An answer the application posts for a closed group's notice, delivered after the same id reopens on this Driver, can match the new incarnation's token, since the same image gives the same EOF token. Activation already had this shape. | worker.h:822; driver.h:191 | Either the contract states that answers for a closed group are not delivered after reopen, or tokens are salted per incarnation. |
| R5 | no | nit | All of a group's received frames share one deque, and a Busy from one peer's channel stops the loop (driver.h:1040). A second sender's repeated Begin therefore holds the current sender's Chunks until the receive timeout. Only two concurrent leaders cause it, and it resolves at the timeout. | driver.h:1030-1041 | Skip frames of a Busy peer, or queue per peer. |

## Not run

- The probe job 261121-003415-2f2c90 (crash-window counts, orphan, orphan-final): queued, no
  result.
- The merged branch with consensus-quiescence (R1): not built.
- Transfer frames that are malformed or truncated, beyond `stale`'s short frame: the security
  lens's concern.
- Leader-side source timeouts and SnapshotReleased reasons: pre-existing Core behavior, not
  reprobed.
