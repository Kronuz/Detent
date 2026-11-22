# local-disk-store-lease-flake: journal

State: waiting on jobs 261122-001451-97bf01 261122-001501-95503f 261122-001517-639f92 261122-001527-9cad3c
Quest: .campaign/detent/quests/local-disk-store-lease-flake.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Fix committed (03583ece); jobs queued. Cause: store is correct, the test asserted too early.

## Next

1. When jobs finish: fill Evidence, confirm CI green on the pushed branch, then ready for review.

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | cause: 261122-001517-639f92 (old test with the first generation's recovery held fails at line 191); before: 261122-001501-95503f (2000 runs under 4 burners at 5580bb6b, expected to fail; the CI flake is rare, so this may pass, and the deterministic job is the proof); after: 261122-001451-97bf01 (2000 runs, 4 burners, at 03583ece) |
| A2 | suite 261122-001527-9cad3c; CI on the pushed branch |

## Result

Cause: AgentStore::collect() is one synchronous pass; it tests each `.lease` with
flock(LOCK_EX|LOCK_NB) and, when held, defers (DiskStore sets collection_retry, 1s doubling to 30s).
The generation's own recovered mappings (Store::build's recover(), confirm_recovered(recover()))
close their lease fd in GenerationMapping::retire_step on the retirement runtime's cleanup worker,
after the handle drops. A pass right after the test closed its lease can run before the worker has
closed those fds, find the lease held, and leave the generation until the retry. The store behaves as
designed; the test asserted too early (test_disk_store.cc line 191).
Fix (test only): hold the recovered first generation across the pass so the deferral is
deterministic and assert it is announced (no error, retry set), release it, then retry collection
until the worker has closed the lease (5s bound, as check_released does).

## Rulings I made

Test-only fix; no store change: the deferral and backoff retry are the store's contract for a lease that is held, and the lease is held for real by a mapping awaiting retirement.
