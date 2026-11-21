# journal-idle-groups: journal

State: correctness review round 1 fixed at journal:b6a7697; waiting on jobs 261120-184647-8c0c61 (A3) 261120-184649-b2db56 261120-184652-47309a 261120-184654-9e1272 261120-184656-0e1021 261120-184658-120ecc 261120-184700-17fc0d (review mutants) 261120-184703-1235cc 261120-184706-b4561c 261120-184709-f6a0ca 261120-184712-0c6212 261120-184715-dc11a8 261120-184717-ef077e 261120-184720-f6328d (quest mutants rerun) 261120-184737-1c0e6e (A2 1M) and CI run 44857689442 (macos-arm64 suite gate)
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
| A1 | journal docs/design/idle-groups.md (journal:177814e, through journal:a336c58: D9-D11 the lead's Q1-Q3) |
| A2 | 261120-095115-033c33 at journal:893fd77 (strict, 1M): 1,000,000 parked, heap slope 0.24 B/group (the shard index), 62.6 B/group on disk, log back to 1 segment; also 261120-092916-3cb2bf at 48df21b |
| Q1 before/after (strict, 1M) | before 261120-092423-3fc8cc (7129c53), 261120-095112-ec7b19 (a0944ca, APFS); after 261120-092916-3cb2bf (48df21b), 261120-095115-033c33 (893fd77, APFS): 15,192 -> 3,143 table B per transition; APFS 59.6 -> 61.7 ms per transition (sync-bound, same within noise) |
| A3 | 261120-100219-c9e590 at a336c58 (idle_crash, split_crash, split_stale_node, table_damage, park_mid_turn, idle_damage); mutants killed: parked-as-open 261120-090247-44cc33, wake-skips-commit 261120-090249-51170f, create-with-block 261120-090252-5f0ac1, park-keeps-log 261120-090255-807b5e, damaged-shard-fails-open 261120-092920-ea237b, split-removes-early 261120-092924-a5abc0, split-keeps-parent 261120-095118-0e43df (survived on split_crash 261120-092922-489113; retargeted to split_stale_node) |
| Open time (coordinator item 1) | APFS warm, 1M parked, three opens: before 3.65/3.32/2.96 s over 4,096 files (261120-100752-65e1de at journal:61d2a42), after 1.92/1.88/1.72 s over 20,958 files (261120-100755-5948a1 at journal:0c56d21); cold cache unmeasured |
| Forward scan (coordinator, from consensus-quiescence) | park_count: forward_slice visits 0.01 live records per turn over 20,000 parked groups (local, journal:09c6fa2); control mutant park-keeps-log 193 per turn, killed (261120-113449-d2ae31); 1M strict 261120-113445-21c05a: 0.02 records per turn |
| A4 | 261120-100217-9e45a1 (suite at a336c58, passed 57/57); 261120-092926-4d25e2 passed at 48df21b (57/57); CI on push |

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
