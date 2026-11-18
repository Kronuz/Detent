# journal-turn-bound: journal

State: waiting on jobs 261117-200353-a0a7d2 261117-200406-4608c2 261117-200837-89c90c
Quest: .campaign/detent/quests/journal-turn-bound.md (the lead's; never edit it)

## Doing

Code at journal:87ea77e, docs at journal:9270185 (head, pushed for CI). Jobs: suite a0a7d2 at
87ea77e and 89c90c at 9270185 (A1,A2,A3,A5); A1 mutant 8a9d1f done: exit 1, "the wave commits:
turn exceeds a segment: seed 1" with main's turn_driver.h; A4 4608c2
(consensus:d668ae2 `consensus_wave_open_test 64`, `64 8`, `32` with journal pinned to 87ea77e
via FETCHCONTENT_SOURCE_DIR_JOURNAL, as probe a23d83 did).

## Next

1. Read the three jobs and GitHub CI for 87ea77e; fix any compile or test failure (the code was
   never built locally: machine work goes through jobs only).
2. When green, paste `campaign brief`'s Evidence table and set State to "ready for review".

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | 8a9d1f fails-without (exit 1, "turn exceeds a segment"); suite pending a0a7d2/89c90c |
| A2 | <pending: a0a7d2> |
| A3 | <pending: a0a7d2> |
| A4 | <pending: 4608c2> |
| A5 | <pending: a0a7d2, CI> |

msg-67 (mine to the lead): granted; CMakeLists.txt replaces test/CMakeLists.txt in the file set.
The registration is one block after the turn_seal foreach.

## Result

journal:87ea77e, include/journal/turn_driver.h:
- `start()` takes the oldest pending records whose `record_cost` sum fits
  `turn_room(segment_bytes, block_bytes)` (the bound seal_turn already spends) and leaves the
  rest pending; run(), commit() and the blocking entry loop while records are pending.
- `refuse()` (before sealing) takes out a record whose cost exceeds that room, and its group's
  later records, fails their operations with std::length_error("turn record exceeds a segment"),
  and fails durable() waits on those sequences. The group's sealer is skipped; forget() retires
  the refused sequences. The driver and other groups carry on.
- Turn bytes are unchanged (contract: no format change).

test/turn_bound_test.cc: wave (16 seeds, 64 groups x 2 records plus a full-room record, each turn
under the room, staged order kept, recovery equal to the model), wave_run (same through run()
and 64 durable() waiters), oversize (A2), crash (A3: power cut at every I/O of two waves, 6
seeds, counts cuts between two turns of one wave).

Docs (journal:9270185): docs/mechanisms/turn-seal.md section "Staged records: one segment per
turn" states the bound and the oversize rule, with why the group's later records fail too (dense
sequences in TurnLog::stage and recover_shared, no renumbering of assigned sequences, the group
already fenced by its own error path); evidence table for turn_bound_test. CHANGELOG.md: one
entry under 2026-11-17 Changed.

## Rulings I made

- The turn bound is turn_room of an empty segment, the same figure the sealers use, so a turn
  that holds sealed records always fits whole (seal_turn spends at most that room after the
  staged records) and only staged records are ever deferred.
- A record above the room is refused at the turn, not thrown from stage(): Journal has already
  advanced its record sequence and staged its operation, so failing the operation keeps the
  caller's error path (the group fences) the same as a failed turn's.
- The refused group's later records fail too: they follow a sequence the log never gets, and
  staging them would fail the whole turn on the log's sequence check.
- refuse() runs before seal_turn so an oversized pending record does not starve the sealers of
  room for that turn.
