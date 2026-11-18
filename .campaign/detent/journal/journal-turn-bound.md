# journal-turn-bound: journal

State: waiting on jobs 261117-202834-ed7481
Quest: .campaign/detent/quests/journal-turn-bound.md (the lead's; never edit it)

## Doing

Code at journal:87ea77e, docs at journal:9270185 (head, pushed for CI). Jobs: suite a0a7d2 at
87ea77e and 89c90c at 9270185 (A1,A2,A3,A5); A1 mutant 8a9d1f done: exit 1, "the wave commits:
turn exceeds a segment: seed 1" with main's turn_driver.h; A4 4608c2
(consensus:d668ae2 `consensus_wave_open_test 64`, `64 8`, `32` with journal pinned to 87ea77e
via FETCHCONTENT_SOURCE_DIR_JOURNAL, as probe a23d83 did).

## Next

0. crc_fixture cleared (lead): 6d9f84 head and e77d9b main make the same syscalls (fsync 68,
   F_FULLFSYNC 217, pwrite 136; 87/87 checks); 20.7 s vs 12.2 s, one run each under different
   load. The fixture builds no TurnDriver. Follow-up (not this quest): journal_crc_fixture's 30 s
   cap is too tight for a ~20 s test under suite load.
1. Suite rerun ed7481 at 9270185: if green, paste brief's table and set "ready for review"; if
   only crc_fixture times out again, report it to the lead, who rules.
2. When green, paste `campaign brief`'s Evidence table and set State to "ready for review".

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | 8a9d1f fails-without at main's driver ("the wave commits: turn exceeds a segment"); suite ed7481 queued |
| A2 | suite ed7481 queued (`journal_turn_bound_oversize`; passed in a0a7d2) |
| A3 | suite ed7481 queued (`journal_turn_bound_crash`, `journal_turn_seal_crash`; passed in a0a7d2) |
| A4 | 4608c2 exit 0, consensus:d668ae2 wave-open 64, 64/8, 32 against journal 87ea77e: all committed, none fenced (brief: ran in consensus, not the quest's repository) |
| A5 | CI green at 9270185; suite ed7481 queued |

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
