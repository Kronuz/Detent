# journal-driver: journal

State: working
Quest: .campaign/detent/quests/journal-driver.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

A1, A2 built and evidenced at journal:6dc25ce (drivers, blocking entry, crash points through
HeldCompletion). A3/A4 designed in journal `docs/design/host-layout.md`; implementation not started.

## Next

1. A3: `include/journal/host.h`: the group table (RJGT0001, replaced whole), the layout record
   (RJHL0001), the host owner lock shared by group views, artifacts named
   `a-<group>-<incarnation>-<serial>` with serial blocks reserved in the table; reclamation and
   inventory by probed name, no scan (artifact.h, reclamation.h, inventory.h, journal.h); a closed
   group reopened from its table entry. Tests: nested ids never collide; reclaiming one group
   touches no other; idle-cost job (0 descriptors per idle group).
2. A4: placement `(slot × L) >> 12`; re-partition steps 1-3 with crash points at every durable step
   over a damaged old generation; TurnLog opens with the table's lost ids (`placement_test remap`,
   `repartition_lost`, retention test). Needs the lead's call below for the per-loop subdirectories.
3. A5: on-disk layout in ARCHITECTURE.md ("one directory per host") once built; rerun the grep.
4. Durability review once A3/A4 are in.

## Blocked on

- Lead: completion's `IO` has no directory operations, and the per-loop turn logs live in
  subdirectories (R13). Recommendation: completion gains `IO::open_directory(name, create)` and
  `remove_directory(name)` (POSIX mkdirat + openat(O_DIRECTORY); journal's MemoryIO by prefix).
  Alternative: prefix segment names in the host directory, which changes RFTW0001's scan. Only
  A4's step 2/3 wait on it; A3's table, naming and reclamation do not.
- Lead: seam review N1 is in completion (`asio_completion.h`: `TurnSource`/`turn_running_` exist
  only without NDEBUG, so the driver's layout differs across translation units); completion is
  outside this file set. Fix there: keep `turn_running_` always, make only the assert conditional.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | 261116-123503-343a08, 261116-123045-cac914 (passed at 170e6fe369dd; ignores docs-only AGENTS.md, ARCHITECTURE.md, docs/design/host-layout.md); fails without: 261116-123048-ac237c, 261116-123051-b79de0 |
| A2 | 261116-123042-8ca6de (passed at 170e6fe369dd; ignores docs-only AGENTS.md, ARCHITECTURE.md, docs/design/host-layout.md), 261116-123045-cac914 (passed at 170e6fe369dd; ignores docs-only AGENTS.md, ARCHITECTURE.md, docs/design/host-layout.md); fails without: 261116-123014-81ccc1, 261116-123054-f575c6, 261116-123057-674517, 261116-123100-c710d8, 261116-123103-55423a |
| A3 | <no evidence yet> |
| A4 | <no evidence yet> |
| A5 | 261116-123349-72c566 (passed at 170e6fe369dd; ignores docs-only AGENTS.md, ARCHITECTURE.md, docs/design/host-layout.md); [artifact] pending the built layout |

Notes on jobs: 261116-122429-d36c87 and 261116-122533-fd233c failed on test expectations (a group's
create stages checkpoint record 1; one append per group at a time), fixed in journal:3d6a455 and
bb48b2e. 261116-123018-fcc2af failed before building (no `.scratch/` for its log), resubmitted as
261116-123042-8ca6de. Jobs 261116-122924..122944 were submitted with a mangled `--quest` argument
and never ran as evidence. Mutant kills: no-sync-data 4,898 crash-point failures; no-barrier-wait
aborts (`storage permit unavailable`); entry-skips-commit and stalled-without-groups fail their
checks; entry-waits-without-deadline hangs (60 s timeout); wake-ignores-claim SEGV under ASan;
cancel-without-hop a TSan data race in `TurnDriver::unlink`.

## Result

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
