# local-disk-store-lease-flake: journal

State: waiting on jobs 261122-211249-037174 261122-211458-6be91a 261122-211501-30df88
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
| A1 | cause 261122-001517-639f92 (done): old test with the first generation's recovery held across the pass fails "disk store invariant at line 192" (the inserted line shifts 191), as predicted: a held mapping lease defers the pass. Loops 261122-001451-97bf01 (fixed) and 261122-001501-95503f (before) hit the 30-minute deadline at load ~100-166, result invalid, not counted; timing probe 261122-211249-037174, then the 2000 runs are split into shards |
| A2 | suite 261122-001527-9cad3c failed: only test_authority_history_ranges (215 tests, unrelated to local store); rechecked alone at the fix 261122-211458-6be91a and at main 261122-211501-30df88 |

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
