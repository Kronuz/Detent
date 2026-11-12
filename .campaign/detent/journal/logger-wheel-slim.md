# logger-wheel-slim: journal

State: waiting on jobs 261112-101934-9835ac 261112-101937-cd3834 261112-101939-91ce7a 261112-101942-4b0fb6 261112-101944-86316f 261112-101946-73c9a9 261112-101949-df1512
Quest: .campaign/detent/quests/logger-wheel-slim.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

A4 (D78): profile, then up to three changes to D.

Profiles (strict, `run.sh profile-d PATTERN P ARM`, `sample` 6 s at 1 ms) and counters
(`run.sh stats-d`, WAB_STATS build) at pattern 3 P=14, D: 261112-081713-5f3fc7 (A: 261112-081716-e81894),
stats 261112-082031-adcffd. Finding: of 1061 active LOG-thread samples, 521 are the memmove of
`std::vector<Deferred>::insert` in `file_deferred` (A: none; A's pop_heap is small). The insert is the
bypass of lines already due when filed (115K bypassed of 717K at P=14, mean 4.9 ms past due, batch up
to 29K, one pass expiring 117,760 lines): quadratic. Why many lines are due at filing: lines sit in
the rings until the LOG thread wakes (it sleeps to the next filed deadline), so lines called ~200 ms
before the wake are due when filed. P=1 pattern 3: 6588 bypassed (13%), batch up to 2047
(261112-082033-7add00); pattern 2 P=1: 42 bypassed, max expired 27 (261112-082035-21c273): no
bypass cost there; its profiles 261112-082040-0cdc5a (D), 261112-082045-8dd224 (A) pending.
P=1 pattern 3 profiles: D 261112-082038-4353a0, A 261112-082042-93b54d (pending).

Change 1 (logger:HEAD "the due batch is a binary heap"): jobs check-d 261112-082223-84e56a, tsan-d
261112-082227-45e3b3, stats 261112-082229-26596a, rows p3 P=1 261112-082231-ac506e, p3 P=14
261112-082234-9559fb, p2 P=1 261112-082236-9714e6.

Change 1 check-d 261112-082223-84e56a and tsan-d 261112-082227-45e3b3 passed. Coordinator: the
cause is filing, not the structure; change 2 (logger:e7a4f3f) caps the LOG thread's sleep at one tick
after a pass that filed lines (A has the same wake mechanism, so it files late too; its heap absorbs
it). Change 2 is on top of change 1. Jobs: check-d 261112-082650-0b5be2, tsan-d 261112-082714-8d6761,
stats p3 P=14 261112-082652-65999f, p3 P=1 261112-082655-1d3288, rows (c2 with c1, `row-d P P 40`)
p3 P=1 261112-082706-07b590, p3 P=14 261112-082708-f1c86e, p2 P=1 261112-082711-f34b89. Change 1
alone rows: 261112-082231-ac506e, -082234-9559fb, -082236-9714e6 (queued before change 2).
If c2's bypass is near zero: drop c1's heap (revert 6c29783), re-measure c2 alone.

Results so far (D/A, 40 rounds). Change 1 alone: p3 P=1 median 0.988 p99 1.438 [1.208-1.564] (still
a loss, 261112-082231-ac506e); p3 P=14 median 0.998 p99 1.034, lag p99 1.17x (was 6.5x), log CPU 1.18x
(was 2.6x) (-082234-9559fb); p2 P=1 median 1.003 p99 0.990 (-082236-9714e6: the earlier 1.133 was noise).
Change 2 (with 1): p3 P=1 1.019 / p99 1.031 [0.880-1.167] (w 0.274); p3 P=14 0.998 / 1.025, lag 1.26x,
log CPU 1.14x; p2 P=1 0.963 / 1.010. But bypass stays: P=14 28672 of 717K (c1: 54524 at c1's own
stats 261112-082724-b5f70b, c0: 114942), P=1 5120 (c0 6588). Trace (261112-100143-5b66af, P=1):
LOG slept 198 ms hot=0, woke 4.77 ms late; lines due in that 4.77 ms window were filed already due.
So change 3 (logger:HEAD, kWakeSlackNs = 8 ms in wake_for) wakes on a push due near the sleep end.
Change 3 jobs: check-d 261112-101934-9835ac, tsan-d 261112-101937-cd3834, stats p3 P=14
261112-101939-91ce7a, p3 P=1 261112-101942-4b0fb6, rows p3 P=1 261112-101944-86316f, p3 P=14
261112-101946-73c9a9, p2 P=1 261112-101949-df1512. If bypass ~0: revert change 1 (heap), re-measure.

## Next

1. Read change 1's rows against the Rule; a change 2 only if a loss remains (up to three, one at
   a time). Write "Arm D, fixed" (cause, each change and its numbers, final verdict per row, the
   lead's line). The tool refuses `--for A4` (artifact: journal row), jobs go with --quest only.

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
