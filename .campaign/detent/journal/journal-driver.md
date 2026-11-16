# journal-driver: journal

State: waiting on jobs 261116-141838-acebfd 261116-141841-9aa2bd 261116-141845-fc7862 261116-141849-473b2c 261116-141853-1c95eb 261116-141857-2492f6 261116-141901-fea9cf 261116-141905-228948 261116-141908-6c54a9 261116-141912-bac21f 261116-141915-1284ed 261116-141919-d95bce 261116-141922-5664cd 261116-141925-5a860e 261116-141928-26c106 261116-141931-c71579 261116-141934-eac4e0 261116-141937-ca8803 261116-141941-8771b6 261116-141944-59a147 261116-141947-a9566f 261116-141950-c5319b 261116-141954-c7b068 261116-141957-09e890 261116-142000-26d170 261116-142003-5516c2 261116-142006-c0c1df 261116-142008-75d3a1 261116-142011-b6fe5a 261116-142013-74b933
Quest: .campaign/detent/quests/journal-driver.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Fix round 1 for the durability review (Detent `.campaign/detent/reviews/journal-driver-durability.md`,
verdict CHANGES at journal:535e9e5): F1-F3 fixed with a test and a mutant each, F4 and F7 folded
in (journal:d4b6380..5a959a1). Evidence for every criterion resubmitted at journal:5a959a1.

## Next

1. When the round-1 jobs finish: mark each in the Fix round 1 table, then set "ready for review"
   (the lead accepts with `campaign accept journal-driver durability --confirm F1=... F2=... F3=...`).

## Blocked on

Nothing. The lead's D105 answered both items: flat directory with (generation, loop)-prefixed
logs (R13 updated), and N1 fixed in completion:7731315 (repinned, R21).

## Fix round 1 (durability review)

| Finding | Fix | Test (fails at 535e9e5) | Mutant killed | Confirming job |
|---|---|---|---|---|
| F1 | `TurnLog::recover` loses every open group the table names (`TurnLogGroups::open`) that a damaged log holds nothing of; open and re-partition both mark it (journal:d4b6380) | `placement_test damaged_absent` (modes 0-2, two restarts, create refused): 261116-142008-75d3a1 | damaged-keeps-absent 261116-142000-26d170 | suite 261116-141838-acebfd; probe rerun 261116-142003-5516c2 |
| F2 | table transitions go through `Host::update` (change, commit, restore on failure); `close_group` commits before `forget` (journal:d4b6380) | `host_test failed_close`: 261116-142011-b6fe5a | close-forgets-first 261116-141941-8771b6, update-keeps-change 261116-141944-59a147 | suite 261116-141838-acebfd; probe rerun 261116-142006-c0c1df |
| F3 | `recover_shared` takes the table's record only when `TurnDriver::holds` is false (no durable or staged record); otherwise Corruption (journal:d4b6380) | `host_test second_recovery`: 261116-142013-74b933 | recover-twice-from-table 261116-141947-a9566f | suite 261116-141838-acebfd; probe rerun 261116-142006-c0c1df |
| F4 | same `Host::update` rollback: memory never ahead of the disk | `failed_close` checks the entry stays open | update-keeps-change | as F2 |
| F5 | not changed: noted (low-water `mark` probes every serial since a pinned artifact) | - | - | - |
| F6 | not changed: refusing a scheduler without `post_call` needs task's `scheduler_ref` to expose it (task repo, outside the file set) | - | - | - |
| F7 | documented in `blocking.h`: a stalled entry's scheduler is not driven again (journal:286582c) | - | - | - |
| F8 | not changed: kept per D43, the owner's call | - | - | - |

Round-1 evidence at journal:5a959a1: suite 261116-141838-acebfd, ASan 261116-141841-9aa2bd, TSan
261116-141845-fc7862, A5 grep 261116-141849-473b2c, idle cost (exclusive) 261116-141853-1c95eb.
Mutants: A1 no-sync-data -2492f6, no-barrier-wait -fea9cf; A2 entry-skips-commit -228948,
entry-waits-without-deadline -6c54a9, stalled-without-groups -bac21f, wake-ignores-claim -1284ed,
cancel-without-hop three TSan samples -d95bce -5664cd -5a860e; A3 group-blind-names -26c106,
group-scans -c71579, reopen-from-log -eac4e0, same-incarnation -ca8803, close-forgets-first,
update-keeps-change, recover-twice-from-table; A4 remap-forget -c5319b, repartition-skips-mark
-c7b068, open-skips-mark -09e890, damaged-keeps-absent. (All 261116-14xxxx; full ids in State.)

## Evidence

| Criterion | Evidence |
|---|---|
Final commit journal:535e9e5 (fixes the group-scans anchor; every mutant's anchor occurs once).
Evidence at 0c52251 (passed, per the lead): suite 261116-131224-3a268f, ASan 261116-131229-921097,
idle cost 261116-131315-8db917 (0 descriptors per open or closed group at 100 and 1,000), mutants
killed -8e5e16 -273447 -d7c2d9 -21826e -29b5c7 -b049cb. Reruns at 535e9e5:

| A1 | suite 261116-134558-0ebf97, ASan 261116-134558-ff571e (pending); fails without: 261116-134559-9de588 (no-sync-data), 261116-134559-290fe3 (no-barrier-wait) (pending) |
| A2 | TSan 261116-134559-096532 (pending); fails without: 261116-134559-acb5ec, 261116-134600-8f3890, 261116-134600-071bcb, 261116-134600-dd6db2, 261116-134600-2c8644 (pending) |
| A3 | suite and ASan as A1; idle cost (exclusive) 261116-134559-716069 (running); fails without: 261116-134601-37aff8 (group-scans, killed: census sees "storage inventory not ready"), 261116-134601-10a9e7 (same-incarnation, killed), 261116-134601-9a13f2 (group-blind-names), 261116-134601-f4630f (reopen-from-log) (pending) |
| A4 | suite and ASan as A1; fails without: 261116-134600-5ef699 (remap-forget, killed), 261116-134601-8b0196 (repartition-skips-mark), 261116-134601-39997d (open-skips-mark) (running) |
| A5 | 261116-134559-d0bfa1 (no Asio under include/, passed at 535e9e5); [artifact] journal `ARCHITECTURE.md` "One directory per host" and `docs/design/host-layout.md` |

Jobs 261116-130823-5e806b, 130827-e7b375, 130843..130921 never ran (zsh kept `$Q` one argument);
resubmitted as above.

Notes on jobs: 261116-122429-d36c87 and 261116-122533-fd233c failed on test expectations (a group's
create stages checkpoint record 1; one append per group at a time), fixed in journal:3d6a455 and
bb48b2e. 261116-123018-fcc2af failed before building (no `.scratch/` for its log), resubmitted as
261116-123042-8ca6de. Jobs 261116-122924..122944 were submitted with a mangled `--quest` argument
and never ran as evidence. Mutant kills: no-sync-data 4,898 crash-point failures; no-barrier-wait
aborts (`storage permit unavailable`); entry-skips-commit and stalled-without-groups fail their
checks; entry-waits-without-deadline hangs (60 s timeout); wake-ignores-claim SEGV under ASan;
cancel-without-hop a TSan data race in `TurnDriver::unlink`.

## Result

A3/A4 (journal:e9d9a57..0c52251): `Host` (`include/journal/host.h`) keeps every group and loop of a
host in one flat directory under the directory's own lock: `layout` (RJHL0001), the group table
sharded by slot (`groups-<slot>`, RJGT0001), loops' segments `l<gen>-<loop>-turns-<n>` and groups'
artifacts `g<group>-artifact-<incarnation><serial>`, reached through `PrefixIO` views. Reclamation
and the census probe a group's serial range by name; close/reopen through the table; lost ids
marked at every open and passed to `TurnLog` (`TurnLogGroups`); re-partition read/write/commit.
Docs: journal `docs/design/host-layout.md`, ARCHITECTURE.md "One directory per host".

A1/A2 (journal:e2dc181..6dc25ce): `TurnDriver::run(completion_ref, stop)`, `TurnDriver::durable`,
`TurnBarrier` (Write+SyncData, Turn lane), Store's tasks, `journal::run` and `journal::stalled`
(`include/journal/blocking.h`); pins completion:9041186 and task:95798a0, no Asio. The shared-store
crash suite runs every crash point through the tasks over HeldCompletion and writes the synchronous
history (967 points per mode at seed 1, identical operation counts). Docs: journal ARCHITECTURE.md
"Coroutine drivers"; design for A3/A4: `docs/design/host-layout.md`.

## Rulings I made

- R23. F1 lives in `TurnLog::recover`, where TurnRecovery's contract is: the host passes its open
  ids (`TurnLogGroups::open`, a vector built in the pass `known()` already makes), so open and
  re-partition get it alike and the log refuses the id at once. A group reopened or created whose
  first record never became durable is lost too while the damage remains: fail closed.
- R24. F2/F4 by rollback, not a host fence: a failed shard write restores the entry; if the rename
  did persist, the disk is ahead of memory, which retrying the same transition rewrites
  idempotently, and the group's own journal is fenced by the failure.
- R25. F3 refuses a second recovery on one host (Corruption, the pre-quest behavior) rather than
  rereading the log; `TurnDriver::holds` also covers a reopen whose record is still staged.
- R26. The new cases are named `damaged_absent`, `failed_close`, `second_recovery` so the review's
  probes, which include the test files, rerun unchanged. `entry_threads_busy` runs 200 rounds: at 20
  the cancel-without-hop mutant survived one TSan run of three (261116-134559-acb5ec).

- R1. task pinned at task:95798a0 (blocking_scheduler) ahead of completion:9041186's task:18eb516, declared
  first in journal's CMakeLists so FetchContent takes it; TASK_ASIO and COMPLETION_ASIO off.
- R2. Store's tasks take the reservation and ReplacementId by value (a coroutine keeps a reference
  parameter as a reference, which dangles once the caller's temporary dies).
- R3. A Store task's token is checked before its operation starts and while a shared operation waits
  for its barrier; a started operation's IO runs with no token, so cancellation never tears an
  operation (D8 rows "not yet queued" and "in the kernel": an unfinished StoreArtifactOperation
  fences the store when destroyed).
- R4. TurnDriver::run takes a stop token (the loop's shutdown): it returns once stop is requested
  and nothing is staged or in flight; turn IO runs with no token, so staged records are always written.
- R5. journal::run has an overload taking the blocking_scheduler, so a task can arm a task::deadline
  on it; the D2 signature `run(TurnDriver&, task<T>)` makes its own.
- R6. A turn that fails inside the blocking entry is delivered to the task; if the task succeeds
  anyway, the entry throws that failure (the entry promises staged records are written).
- R7. The holder tag (none, driver, entry) refuses run() under the entry, the entry under run(),
  and a manual begin()/commit() while run() holds the driver.
- R13 (revised per D105). One flat directory: each loop's segments are named
  `l<generation:16x>-<loop:4x>-turns-<n>` through a prefixed view (`PrefixIO`), so `TurnLog`
  keeps naming and scanning `turns-<n>` inside it; completion gains no directory API.
- R14 (D43). The R20 migration, the `journal.shared` marker, `Journal::Layout` and per-group
  shared directories are removed, no shims. consensus `include/consensus/worker.h:167-181` and its
  shared tests call `migrate`/`layout`/`finish_migration`; they drop them when consensus repins
  journal (consensus-driver).
- R15. The group table is sharded by slot (`groups-<slot:3x>`, 4,096 shards, each replaced whole):
  one whole table would rewrite every entry (about 330 MB at a million groups) on every open,
  close and serial block.
- R16. The host lock is the directory's own (`OwnerLockMode::directory`, strata's
  `acquire_directory_owner`); no `owner.lock` file. A group's view hands out a token lock.
- R17. An artifact's identity is (incarnation, serial) big-endian, so its name follows from its
  descriptor; reclamation still checks each candidate's ownership prefix.
- R18. A closed group's last record (`base`, manifest) lives in the table; `TurnLog` ignores a
  group's records through `base`; reopen flips the entry first, then stages the record as
  `base + 1`, so a crash between them recovers from the table.
- R19. A re-partition reads the stored generation with `TurnLog::read` (writes nothing) and
  writes turns of at most a quarter segment, counting 64 bytes of framing per record.
- R20. A host open lists the directory once (twice when it re-partitions) for the layout, shards
  and logs; artifact names are skipped.
- R21. completion pinned at completion:7731315 (N1 fix); task stays at task:95798a0, a descendant
  of completion's task:18eb516, declared first so it wins.
- R22. A group's view reads its serial range from a locked copy the host updates (a reclamation's
  scan primitive may run on a completion worker), never the table's map.
