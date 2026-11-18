# journal-faultfs: journal

State: blocked on the lead (A1: main's journal_test.cc includes memory_io.h); waiting on jobs 261118-072542-e8efc9 (suite, fresh) 261118-072547-893f8e (A3 mutant) 261118-072611-01fdf0 (200-seed power, A3 plain) 261118-072547-8c7169 261118-072548-43baa4 (A5 fuzz) 261118-072548-24b77f (A1, fails) 261118-072559-15ae05 (A1 at main 4d364f1)
Quest: .campaign/detent/quests/journal-faultfs.md (the lead's; never edit it)
Code: /Users/kronuz/code/Kronuz/journal-wt/detent/journal-faultfs (branch quest/detent/journal-faultfs)

## Doing

journal:6ea9494 (pushed) merges main at journal:4d364f1 (journal-reopen-closed, A9's ordered
reclaim) and redoes host_test.cc's faultfs move on main's version (same rules as the case map's
host rows). Local probe at 6ea9494 (`.scratch/build`, not evidence): host_test 608 checks, 0
failures, sealer_close_crash 75 cuts; turn_log_power 8 seeds: 2040 crash points (677 with a
second loss), 7641 checks, 0 failures. Cut numbers moved (2029 before: A9's ordering points are
mutations), so 5/116, 5/161 and 7/219 now land on other operations; the seed 5 and 7 sweeps
cover every cut of those seeds.

## Next

0. msg (lead: reopen-closed landed): done. Merged origin/main, host_test.cc redone, the power
   sweep at 200 seeds and every check queued (State).
1. The lead rules on A1 (Blocked on), then: fold journal_test.cc's move if it joins the file
   set; rerun A1, the suite and CI.
2. Fill Evidence from the jobs; ready for review.

## Blocked on

**A1 fails on main.** journal-reopen-closed's reopen tests (journal:4d364f1) put
`#include "journal/memory_io.h"` back in test/journal_test.cc:33, with `jt::Disk` and
`jt::MemoryIO(disk, "host/")`/`"log/"` at :1794-2192 (no cut or power loss: storage for heap
metering only). journal_test.cc is outside this quest's file set. Job 261118-072559-15ae05 runs
A1's grep at main 4d364f1. Options: (1) add test/journal_test.cc to the file set and I move it
(mechanical: `fl::filesystem`, `host_io`, a `log` directory; MeteredIO subtracts IO-call heap,
so the metering should hold, checked by the suite); (2) narrow A1 to exclude journal_test.cc
until step 8. Recommendation: (1).

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
| A1 | 261118-072548-24b77f at 6ea9494 (fails: journal_test.cc, Blocked on); main: 261118-072559-15ae05 |
| A2 | 261118-072542-e8efc9 (suite, fresh checkout, at 6ea9494) |
| A3 | fails without: 261118-072547-893f8e (mutant at 6ea9494); plain: 261118-072611-01fdf0 |
| A4 | reported (Resolved section); reproducer 261118-040056-965498 at c606068; fixed by reopen-closed A9: 261118-072611-01fdf0 (200 seeds, 0 failures expected) |
| A5 | 261118-072547-8c7169, 261118-072548-43baa4 (at 6ea9494) |
| A6 | 261118-072542-e8efc9; CI at 6ea9494 |

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
| sync_seam `journal_tail` | `disk.names` | `disk.list()` |
| framing, driver, sync_seam, turn_seal, turn_bound (non-crash), fuzz_framing, fuzz_turn_log, replay | `MemoryIO(disk, "dir/")` | `make_directory("dir")`, `io(disk, "dir")` |

## Result

Not final.

## Rulings I made

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
