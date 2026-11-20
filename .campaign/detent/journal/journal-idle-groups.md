# journal-idle-groups: journal

State: waiting on jobs 261120-095112-ec7b19 261120-095115-033c33 261120-095118-0e43df (Q1 before/after on a real disk, split-keeps-parent)
Quest: .campaign/detent/quests/journal-idle-groups.md (the lead's; never edit it)

## Doing

Lead rulings (coordinator, after the first hand-off): Q1 bounded table shards (D9), Q2 park call
contract (D10), Q3 damaged shard fails only its groups (D11), F3 unmanifested test written and
pending journal-double-damage. Code at journal:893fd77; docs (idle-groups.md D9-D11) in progress.

## Next

1. Fill idle-groups.md D9's real-disk row from 261120-095112-ec7b19 (before) and 261120-095115-033c33 (after); commit docs.
2. Suite and A3 at the final head (`campaign verify`), CI on the branch, then State: ready for review (D135).

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | journal docs/design/idle-groups.md (journal:177814e, updated through journal:7b64376) |
| A2 | 261120-090241-b0ccee (park_count, 1M); at b55e34d: 261120-084841-834ac9: 1,000,000 parked, heap slope -0.0006 B/group, 62.0 B/group on disk, log back to 1 segment, 93 s |
| A3 | 261120-090244-85f26d (idle_crash 176 cuts x3 restarts, idle_damage, failed_reopen); mutants killed: parked-as-open 261120-090247-44cc33, wake-skips-commit 261120-090249-51170f, create-with-block 261120-090252-5f0ac1, park-keeps-log 261120-090255-807b5e |
| A4 | 261120-090240-a27b5a (suite); CI on push |

Coordinator's design inputs (review F2, F3, F5 of Xapiand turn-log-parity-design.md): answered in
idle-groups.md D1 (F3: parked is closed, a durable state recovery already exempts), D5 (F3:
unpark committed before the turn), D7 (F2), D8 (F5).

Coordinator's naming input: journal-idle-groups owns the one park mechanism, journal:idle-park.
Names, in idle-groups.md "Names": **evicted** = in memory only (TurnDriver::evict,
TurnLog::evict, run by Host::settle; renamed from park in journal:7b64376); **parked** = the
durable table state (Host::park, Host::close_group; on disk the `closed` entry state).

## Result

- journal include/journal/host.h: Host::park(loop); create_group/reopen_group deferred to a
  pre-turn commit (TurnDriver::before_turn), one sync_files + one directory sync per commit;
  streaming re-partition. turn_log.h: peek, idle, evict rename. turn_driver.h: before_turn.
- Measured (A2): 1M parked groups cost 0 heap and 62 B each on disk at 17-B payloads.
- Docs: docs/design/idle-groups.md, host-layout.md, mechanisms/group-lifetime.md, CHANGELOG.
- Open: Q1 (shard rewrite per transition; 93 s for 1M on a memory disk), Q2 (park policy),
  Q3 (damaged shard fails the host open).
- Pre-existing, for the lead (not this quest's): on journal main 1222e3b, a damaged block whose
  next block's header (its describing manifest) is also damaged makes TurnLog recovery report
  no damage and silently drop acknowledged records after it. Probe:
  journal-wt/detent/journal-idle-groups/.scratch/probe/probe.cc, built against main's headers:
  "payload: damaged 1 lost 2" vs "payload+header: damaged 0 lost 0 groups 6 ... g6:1" (g6 wrote 5).
  It blocks F3's "unmanifested" damage test; idle_damage uses manifested damage.

## Pending another quest

- `host_test idle_unmanifested` (review F3's unmanifested case: header plus payload damage of a
  woken group's turn) fails today, as the double-damage gap predicts ("the log reports the
  damage", "the woken group ... is lost"). It runs by name only, outside the default run, until
  journal-double-damage lands; then it joins the cases list.

## Rulings I made

- R1. Parked is the existing closed state (D1), not a new table state: same bytes, same exemption.
- R2. turn-log-parity's one-log-per-device shape is not on journal main: designed against its quest
  file; assumptions in idle-groups.md "Assumptions about turn-log-parity".
- R3. CMakeLists.txt is outside the file set: park_count goes in journal_test (whole run, 20k
  default; 1M via JOURNAL_PARKED_GROUPS in the job), idle_crash/idle_damage in host_test.
- R4. A failed pre-turn table commit fails the driver (fail-stop, as a failed turn); host_test
  failed_reopen rewritten to that contract.
- R5. A create reserves no serial block; the first artifact reserves one durably (no leaked
  artifact names after a lost create; mutant create-with-block).
- R6. Contract change (in the quest's outcome): create/reopen are durable at the group's first
  turn, not on return; strata's turn_seal.hh comment and Xapiand group-core (journal:driver
  consumer) need telling.
- R7. Dropped mutant wake-skips-commit-damage: manifested damage names the group lost regardless
  of the table, so it survives (261120-084852-c797f8); idle_crash kills the same mutant.
- R8. Q1: bounded shards (D9), not the turn-carried transition: the latter leaves a wake's only
  evidence in the blocks it would protect (F3), shown by idle_unmanifested once double-damage lands.
  leaf_bytes = 4 KiB (one page); shards never merge.
- R9. Q1 measurement: per-transition time on the memory disk rose (646 -> 1,749 us at 1M) because
  faultfs walks every inode at each ordering point (settle_staged/recount), O(files); a real-disk
  phase (JOURNAL_DISK_DIR, PosixIO, 100 samples) times the same transitions with real syncs.
  The before side is journal:a0944ca (branch measure/idle-groups-before: 7129c53 with 893fd77's
  journal_test.cc), local only.
- R10. Q3: a damaged shard's whole path range is lost (ids unknown), never rewritten; the log
  forgets its groups (artifact names unknown). Read errors other than Corruption still fail open.
- R11. Q2: park is safe mid-turn (skips staged groups; forwarded copies superseded by base);
  no park-ignores-flight mutant: opened_ already excludes a released group with records in flight.
- R12. Took over the stopped second contributor's park_count edit (sample skipped group 1);
  redone in journal:7129c53 counting only transitions made.
