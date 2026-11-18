# consensus-close-fenced: journal

State: waiting on jobs 261118-000951-aad80b 261118-000959-776077 261118-001006-e17e93 261118-001028-7ef567 261118-001036-648cac 261118-001042-9c4249
Quest: .campaign/detent/quests/consensus-close-fenced.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Code at consensus:c5d15d9 (pushed). All earlier jobs are void: my command piped the build into
`grep` under `set -e`, which exits 1 on a clean build, so plain/ASan/TSan never ran the test.
Resubmitted without the grep: plain A1-A3 aad80b, ASan A4 776077, TSan A4 e17e93, suite A5 9c4249,
fails-without under ASan (build failure exits 0 so it cannot pass vacuously; prints "reached the
test"): settle() 7ef567, pump() erase 648cac. CI at 1c4f173: ASan, FreeBSD, arm64, clang, gcc, macos-arm64
green with the test; TSan failed at link (my operator new vs TSan's runtime; fixed with COUNT_BYTES);
macos-x86_64 groups_upload is the known flake.

## Next

1. Read the four jobs; fix what fails.
2. Read the two fails-without jobs.
3. Fill the Evidence table; ready for review.

## Blocked on

Nothing. The lead put CMakeLists.txt in my file set; the registration is committed (consensus:c06510a).

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
