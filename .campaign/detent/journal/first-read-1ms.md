# first-read-1ms: journal

State: ready for review (fix round 3; code `Detent:d12f2887`; jobs queued there, see Evidence)
Quest: .campaign/detent/quests/first-read-1ms.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Fix round 3 (review Round 3, CHANGES at `Detent:28a8d276`), under V5: table first, class sweep.

### `find` cases (V5 table, before any code)

Every `find` a reader makes, 1,000 entries; ns a lookup. Main is `StoreTable::find` on the decoded
table (main verifies the whole table at open: `selected` 1,837 us of A1's profile). Head is
`Detent:28a8d276`. "hot" is a marked page (verified, offsets kept); "cold" is a page's first touch
(hash and checks; with no account every reach is cold). Strict 261124-221108-47be0b (medians of 10
pairs) where it exists; the rest is a light screen at `Detent:28a8d276` (extended
table_find_bench: 255-byte names, cold), to be replaced by the strict A/B.

| Width | Name | Path | Page | Main | Head | Head - main (floor) | Source |
|---|---|---|---|---|---|---|---|
| 16 | present | paged | hot | 176.3 | 153.8 | within (0.87x) | 47be0b |
| 16 | absent | paged | hot | 182.2 | 179.4 | within (0.98x) | 47be0b |
| 40 | present | paged | hot | 152.3 | 173.8 | +16.5 (6.6): R3-1 | 47be0b |
| 40 | absent | paged | hot | 143.1 | 191.7 | +36.9 (25.3): R3-1 | 47be0b |
| 255 | present | paged | hot | 279 | 283 | screen | light |
| 255 | absent | paged | hot | 274 | 318 | screen (+44) | light |
| 16/40/255 | either | paged | cold | (at open) | 20,400-23,000 | page hash, once a page | light |
| 16 | either | decoded | - | 176.3/182.2 | 172.8/174.7 | same code as main | 47be0b |
| 40 | either | decoded | - | 152.3/143.1 | 155.5/157.1 | same code as main | 47be0b |
| 255 | either | decoded | - | 279/274 | same code | - | light |

The decoded path (`StoreTable::find`) is the agent's and `decode()`'s, the same code at both commits.
The cold row is the first-touch hash the design moves from the open (A2's cells measure it end to
end); its find part is the hot path's search plus `check`.

### R3-1: cause and fix

- Profile (`sample`, light, `table_find_bench loop 40`, `.scratch/r3/sample_*.txt`): memcmp is 58% of
  a paged `find` at 40 bytes against 48% of main's `StoreTable::find`; the rest of `find` is less
  than main's. Same number of compares (about 5.5 over the index, 4.5 on the page, against main's
  10 plus one equality), each over the 36 bytes the names share. Cause: the comparison length.
  Not the indirection (offsets, mark) and not the early-out: those are in `find`'s own share.
- Experiment 1 (`.scratch/r3/bench_v1`): compare past the prefix the bounds share (design:
  docs/perf/first-read-1ms.md, "Compares past the shared prefix"). Light screen, median of 3, ns,
  present/absent: 40-byte 229/255 -> 156/165 (main's decoded 192-205/195-201 in the same runs);
  255-byte 399/459 -> 183/182; 16-byte 229/257 -> 168/188. Kept: `Detent:5fa1f6c6`.
- Experiment 2 (`.scratch/r3/bench_v2`): lower-bound page search (one branch a step, equality at
  the end) on top of 1: 168/181 at 40, 202/188 at 16, no gain over 1. Not kept.
- Class sweep: every `find` row goes through the one changed search (hot and cold pages share it;
  the decoded path is main's code). Test test_table_pages `shared_prefixes` (`Detent:5fa1f6c6`,
  `Detent:d12f2887`): widths 16, 40, 255 and four random tables (a first page below the later
  prefix, bytes above 0x7f, names prefixes of each other); every entry, the names around it, its
  prefixes and its name with one byte up or down at start, middle and end answer as the decoded
  `find`, marked and unmarked. Mutants P1 (no early-out above the prefix) and P2 (every page past
  the table prefix) fail it locally on a wrong entry; jobs in Evidence.
- Before/after (strict, 10 rounds of main / `Detent:28a8d276` / head / head again as A/A):
  261125-031218-4505c8; the table above gets its numbers when it ends.

### Non-blocking

- R3-2 `Detent:a0567b5f`: `cell_pass(mode, cell, branch_p99, main)` is the one rule; selftest and
  both loop paths call it; selftest has 10 cases. Mutants L1 (bound alone) and L2 (a refused cell
  passes) fail selftest locally.
- R3-4 `Detent:a0567b5f`: `MAIN_CANNOT_RUN = {("running", 1000)}`; main refusing any other cell
  fails it (`within_main` false); selftest has running:1 and stopped:1000 refused cases.
- R3-5 `Detent:8127a259`: `AgentStore::Options::profile` is optional; none takes the verified
  table's (a new store's default); DiskStore's own completion read of `selected` is gone. Test
  test_agent_store `table_profile`. Sweep of the class (store-file reads outside the helper): the
  remaining completion opens in agent_store.cc are `.lease` opens for locking (lines 362, 537),
  which read nothing; PackScrub stays as R6.
- R3-6 `Detent:a0567b5f`: first-read-gate.md Results names the four round 2 cells.
- R3-3: the lead's (CI rerun).

Fix round 2 finished at `Detent:28a8d276` (code); jobs queued there (Evidence).
- R2-1 `Detent:a3da0693`: `reader_bench_ab.sh build` copies both binaries after `mkdir -p
  "$out/$code"`. Checked on a fresh OUT under a light hold (build, `run` 1 pair, `find` 1 pair:
  exit 0). Light screen, head vs main, ns: find present/absent 16-byte 222/261 vs 260/227, 40-byte
  266/311 vs 187/198; warm p50 hot 507 vs 711 (1 thread), 735 vs 883 (8). The strict pair at the
  head is the number that counts: 261124-221108-47be0b (find), -221111-38e2a3 (warm).
- R2-3 (lead: option a) `Detent:28a8d276`: `measure` fails a cell on either rule: pooled branch
  p99 at 1 ms or more (A2), or where main ran the paired difference or pooled p99 beyond main's
  plus their floors (D140, D142). One `verdict()`; each cell line carries `below_1ms` and
  `within_main`; `first-read-gate.sh selftest` checks it on fixed numbers (7 cells). Mutants G1
  (no bound) and G2 (no D140) each fail it (locally; jobs in Evidence). first-read-gate.md says
  both gate.
- R2-2 `Detent:28a8d276`: the marks paragraph says the hash and entry checks run once, at the
  first touch, and a marked page answers in place from the kept offsets.
- Security R1 `Detent:c125fd9e` (agent_store.cc is `src/detent_local`, inside the set): class =
  every read of the agent's `slurp`: startup `selected`, `verify`'s `.image`, `.read`, `.live`, a
  reached name a publish reuses, a held generation's `.live` in collection. `slurp` now opens the
  name under the store directory's descriptor and calls `generation_file_refusal` (agent's euid,
  the file's size bounded by the caller's limit). Tests: test_agent_store `acl_grants` (macOS):
  each of `.image`, `.read`, `.live` fails "alpha" at startup and reads again once removed;
  `selected` refuses the open; a granted reached `.image` refuses reuse (`ReachedNameMismatch`).
  Mutant A1acl (the old owner/link/mode check without ACL). "Store file checks" lists the agent
  reads. Local (Release, light): test_agent_store, _crash, catch_up, agent_cli, disk stores,
  managed reader, table pages, paged recovery, reader core, managed delivery process: 12/12 pass.
- R2-5: the old cells aafc88 (running), 891b76, 7e39f4 cancelled (gate code changed); four cells
  requeued at the head.
- R2-4 / CI: pushed at the head; macos-x86_64 at `Detent:9cc06bc9` ran its Test step 58 min with
  no log (run 45541685998, warm-relay's the same). Pushed `Detent:c75d6215` (code as at
  28a8d276): CI run 45558888521 queued; its macos-x86_64 result is not in yet (the lead reads it).

Fix round 1 finished at `Detent:47216200` (code); jobs queued there (Evidence).
- C2 `Detent:91a03ba0`, `Detent:b8bd0b69`, `Detent:47216200`: the verified page index as designed.
  `PagedTable::check` is the one page walk (hash unless marked, every entry check, offsets out); with
  an account it stores count and offsets (atomic uint16, 30 a page, ledger-allocated beside the
  marks) relaxed, then sets the mark (release). `find` on a marked page reads them after the mark's
  acquire, binary-searches comparing names in place, and returns a `TableEntryView`: the entry read
  in place (name a `string_view`, fields from the page) or none, the shape of main's `const
  TableEntry *`, borrowing the table for its lifetime. Callers that keep an entry copy it
  (`entry()`): the core only when it attaches a new generation; `read_selected`. One decoder,
  `entry_at`. Unmarked reach: a local index. Tests: test_table_pages `page_index` (marked pages, 8
  threads x 50 rounds of first touches together, without an account); mutants C2a, C2b.
  Light screens, 1,000 entries, ns, head vs main: by value (`Detent:b8bd0b69`) present 16-byte
  179-193 vs 157-169, 40-byte 218-251 vs 142-150 (profile: name malloc/free 15% plus the decode);
  as a view (`Detent:47216200`, a slower host moment) present 16-byte 226-233 vs 229-254, 40-byte
  244-287 vs 198-231; absent 16-byte 245-259 vs 243-248, 40-byte 290-309 vs 220-231. The strict
  pair (Evidence) gives the numbers that count.
- msg-4 widening `Detent:16edb38a`: run_relay_pages.py and dense_reader_measure.py read `selected`
  through `table_entries`. dense_reader_measure's four identity arguments were raw offsets of an
  older table ([72,152)); mixed_oracle reads them as cluster, group, restore_epoch and scope, which
  no `selected` layout since 2 carries. Moved byte for byte onto the entry's fields; the tool was
  already stale on main (not in the suite; the lead's if it matters).

Security review round 1 at `Detent:e52669a7` (.campaign/detent/reviews/first-read-1ms-security.md),
on main merged at `Detent:f35ad03a`:
- S1 `Detent:1e8de679`: `PagedTable` reads magic and layout first; layouts 1-2 (64-byte header, whole
  digest, 96 bytes empty) are `LayoutRefused` at their own minimum, shorter or damaged ones
  `TableRefused`. Tests: test_table_pages `older_layouts` (empty, short at 12/64/95 bytes, full,
  damaged; layouts 1 and 2; `read_generation_selection`; a managed store over an empty layout-2
  table opens Ready, refusal recorded, `selected` removed, `Detent:c7e76dc4`).
- S2 `Detent:ebb4c103`: class = every file the store reads or maps, checked on the descriptor it
  reads. `generation_file_refusal` now checks the ACL; segment mappings use it; the separate checks
  after it go; recovery's directory opens take the walk's directory rule (`store_directory`).
  Gaps closed: `selected` (reader and recovery), and on the recovery path `.lease`, `.image`,
  `.owner`, packs, the directory, and trusted restart's `.live` and `.read`. Inventory:
  docs/perf/first-read-1ms.md "Store file checks". Tests: test_reader_core `acl_grants`, one case
  per kind (selected, .image, .lease, .pack, change, .read, .owner, directory) on each path that
  reads it, plus the helper's "ACL" refusal; it names every grant a path accepted before failing.
- N1: design line on `selected` immutability after rename (docs/perf/first-read-1ms.md).
- N2: test_table_pages `concurrent_first_touch` (8 threads x 50 rounds, fresh and damaged page).
- N3: test_table_pages `mutations`: 20,900 single-byte changes, truncations and extensions of a
  three-page table, 8,647 refused at the read, every lookup otherwise original or refused.
- N4 skipped: not a regression (the old table was an unaccounted heap vector of the same bound),
  and mapping-ledger admission of `selected` is a reader-wide change, not this finding's fix.
Local (Release, light hold): test_local_table_pages, test_reader_core, test_local_disk_store,
test_local_paged_recovery, test_local_managed_reader, test_agent_store pass.

CI at `Detent:e52669a7` failed the same 8 tests on every platform (test_managed_delivery_process,
test_system_managed_* x6, test_system_relay_pages): three Python parsers of `selected` read layout 2.
`Detent:244990b9`: one reader, `table_entries` in tests/detent_delivery/verify_local_format.py
(table digest, page digests, entry bounds); managed.py imports it, test_restart_capacity uses it
through managed; HOW_IT_WORKS gives layout-3 sizes. Checked against real layout-3 tables (1 and 70
entries, a damaged and a truncated one); the eight tests rerun locally (Evidence).

## Next

1. Review of fix round 3 at `Detent:d12f2887`. When 261125-031218-4505c8 ends, fill the V5 table's
   after column (`table_find` lines) and the four `A16 cell` lines into first-read-gate.md Results.

Lead: pin-wave also changes src/detent_local/disk_store.cc and tests/detent_agent/test_agent_store.cc
(`campaign status` warning); this round touches both (R3-5).

## Messages

- Correctness review round 2 (CHANGES at `Detent:9cc06bc9`) and security round 2 (APPROVE), folded
  by id: R2-1, R2-2, R2-3 (option a, lead's ruling), R2-5, security R1 done (Doing). R2-4: CI
  rerun by the push (Evidence). R2-6, security R2: none (stated in the design, N1).

- msg-4 (lead: C2 checked; target main's warm find; widened two Python tools): C2 built as
  written, measured as interleaved strict pairs at main and the head (Evidence); the Python tools
  moved (`Detent:16edb38a`). Lead's follow-up (find returns a borrowed view, as main's pointer):
  done in `Detent:47216200`; the jobs at the older head cancelled and requeued there.
- Correctness review (CHANGES at `Detent:e52669a7`, .campaign/detent/reviews/first-read-1ms-correctness.md),
  folded in by id:
  - C1 `Detent:fd01d67f`: the gate fails a cell unless its pooled branch p99 is below 1 ms, the
    refused-main cell included; vs main is a second line and a `within_main` field per cell.
  - C2: a new mechanism, so its lifecycle table first (`Detent:08cca469`); code after the design check.
  - C3 `Detent:ed6f9ce5`: test_table_pages `cross_page`, one re-signed value test per check (M7
    duplicate name across pages, M8 short body, M12 index order, M1 count, M3 slots, M4 entry slot,
    M5 page end, M6 padding). Mutant runs wait for the final head (C2 rewrites `walk`).
  - C4 `Detent:ed6f9ce5`: test_reader_core `damaged_attached` (mutant M9 at the final head).
  - N1 done (comment). N2 done (three stale sections). N3: docs/mechanisms/read-counters.md is outside
    the files (msg-398). N4, N5, N7: none (optional or by design). N6: branch-alone kept for the running
    1,000 cell. N8: `publish` already checks a new table starts unmarked; left as is.
  Local: test_local_table_pages and test_reader_core pass at `Detent:08cca469`.
- msg-393 follow-up: two more layout-2 parsers outside the files (run_relay_pages.py, which fails
  test_system_relay_pages, and dense_reader_measure.py); asked in msg-398. Locally at
  `Detent:244990b9` the other seven of the eight CI failures pass (test_managed_delivery_process,
  test_system_managed_authority_failover, partition, all-down, no-quorum, relay, relay_ipv6).

- msg-393 (to the lead; widened with one change, one parser): done in `Detent:244990b9`, see Doing.
  The parser lives in verify_local_format.py, the dependency-free module, and managed.py imports it
  by the tests' existing `sys.path` pattern (authority.py does it for tests/support).

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

Nothing.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), name every job you examined, failures included (`campaign brief` lists failed
jobs no journal names), and answer each message by its id (msg-N).

Round 3, at `Detent:d12f2887` (queued):
- A2: R3-1 find A/B 261125-031218-4505c8 (strict); warm 261125-031221-f08ca9 (strict); A2 cells
  (exclusive) stopped:1 -031224-b0beee, stopped:1000 -031228-873097, running:1 -031230-95829a,
  running:1000 -031233-4a1cfb.
- A1: profile 261125-031306-9e9536 (strict).
- A3: tests 261125-031135-fda3dd (test_local_table_pages, test_agent_store, test_reader_core);
  gate selftest -031236-1267a1; fails without (`.scratch/r3/mutants.txt`): P1 -031138-e7f754
  (`Detent:0693dda8`), P2 -031141-283064 (`Detent:428e2490`), L1 -031144-c056ef
  (`Detent:2e6f8c5a`), L2 -031147-bc9ddd (`Detent:d55040a3`). Suite: CI macos-arm64 at the pushed head.
- Failed, my command (zsh kept `$Q` as one word, exit 127): 261125-031108-7138fa, -031111-cf189c,
  -031113-f22498, -031116-32a472, -031119-0e84bb, -031122-784335, -031125-2105d9; resubmitted above.

| Criterion | Evidence |
|---|---|
| A1 | 261124-221056-6a4db0 (strict profile, four cells, at `Detent:28a8d276`); before-profile 261124-144100-b5ba51 (replaces 134303-81190d, main's reader code): see "Profile (A1)". Suite: CI macos-arm64 at the pushed head |
| A2 | Round 2, at `Detent:28a8d276` (gate fails on either rule): exclusive cells 261124-221058-b76dd6 stopped:1, -221101-ac8c51 stopped:1000, -221103-8b1704 running:1, -221106-893160 running:1000. C2 strict pair vs main `Detent:34fe15ad`: -221108-47be0b (find, 10 pairs), -221111-38e2a3 (warm, 6 pairs x 1 and 8 threads). Round 1 (superseded): 261124-193929-8a61ac stopped:1, -193933-aafc88 stopped:1000, -193936-891b76 running:1, -193939-7e39f4 running:1000 (exclusive, fixed gate: a cell fails at a pooled p99 of 1 ms or more; vs main `Detent:34fe15ad`, main's code). C2 pair (strict, interleaved vs main): 261124-193942-688ac7 (table_find_bench, 10 pairs), 261124-193945-948831 (reader_bench warm, 6 pairs x 1 and 8 threads) |
| A3 | Round 2, at `Detent:28a8d276`: test_local_table_pages and test_reader_core 261124-221148-a692ec; gate selftest -221113-4eee87, fails without: G1 -221128-cf14d1, G2 -221131-053a2a (`.scratch/mutants-r2.txt`); test_agent_store `acl_grants` -221122-a481db, fails without: A1acl -221125-540805. Round 1: 261124-193948-acb3a6 (test_local_table_pages, test_reader_core at the head). Fails-without at mutants of the head (`.scratch/mutants-final.txt`): S1 261124-194035-e14cfa, S2 -194041-ed977a, page digest -194044-f809e0, C3 M1 -193951-bcf9aa, M3 -193954-0ee7cd, M4 -193958-5b5a56, M5 -194001-fa68c0, M6 -194004-921017, M7 -194008-5f78a9, M8 -194013-93c0b9, M12 -194017-63c5aa, C2a -194022-3f543c, C2b -194026-24bc4a, C4 M9 -194031-002ccd. Suite: CI macos-arm64 at the pushed head |

Cancelled at older heads (superseded): 261124-193933-aafc88, -193936-891b76, -193939-7e39f4 (A2 cells at `Detent:47216200`, gate changed); 261124-221147-3bf027 (duplicate of a692ec). Failed (R2-1, the tool's bug): 261124-193942-688ac7, -193945-948831. Examined: review r2 jobs 261124-195252-850b11, -195256-361bfa (failed, the security reviewer's first attempt; its rerun -195512-09902d and -195515-e40ef1 passed), -195304-d1e38c, -214801-af7619.
 every job 261124-193020 to -193130 (at `Detent:e719e352`, before the view); 261124-182421-35bcef (A2 stopped:1, was running), 261124-182431-301852, -182433-9d39d1, -182435-760c57 (mutants at `Detent:244990b9`).
Examined: security review jobs 261124-160310-594aee, 261124-164244-e6d442 (S1, S2 FAILs, fixed this
round), correctness review jobs 261124-160346-d09bca, 261124-164355-085f7f (the reviewer's probes;
its findings come by message). Superseded at `Detent:e52669a7`: 261124-155453-d2faac (stopped while
running), -155456-b57f68, -155500-f39f9c, -155507-a4b568, -155516-489edc. Cancelled (wrong command):
261124-182008-ba4a88, -182006-e01a39, -182017-e37601.

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

- R12 (R3-1): the fix is the comparison length, measured (profile, two one-hypothesis experiments);
  the target stays main's warm `find` at every width. The cold row has no main counterpart: main
  hashes the whole table at open.
- R13 (R3-5): `Options::profile` optional rather than a second constructor: one shape; existing
  `{profile}` callers compile unchanged. The test is a value test of the new option: the removed
  read had no observable effect to kill (the agent refused a granted `selected` either way, a481db).
- R14 (R3-4): option a of the finding (only running 1,000 takes the bound alone).

- R8 (C2): a marked page's match is read in place without re-checking, as the design's "what a
  marked page does not re-check" says; `take_entry` (bounds, then `entry_at`, then name and
  generation checks) is the checked decoder. `TableEntryView::operator==(const TableEntry &)` is
  false for none, as `std::optional`'s, so the tests compare a lookup to its entry directly.
- R9 (dense_reader_measure): moved byte for byte, not reinterpreted (see Doing).
- R5 (S2 shape): the ACL check lives in `generation_file_refusal`, the one owner/link/mode/size
  check, not beside each caller; `Selected::image` is always the bytes `select_entry` read, since an
  ACL grant now refuses there. Error type on the reader path moves from `ReadSourceFailure` to the
  helper's `runtime_error` for a pack ACL, as for its other refusals; the core maps both to
  `DETENT_SOURCE_FAILURE`.
- R6 (sweep scope): `change` keeps its own check (owner, link, mode, ACL; its size rule differs);
  `PackScrub` trusts nothing it reads (hashes against the owner record) and stays unchecked. The
  agent's own reads joined the class in round 2 (R10).
- R10 (security R1): the agent's `slurp` opens with `openat` on the store directory's descriptor
  instead of completion's `PosixIO` open, so the helper checks the descriptor read without
  reaching into completion's `detail::PosixFile`; owner is the agent's euid, as its directory
  check. The held-generation `.live` read in collection has no own test: a refusal there keeps
  every segment (the conservative side), and it is the same `slurp`.
- R11 (R2-3): a cell main cannot run (running:1000) has the bound alone; D140 needs main's arm. ACL tests
  are macOS-only (`chmod +a`); Linux ACL grants show in the mode's group bits, FreeBSD not run.
- R7: test_local_table_pages links `detent::local_publication` (the managed store case) and moves
  under that target's guard with the other publication tests.

- R2: change 2 keeps the post-open name check (`test_trusted_paths` "raced" pins it); only the
  pre-open `fstatat` goes. Change 3 refuses the same tables; a duplicate slot is now found after
  the whole table decodes, not at its entry (one TableRefused either way, D43).
- R3: the A1 profile ran main's reader code (`Detent:3f0d12cb`, before any change) on purpose:
  it names the costs to remove; its rerun at head is the after-profile.
- R4: the hot path is fixed here (paged-decode-one-shape was dropped; the code is in
  `src/detent_local` and `include/detent_local`).
- R1: the gate runs the reader core in both trees once a tree has `src/detent_reader` (reader-s6
  landed): this quest's A/B is core against core; main before reader-s6 still runs its Client.
