# consensus-follower-read: journal

State: ready for review
Quest: .campaign/detent/quests/consensus-follower-read.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Round 1 complete at consensus:49deff5, pushed. A4's fuzz reruns at the head:
261124-033426-3bcef3 (rerun of 261124-020820-b56679, which ran at d2126d0). CI at the head is
running (A5).

## Next

1. The correctness review (lead); fold findings in by id, each fix with a failing-without test.

## Blocked on

Nothing. msg-303 (file set) is resolved by the lead: the set now holds
include/consensus/transport/codec.h, include/consensus/admission.h, fuzz/seed.cc and
test/follower_read_mutants.py, and the nonexistent include/consensus/codec.h is gone.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | 261124-022329-b17412, 261124-022331-49a7de; fails without: 261124-022336-337103, 261124-022339-c81897, 261124-022342-175f5a, 261124-022346-dcf92d, 261124-022350-7f7c21, 261124-022354-8dbf8a, 261124-022418-57781f, 261124-022426-5d0874 |
| A2 | 261124-022331-49a7de; fails without: 261124-022358-7ae2c2 |
| A3 | 261124-022331-49a7de; fails without: 261124-022402-acefff, 261124-022406-48b968, 261124-022410-d8cba0, 261124-022414-e779c8, 261124-022422-fbfaaf |
| A4 | 261124-033426-3bcef3 (fuzz at 49deff5, running); codec mutant 261124-022430-586762 (fails without, at 49deff5's mutant); seeds `fuzz/corpus/decoders/seed-read-index-request`, `seed-read-index-response`, `seed-read-index-refusal` |
| A5 | [artifact] `docs/mechanisms/follower-read.md`: "A follower answers a `Read` only at an index its leader confirmed current with a quorum"; suite 261124-022329-b17412 (163 of 163 tests); TSan and CI: CI at 49deff5 (running) |

Other jobs examined: 261124-020815-08319a (suite at d2126d0, superseded), 261124-020817-d6cced
(cancelled, superseded by 49a7de), 261124-020820-b56679 (fuzz at d2126d0; rerun at the head).

## Result

A follower's `Read` is forwarded to its leader (ReadIndex): `ReadIndexRequest{term, start,
token}` (DTC2 kind 12), answered by `ReadIndexResponse` (kind 13: Ready with the leader's commit
index at arrival, confirmed by a probe sent after it, or NotReady, Busy, NotLeader). The follower
emits `ReadReady` once it applied the index. A change of its term or leader, its own campaign, or
`election_max` from the send without answer and apply refuses `NotLeader`. `start` is
`Start::nonce` (the Driver draws it from `std::random_device`); with none a follower refuses, as
before. A follower holding a read does not rest and arms its deadline.

Numbers: 15 mutants, one per rule, each killed by its case (jobs above). Randomized 3- and
5-voter clusters with partitions, drops and restarts, 2000 seeds (49a7de): 231,988 reads
completed, 134,742 of them at followers, none below a commit made before it began; 386,932
refused. Suite 163/163 at 49deff5. Cost: 36- and 52-byte frames per read plus the leader's
shared probe round; four words per idle Core (computed, not measured).

Docs: docs/mechanisms/follower-read.md (rule, identity, deadline, rest, wire, tests), CHANGELOG.
Open: no Driver-level test of the nonce (the Driver simulation now issues follower reads, so its
read oracle covers them); ARCHITECTURE.md still describes leader reads only (outside the set).

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
