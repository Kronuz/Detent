# journal-faultfs: journal

State: ready for review once the jobs at journal:d90797c pass; waiting on jobs 261118-102018-d5ffa8 102019-20438b 102020-1b65f6 102021-8afec5 102022-b1dd60 102023-34b4b7 102028-2589c8 102034-6081ab 102035-129e98 102037-7d70a8 102038-3e3b72
Quest: .campaign/detent/quests/journal-faultfs.md (the lead's; never edit it)
Code: /Users/kronuz/code/Kronuz/journal-wt/detent/journal-faultfs (branch quest/detent/journal-faultfs)

## Doing

journal:d90797c (pushed): faultlab pinned at a3b71d1 (faultlab main, faultfs-collect-linear: references
counted incrementally). Origin main had not moved, so no merge. Every evidence job reran at it; the
heap cases must hold reopen_churn 824 B, reopen_heap 248.5 B per group, reopen_distinct slope 0.

journal:36912d7 (pushed): journal_test.cc moves to faultfs (lead: A1 stays as written; the file
joined this quest's set). `git grep memory_io.h -- test fuzz` is empty locally. Shared's host is
faultfs directory `host` behind the unchanged MeteredIO; reopen_crash is `power_off_after(cut -
1)`, `powered_off()`, `crash(seed * 7919 + cut, size_pick::sector)` (was `power_loss()`, stream
0); reclaim_syncs runs on the root directory (was prefix `log/`). CMake links faultlab to
journal_test. TSan carve-out untouched. Before it, journal:6ea9494 merged main at 4d364f1 and
redid host_test.cc's move.

Local probe at 36912d7 (RelWithDebInfo, `.scratch/rel`, not evidence): default run 194415 checks,
0 failures, 3.8 s; reopen_crash 16 seeds, 1788 power cuts, 0 failures, 0.9 s. The heap cases'
numbers come from the jobs.

The reopen-closed numbers each job must hold (journal:4d364f1): reopen_churn 522872 -> 523696 B
(824 B over 10,000 cycles); reopen_heap (A7) 248.469 B per group before settle, 248.452 after;
reopen_distinct (A8) 109976 / 110024 / 109976 B at 1,000 / 5,000 / 10,000, slope 0.

## Next

1. Read the jobs; confirm the heap numbers; replace the Evidence ids with the passing ones.
2. CI at d90797c; then review.

## Blocked on

Nothing. Resolved by faultlab:a3b71d1 (faultfs-collect-linear) and the repin at journal:d90797c. The
earlier diagnosis follows.

**The heap cases time out on faultfs because faultlab's `filesystem::collect()` is O(inodes x
(names + durable names + log)) and runs on every remove, replace and sync_directory; it is cubic
over a run.** Not a hang.

- Jobs at 36912d7: reopen_churn 261118-073233-28994b (30 min), reopen_heap (A7)
  261118-073233-3e7cc4 (30 min), reopen_distinct (A8) 261118-073233-6ed20f (15 min): each killed
  at its deadline after building.
- Diagnosis 261118-083844-874284 (`.scratch/heap_probe.sh`, light, not a measurement):
  reopen_distinct at reduced counts, MemoryIO (4d364f1) against faultfs (36912d7):
  100 groups 0.60 / 0.84 s, 200 0.26 / 2.69 s, 400 0.78 / 24.51 s. faultfs grows ~9x per
  doubling (cubic); MemoryIO stays flat. At 10,000 that extrapolates to ~4 days. `sample` 5 s
  into a running reopen_heap: 1514 of 1522 top-of-stack samples in `collect()`; reopen_churn:
  3831 of ~3870, the rest `settle_directory` and memcmp.
- Cause (faultfs.h:477-489 at faultlab:a452a8c): per inode it scans every name, durable name and
  log entry, without even stopping at a match. 10,000 groups leave tens of thousands of inodes
  and names, and every release's removes and directory syncs pay that product.
- faultlab is outside this quest's file set (journal tests only; faultlab pinned at a452a8c per
  the contract), so the fix is not mine to make.
- Options: (1) a faultlab quest makes `collect()` linear per call: gather the named ids once
  (a `std::set`, 4 lines), then one pass over inodes; same behavior. Prototype (copy of a452a8c
  with only that change, via FETCHCONTENT_SOURCE_DIR_FAULTLAB): job 261118-084810-2701ea runs the
  three cases at full counts. Then repin journal to that commit here. A per-inode reference count
  (O(1) per change) is the further step if (1) alone is still slow; `settle_directory` and
  `entries` stay O(names) per call. (2) Run the heap cases at reduced counts: changes A7/A8 as
  written and the bound, rejected by the assignment. (3) Keep the heap cases on MemoryIO: breaks
  A1. Recommendation: (1), as its own faultlab quest; this quest's only change is the pin.


### Resolved: A4 finding (verdict (b)), fixed by journal-reopen-closed A9

**A4: reclaim's segment releases are not ordered; a power cut at their directory sync leaves a
gap that recovery reports as damage.** Verdict (b), a journal behavior MemoryIO hid, not a test
translation error.

- Reproducer: `journal_turn_log_power_test 8` at journal:c606068 (job 261118-040056-965498).
  Seed 5 cut 116: before the crash `turns-0b..0f`, after it `turns-08, 0b..0f`. Seed 5 cut 161:
  `11..17` then `0f, 11..16`. Seed 7 cut 219 (second 3): `18..20` then `16, 18..1f`, kept
  through the recovery's own cut. Each time the cut is the `sync_directory` ending one
  `TurnLog::reclaim()` (turn_log.h:369-384) that released two or more oldest segments: faultfs
  kept a later removal and dropped the earlier one (seed 5 cut 116: 08 stays, 09 and 0a go).
  Recovery reads the missing middle number as damage (turn_log.h:560): `damaged = true`,
  `lost` empty, every acknowledged record present (`matches` true in all three).
- Attribution (local probe, `.scratch/probe.cc`, not evidence): the same cuts with
  `names = durable`, `all` or `additions` recover clean; `sectors = old/fresh`, `epochs = all`
  still fail. Only the namespace coin matters.
- Why MemoryIO could not produce it: its `power_loss()` restores `durable_names` whole
  (memory_io.h, `names = durable_names`); a directory's changes since its last sync survive
  all or none, never a subset.
- faultlab rule: F7 "Namespace durability" (faultfs.h:13-15): each create, link, rename and
  unlink since the directory's last sync survives per its own coin, replayed in order; F5 calls
  this legal under the IO contract and unreachable on ext4, xfs and APFS, which commit
  directory operations in order. completion io.h:284-296 promises order among namespace changes
  only through `order_files_and_directory`, which reclaim does not call. So the test's claim is
  right to cover it; changing the test's names pick would drop every partial-namespace state.
- Consequence beyond the flag (read, not run): when damaged, a host's open group with nothing in
  the log is declared lost (turn_log.h:623, `TurnLogGroups::open`), so on a reordering
  filesystem one power cut could cost such a group. This test passes no `known`, so it does not
  exercise that.
- Options: (1) order each release: `io_.order_files_and_directory({})` between successive
  replace/remove calls in `reclaim()`, so any surviving subset is a prefix (one ordering barrier
  per extra segment in a batch; faultfs honors it). (2) One release per `sync_directory`: the
  same guarantee, a full directory sync per segment. (3) Accept it as unreachable on journaled
  filesystems and have the test pick `names = additions`: loses coverage of dropped creates.
  Recommendation: (1), as its own quest; this quest's test needs no change once it lands.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | 261118-102019-20438b at d90797c |
| A2 | 261118-102018-d5ffa8 (suite, fresh); 261118-102023-34b4b7 (reopen_crash, reclaim_syncs); 261118-102038-3e3b72 (reopen cases) |
| A3 | fails without: 261118-102028-2589c8; plain: 261118-102022-b1dd60 |
| A4 | reported (Resolved section); reproducer 261118-040056-965498 at c606068; fixed by reopen-closed A9: 261118-102022-b1dd60 (200 seeds) |
| A5 | 261118-102020-1b65f6, 261118-102021-8afec5 |
| A6 | 261118-102018-d5ffa8; CI at d90797c; heap holds: reopen_churn 261118-102034-6081ab, reopen_heap (A7) 261118-102035-129e98, reopen_distinct (A8) 261118-102037-7d70a8 |

### Old MemoryIO case to faultfs case (A2)

Common to every crash case: MemoryIO's `disk.cut = k` (operation k and every later one throw
`PowerLoss`) is `power_off_after(k - 1)` (k - 1 mutations succeed, then every one fails with EIO
until `crash`); `disk.cut = disk.operations + s` during recovery is `power_off_after(s - 1)`;
`power_loss()` is `crash(seed, {.size = size_pick::sector})`. faultfs counts mutations (writes,
truncates, syncs, creates, renames, removes, directory syncs); MemoryIO counted the same ops plus
a second tick per truncate, so every "cut at every operation" loop still runs to the
uninterrupted run's own count (`mutations()`). Where a case caught `PowerLoss` and let any other
exception escape, it now catches `std::exception` and checks `powered_off()`: an error with the
power on fails the case by name (was: an uncaught exception).

| Test | Old (MemoryIO) | New (faultfs) |
|---|---|---|
| turn_log_power `run` | seed s, cut k, random `s*7919+k`; second cut `ops+j`, same stream | seed s, `power_off_after(k-1)`, crash seed `s*7919+k`; second `power_off_after(j-1)`, crash seed `s*7919+k+(j<<32)` |
| turn_log_power `corrupted_blocks` | `power_loss()` (stream 0), XOR 0x5a into `durable[offset]` of a Disk copy | `crash(s)`, `snapshot()`, read byte, XOR 0x5a, `write_at` + `sync` through the IO; checks the byte is in the segment |
| shared_store `run` | seed s, cut k, random `s*7919+k`; `operations` | seed s, `power_off_after(k-1)`, crash seed `s*7919+k`; `mutations()` (sync and tasks must match) |
| turn_bound `crash` | seed s, cut k, random `s*7919+k`; `live = !dead()` at submit | `power_off_after(k-1)`, crash seed `s*7919+k`; `live = !powered_off()` (stricter: powered off before the failing call is attempted, so a turn submitted then can never be in flight) |
| turn_seal `crash` | seed s, cut k, random `s*7919+k` | `power_off_after(k-1)`, crash seed `s*7919+k` |
| host `recreate` | `power_loss()` stream 0 | `crash(0)` |
| host `sealer_close_crash` | cut k, stream 0 | `power_off_after(k-1)`, crash seed k |
| placement `remap` | copy of `before`, cut `ops+k`, stream 0 | `before.snapshot()`, `power_off_after(k-1)`, crash seed `L*7919+k` (L the loop count) |
| placement damage, `damaged_absent` | edit `durable` bytes and names directly | overwrite + `sync` through the IO; the removal is `remove` + `sync_directory` |
| placement `repartition_damaged` | `Disk probe = disk` | `disk.snapshot()` |
| journal_test reopen cases (`Shared`) | `MemoryIO(disk, "host/")` behind MeteredIO | `host_io(disk)` (faultfs directory `host`) behind MeteredIO |
| journal_test `reopen_crash_run` | `disk.cut = cut`, `dead()`, `power_loss()` stream 0 | `power_off_after(cut-1)`, `powered_off()`, crash seed `seed*7919+cut`, `size_pick::sector` |
| journal_test `reclaim_syncs` | `MemoryIO(disk, "log/")` | `fl::io(disk)` (root) |
| sync_seam `journal_tail` | `disk.names` | `disk.list()` |
| framing, driver, sync_seam, turn_seal, turn_bound (non-crash), fuzz_framing, fuzz_turn_log, replay | `MemoryIO(disk, "dir/")` | `make_directory("dir")`, `io(disk, "dir")` |

## Result

Not final.

## Rulings I made

- journal_test.cc: reopen_crash seeds its crash `seed * 7919 + cut` as turn_log_power does
  (MemoryIO drew stream 0 for every run); reclaim_syncs needs no directory, so it runs on the
  root (only the barrier count is read).

- host_test.cc's redo at 6ea9494 keeps the earlier rows: `host` directory via `host_io`,
  `power_off_after(cut - 1)`, `crash(0)` in recreate and `crash(cut)` in sealer_close_run, both
  `size_pick::sector`.

- The suite check reran with `--fresh`: job 261118-021353-62f2a6 never ran because the slot's
  `build` held a Unix Makefiles cache from the build job and the check configures Ninja there.
  My own jobs use their own build directories (`build-a4`, `build-mutant`).

- completion moves from f4c19b1 to d8b538c (completion main): faultfs overrides the seam members
  (`preallocate`, `map_shared`, `link`) that d8b538c adds; f4c19b1 is its ancestor, no journal
  API changes.
- faultlab links only to the targets that use faultfs (a `faultfs_tests` list, turn_bound and the
  fuzz targets); journal::journal does not link it.
- Hosts keep their directory: MemoryIO's `"host/"` prefix is faultfs directory `host`
  (`make_directory`, F4), so names inside are unchanged.
- Damage that wrote durable bytes directly is an overwrite and sync through the IO: at that point
  every damaged byte is already durable, so the state is the same, and no test reaches into the
  model's internals.
