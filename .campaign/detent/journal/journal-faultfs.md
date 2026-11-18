# journal-faultfs: journal

State: blocked on the lead (A4 finding, verdict (b)); waiting on jobs 261118-040049-318877 (suite, fresh checkout) 261118-040050-7b72ed (A1) 261118-040056-965498 (A4 reproducer)
Quest: .campaign/detent/quests/journal-faultfs.md (the lead's; never edit it)
Code: /Users/kronuz/code/Kronuz/journal-wt/detent/journal-faultfs (branch quest/detent/journal-faultfs)

## Doing

journal:c606068 (pushed): turn_log_power's "damages nothing" failure now prints the namespace
before and after each crash. Everything else is journal:37c2769's faultfs move.

## Next

1. The lead rules on the A4 finding below (a product fix is its own quest). Until it lands,
   turn_log_power fails at seeds 5 and 7, so A3's plain run, A6 and CI stay red.
2. After the fix: rerun A1, A5 fuzzers (`campaign job rerun`), the suite, A3's plain test; CI.

## Blocked on

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
| A1 | 261118-040050-7b72ed (journal:c606068); 261118-021429-6ea95f passed at 37c2769 |
| A2 | 261118-040049-318877 (suite, fresh checkout); red on turn_log_power until the A4 fix |
| A3 | fails without: 261118-021438-24c9d7 (mutant reached "FAIL: every acknowledged record survives"); plain run waits on the A4 fix |
| A4 | reported above; reproducer job 261118-040056-965498 |
| A5 | 261118-021430-5b9917, 261118-021432-aeda2f passed at 37c2769 (fuzz code unchanged since) |
| A6 | 261118-040049-318877; CI at c606068 red on turn_log_power until the A4 fix |

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
