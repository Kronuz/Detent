# consensus-batch-turn-room: journal

State: blocked on lead: A2 suite fails on 7 configurations the check correctly refuses
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

A2: suite 261119-233230-df57ad at 0c52e87 fails 7 tests, all on the new refusal. Each configuration can truly produce a record larger than one segment (the follower persists an AppendRequest of up to rpc_entries/rpc_bytes; pipeline_test shows frames go up to rpc_bytes), so the bound is not too strict. Room on 16 KiB segments with 1 KiB blocks is 13661 record-cost bytes; no segment can hold more than 16384 bytes whatever the bound's conservatism.
- wave_open, wave_open_256, wave_open_cut (test/wave_open_test.cc:65): rpc_bytes 16384, rpc_entries 8: batch 16598 bytes, cost 16667 > 13661 (F6's own case).
- transport groups_driver, groups_joiner, groups_lagging, groups_relayout (test/transport/groups_test.cc:997, :1080): default Limits (rpc_bytes 2 MiB, rpc_entries 256): batch 2102574, cost 2102643.
Follower batching and the founding record play no part (none set; founding is ~1 KiB). Behavior narrowing: configurations accepted before now throw at Worker construction. I did not touch those tests (outside my file set). Options: (1) recommended: lead assigns the test fix, wave_open rpc_bytes <= 4096 (or segments >= 64 KiB) and groups_test limits with rpc_bytes 4096, rpc_entries 8 (or larger turn_options); (2) drop the check (F6 stays open); (3) bound by the largest command only (leaves merged appends unchecked).

CHANGELOG.md is held by another quest; entry text for the lead to add under the current Unreleased section:

> Worker: a group on a journal host refuses at construction (`std::invalid_argument`, "worker batch exceeds the host's turn room") a configuration whose largest record, a batch of `Limits::rpc_entries`/`rpc_bytes` (or `WorkerLimits::follower_batch_*` when larger) or the founding record, exceeds one empty segment of the host's turn log. Before, it reached the journal and fenced its group at the first append.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | jobs 261119-233216-39451c (fails at test-only 1421fb5), 261119-233229-c53762 (passes at 0c52e87) |
| A2 | job 261119-233230-df57ad, CI after push |

## Result

worker.h `validate_turn_room` (host constructor only): record_cost(max(maximum_initialization_size, storage_batch_size(true,true,max(rpc_entries, follower_batch_entries), max(rpc_bytes, follower_batch_bytes)))) must not exceed journal's `turn_room(segment_bytes, block_bytes)`, the driver's own refusal bound. Test: `batch_turn_room` in worker_test.cc (also `--turn-room`). Docs: docs/DRIVER.md (Seams comment).

## Rulings I made

- Bound is the operating limits, not `RetainedStatePolicy::batch_*`: the retained defaults are the 8 MiB ceilings, which would refuse every host with small segments (all driver tests use 16 KiB). A caller-submitted merged Receive up to the retained bound can still exceed the room and fences its group only (as before); the Worker's own follower batching is covered.
- Used the conservative `turn_room` bound (as TurnDriver::refuse does before measuring), so a record the log could still fit by measurement is refused at the edge.
- Founding record included in the max (same class of record).
