# consensus-late-append: journal

State: working
Quest: .campaign/detent/quests/consensus-late-append.md (the lead's; never edit it)

## Doing

Local build of consensus_consensus_test and consensus_transport_peers_test at
consensus:3178a27 (fix + tests) to see the new tests pass and which existing assertion breaks.

## Next

1. Run both tests locally; amend `stale_rpc_and_failure_transitions` (see Blocked on).
2. Submit jobs: suite (A3), A1 hog test + revert job (`--fails-without`), A2 mutant job.

## Blocked on

Nothing yet.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | <pending> |
| A2 | <pending> |
| A3 | <pending> |
| A4 | `CHANGELOG.md` 2026-11-15 "Fixed": "A leader counts a late append success" (consensus:3178a27) |

## Result

<pending>

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
