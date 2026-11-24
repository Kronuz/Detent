# first-read-1ms: journal

State: ready for review; waiting on jobs 261124-155453-d2faac, 261124-155456-b57f68, 261124-155500-f39f9c, 261124-155507-a4b568 (A2), 261124-155516-489edc (A1), 261124-155204-3e4aa2 (mutant) and CI at `Detent:e52669a7`
Quest: .campaign/detent/quests/first-read-1ms.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Layout 3 is committed: `Detent:91d391cf` (PagedTable, per-page verification with VerifiedPages marks,
core on it, test_table_pages, a damaged-page test in test_reader_core), `Detent:e11f470c` (format doc,
writer limits), `Detent:160d37d7` (the gate reports a cell main cannot run), and the mapped table read.
Local tests pass: test_local_table_pages, test_reader_core, test_reader_files, test_local_managed_reader,
test_local_disk_store, test_local_read_counters (counters on), test_agent_store_crash and 8 more; only
test_agent_store fails (Blocked on 1).
Mutant `Detent:1bb490d8` (the page digest is not checked) is ready for a --fails-without job.
Screens, strict beside light holds, branch median vs main (us): stopped 1 ns 930 vs 1,224; stopped
1,000 ns 1,111 vs 2,880; running 1 ns 1,053 vs 1,309; running 1,000 ns 1,297 (main cannot run it).

## Next

Jobs at `Detent:e52669a7`, the final head (test_agent_store updated for layout 3; it passes locally):
- A2, exclusive, one cell each against main: 261124-155453-d2faac (stopped 1), 261124-155456-b57f68
  (stopped 1,000), 261124-155500-f39f9c (running 1), 261124-155507-a4b568 (running 1,000; main's
  publisher cannot run it, so the cell reports the branch alone).
- A1 profile: 261124-155516-489edc.
- Mutant `Detent:1bb490d8`, --fails-without: 261124-155204-3e4aa2 (a build failure would also exit
  nonzero; read its log).
- A3: CI on the pushed branch (`campaign verify --for A3` reads it).
Cancelled at `Detent:bfad7733`: 261124-155138-5da24c, -155140-00faf4, -155142-9a0f4d, -155155-541c14,
-155200-b45b23. 261124-155135-e8bc6d had started; submitting d2faac stopped it as superseded (the
tool did it, not a cancel).
Strict screen of the mapped table, beside light holds, branch vs main medians (us): stopped 1 ns
745 vs 1,072; stopped 1,000 ns 846 vs 2,349; running 1 ns 680 vs 860; running 1,000 ns 600 (main
refused). Pooled p99s under that load were 1.4-2.0 ms; the exclusive runs decide A2.
Local scratch: `.scratch/fr`, `.scratch/main-tree` (detached main worktree), build dirs.

## Messages

- msg-3 (lead: design passed, files widened): built per its three conditions. 1: the marks are
  `VerifiedPages` (one line in the design says why it fits). 2: `static_assert(table_entry_bytes(255) <
  table_page_bytes)` in store_table.hh and test_table_pages' `bound` (100 entries of 255-byte names,
  ten to a page). 3: one value test per lifecycle row in test_table_pages, the corrupt page included,
  with mutant `Detent:1bb490d8`; test_reader_core's `damaged_page` reads around a damaged page.

- msg-1 (lead, Blocked on 1-3): (1) option a taken: design section and lifecycle rows in
  docs/perf/first-read-1ms.md "Design: table pages" (`Detent:aacf37cf`); files outside the set it
  touches are listed there and in State. (2) changes 2 and 4 committed with their test updates
  (`Detent:dee58172`, `Detent:9c8a44ee`). (3) generation_writer.cc: next step, after the table code.
- msg-2 (lead, sha256 AVX2): measured once, strict screen, branch at the pin against sha256 main:
  1,000 ns 1,505 to 1,252 us median (-303, floor 265); 1 ns -19 (floor 67). Recorded in
  docs/perf/first-read-1ms.md "sha256 main". Option a stays the fix.

## Blocked on

Nothing. Item 1 (test_agent_store.cc) was widened by the lead and fixed in `Detent:e52669a7`.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), name every job you examined, failures included (`campaign brief` lists failed
jobs no journal names), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| A1 | 261124-134303-81190d (strict, probe at `Detent:3f0d12cb`, the reader code main has): see "Profile (A1)"; rerun at the final head for the after-profile |

## Profile (A1)

Job 261124-134303-81190d (strict beside light holds, main's reader code at `Detent:3f0d12cb`), 200
reads per cell, median of each marked step (us):
- 1 ns stopped: median 1,361, p99 12,346 (one stall). Trust walk 567 (42%), attach 236 (selection
  58, lease 53, image 51, `.read` 64, root decode 10), first-touch pages 141 (SHA-256 verify 113,
  node checks 28), segment open and map 103, `selected` 96, `change` 76, handle 31.
- 1,000 ns stopped: median 3,082, p99 4,915. `selected` 1,837 (60%: its SHA-256 and decode), trust
  walk 534 (17%), attach 330, segment 110, `change` 75.
- 1 ns running: median 1,369, p99 2,462; the same shares (trust walk 518).
- 1,000 ns running: skipped, the publisher cannot start (Blocked on 3).

## Profile (local, before the A1 job)

Probe marks (`docs/perf/first-read-probe.py`), 1,000 namespaces, stopped, strict beside light
holds at `Detent:d599a61d`, 100 reads: median 3,659 us, p99 4,859 us. Top costs (medians):
`selected` read 1,987 us (SHA-256 of 141 KB 1,154 us, decode 310 us), trust walk 754 us (12
components, each fstatat, openat, fstat, ACL, fstatat, close), attach 420 us (select_entry 116,
lease 110, image 72, metadata 102), segment open and map 150 us, `change` open 98 us.
Micro (strict): directory openat O_RDONLY 9.7 us, O_SEARCH 1.6 us; fstatat 4.8, fstat 1.2, ACL
3.1, file openat+close 18.7. The host ran at a 68% CPU speed limit under load 30-90, so the
absolute numbers are inflated; the shares are the finding.

## Screens (local, strict beside light holds, `.scratch/fr/ab.sh`, stopped, 6 pairs x 200)

Median of run medians, us, main against branch; paired difference (floor):
- Change 1 (O_SEARCH, `Detent:5d879068`): 1 ns 1,386 / 1,140, -243 (71); 1,000 ns 3,149 / 2,967,
  -214 (121).
- Changes 1+2: 1 ns 1,375 / 1,024, -333 (30); 1,000 ns 3,169 / 2,918, -348 (155). Held (Blocked 2).
- Changes 1+3+4 (3 = `Detent:e7dc35d6`): 1 ns 1,491 / 1,200, -317 (96); 1,000 ns 3,384 / 2,879,
  -527 (162). Change 4 held (Blocked 2).
The host ran loaded (load 30-90, CPU speed limit 68%): main's 1 ns median is 1.39-1.49 ms here
against about 0.6 ms on an exclusive hold, so these are screens, not A2.

## Result

So far (screens and benches; A2 pending):
1. `Detent:5d879068`: trust walk opens directories with O_SEARCH on macOS: -243 us (1 ns), -214 us
   (1,000 ns) against main, strict screen.
2. `Detent:e7dc35d6`: table slots checked by sorting, not a std::set.
3. `Detent:182bb900` and `Detent:d20bbadb`: warm lookups 19-20% faster than main hot (737.5 to
   592.5 ns p50, 1 thread) and 17% scattered (1,198 to 989), strict beside light holds; on an
   exclusive hold that puts hot back at about the pre-S6 393 ns.
4. Tooling: `first-read-gate.sh profile-prepare|profile`, `first-read-probe.py`,
   `benchmarks/reader_bench_ab.sh`; the gate links and runs on main again (legacy-h removed
   `local_client`). Results: `docs/perf/first-read-1ms.md`.
Open: the 1,000-namespace cells (table SHA-256, Blocked on 1), changes 2 and 4 (Blocked on 2),
the running 1,000-namespace cell (Blocked on 3).

<When ready for review: what changed, its measured effect with numbers and the jobs behind them,
and what it leaves open. Results and how it works go in the docs of the repository it changed
(docs/perf/, docs/mechanisms/, a library's own); name them here. `campaign map` shows this.>

## Rulings I made

- R2: change 2 keeps the post-open name check (`test_trusted_paths` "raced" pins it); only the
  pre-open `fstatat` goes. Change 3 refuses the same tables; a duplicate slot is now found after
  the whole table decodes, not at its entry (one TableRefused either way, D43).
- R3: the A1 profile ran main's reader code (`Detent:3f0d12cb`, before any change) on purpose:
  it names the costs to remove; its rerun at head is the after-profile.
- R4: the hot path is fixed here (paged-decode-one-shape was dropped; the code is in
  `src/detent_local` and `include/detent_local`).
- R1: the gate runs the reader core in both trees once a tree has `src/detent_reader` (reader-s6
  landed): this quest's A/B is core against core; main before reader-s6 still runs its Client.
