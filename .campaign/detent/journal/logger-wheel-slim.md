# logger-wheel-slim: journal

State: ready for review
Quest: .campaign/detent/quests/logger-wheel-slim.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

All rows in; Verdict D written (logger:7f3b850, docs/perf/wheel-ab.md "Verdict D").

## Next

1. Review. If asked: more iterations on D's layout (budget: five measured; none used beyond D1).

## Blocked on

Nothing.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| A1 | artifact: docs/perf/wheel-ab.md "Arm D" (logger:123f5e2); sizeof A 184, C 264, D 192 (261112-062455-24c582) |
| A2 | check-d 261112-071531-bd8c2e, tsan-d 261112-071533-689a79; rows pattern 3 P=1,4,14: 261112-071159-b0cda2, 261112-071201-563e40, 261112-071203-028190; pattern 2 P=1,4,14: 261112-071206-e00583, 261112-071208-a53db3, 261112-071210-60b58a (all passed at 8094113edbb0; later commits are docs-only) |
| A3 | artifact: docs/perf/wheel-ab.md "Verdict D" (logger:7f3b850) |

## Result

Verdict D (docs/perf/wheel-ab.md): median r 0.98 to 1.02 on all six rows, heap 1.031 times A.
D is no worse than A on 4 of 6 rows; it loses p99 at P=1: pattern 3 r 1.469 [1.149-1.582], pattern
2 r 1.133 [0.982-1.424] (straddles 1.10). Outside the Rule: lag p99 4.9 to 19.6 times A at
pattern 2 and 6.5 times at pattern 3 P=14; LOG CPU 2.6 times at pattern 3 P=14. The owner
decides: accept the two p99 losses or keep logger's heap.

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
