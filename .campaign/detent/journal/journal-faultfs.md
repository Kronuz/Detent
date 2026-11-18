# journal-faultfs: journal

State: working
Quest: .campaign/detent/quests/journal-faultfs.md (the lead's; never edit it)
Code: /Users/kronuz/code/Kronuz/journal-wt/detent/journal-faultfs (branch quest/detent/journal-faultfs)

## Doing

journal:37c2769 moves every test and fuzzer off memory_io.h onto faultfs (faultlab:a452a8c,
test-only FetchContent after completion). Build job 261118-020820-c60ee1 running.

## Next

1. Build green, then the suite (`campaign verify --for A2,A6`), the A1 grep, the A3 mutant, the
   two 10-minute fuzz runs (A5).
2. Push the branch for CI.

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | pending |
| A2 | pending |
| A3 | pending |
| A4 | pending |
| A5 | pending |
| A6 | pending |

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
