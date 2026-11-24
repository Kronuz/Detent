# consensus-follower-read: journal

State: waiting on jobs 261124-043342-61c0c9, 261124-043347-acca2c
Quest: .campaign/detent/quests/consensus-follower-read.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Round 2 (review N4, N5) at consensus:0b8c911, pushed. The decoder is unchanged since 49deff5, so A4's
fuzz 261124-033426-3bcef3 stands. CI at 0b8c911 is running (A5).

## Next

1. When the suite, the mutant and CI finish, cite them and CI at 0b8c911 in Evidence, set State "ready for review".
2. Follow-up (lead): N2, below.

## Blocked on

Nothing. msg-303 (file set) is resolved by the lead: the set now holds
include/consensus/transport/codec.h, include/consensus/admission.h, fuzz/seed.cc and
test/follower_read_mutants.py, and the nonexistent include/consensus/codec.h is gone.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | at 0b8c911: suite 261124-043342-61c0c9, plain 261124-043343-fb8ffd; fails without: 261124-043347-acca2c (nonce-reused, review N4). At 49deff5 (core unchanged since): 261124-022329-b17412, 261124-022331-49a7de; fails without: 261124-022336-337103, 261124-022339-c81897, 261124-022342-175f5a, 261124-022346-dcf92d, 261124-022350-7f7c21, 261124-022354-8dbf8a, 261124-022418-57781f, 261124-022426-5d0874 |
| A2 | 261124-043343-fb8ffd (0b8c911), 261124-022331-49a7de; fails without: 261124-022358-7ae2c2 |
| A3 | 261124-043343-fb8ffd (0b8c911), 261124-022331-49a7de; fails without: 261124-022402-acefff, 261124-022406-48b968, 261124-022410-d8cba0, 261124-022414-e779c8, 261124-022422-fbfaaf |
| A4 | 261124-033426-3bcef3 (fuzz at 49deff5; the decoder is unchanged at 0b8c911); codec mutant 261124-022430-586762 (fails without, at 49deff5's mutant); seeds `fuzz/corpus/decoders/seed-read-index-request`, `seed-read-index-response`, `seed-read-index-refusal` |
| A5 | [artifact] `docs/mechanisms/follower-read.md`: "A follower answers a `Read` only at an index its leader confirmed current with a quorum"; suite 261124-043342-61c0c9; TSan and CI: CI run 45394693963 at 0b8c911 (running). CI at 49deff5 (run 45374547236): linux-tsan passed; linux-asan-ubsan timed out on consensus_follower_read (fixed at 0b8c911); macos-x86_64 failed consensus_transport_rpc_host_fetch, which also fails on main at 512e3ef (run 45366654906): pre-existing |

Other jobs examined: 261124-020815-08319a (suite at d2126d0, superseded), 261124-020817-d6cced
(cancelled, superseded by 49a7de), 261124-020820-b56679 (fuzz at d2126d0; rerun at the head);
261124-040935-e4aada, 261124-040937-e8b581, 261124-040949-975e1f (at c88f5b7, cancelled, superseded by 0b8c911).

## Result

A follower's `Read` is forwarded to its leader (ReadIndex): `ReadIndexRequest{term, start,
token}` (DTC2 kind 12), answered by `ReadIndexResponse` (kind 13: Ready with the leader's commit
index at arrival, confirmed by a probe sent after it, or NotReady, Busy, NotLeader). The follower
emits `ReadReady` once it applied the index. A change of its term or leader, its own campaign, or
`election_max` from the send without answer and apply refuses `NotLeader`. `start` is
`Start::nonce` (the Driver draws it from `std::random_device`); with none a follower refuses, as
before. A follower holding a read does not rest and arms its deadline.

Numbers: 16 mutants, one per rule, each killed by its case (jobs above). `randomized` runs the
correctness review's fault schedule (by seed: no faults, ReadIndex frames, or every frame dropped,
delayed, duplicated and re-injected; restarts with a fresh nonce); ctest runs 30 seeds (12 s of
CPU under ASan, inside ctest's 30 s), the jobs 2000. At 0b8c911, 2000 seeds (fb8ffd): 179,110 reads
completed, 100,015 at followers, none stale; 424,318 refused; 156,587 frames re-injected. At 49deff5, before the schedule, 2000 seeds (49a7de): 231,988 reads
completed, 134,742 of them at followers, none below a commit made before it began; 386,932
refused. Suite 163/163 at 49deff5. Cost: 36- and 52-byte frames per read plus the leader's
shared probe round; four words per idle Core (computed, not measured).

Docs: docs/mechanisms/follower-read.md (rule, identity, deadline, rest, wire, tests), CHANGELOG.
ARCHITECTURE.md describes follower reads (N5). Open: no Driver-level test of the nonce (the Driver
simulation now issues follower reads, so its read oracle covers them); N2 (follow-up below).

## Review notes

Correctness review APPROVE at consensus:49deff5 (.campaign/detent/reviews/consensus-follower-read-correctness.md,
Detent:37e94ab7; probe 261124-034400-55de11).

- N1: the lead's decision D153, the nonce stays as is (std::random_device per open).
- N2, follow-up (not fixed here): forwarded and local reads share the leader's `limits.reads`,
  and `receive_message(ReadIndexRequest)` does not check that the source is a member, so any
  authenticated incarnation can keep local reads Busy while it floods (one probe round and one
  52-byte refusal per request). Options: a per-peer cap, or refuse non-members.
- N4: folded in: `randomized` now runs the reviewer's fault schedule (none / ReadIndex frames /
  every frame delayed, duplicated and re-injected, by seed), and `nonce-reused` in
  test/follower_read_mutants.py keeps the forced-nonce-reuse variant as a fails-without mutant.
- N5: folded in: ARCHITECTURE.md's read paragraph describes follower reads (consensus:c88f5b7).
- CI at 49deff5 timed out `consensus_follower_read` under ASan (200 seeds, 73 s locally against
  ctest's 30 s); 0b8c911 runs 30 seeds by default (the nonce-reused mutant still fails 13 of 30).

## Rulings I made

1. **CMakeLists.txt** (xapiand:consensus-join-v2 also changes it): one word, `follower_read`, in
   the unit-test `foreach` at line 52, a hunk join-v2 does not touch; a merge of either is clean.
2. **The nonce comes from the host.** The Core takes no entropy, so `Start` gains `Token nonce`
   (default 0). The Driver draws 64 bits from `std::random_device` at each open, not from its
   seeded `random_` stream: a restart under a fixed `Seams::seed` still names a new start, and the
   seeded stream (election delays) is unchanged for every existing simulation seed. With nonce 0
   a follower's `Read` is refused `NotLeader`, today's behavior, so direct-Core hosts that never
   pass one change nothing (AGENTS.md: additive by default).
3. **No resend.** A follower sends its `ReadIndexRequest` once; a lost frame costs the read its
   `election_max` deadline. The leader still drops a duplicate `{peer, start, token}` while it is
   pending, so a resend added later answers once.
4. **The leader answers `Ready` on confirmation**, not after its own apply: the follower waits for
   its apply. Learners may forward; witnesses are refused (no application state).
5. **Wire version stays 1**, as when kinds 9 to 11 were added (docs/mechanisms/install-request.md):
   formats are open until the first release. No compat fixture: unknown kinds already decode as
   `Corruption("unknown control kind")` in old readers, and tools/compat* is outside the set.
6. **The codec round trip lives in `follower_read_test codec`**, not test/transport/codec_test.cc
   (outside the set).
7. **Leader change in the same term** (LeaderGone, Gone, CheckQuorum at a follower) refuses at
   once, as a term change does; the design's table names leader changes and term moves alike.
8. **ctest runs 30 randomized seeds**, not 200: the 30 s ctest timeout in CMakeLists.txt is shared
   by the unit tests, so the default moved in the test (my file), not a per-test TIMEOUT there.
