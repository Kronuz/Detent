# consensus-close-fenced: journal

State: waiting on jobs 261118-091954-0c01fa (suite at a699c96, the merge of main 42019eb); the other evidence jobs are reused at a699c96 (only the merge since 901d9ce)
Quest: .campaign/detent/quests/consensus-close-fenced.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Review fix round 2 (last), at consensus:901d9ce (pushed), from the correctness review at 50b91ae:
1. F1 (blocking, shutdown hangs): the watch takes `shutdown_.token()` again, and step starts no
   watch once `stopping_` (driver.h step, watch). Shutdown cancels the watch, its callback wakes
   the group once, and the step does not re-watch: no spin, and run() does not wait for a barrier
   no turn driver writes. New case `shutdown_ends` (the review probe's `hang`): a higher-term
   VoteRequest stages a record after the turn driver returned while h's checkpoint IO is held;
   run() ends once that IO lands, the record stays `TurnDriver::staged`. DRIVER.md "Shutdown
   order" says run() does not wait for a turn barrier after stop.
2. N1: shutdown_idles checks the record staged before shutdown is durable (sequence advances,
   not staged) and that the proposal and checkpoint complete with task::cancelled.
3. N4: fenced_in_flight expects `fenced` only.
Local (ASan Debug): all 76 checks pass; shutdown_ends at 50b91ae's driver.h fails "the Driver's
run ends once the IO lands: in_flight 1"; the guard removed fails shutdown_idles "the loop idles
after shutdown with the barrier pending" (the spin).

Jobs at 901d9ce: plain 7a71bf, ASan 3e11d5, TSan 925f6c, created_bytes db 0b29b6; mutants
(fails-without, read "reached the test"): A6 guard removed cfcb5b, F1 = 50b91ae's driver.h on
shutdown_ends db1d26, N3 3b7683, settle ff3bc1, erase aa0fea; Xapiand repro fixed 6d7d6c (also
runs SIM_DRIVER_SEEDS=24 from seed 1 once) and guard removed 990ff9; review probe 369ac2 (expects
`hang: RESULT driver_done=1`); suite 8ec065 (consensus_transport_idle still fails until
transport-idle-budget lands: ignore only that).

## Lead's report at 50b91ae

Passed: plain, ASan, TSan, three mutants (reached the test, exit 134), A6 mutant 75b4b1, repro
d255a1 (0 spins) and b8cb43 (spins without the fix). CI green but freebsd (pkg fetch; lead reran).
Suite 78323a fails consensus_transport_idle (per host 0.2% over 52 KB + 2P x 44 KB at loops=4).
Attribution: the branch differs from main 5923b6c only in driver.h, DRIVER.md, driver_close_test.cc
and its CMake registration; no transport source or bench includes driver.h (only bench/driver_idle.cc),
so consensus_transport_idle builds from identical sources. Jobs: transport_idle x3 at main 1fb596,
at head e7e9cf. Result: both fail 3 of 3 with the same bytes (loops=4: 154,504 /
254,021 / ~449,700 B per host, 108-109% of the fit; "an idle connection end within 48 KB, by the fit
(loops 4)"), so it is pre-existing at main 5923b6c, not this quest's. (Lead named main "4ef...": origin/main is 5923b6c, no 4ef commit exists.)

## Next

1. When suite 0c01fa finishes: it must pass fully (A5). Then set "ready for review".

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
| A1 | 261118-080701-7a71bf; fails without: 261118-080714-ff3bc1 (settle mutant, reached the test) |
| A2 | 261118-080701-7a71bf |
| A3 | 261118-080701-7a71bf, 261118-080709-0b29b6; fails without: 261118-080715-aa0fea (erase mutant) |
| A4 | 261118-080707-3e11d5 (ASan+UBSan), 261118-080708-925f6c (TSan); fails without: 261118-080713-3b7683 (N3 unqueued mutant) |
| A5 | suite 261118-091954-0c01fa at a699c96 (queued; transport-idle-budget landed at 42019eb, so it must pass fully). CI at a699c96 running |
| A6 | 261118-080701-7a71bf, 261118-080707-3e11d5, 261118-080708-925f6c, 261118-080716-6d7d6c (Xapiand repro), 261118-080719-369ac2 (review probe); fails without: 261118-080710-cfcb5b (guard removed), 261118-080711-db1d26 (F1 mutant), 261118-080717-990ff9 (repro spins) |

Messages: msg-71 is A6, done above.

Review at 50b91ae: F1 fixed at 901d9ce, F1=261118-080711-db1d26 (shutdown_ends at 50b91ae's driver.h); N1, N4 fixed in the test; N2 (`~Driver` with work in flight) and N3 (quest text) not changed: N3 is the lead's; N2 is non-blocking and F1's fix ends run() in that state.

## Result

consensus:50b91ae. `close(id)` erases the row on an open, fenced, half-opened or unknown id, and
the id opens again (driver.h pump/erase; docs/DRIVER.md close row). After shutdown a group awaiting
its turn barrier is watched once, uncancellably, so the loop idles instead of spinning (A6;
Xapiand's 15 spinning seeds of 24 go to 0). Bytes (96c6ec, 2,000 created rows, process heap per
row net of the simulated disk): 0 proposals: open 6,625, closed 729, after the log forgets 303;
10 proposals: 9,062 / 2,206 / 331. Driver part after close: 0 (groups()==0; opened-only rows
3,666 open, 10 after the Host's purge, fff668). The rest is the journal's (TurnLog index and Host
table), owned by journal-reopen-closed A4b. Open: consensus_transport_idle (transport-idle-budget);
Xapiand's simulation fails its post-healing check on seeds 1, 3, 7, 8, 13, 18-21, 23, of which
1, 3, 7 and 13 also fail without the fix (their WIP sim).

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
7. F1: the reviewer's option (a), which the lead assigned. One guard (no watch once stopping) and a
   cancellable token; the cancelled watch's callback still marks the group ready, so its step
   sees a barrier that landed (the blocking entry's rearm path) and the A6 mutant is that guard.
