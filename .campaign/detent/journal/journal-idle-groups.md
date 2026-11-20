# journal-idle-groups: journal

State: waiting on jobs 261120-090240-a27b5a 261120-090241-b0ccee 261120-090244-85f26d 261120-090247-44cc33 261120-090249-51170f 261120-090252-5f0ac1 261120-090255-807b5e
Quest: .campaign/detent/quests/journal-idle-groups.md (the lead's; never edit it)

## Doing

Evidence jobs at journal:7b64376 (suite A4, park_count 1M A2, idle_crash/idle_damage A3, four
mutants A3). The same set passed at journal:b55e34d except the suite, whose three failures
(sync_seam, host view_sweep, shared_store census) 7b64376 fixes; the full suite is green locally.

## Next

1. Paste the jobs into Evidence; push CI; set State to ready for review (one review, D135).

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
