# journal-driver: journal

State: waiting on jobs 261116-130823-5e806b 261116-130827-e7b375 261116-130921-66d097 and mutant/rerun jobs listed under Evidence
Quest: .campaign/detent/quests/journal-driver.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

A1-A5 built (journal:e2dc181..HEAD). A3/A4 per D105: one flat directory per host
(`include/journal/host.h`), tests `test/host_test.cc`, `test/placement_test.cc`,
`test/completion_test.cc` host_lock, crash rounds with close/reopen in `test/shared_store_test.cc`,
`bench/idle_cost.cc`. Evidence jobs submitted at journal HEAD; waiting on them.

## Next

1. Read the jobs below; fold any failure in, rerun at HEAD.
2. Fill the Evidence table; set "ready for review" (durability review next).

## Blocked on

Nothing. The lead's D105 answered both items: flat directory with (generation, loop)-prefixed
logs (R13 updated), and N1 fixed in completion:7731315 (repinned, R21).

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | 261116-123503-343a08, 261116-123045-cac914 (passed at 170e6fe369dd; ignores docs-only AGENTS.md, ARCHITECTURE.md, docs/design/host-layout.md); fails without: 261116-123048-ac237c, 261116-123051-b79de0 |
| A2 | 261116-123042-8ca6de (passed at 170e6fe369dd; ignores docs-only AGENTS.md, ARCHITECTURE.md, docs/design/host-layout.md), 261116-123045-cac914 (passed at 170e6fe369dd; ignores docs-only AGENTS.md, ARCHITECTURE.md, docs/design/host-layout.md); fails without: 261116-123014-81ccc1, 261116-123054-f575c6, 261116-123057-674517, 261116-123100-c710d8, 261116-123103-55423a |
| A3 | pending: suite 261116-130823-5e806b, ASan 261116-130827-e7b375, idle cost (exclusive) 261116-130921-66d097; fails without: 261116-130859-f3128c (group-blind-names), 261116-130904-cac914 (group-scans), 261116-130910-fbe621 (reopen-from-log), 261116-130916-67da9a (same-incarnation) |
| A4 | pending: suite 261116-130823-5e806b, ASan 261116-130827-e7b375; fails without: 261116-130843-ee8021 (remap-forget), 261116-130848-d5d008 (repartition-skips-mark), 261116-130854-281a18 (open-skips-mark) |
| A5 | grep rerun 261116-130832-d4538a (pending); [artifact] journal `ARCHITECTURE.md` "One directory per host" and `docs/design/host-layout.md` |

Reruns at journal:0c52251 (pending): A1 suite 261116-130823-5e806b, ASan 261116-130827-e7b375,
mutants 261116-130833-94034b, 261116-130833-16ae42; A2 TSan 261116-130832-8903d2, mutants
261116-130833-de3054, 261116-130833-7ab02f, 261116-130834-4f5c4b, 261116-130834-5d1eb9,
261116-130834-32f7a8.

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
