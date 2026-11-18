# journal-turn-bound: journal

State: fix round 1 (durability CHANGES + N1 ruling) at journal:e78af79, pushed; waiting on jobs
261117-211024-d97103 (suite), 261117-211027-8e327b (N1 fails-without: `bound` at 68271af's driver
and log), 261117-211031-1d5f49 (N1 probe, replaces 1ea201 with both boundaries),
261117-210245-7c1b35 (F1 fails-without), 261117-210250-0a2126 (F2 fails-without)
Quest: .campaign/detent/quests/journal-turn-bound.md (the lead's; never edit it)

## Doing

Fix round 1 for review .campaign/detent/reviews/journal-turn-bound-durability.md.
- journal:a1ac164 drops the review file the reviewer committed into the journal repo.
- journal:2c22dc8 F1: refuse() quiets each refused group's sealer; link() never marks a refused
  group owed. F2: forget() retires through `last + 1`. Cases `owed`, `forget`.
- journal:68271af docs: N2, N3, N4, F1/F2 behavior.
- journal:e78af79 N1 (lead: exact fit; turn_log.h added to the file set): const
  `TurnLog::fitting(span)`, the longest prefix one turn encodes in an empty segment without copies
  (logical-bytes cap, then binary search over `fits()`). start() takes all while cost fits
  `turn_room`, else `fitting`'s prefix; refuse() refuses only a record that does not encode alone.
  Sealers keep `turn_room`. Case `bound`: main's limits via `TurnLog::append` (single record; 10,
  100, 1000-byte loads), at the limit and one past. Oversized records elsewhere are `over()`.
- Fails-without: 7c1b35 (`owed`), 0a2126 (`forget`) at 7990fc6's driver; 8e327b (`bound`) at
  68271af's driver and log.

## Next

1. When green: `campaign accept journal-turn-bound durability --confirm F1=d97103 F2=d97103`;
   paste brief's Evidence table; State "ready for review".

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

- Round 1: journal:e55d41d (the reviewer's) put the campaign review file into the journal repo;
  journal:a1ac164 removes it. The review stays in the home.
- Round 1, N1: `fitting` runs only when the pending cost passes `turn_room`, so a turn within the
  room is encoded once, as before; a refuse() check runs only for a record above the room.
- Round 1, F1: seal() is unchanged. A refused group's linked waiters are all below the hole and
  their records staged or in flight, so a re-registration's wanted never exceeds its staged; the
  `owed` case covers re-registration anyway.

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
