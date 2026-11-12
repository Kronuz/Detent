# logger-wheel-slim: journal

State: waiting on jobs 261112-071159-b0cda2 261112-071201-563e40 261112-071203-028190 261112-071206-e00583 261112-071208-a53db3 261112-071210-60b58a
Quest: .campaign/detent/quests/logger-wheel-slim.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

A1 committed (logger:123f5e2: docs/perf/wheel-ab.md "Arm D"; sizeof A 184, C 264, D 192;
261112-062455-24c582). Arm D is bench/wheel_ab/d/ (copy of C with the entry replacing
ScheduledTask), built by its own d/CMakeLists.txt against wheel@45f98eb. Jobs running: check-d
261112-062659-2d9cc1, tsan-d 261112-062710-0f5517 (A2 correctness). Pattern 3 rows (D at
8094113, first iteration D1, 40 rounds, exclusive) queued `--after` both: P=1 261112-063105-c97a20,
P=4 261112-063108-826152, P=14 261112-063111-1279f7. If a check fails the rows are skipped.

## Next

0. check-d 261112-062659-2d9cc1 and tsan-d 261112-062710-0f5517 passed (at 123f5e2; 123f5e2..8094113
   touched only summarize.py and the doc, no arm code). The old pattern 3 rows exited 127 (the
   `--after` ids landed in the command). Resubmitted at 8094113 without ordering, exclusive, 10 min:
   pattern 3 P=1,4,14: 261112-071159-b0cda2, -071201-563e40, -071203-028190; pattern 2 P=1,4,14:
   261112-071206-e00583, -071208-a53db3, -071210-60b58a. Then `summarize.py --paired` per row, Verdict D.

1. Read the check and tsan jobs; fix D if it breaks an order or an exactly-once.
2. Read pattern 3 rows (queued); submit pattern 2 rows (`run.sh row-d 2 1|4|14`) once a row's duration is known (10 min cap).
3. Verdict D in docs/perf/wheel-ab.md; at most five measured iterations on D's layout.

## Blocked on

Nothing.

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

1. The quest's file set is bench/wheel_ab/** and docs/perf/wheel-ab.md, so the library's
   CMakeLists.txt stays untouched: arm D builds from its own bench/wheel_ab/d/CMakeLists.txt,
   which add_subdirectory()s the repository root for (A) and builds D against wheel@45f98eb
   (A and (B) in that build link the same wheel; only B uses it).
2. D drops the filing-order tie-break of C's sort key: key (due, created) from the entry's
   deadline and the record's created time. Equal keys need two lines in the same nanosecond; the
   order check counts inversions.
3. "No worse than A" (D72) is stated in the Rule for (D) before data: r(median) <= 1.10, r(p99) <=
   1.10 (lead ruling msg: not 1.10 + w; w and the interval reported beside it, a straddle of 1.10
   reported as such, for median and p99), heap <= 1.10 A. The Rule's win/loss/no win stays beside
   it, under D77's statistic, with the same p99 bound.
4. One round = one process with one warm-up round and one measured round, so a per-round paired
   ratio exists (the harness's 8-round process has no per-round output); 40 rounds a row.

5. p99 bound for D is r <= 1.10 (lead ruling), applied in the doc "Arm D", summarize.py (win and
   no-worse) and ruling 3. Validation of `summarize.py --paired` on a real log: the A vs C pattern 3,
   P = 1 log of logger-wheel-ab (261112-040552-6a1eea) turned into rounds gives r 1.245 (median),
   1.631 (p99), heap 1.372, the same as that row's published values. The first row jobs
   (261112-062939-8e3351, -062944-bbdd36, -062949-8104aa) were cancelled unrun and resubmitted at
   8094113 as 261112-063105-c97a20, 261112-063108-826152, 261112-063111-1279f7.
