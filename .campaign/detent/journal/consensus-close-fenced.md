# consensus-close-fenced: journal

State: waiting on jobs 261118-054326-3bec50 261118-054327-2a5c32 261118-054328-5e174f 261118-054328-b1c614 261118-054329-dfa037 261118-054330-ad2586 261118-054331-529838 261118-054332-ffe478 261118-054333-0cc7e6 261118-054334-479f45
Quest: .campaign/detent/quests/consensus-close-fenced.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Code at consensus:178150a (pushed). At 0b48da4 shutdown_idles failed everywhere ("the loop waits for the
barrier") because my Loop stopped the turn driver and the Driver with one token, so the turn driver
ended its waits and nothing was left to watch: the test's expectation was wrong, not the fix (the
fix's repro, Xapiand's seed 2, passed in cc8bb4, which ran one seed: SIM_DRIVER_SEEDS is the seed
count and the third argument the first seed). The A6 mutant had failed on that wrong check, so it
proved nothing. TSan and the suite timed out because created_bytes (20,000 proposals) ran in the
default all-cases run: now opt-in by name. Fix: Loop has a separate driver_stop; shutdown_idles stops
only the Driver (the turn driver keeps committing, as in Xapiand's simulation) and exits with a
failure at once if the loop does not idle within 20,000 turns. Jobs at 178150a: plain A1-A3+A6 3bec50,
created_bytes report 2a5c32, ASan 5e174f, TSan b1c614, A6 mutant dfa037 (must fail on "the loop idles
after shutdown", not on a build or another check: read its log), N3 mutant ad2586 (reached + nonzero),
settle mutant 529838, erase mutant ffe478, Xapiand repro 0cc7e6 (SIM_DRIVER_SEEDS=24 0 1000 1, their
driver_simulation.cc via --ref SIM=ecb2a37; seeds 1-24 must pass; it printed no "reached the test"
because it is not a mutant job), suite 479f45.
If a job fails, read its log; then fill the Evidence table from `campaign brief` and set "ready for
review".

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
