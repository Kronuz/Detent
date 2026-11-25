# first-read-1ms review: correctness

Commit: Detent:e52669a79d2961b526ff0f2f643ac0dfbbb41398
Verdict: CHANGES
Date: 2026-11-24

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

## Evidence I relied on

- None of the quest's own jobs at this commit ended. The A2 gate jobs (d2faac, b57f68, f39f9c,
  a4b568), the A1 profile (489edc) and the digest mutant (3e4aa2) were cancelled when the branch
  moved on (`Detent:244990b9`), with no `A16 cell` line. CI run 45513889725 was still running. A2
  has no result at this commit (see C1).
- I accepted the suite tests in my own probe job as the behavior check. 772f59 ran them at this
  commit: test_local_table_pages, test_agent_store, test_reader_core, test_local_read_counters
  (counters on) and test_local_trusted_paths all exit 0.

## What I checked myself

Scripts are under `.scratch/detent/review-first-read-1ms-correctness/`. They run from `$JOB_SRC`.

- `run.sh` (job 261124-174925-772f59, light, Debug, counters on) runs two probes and the suite
  tests above at the commit, then twelve mutants, and lists which tests kill each one.
  - `probe_table_oracle.cc` encodes 60 random tables (0 to 3,000 entries, names of 1 to 255
    bytes) and checks them against `StoreTable` as an oracle. It also parses the layout
    independently: whole entries per page, the index's last names, page digests, zero padding and
    `table_bytes`. It refuses re-signed cross-page damage and runs 8 concurrent threads doing
    first touches. Everything passes.
  - `probe_core_damaged.cc` attaches four namespaces, publishes, damages the new table on
    n060's page, then reads. n001 and n099 switch (1001, 1099). n060 and n070 keep their
    generations (60, 70, stale, OK). A forced refresh after the repair switches n060 (1060).
    This passes.
  - Earlier run 085f7f failed on the probe's own wrong expectation; I traced it to N1.
- `find_cost.sh` (job 261124-160446-6df9d3, light, Release, a screen) times one lookup in a
  1,000-entry table whose pages are already verified. It compares `PagedTable::find` with main's
  `StoreTable::find`.
- I read the diff `main...e52669a7`, the design's lifecycle table, the gate script, and the test
  changes for loosening (D152).

Lifecycle rows and the value test behind each:

| Row | Test | Status |
|---|---|---|
| Attach | test_table_pages `attach` | ok |
| First read of an entry | test_table_pages `first_reads` | ok |
| Another entry on a verified page | test_table_pages `first_reads` | ok |
| A corrupt page | test_table_pages `corrupt_page`; test_reader_core `damaged_page` (first attach) | ok |
| Publish | test_table_pages `publish` | cannot fail: a new `PagedTable` always starts unmarked. The core's re-read at the bump is covered by `selected_reads` (N8) |
| Close | test_table_pages `close` (ledger returns to 0) | ok at `PagedTable` level |
| An attached namespace whose page is damaged in a later table (the design's "fails this namespace alone") | none in the suite | C4 |

Writer and reader agree. The bound is `static_assert(table_entry_bytes(255) < table_page_bytes)`,
checked by test_table_pages `bound` and by the oracle's independent parse on random name lengths.
`decode()` walks and hashes every page (no marks without an account), then checks the entry
count, unique slots and `slots == max + 1`. The checks are correct, but most of them have no test
that fails without them (C3).

No test was loosened:
- `test_read_counters` keeps exact values. It drops to 4 records because the image is read once,
  and `table_bytes` drops the table because it is mapped now. `hashed` still equals
  `selected - 32`, which for one page is header + index + page.
- `test_trusted_paths`' race moves to the first `fstatat` call, because the walk now opens the
  component first. That call is the same post-open name check.
- `test_agent_store` changes its pinned name because the layout feeds the derivation, and
  re-signs per page. It keeps every refusal and adds a check of the page offset.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| C1 | yes | gap | A2's jobs pass on D140, not on p99 below 1 ms. The gate exits 0 when the paired difference and the pooled p99 are no worse than main's plus their floors, and says 1 ms is "reported, not gated". A cell where main's publisher fails to start passes with `"pass": True` unconditionally. A green A2 job therefore does not show p99 below 1 ms in any cell. The screens in the journal had pooled p99s of 1.4-2.0 ms. | `docs/perf/first-read-gate.sh` measure: `passed = mode == "count" or (diff <= floor and p99["branch"] <= p99["main"] + p99_floor)`; the refused-cell branch sets `"pass": True` | Make the gate fail a cell whose `branch_pooled_p99_ns >= 1_000_000` (a `--bound` argument the A2 jobs pass, or always for `measure`), and rerun A2 with it. Otherwise A2 is a [manual] criterion that reads the four `A16 cell` lines. |
| C2 | yes | defect | `PagedTable::find` on a verified page costs 10-16 times main's lookup. With 16-byte names it is 4.8 us present and 4.9 us absent, against 0.31 and 0.47 us. With 40-byte names it is 12.5 and 8.4 us, against 0.49 and 0.51 us. `walk` builds every entry of the page on each reach (a `std::string` and a `TableEntry` copy each) and scans the padding. The reader core calls it on two paths. `View::listed` runs on every lookup of a namespace the table does not list, in range (0.5 us before, about 5 to 12 us now). The refresh loop calls it once per attached namespace: with 1,000 attached that is about 5 to 12 ms per refresh, against main's whole-table read of about 1.8 ms (A1 `selected`). The first-read gate does not measure either path. | job 261124-160446-6df9d3; `store_table.cc` walk (`take_entry` per entry, line 261 on); `core.cc` `listed()` and the installed-entries loop | Compare names in place and build only the matching entry, keeping each entry's bounds and order check. Optionally, the refresh merges the sorted installed names against the pages once. Test: a `reader_bench` line or a micro check that puts verified-page `find` within 2 times `StoreTable::find`, plus a refresh with 1,000 attached namespaces interleaved against main. |
| C3 | yes | gap | The cross-page and index checks have no test in the suite that fails without them. With each check removed, the suite still passes: entry count (M1), `slots == max+1` (M3), entry slot below `slots` (M4), page last equals index last (M5), page padding (M6), order across pages (M7), page bytes (M8), index order (M12). Only the duplicate slot (M2) is killed, by test_agent_store. My probe kills M1, M3, M4 and M5. M7 is load-bearing: without it, a re-signed table whose page 1 starts with page 0's last name decodes into duplicate names. M8 guards memory: without it, an index naming more pages than the body holds makes `in.size() - pages_[k].offset` (line 236) underflow, and the read leaves the buffer. | job 772f59 mutant table | Add the `cross_page` cases of `probe_table_oracle.cc` to test_table_pages, plus three re-signed cases: page 1's first entry equal to page 0's last (M7); a body one page and one byte short of its page count, refused at construction (M8); two index records' last names swapped (M12). M6 needs a non-zero byte after a page's zero padding, re-signed. Each new test needs a `--fails-without` run on its mutant. |
| C4 | yes | gap | The refresh loop's new `catch` for an attached namespace whose page is damaged has no suite test. Turning it back into a whole-refresh failure (M9) passes the whole suite. The behavior is right at this commit, as `probe_core_damaged` shows. | job 772f59 (M9 killed only by `probe_core_damaged`) | Add `probe_core_damaged`'s scenario to test_reader_core, with a `--fails-without` job on M9. |
| N1 | no | nit | `page_verifications()` says "Pages hashed so far", but it counts only hashes that passed. A corrupt page reached 3 times reads 0. | `store_table.hh` comment; `store_table.cc` walk: `refuse(...)` comes before `verified.fetch_add` | Say "pages verified" (or count every hash). |
| N2 | no | nit | `docs/perf/first-read-1ms.md` is stale in three places. "What is left" says the 1,000-namespace cells hash the whole table. "Held for test updates" names changes that landed (`Detent:dee58172`, `Detent:9c8a44ee`). The design says "64-byte header" while LOCAL_IMAGE_FORMAT says 72. | the doc at this commit | Update the three sections. |
| N3 | no | gap | `docs/mechanisms/read-counters.md` no longer matches what the counters count. `table_bytes` "counts that read", but the table is mapped and uncounted. `record_opens` still lists "the image peek, lease, image". The `bytes_hashed` formula for the table is "its length minus 32". `pages_verified` leaves out table pages. | that doc against `generation.cc` and `store_table.cc` | Update the table of where each counter moves (the doc is outside the quest's files: the lead's call). |
| N4 | no | nit | A last page with trailing zero bytes is accepted, re-signed. The encoding is not canonical, but no value differs. | 772f59 note | Optional: require the last page to end at its last entry. |
| N5 | no | nit | `find` answers both entries of a slot duplicated across pages. That matches the design: only the agent's and the store's whole read check uniqueness and fence the store. | 772f59 note | None; recorded. |
| N6 | no | gap | Running cell at 1,000 namespaces: reporting the branch alone is sound for A2's absolute bound. The D140 check (no slower than main) is unanswered there. Main's 1,000-namespace publisher fails only because of its test writer's limit (tooling, not the measured reader). | gate `PublisherRefused` path; `generation_writer.cc` limits | To get main's number, apply the branch's `generation_writer.cc` limits line to the BASELINE checkout in `prepare` and keep main's reader as it is. Keeping branch-alone is also acceptable for A2. |
| N7 | no | nit | The reader core takes `slots` from the header for change coverage. Only `decode()` cross-checks it against the entries. A header `slots` that is too high makes the reader remap or fail where main computed it from the entries. Only a re-signed table can reach this. | `core.cc` read_table `named = read->slots()` | None needed; C3's M3 test pins the writer side. |
| N8 | no | nit | test_table_pages `publish` cannot fail (see the lifecycle table). | `test_table_pages.cc` publish | Optional: share one account and check that marks do not carry over, or drop the row's claim. |

## Not run

- A2 and A1 at this commit: their jobs were cancelled with no result, so I have no p99 numbers.
  Under C1 they would not prove A2 even if green.
- CI at this commit (run 45513889725 was in progress) and the full suite. I ran only the five
  tests named above.
- Authenticity, the trust walk's races, the image reuse's ACL, and mapped-file truncation (SIGBUS):
  the security reviewer's.
- No TSan run. The concurrency probe is a value check without a sanitizer.
- The branch moved on after this commit (`Detent:244990b9`, including test_table_pages +206 and
  test_reader_core +96). I did not review that range. Its new mutation tests are not re-signed,
  so they likely do not close C3.
- This review ran as a subagent, not a session titled `detent · reviewer first-read-1ms
  correctness`.

## Round 2

Commit: Detent:9cc06bc9 (code at Detent:47216200; 9cc06bc9 adds only the journal)
Verdict: CHANGES
Date: 2026-11-24

I re-reviewed the whole table-read mechanism, not only the fix diff: the `PagedTable` constructor, `check`,
`find` and `decode`; `TableEntryView`; the verified page index; and the reader core's `listed`, refresh loop
and attach.

### Evidence I relied on

- The new tests at the head (job 193948-acb3a6) pass.
- Each mutant fails the suite (the quest's `--fails-without` jobs at the head):
  - In test_local_table_pages: M1, M3, M4, M5, M6, M7, M12, C2a, C2b, S1 and page-digest
    (bcf9aa, 0ee7cd, 5b5a56, fa68c0, 921017, 5f78a9, 63c5aa, 3f543c, 24bc4a, e14cfa, f809e0).
  - M8 (93c0b9) kills by an uncaught `invalid verified page start`, which aborts the test, not by
    a refusal check.
  - In test_reader_core: M9 (002ccd) and S2 (ed977a).
- A2 stopped:1 (job 193929-8a61ac, exclusive, at Detent:47216200): the branch's pooled p99 is
  654 us against main's 824 us. Run medians are 443-477 us against 537-620 us, the paired difference
  is -122 us (floor 51), and all 200 of 200 reads come in below 1 ms on both sides. The cell passes,
  and it is within main.
- A1 profile 193926-75ef7a.

### What I checked myself

- `run_r2.sh` (job 261124-195304-d1e38c, light, Debug, counters on) passes.
  - `probe_table_oracle_r2.cc` runs the random-table oracle against `TableEntryView`: each
    entry's name, slot, generation, cursor and `entry()` are checked at first touch, then again
    on marked pages from the kept offsets, and on an unaccounted table. Absent names match
    `StoreTable::find`. The re-signed cross-page cases and 8-thread first touch also pass.
  - The run also covers test_local_table_pages (20,900 mutations), test_reader_core,
    test_agent_store and test_local_read_counters.
- `find_cost_r2.sh` (job 261124-214801-af7619, light, Release, a screen) shows C2 is fixed. At
  16-byte names, paged `find` is 242 and 251 ns against main's `StoreTable::find` at 273 and 271
  (0.9x). At 40-byte names it is 259 and 287 against 222 and 225 (1.3x). Before the fix it was
  4.8-12.5 us.
- Memory ordering (store_table.cc `check`, `find`; verified_pages.hh) is correct:
  - The offsets are stored relaxed, before `VerifiedPages::set`'s release CAS.
  - `find` reads the mark with acquire (`test`), then the count and offsets relaxed.
  - Other pages' `set` calls on the same word are read-modify-writes, so they continue the
    release sequence.
  - Two first touches together store identical values into the same atomics, so a reader never
    sees a torn or mixed index.
  - A reach that sees the mark clear walks into its own local array and does not write the
    shared index when `check` finds the page already marked.
- Views do not outlive their table (core.cc, generation.cc):
  - Every `TableEntryView` is local to one call. `listed()` runs under the lookup's `Pin` of
    the view that owns the table.
  - In `refresh_locked`, the views come from `table`, which is either the installed view's
    table (current, so not retired) or `new_table`. They die before `delete new_table`, and
    `attach` copies `entry()` before it builds a `Generation`.
  - `read_selected` copies `entry()` while `read` is alive.
  - No view is stored in a `View`, an `Entry` or a `Generation`, so none can cross a publish.
- C1: the gate diff fails any cell whose pooled branch p99 is 1 ms or more, the refused cell
  included. The comparison with main is the second line (8a61ac prints both).
- C3, C4: test_table_pages `cross_page` re-signs each case. test_reader_core `damaged_attached`
  is the round-1 probe's scenario.

### Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| R2-1 | yes | defect | `reader_bench_ab.sh build` copies `table_find_bench` into `$out/$code/` before it creates that directory, then the next lines `rm -rf` it and recreate it. On a fresh OUT the copy fails, so `build` fails, and both strict C2 jobs failed in their build step. If OUT already existed, `find` would run a binary that was just deleted. The warm A/B tool (`run`) is unusable too, because it shares `build`. C2's claim has no strict measurement. | jobs 261124-193942-688ac7 and -193945-948831: `cp: directory .../c2-find/base does not exist`; benchmarks/reader_bench_ab.sh `build()` | Copy `table_find_bench` after `mkdir -p "$out/$code"`. Rerun 688ac7 and 948831; their success is the test. Target: main's warm `find` (my light screen: 0.9x and 1.3x). |
| R2-2 | no | nit | The design still says "A marked page skips only its hash; its entries are still bounded and checked on every reach". "Verified page index" says the bound, order, slot, last-name and padding checks run once, at the first touch, and a marked page answers in place, unchecked. The second statement is the code. | docs/perf/first-read-1ms.md "Design: table pages", the marks paragraph | Change the marks paragraph to match. |
| R2-3 | no | gap | `measure` no longer fails on D140: a branch slower than main but below 1 ms exits 0. `docs/perf/first-read-gate.md` still says "The gate is D140" and that 1 ms is "reported, not gated". This is the owner's call. Option a: fail on either rule (A2's bound or D140), which keeps the regression gate for later reader quests. Option b: keep the bound alone and update the doc. | gate diff; first-read-gate.md line 68 | Option a, or option b with the doc changed. |
| R2-4 | no | gap | CI at 9cc06bc9 failed: `ci / macos-x86_64`'s Test step did not finish (58 min, no log). warm-relay-agent-boot's macos-x86_64 failed the same way at about the same time, and relay-mirror-idle-wake's passed. The failure is not attributed to this quest, and A3's `met` reads macos-arm64. | CI run 45541685998 | The lead reruns the job; landing needs it green regardless. |
| R2-5 | no | gap | A2 has 3 of 4 cells pending (aafc88, 891b76, 7e39f4 queued behind about 14 exclusive jobs). A2 stands only once all four pass. | campaign job list | Read the three `A16 cell` lines when they end. |
| R2-6 | no | nit | A lookup on a marked page reads the entry in place with no bounds check. That is sound only while the mapped `selected` never changes, which the design states (an in-place write or truncation is outside the model; security N1). | store_table.cc `find`; `TableEntryView` accessors | None; recorded. |

### Not run

- No strict C2 numbers (R2-1). A2's three pending cells (R2-5).
- No local TSan or ASan. CI's linux-tsan and linux-asan-ubsan passed at 9cc06bc9, including the
  concurrent first-touch tests.
- S2's ACL checks across store files and the Python tools' shared reader: the security reviewer's.
  I only read that the reader parses layout 3.
