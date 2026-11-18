# consensus-close-fenced: journal

State: waiting on jobs 261118-063956-fff668 261118-063958-96c6ec 261118-064000-5e01ed 261118-064002-13f33c 261118-064120-75b4b1 261118-064122-6a437c 261118-064124-3e5bb0 261118-064126-e68ae2 261118-064218-d255a1 261118-064220-b8cb43 261118-063950-78323a
Quest: .campaign/detent/quests/consensus-close-fenced.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Code at consensus:50b91ae (pushed): 9118cfa fixes shutdown_idles, then main merged (CMakeLists
conflict: kept main's mutants TIMEOUT line). The ten jobs at 178150a never ran (exit 127: the
submit put `--quest ...` inside the command).

Why shutdown_idles failed at 0b48da4 and 178150a (reproduced locally, not read): the Driver's only
in-flight frame was the barrier watch. Shutdown cancels it inline, in_flight drops to 0, run()
returns with the turn still held: no spin with or without the fix, so the case proved nothing
(DBG: in_flight 1 before stop, 0 and driver_done after). Xapiand's spin needs IO shutdown cannot
cancel (an issued primitive completes first) keeping the loop alive while the cancelled watch
re-arms. The case now holds a second group's checkpoint artifact IO beside the staged record, and
stops the turn driver and the Driver with one token, as Xapiand does. Local: fixed passes (in_flight
2 after stop, loop idle); unfixed watch fails "the loop idles after shutdown" within 20,000 turns.

Xapiand repro, local, per seed (SIM_DRIVER_SEEDS=1, first seed s, 1000 steps; their
driver_simulation.cc from ecb2a37 on this branch): fixed, 0 of 24 seeds spin; unfixed watch, seed 2
spins (others running). Seeds 1, 3, 7, 8, 13, 18-21, 23 fail "did not apply a new entry on every
data voter after healing" with the fix; seeds 1 and 3 fail that way without it too, so it is their
WIP simulation's, not a spin: reported to the lead, not fixed here. The suite case runs 5 s.

Jobs at 50b91ae: plain A1-A3+A6 fff668, created_bytes report 96c6ec, ASan 5e01ed, TSan 13f33c,
mutants (setup failures exit 0, so only the test can fail them; read "reached the test"): A6 watch
75b4b1, N3 unqueued 6a437c, settle 3e5bb0, erase e68ae2; Xapiand repro fixed d255a1 (0 spins over
1-24) and unfixed b8cb43 (fails-without); suite 78323a; CI running.

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

1. When the jobs finish: read each log (mutants: "reached the test" then the failing check; repro:
   the per-seed lines), and the created_bytes report's figures into Result (Driver part / journal
   part, journal part owned by journal-reopen-closed A4b).
2. Fill the Evidence table from `campaign brief`; set "ready for review".

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
