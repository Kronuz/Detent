# consensus-close-fenced: journal

State: waiting on jobs 261118-021905-0952ab 261118-021907-3d767d 261118-021909-dc9230 261118-021911-f17875 261118-021757-fd5aa8 261118-021759-37b257 261118-021800-0604b4
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

1. When the jobs finish: read plain 0952ab's output (created_bytes prints process heap per row open,
   closed and after the log forgets it, net of the simulated disk, for 0 and 10 proposals per row).
   Put the figures in the Result split Driver part / journal part; mark the journal part owned by
   journal-reopen-closed A4b.
2. Confirm: mutant jobs 3d767d-dc9230-f17875 and 37b257 ran the test (log shows "reached the test")
   and exited nonzero; TSan 3d767d is the A4 TSan job (ids: plain 0952ab, tsan 3d767d, settle mutant
   dc9230, erase mutant f17875, ASan fd5aa8, unqueued mutant 37b257, suite 0604b4).
3. Fill the Evidence table from `campaign brief`; set "ready for review".

## Blocked on

Nothing. Lead ruled option (b): journal-reopen-closed A4b adds a Host release for fenced or
uncheckpointed Stores that keeps records durable; a follow-up quest has Worker::close call it.
worker.h untouched. A3 is rescoped to the Driver's rows.

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
