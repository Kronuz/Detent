# consensus-close-fenced: journal

State: waiting on jobs 261118-040534-e39e8e 261118-040536-706b0d 261118-040537-300b61 261118-040539-5a1dfa 261118-040540-ea618e 261118-040542-cc8bb4 261118-040544-24d42c 261118-040545-2b4cb4 (+ erase mutant, see Doing)
Quest: .campaign/detent/quests/consensus-close-fenced.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Code at consensus:0b48da4 (pushed). A6 fix: driver.h watch() takes no token once stopping_ (a cancelled
watch ended at once and woke the group to watch again: the spin); test shutdown_idles bounds the loop's
turns after shutdown. N3: the first erase_queued passed its mutant (job 37b257 exit 0), so it never
reached the queued branch; rewritten to queue close ahead of the fence. Jobs at 0b48da4: plain A1-A3+A6
e39e8e, ASan 706b0d, TSan 300b61, fails-without A6 (old token) 5a1dfa, N3 mutant ea618e (must exit
nonzero after "reached the test"), Xapiand repro with --ref SIM=ecb2a37 cc8bb4 (their
driver_simulation.cc copied over, seeds 1-24), settle mutant 24d42c, suite 2b4cb4, erase mutant
(submitted last, `campaign job list`).

F2 numbers (plain job 261118-021905-0952ab, 2,000 created rows, process heap net of the simulated disk):
| rows | records/row | open | closed | after the log forgets them |
|---|---|---|---|---|
| 0 proposals | 5.0 | 6,569 | 681 | 207 |
| 10 proposals | 25.0 | 9,006 | 2,158 | 235 |
Driver part after close: 0 (its rows are erased, groups()==0; opened-only rows 3,658 open, 10 B left
after the Host's purge). Journal part: closed minus forgotten is the TurnLog's per-record index
(index_[group], one map node per live record): 474 B at 5 records, 1,923 B at 25, about 72 B a record
plus about 115 B fixed; the 207-235 B left after forget is the Host table (shards_ and opened_ nodes)
plus sequences_: fixed per id. The reviewer's 2,398 B included the simulated disk. Owned by
journal-reopen-closed A4b (Host release), not changed here.

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
