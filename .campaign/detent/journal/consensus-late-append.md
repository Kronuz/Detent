# consensus-late-append: journal

State: ready for review
Quest: .campaign/detent/quests/consensus-late-append.md (the lead's; never edit it)

## Doing

Nothing running but job 261115-214258-d68788 (A2's passing test, consensus_test at
consensus:615e760).

## Next

1. Lead: push the branch for `ci:linux-tsan` (A3's tsan half) and CI; launch the correctness
   review; rule on the two items below.

## Blocked on

The lead, on two small rulings (the work is done either way):

1. A3 says the existing suite passes unchanged, but `stale_rpc_and_failure_transitions` asserted
   "old successes and failures cannot advance or rewind a replaced RPC": a term-1 success for
   the RPC the retry replaced must not commit. That is exactly the behavior the quest's
   Outcome reverses, and no principled rule tells the two apart (it is a genuine late success
   from voter two in the leader's term). consensus:615e760 amends only that half: the late
   success now commits index 2, the old failure still cannot rewind, the higher-term half is
   unchanged. Options: (a) accept the amendment (recommended); (b) keep the assertion, which
   means dropping the quest's Outcome.
2. `ARCHITECTURE.md` is outside the file set and now says "obsolete replies cannot
   manufacture or rewind progress" (line 23) and "A success retires every older flight"
   (line 41). Recommended text: line 23 "a late success in the leader's term still counts,
   since it proves the follower's log through its index; late failures and older-term replies
   cannot manufacture or rewind progress"; line 41 "A success in the leader's term retires
   every flight it covers, including one answered after its flight timed out and was re-sent."
   Options: add `ARCHITECTURE.md` to the file set and I make that edit, or the lead makes it.

## Evidence

All at consensus:615e760.

| Criterion | Evidence |
|---|---|
| A1 | 261115-213819-1f8a35 (peers `late-append`: 5 ms hog, rpc_timeout 40 ms, three commits, slowest 236 ms, 28 appends superseded); fails without: 261115-213822-6700e2 (core.h from consensus:a95847e: `commits FAILED, slowest 2285 ms, 95 appends superseded`, CHECK abort) |
| A2 | 261115-214258-d68788 (pending: consensus_test with `stale_append_responses`, `late_append_responses`); mutant killed: 261115-213824-79ec49 (`response.term > hard_.term`: "a term-1 acknowledgement through index 4 neither matches voter two nor commits index 4" fails) |
| A3 | suite 261115-213811-8d0872 (24/24 ctest, Release, transport on); tsan: `ci:linux-tsan`, needs the branch pushed |
| A4 | `CHANGELOG.md` 2026-11-15 "Fixed": "A leader counts a late append success" (consensus:3178a27) |

## Result

`Core::receive_message(AppendResponse)` (`include/consensus/core.h`) counts a success whose
flight was superseded, when it is in the leader's term, within the leader's log and no snapshot
install owns the peer; one `acknowledge` helper serves in-flight and late successes. Under a
5 ms per-cycle hog with compact timing, three commits land within 236 ms each where the base
commits nothing within 2 s (A1 jobs). Tests: `test/transport/peers_test.cc` `late-append`;
`test/consensus_test.cc` `late_append_responses`, `stale_append_responses` (Figure-8 shape:
five voters, a term-1 acknowledgement of entries a term-2 leader truncated, held back to the
term-3 leader, where counting it would commit index 4 on two voters). Open: the two items
under Blocked on; `fetch-alongside-consensus` keeps its `loaded` timing (D92), only its
comment changed.

## Rulings I made

- R1 Accept rule (`Core::receive_message(AppendResponse)`): a success whose rpc matches no
  flight counts when its term is the leader's current term (the existing top check), its
  `matched` is within the leader's log, and no snapshot install owns the peer. It advances
  `matched` only upward and retires every flight ending at or below it. This is Raft's rule
  (etcd's `MaybeUpdate`): in its own term a leader's log only grows and a follower truncates
  only entries that conflict with that leader, so a current-term success proves the follower
  holds the leader's log through `matched`, whichever flight it answers.
- R2 "An index the follower has since truncated" can only arise across terms: in the leader's
  term a follower never truncates an index it acknowledged in that term (R1). A2's test is
  therefore the Figure-8 shape: voter two acknowledges 2..4 in term 1, a term-2 leader
  truncates them, and the stale term-1 acknowledgement arrives at the term-3 leader, where
  counting it would commit index 4 on two of five voters.
- R3 A late failure stays dropped (it would rewind `next` from a superseded flight's start).
- R4 A late success also acknowledges its read probe: the probe token exists only after the
  read's admission and the success is in the leader's term, which is exactly what a ReadIndex
  quorum probe proves. Without it, linearizable reads livelock under the same hog.
- R5 The success path is one helper (`acknowledge`) for in-flight and late answers; retiring
  "flights ending at or below matched" equals the old "this flight and every older one" for an
  in-flight answer.
