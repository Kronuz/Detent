# consensus-follower-read review: correctness

Commit: consensus:49deff5cd6b81c2927c299420d2c764b75b5cba3
Verdict: APPROVE
Date: 2026-11-24

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

The approval covers the code. A4 (fuzz 261124-033426-3bcef3, still running) and CI at 49deff5
remain landing gates; this review does not stand in for them.

## Evidence I relied on

- 261124-022329-b17412: the suite at 49deff5, 163 of 163.
- 261124-022331-49a7de: `follower_read_test all 2000`, every deterministic case plus 2000
  randomized seeds (231,988 reads, 134,742 at followers, none below the floor).
- Mutant jobs 261124-022336-337103 through 261124-022430-586762 (15, every one `--fails-without`
  and passed). I read each mutant's edit in `test/follower_read_mutants.py` against the case that
  kills it. Each removes the rule its case names: `any-term` keeps the `hard_.term` check and
  drops only `response.term`, and `ready-without-quorum` drops the quorum for forwarded reads
  only. None of them is vacuous.

## What I checked myself

Probe job 261124-034400-55de11 (light, 120 s, exit 0), from
`.scratch/detent/review-consensus-follower-read-correctness/run.sh` with `adversarial.inc`. It
builds the suite's harness with asserts on and runs randomized 3- and 5-voter clusters with the
suite's partitions, drops and crashes. On top of those it adds faults the suite does not inject.
The oracle is unchanged: every completed read returns an index at or above the highest commit
index any replica held when the read began.

| Mode | Faults | Seeds | Reads (at followers) | Frames reinjected | Stale |
|---|---|---|---|---|---|
| 0 | none (baseline) | 1000 | 106,644 (63,123) | 0 | 0 |
| 1 | ReadIndex frames delayed, duplicated and reinjected out of order, across restarts and term changes | 3000 | 314,317 (183,014) | 219,493 | 0 |
| 5 | mode 1's faults on every frame kind | 1000 | 56,845 (26,186) | 160,745 | 0 |
| 3 | mode 1, plus each restart reuses the previous nonce | 2000 | 210,527 (123,194) | 147,201 | 1,138 forwarded, 0 local |

I also read the code against the lead's questions:

1. **Linearizability.** The leader records `durable_.commit_index` when the request arrives
   (core.h:1245). It refuses with NotReady until an entry of its own term has committed
   (core.h:1077). It answers Ready only after a quorum acknowledges a probe sent after admission
   (`ready_reads`, core.h:1655-1657), using the same `needed_probe` correlation that local reads
   use. A deposed or minority leader therefore never confirms (`stale`, and modes 1 and 5).
   Duplicated or reordered responses match at most one pending token, and once matched the read
   no longer accepts another (`read->index`, core.h:1257). Every path that changes the term or
   the leader runs through `follower()` (core.h:1137, :780, :803, and `leader_lost` :610) or
   through `campaign()` (core.h:1527). Both refuse every forwarded read.
2. **Matching.** A response counts only if it carries the follower's current `start`, a pending
   token, the right source, the read's term, and the follower's term and leader still unchanged
   (core.h:1255-1257). Tokens come from a per-start counter, unique within a start. The nonce is
   load-bearing: mode 3 shows that a repeated nonce lets a pre-restart answer complete a new read
   stale, which is exactly the V1 window. With 64 bits from `std::random_device` the chance is
   about 2^-64 per pair of restarts that meet in one term under one leader with an answer still
   in flight. That is acceptable (N1).
3. **Wire and admission.** The kind 12 and 13 decoders read fixed `get64` fields. They reject an
   unknown result, Ready without an index and an index without Ready, and the generic decoder
   rejects trailing bytes (`codec` case). The Core drops a zero term, start or token
   (core.h:1164-1168). The frames go through the same `Receive` path and the same
   authenticated-peer rule as kinds 9 to 11, and reserve the same `hard` admission as the other
   control frames, because a higher term persists. Leader state is bounded by `limits.reads` and
   the dedup scan by the same limit. xapiand:consensus-join-v2 changes transport-level node
   admission and shares only CMakeLists.txt with this quest. `git merge-tree` of 49deff5 with
   join-v2's b92b6c8 is clean (tree 79845e48), and join-v2 adds no exhaustive visit over
   `Message` (N3).
4. **Liveness.** One `election_max` deadline runs from the send to the apply. It is listed in
   `next_deadline()` and expired in `drive()` (core.h:1102-1112, :1455). A step-down answers
   NotLeader at once. If the step-down came from a higher term, the answer carries that term and
   the follower's own term bump refuses the read. Fencing and retirement drop forwarded reads
   without a reply. The Driver's close resolves every waiter with `closed` (driver.h:1054-1062),
   and the followers of a fenced leader refuse at their deadline. Messages are not lost while the
   Core is busy, because the Worker refuses submission until it is free (worker.h:746).
5. **Tests.** They check values: the returned index equals the leader's commit, the old answer's
   index is below the write in `restart`, and refusal reasons and their times are checked. The
   randomized oracle checks the index, and the harness asserts `applied >= index` on every
   ReadReady. The Driver simulation sends 1 read in 8 to a non-leader (driver_simulation.cc:584),
   so its read oracle covers forwarded reads.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| N1 | no | nit | Uniqueness across restarts rests on `std::random_device` alone. Forced nonce reuse yields stale follower reads, so a deterministic `random_device` (as on older MinGW libstdc++) or a constructor that throws would break V1 or fail the open. On macOS and Linux it is the OS's CSPRNG, and 64 bits leave about a 2^-64 window. | probe 261124-034400-55de11 mode 3 (1,138 stale); driver.h:851-855 | Owner's call: keep it as is, or mix a durable per-replica start counter into the nonce so uniqueness does not depend on the platform. |
| N2 | no | nit | Forwarded and local reads share the leader's `limits.reads`. `receive_message(ReadIndexRequest)` does not check that the source is a member, so any authenticated incarnation can keep local reads Busy for as long as it floods. The cost is bounded: one probe round and one 52-byte refusal per request. | core.h:1244-1250, :1078 | Optional: a per-peer cap, or refuse non-members. Not a safety issue. |
| N3 | no | nit | The campaign warning says this quest changes contract `consensus:node-admission` because it edits include/consensus/admission.h. That file is event admission (`plan_event`), not node admission, and join-v2 merges cleanly without a conflict, so the "land xapiand:consensus-join-v2 first" order is not needed. | merge-tree 79845e48; admission.h:102 | The lead may drop the `after` order or fix the contract tag. |
| N4 | no | gap | The suite's `randomized` case injects no duplication or reordering of ReadIndex frames. This probe shows those faults are harmless (mode 1: 0 stale in 183,014 follower reads), but the suite does not keep that result. | follower_read_test.cc:561-622 | Optional: fold mode 1's stash-and-reinject into `randomized`. |
| N5 | no | nit | ARCHITECTURE.md still describes leader-only reads (the journal's open item; the file is outside this quest's set). | journal "Result" | A follow-up docs edit. |

## Not run

- TSan and sanitizers: the Core changes are single-threaded, and the only Driver change is a
  function-local `std::random_device`. Sanitizers are CI's job.
- The A4 fuzz: job 261124-033426-3bcef3 is still running, and I did not fuzz separately.
- A mixed-version cluster, where an old leader meets kind 12: I did not check whether the
  transport drops the frame or closes the link on `Corruption`. The doc assumes one build per
  cluster.
- Linux and the CI matrix.
