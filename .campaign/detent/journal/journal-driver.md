# journal-driver: journal

State: working
Quest: .campaign/detent/quests/journal-driver.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Step 1 (A1, A2): TurnDriver::run, TurnDriver::durable, Store tasks, journal::run (blocking.h) at
journal:e2dc181 and after; driver_test and the shared_store task mode under test.

## Next

1. Green driver_test and shared_store (both modes) in Debug; TSan build of driver_test (entry_threads), ASan build.
2. Mutants for A1/A2: wake-before-unlink, entry returning before commit, entry waiting with no deadline, runner taking a caller token.
3. A3 layout: per-host directory and owner lock, artifacts named `artifact-<group 16 hex>-<incarnation 16 hex>-<id>`, per-host group table, turn logs in `turns-<generation>-<loop>/`; then A4 re-partition.
4. A5: grep job for Asio under include/; docs "one directory per host".

## Blocked on

- N1 (seam review) is in completion (`asio_completion.h`, `TurnSource` and `turn_running_` exist only
  without NDEBUG, so the driver's layout differs across translation units); completion is outside this
  quest's file set. For the lead: keep `turn_running_` always and make only the assert conditional.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | <no evidence yet> |
| A2 | <no evidence yet> |
| A3 | <no evidence yet> |
| A4 | <no evidence yet> |
| A5 | <no evidence yet> |

## Result

<pending>

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
