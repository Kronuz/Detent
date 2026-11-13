# coordinator-idle-flake: journal

State: waiting on jobs 261112-173324-e72d65 261112-173327-57c94d 261112-173329-6db2dc
Quest: .campaign/detent/quests/coordinator-idle-flake.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Jobs queued at the fix commit: A2 repeat (e72d65), A1 mutant (57c94d), suite (6db2dc).

## Next

1. When they finish: paste `campaign brief` Evidence table, set State to ready for review.

## Blocked on

Nothing blocks the fix. The cause of the stray byte is not observed: 300 plain and 320 ASan+UBSan (8-way parallel) local macOS runs never failed, and the Linux VM key belongs to another project. The lead may want the next CI flake's `stray=N` checkpoint to confirm.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| A1 | <no evidence yet> |
| A2 | <no evidence yet> |

## Result

Cause (by elimination, not observed): the fixture polled the raw fd once, then required one read_some to fail. poll returns for readable bytes as well as for EOF; a byte the peer had in flight makes the read succeed and `check(bool(error))` fail (line 1118). Client never sends TLS close_notify (it closes the TCP socket), and nothing in the base case sends a second request (poll interval 12 h), so which byte it was is unconfirmed. Fix: tests/detent_delivery/peer_close.hh `peer_closed` reads until the stream errors, bounded by a deadline, and counts the stray bytes; both the retained-idle and the session-close waits use it and log `stray=N`. Unit test tests/detent_delivery/test_peer_close.cc (byte then close); the single-read mutant fails it. No client change.

## Rulings I made

- Fixed the test, not the client: the client's close (TCP close, no close_notify) always yields an end-of-stream or reset error; only data can make the read succeed.
- Also applied to the session-close wait (same single-read pattern).
