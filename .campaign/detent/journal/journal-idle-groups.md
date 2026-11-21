# journal-idle-groups: journal

State: done; correctness review accepted at journal:a76ef25, CI 44863560566 green; landing waits on xapiand:turn-log-parity (the lead's)
Quest: .campaign/detent/quests/journal-idle-groups.md (the lead's; never edit it)

## Doing

Correctness review round 1 (Detent:ca65d7c2, CHANGES at journal:d2ce548) fixed at journal:b6a7697.
Locally (Debug): host_test 80,599 checks, 0 failures; journal_test park_count 13 checks, 0 failures.

- F1: `host_test split_orphan` (review's probe_orphan as a case); mutant `no-orphans` 261120-184649-b2db56.
- F2: only a stored node splits (a slot's first commit writes its root whole), so open reads paths
  under no stored node and no cover as a missing shard: lost, siblings kept, never `none`.
  `host_test missing_shard`, `first_split_crash` (47 cuts); mutants `missing-shard-stale`
  261120-184652-47309a, `split-unstored` 261120-184654-9e1272.
- F3: `Host::park` takes every idle group no consumer has open (no hold, no sealer);
  `close_into_table` erases `opened_`. One contract in D2/D10/Contracts. `host_test park_unheld`;
  mutants `park-skips-opened` 261120-184656-0e1021, `close-keeps-opened` 261120-184658-120ecc,
  `park-ignores-sealer` 261120-184700-17fc0d. Xapiand told: msg-188.
- Tests whose "open" groups were open only by create_group now hold them (idle_crash, split_run,
  split_stale_node, table_damage, park_mid_turn, idle_damage, idle_unmanifested, park_count's anchor).
- F4-F6 (non-blocking): not taken; F6 relayed to Xapiand in msg-188.

## Next

1. When every job above passes and CI 44857689442 is green, write here:
   `campaign accept journal-idle-groups correctness --confirm F1=261120-184649-b2db56 --confirm F2=261120-184652-47309a --confirm F3=261120-184656-0e1021`
   (F2 also 261120-184654-9e1272; F3 also 261120-184658-120ecc, 261120-184700-17fc0d).

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | journal docs/design/idle-groups.md (journal:177814e, through journal:a336c58: D9-D11 the lead's Q1-Q3; review round 1 contract in D2, D9, D11 at journal:b6a7697) |
| A2 | 261120-184737-1c0e6e (strict, 1M, at b6a7697), rerun at a76ef25 261120-210625-18c411; 261120-210624-d69943 and 261120-210755-f25487 (1M park_count at a76ef25). Earlier: 261120-095115-033c33 at 893fd77: heap slope 0.24 B/group (the shard index), 62.6 B/group on disk, log back to 1 segment |
| Q1 before/after (strict, 1M) | before 261120-092423-3fc8cc (7129c53), 261120-095112-ec7b19 (a0944ca, APFS); after 261120-184737-1c0e6e (b6a7697; rerun 261120-210625-18c411 at a76ef25), 261120-095115-033c33 (893fd77, APFS): 15,192 -> 3,143 table B per transition; APFS 59.6 -> 61.7 ms per transition (sync-bound, same within noise) |
| A3 | at a76ef25: 261120-210624-71f0cc (split_orphan, missing_shard, first_split_crash, park_unheld and the idle cases), 261120-210755-0fdc7b, 261120-210755-db9a92, 261120-210755-913cdd, 261120-210755-6b1c50. Mutants killed: parked-as-open 261120-210624-1346c2, wake-skips-commit 261120-210625-19b6bd, create-with-block 261120-210625-476ecf, park-keeps-log 261120-210624-21f2ec, damaged-shard-fails-open 261120-184715-dc11a8, split-removes-early 261120-184717-ef077e, split-keeps-parent 261120-184720-f6328d; review round 1: no-orphans 261120-184649-b2db56, missing-shard-stale 261120-184652-47309a, split-unstored 261120-184654-9e1272, park-skips-opened 261120-184656-0e1021, close-keeps-opened 261120-184658-120ecc, park-ignores-sealer 261120-184700-17fc0d |
| Open time (coordinator item 1) | APFS warm, 1M parked, three opens: before 3.65/3.32/2.96 s over 4,096 files (261120-100752-65e1de at journal:61d2a42), after 1.92/1.88/1.72 s over 20,958 files (261120-100755-5948a1 at journal:0c56d21); at a76ef25 261120-210624-d69943; cold cache unmeasured |
| Forward scan (coordinator, from consensus-quiescence) | park_count: 0.01 live records per turn over 20,000 parked groups; control mutant park-keeps-log, killed (261120-210624-21f2ec at a76ef25); 1M strict 261120-113445-21c05a: 0.02 records per turn |
| A4 | 261120-210755-c4b2c6 (suite at a76ef25); CI run 44863560566 green at a76ef25 |
| Review correctness | accepted at journal:a76ef25 (coordinator; confirming reruns ced8ca, cf3c4c, dd37e7) |

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

- R14. Review F2: option (a), never split an unstored node; a slot's first file may pass 4 KiB until
  its next commit splits it. Option (b) would turn a crash in a slot's first commit into lost groups.
- R15. Review F3: park's skip is "a consumer has it open" = hold or sealer; `opened_` stays only for
  `status()` of a created group with no records. Tests keep open groups open with a live hold.
- R17. CMakeLists.txt (outside the file set; the coordinator directed it): host_test registers one
  ctest per case, parsed from host_test.cc's case list (configure depends on it), TIMEOUT 600 each;
  an unknown case name now fails. Locally 29 ctests, 11.9 s at -j4 (Debug).
- R16. The reviewer's probe.sh no longer applies (it patches host_test's case list); its three
  probes are host_test cases now, so the fix jobs are the cases and their mutants.

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
- R13. Forward scan: parked groups are forgotten, so forward_slice's walk (index_ and evicted_)
  never sees them; no fix needed. TurnLogStats::forward_scanned added as the counter. Held and
  evicted-but-unparked groups still cost the walk each forwarding turn (pre-existing; a
  per-segment index would fix it, outside this quest).
