# consensus-close-fenced: journal

State: waiting on jobs 261118-021458-8f5b01 261118-021757-fd5aa8 261118-021759-37b257 261118-021800-0604b4
Quest: .campaign/detent/quests/consensus-close-fenced.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Review CHANGES at c5d15d9. Pushed consensus:26f7dbf. F1 done: docs/DRIVER.md lines 83, 91, 135 and a
driver.h comment now say close erases the row, the id opens again, and `dropped(id)` for an id not
open is the Driver-wide count of drops on ids not open (kept as is, documented); line 135 also states
the journal's refusal of create/recover until the host reopens (N1). F2: `created_bytes` (report only)
prints, for 2,000 created rows with 0 and 10 proposals, process heap per row open / closed / after the
turn log forgets the group, net of the simulated disk's heap (the MemoryIO Disk lives on the counted
heap, so the reviewer's 2,398 B probably includes it). Job 8f5b01 runs it; suite b2a48d at 26f7dbf.
By reading journal 7c08540 what stays after a Driver close (it never calls Journal::close, so
Host::close_group and TurnDriver::forget never run): Host::shards_ node (GroupEntry) and Host::opened_
node, fixed per id; TurnLog::sequences_ node and index_[group] (one map node per live record), which
scale with the group's records. Fill the numbers from 8f5b01 into Result and the report to the lead.
Not done: N3 (a test erasing a still-queued group).

## Next

1. Read the four jobs; fix what fails.
2. Read the two fails-without jobs.
3. Fill the Evidence table; ready for review.

## Blocked on

Lead: who closes the journal group when a Driver row closes (F2). Owner: the Worker (worker.h:1139
`kronuz::journal::Store store_`); `Worker::close()` (worker.h:179) only sets `closing_`, and nothing
calls `Store::close()` (store.h:389) -> `Journal::close()` (journal.h:355) -> `Host::close_group` ->
`TurnDriver::forget`. That call cannot simply be added in driver.h or worker.h: Journal::close
requires a healthy store (a fenced row throws), no pending replacement, the last record durable, and
a checkpoint covering every batch (`frontier_.sequence == base_sequence`). A lone row with batches
past its last checkpoint, or any fenced row, fails it, and `TurnLog::forget` on such a group would
let retention reclaim live uncheckpointed records (data loss). TurnLog `index_` is the retention's
live-record index, correct while the records are live. Options: (a) Worker::close writes a covering
checkpoint, then store_.close() (healthy rows only; needs the application's capture, so not automatic);
(b) a Host-side release that drops the in-memory per-group state (shards_/opened_/sequences_/index_
and the recover guard) while the records stay durable and recoverable (the journal-reopen-closed
quest). Recommendation: (b). worker.h is needed only for (a); I do not change it.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| <A1> | <from `campaign brief`> |

## Result

<When ready for review: what changed, its measured effect with numbers and the jobs behind them,
and what it leaves open. Results and how it works go in the docs of the repository it changed
(docs/perf/, docs/mechanisms/, a library's own); name them here. `campaign map` shows this.>

## Rulings I made

1. Erase in pump() after step(), not inside close_step: close_step runs under step()/settle()/
   fail_all(), which still hold `g` and the Worker reference.
2. The ready list is unlinked by a scan only when the node is still queued at erase (a witness's
   own apply marks it ready in the same step); the common close leaves it unqueued.
3. Dropped the closed-row branches (open's erase, live(), close(), state()): a closed group no
   longer outlives its step, so they were dead. Added `groups()` for A3.
4. Recover after close (CI: "recovered on this host; reopen the host to recover it again"): a real
   journal contract (journal.h recover_shared: an id whose records the turn log handed to a Store of
   this host recovers only after the host reopens), not Driver state; close does not clear it and I
   did not touch it. The fenced-row test reopens with open, not recover.
5. Bytes left after 10,000 closes (about 120-180 B a row): Host::group_io keeps an expired weak entry
   per handed IO in Host::ranges_ until its next group_io call purges them (journal host.h:284), so
   the retention is the Host's, outside the Driver; Driver rows are erased. The test opens one more
   row (that purge call) before counting, and prints the figure before and after. Whether the Host
   should purge eagerly is a journal question for the lead; not changed here.
6. consensus_mutants timed out in the suite at 600 s: it had printed 8 of 14 faults (mutant 9, "a
   leader grants pre-votes", was running); no hang evidence, it builds each fault three ways under
   -j4 load. Its mutants edit core.h only. I set its ctest TIMEOUT to 1800 (CMakeLists, in my file set).
   If the suite still times out at 1800 it is a hang: read its log.
