# consensus-batch-turn-room: journal

State: waiting on jobs 261120-000859-5ca9f3
Quest: .campaign/detent/quests/consensus-batch-turn-room.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Fix and test committed (consensus:1421fb5 test only, consensus:0c52e87 fix plus docs/DRIVER.md). Jobs queued: 233216-39451c (A1, --fails-without at the test-only commit), 233229-c53762 (A1, test with the fix), 233230-df57ad (A2 suite). Pushed; the DRIVER.md states the merged-Receive residual.

## Next

1. When the jobs finish, check them; if the suite fails on an existing test whose host segment is too small for its limits, fix the test's limits (worker_test.cc) or report.
2. Push the branch for CI.

## Blocked on

CHANGELOG.md is held by another quest; entry text for the lead to add under the current Unreleased section:

> Worker: a group on a journal host refuses at construction (`std::invalid_argument`, "worker batch exceeds the host's turn room") a configuration whose largest record, a batch of `Limits::rpc_entries`/`rpc_bytes` (or `WorkerLimits::follower_batch_*` when larger) or the founding record, exceeds one empty segment of the host's turn log. Before, it reached the journal and fenced its group at the first append. The default 2 MiB rpc batch needs segments big enough for it, or explicit lower limits.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | jobs 261119-233216-39451c (fails at test-only 1421fb5), 261119-233229-c53762 (passes at 0c52e87) |
| A2 | job 261120-000859-5ca9f3 at e904c8d (earlier df57ad failed at 0c52e87 on the 7 configurations below; D132), CI after push |

## Result

worker.h `validate_turn_room` (host constructor only): record_cost(max(maximum_initialization_size, storage_batch_size(true,true,max(rpc_entries, follower_batch_entries), max(rpc_bytes, follower_batch_bytes)))) must not exceed journal's `turn_room(segment_bytes, block_bytes)`, the driver's own refusal bound. Test: `batch_turn_room` in worker_test.cc (also `--turn-room`). Docs: docs/DRIVER.md (Seams comment).

## Rulings I made

- D132 (lead): the refusal stands and the tests' limits are fixed. consensus:e904c8d: wave_open_test.cc rpc_bytes 16384 to 8192 (8 entries, command_bytes 4096 kept; batch 8362, cost 8431 <= room 13661); groups_test.cc node open sets command_bytes = rpc_bytes = 4096 with the default 256 rpc entries (batch 9482, cost 9551). Segments unchanged. DRIVER.md states the default 2 MiB batch needs big segments or lower limits.

- Bound is the operating limits, not `RetainedStatePolicy::batch_*`: the retained defaults are the 8 MiB ceilings, which would refuse every host with small segments (all driver tests use 16 KiB). A caller-submitted merged Receive up to the retained bound can still exceed the room and fences its group only (as before); the Worker's own follower batching is covered.
- Used the conservative `turn_room` bound (as TurnDriver::refuse does before measuring), so a record the log could still fit by measurement is refused at the edge.
- Founding record included in the max (same class of record).
