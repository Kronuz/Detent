# consensus-follower-read: journal

State: waiting on jobs 261124-020815-08319a 261124-020820-b56679 261124-022329-b17412 261124-022331-49a7de and the 15 mutant jobs 261124-022336-337103 .. 261124-022430-586762
Quest: .campaign/detent/quests/consensus-follower-read.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Round 1 at consensus:49deff5, pushed. Jobs:

- Suite: 261124-022329-b17412 (`campaign verify --for A1`, head); 261124-020815-08319a at
  consensus:d2126d0 (an early signal; only the test harness changed since).
- Plain test, every case, 2000 randomized seeds: 261124-022331-49a7de (A1, A2, A3).
- Decoder fuzz, control frames, ASan+UBSan, 2 x 15 min: 261124-020820-b56679 (A4), at d2126d0.
- Mutants (`--fails-without`, `test/follower_read_mutants.py 49deff5 NAME`, the case it names):
  A1 apply-skipped 337103, ready-without-quorum c81897, leader-change-kept 175f5a,
  campaign-keeps-reads dcf92d, any-peer 7f7c21, any-term 8dbf8a, refusal-unsent 57781f,
  nonce-unchecked 5d0874; A2 start-ignored 7ae2c2; A3 no-deadline acefff, deadline-unarmed
  48b968, probe-shared d8cba0, answer-completes-all e779c8, rests-with-read fbfaaf;
  A4 codec-unchecked 586762 (ids prefixed 261124-0223 / -0224).
- Local pass before submitting: all 15 mutants killed (no-deadline hung the harness; fixed in
  49deff5 so it fails at once).

## Next

1. Read the jobs; paste `campaign brief`'s Evidence table; write the Result; State "ready for
   review".

## Blocked on

Lead (msg-303): the file set names `include/consensus/codec.h`, which does not exist. The quest
also changes, outside the listed set:

1. `include/consensus/transport/codec.h`: the two new kinds (12, 13). Held by
   consensus-rpc-host-stream-gap through `include/consensus/transport/**`.
2. `include/consensus/admission.h`: its plan static_asserts on every Message type; one line.
3. `fuzz/seed.cc`: writes the new seeds.
4. `test/follower_read_mutants.py`: the mutant writer (new file).

Recommendation: `campaign set consensus-follower-read files` += those four, and take
`transport/codec.h` out of rpc-host-stream-gap's set (it does not change it). The work is built
against them; landing waits on that.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | <pending> |

## Result

<pending>

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
