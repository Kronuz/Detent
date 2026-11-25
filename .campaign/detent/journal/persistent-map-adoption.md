# persistent-map-adoption: journal

State: waiting on jobs 261125-055021-ea057c 261125-055028-de0f76 261125-055647-e5334d 261125-055718-b49932 261125-055733-bfba29 261125-055734-d461d7
Quest: .campaign/detent/quests/persistent-map-adoption.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Waiting on the criterion jobs (ids under Evidence); then A6 analysis
(`.scratch/pm/ab.py` over the A6 job log) and the A9 numbers into persistent-map's
`docs/perf/costs.md` (drafted, uncommitted in ~/code/Kronuz/persistent-map).

## Next

1. A6: read 261125-055021-ea057c with ab.py; attribute or bring the gap to the lead.
2. A9: fill persistent-map docs/perf/costs.md from 261125-055028-de0f76; push its main.
3. EX-4 row in docs/EXTRACTION_CHECKLIST.md (A7); asan/tsan from CI at the head.

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | job 261125-055718-b49932 (queued; grep for the tree body, public-pin fetch check, map tests). 261125-055700-53c71a cancelled: its subbuild grep does not hold under CMake 4's direct population |
| A2 | [manual] persistent-map:4045cbb, CI run 45624822026: linux x86-64 gcc and clang, linux arm64 gcc, macOS arm64 and x86-64, FreeBSD, ASan+UBSan all green (TSan off by the library's single-executor design); main fast-forwarded to it |
| A3 | pins 261125-055734-d461d7, native 261125-055733-bfba29 (queued/running at Detent:69cb849) |
| A4 | native 261125-055733-bfba29; asan/tsan from CI on the pushed head |
| A5 | job 261125-055647-e5334d (queued): baseline a8bf087 private tree vs head, cmp of checkpoint.bin, export-0.bin, export-1.bin |
| A6 | job 261125-055021-ea057c (queued, exclusive): one binary, private-a/private-b/library rotating, 9 rounds x {100K/200/1/16 at 1024 and 16384 turns, 100K/34, 1K/200, 1K/34 at 16384}; analyse with .scratch/pm/ab.py |
| A7 | <after A6/A9> |
| A8 | [manual] persistent-map:2685cf8 (erase_range; oracle with erase_range, lower-bound walks, held part-advanced cursors on 16 versions, invariants after every op, exact drain turns, failure injection at every allocation/comparison/copy of set/erase/erase_range), f70aa6b (bench), 4045cbb (implicit default ctor); CI run 45624822026. Inverted-range guard removed: test fails (invariant at line 111), run locally |
| A9 | job 261125-055028-de0f76 (queued, exclusive): bench/bench.cc at persistent-map:4045cbb, 3 rounds to 1M keys |

## Result

<When ready for review: what changed, its measured effect with numbers and the jobs behind them,
and what it leaves open. Results and how it works go in the docs of the repository it changed
(docs/perf/, docs/mechanisms/, a library's own); name them here. `campaign map` shows this.>

## Rulings I made

1. main's private tree grew `erase_range` (Detent:53abd637) after persistent-map:938d65e, so the
   library takes it (persistent-map:2685cf8). An empty or inverted range returns the same
   version; the private tree corrupted the tree on an inverted range (no caller passes one).
2. persistent-map's `explicit PersistentMap(Compare = {}, Alloc = {})` made `map = {}` (state.cc
   seal maps) fail to compile; the library gets an implicit default constructor (4045cbb) instead
   of editing state.cc, which is outside this quest's files.
3. `erase_range_matches_map` moves from test_namespace_positions.cc to the library's oracle.
4. A5 compares a seeded state's checkpoint and two exports built at the baseline commit (the test
   copied into it) and at the head, by cmp, rather than the 2026-10-28 fixtures, whose format main
   has since changed.
5. A6 builds both trees into one binary (the private header from the baseline commit), so the
   A/A arms run identical code; the 2026-10-28 runs used two binaries.

