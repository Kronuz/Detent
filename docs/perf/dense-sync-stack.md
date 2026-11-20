> Status: CURRENT (2026-11-19) Cold bootstrap, restart and publication sync counters of Dense pages and grouped syncs, alone and stacked, at 100K and 5M records. Mechanism: [sync-groups.md](../mechanisms/sync-groups.md). Maintainer: Detent campaign.

# Dense pages with grouped syncs: the stacked effect

Dense pages and grouped syncs stack. At 100K records the stacked tree (`Detent:d04794d`) publishes Dense's 17 files in S2's 2 groups with 3 device flushes, where Dense alone issues 35 and S2 alone publishes 63 files; at 5M it publishes Dense's 371 files in 4 groups with 5 device flushes, where Dense alone issues 743 and S2 alone publishes 2,653 files. The landing code (`main`'s agent store, which syncs the descriptor table alone) issues 4 device flushes per publication on macOS, measured at this branch's head at 100K and on `main` at 100K and 5M. At 100K the stacked agent selects its first generation in 5.78 s (median), 0.88 s (13%) faster than Dense alone and 3.20 s (36%) faster than S2 alone; the stacked runs overlap neither single-change arm. Restart stays at 0.07 s in every arm, within noise. The 5M times are not measured here; see quest `dense-sync-stack-5m-times`.

## Method

Each run is one job: it builds the arm's tree in its own slot, smoke-tests it at 10K, then runs `docs/measurements/sync-groups-20261107/coldboot.py` with `--stage=confirm` on a fixed fixture and summarizes it with `docs/measurements/sync-groups-20261108/summarize.py`. The harness is `main`'s at `Detent:ea1fddc` for every arm; each arm builds only its own `detent-authority`, `detent-agent` and `detent-relay`. The fixture is restored by `cp -cR`, never written.

- **Cold bootstrap**: seconds from starting the relay and agent until the agent selects the generation holding the fixture's revision (`agent_selected`).
- **Restart**: the median of three restarts of `detent-agent` on its existing store, each until `store ready`.
- **Sync counters**: the agent's "publication synced" line and the relay's status (`publication_files`, `publication_groups`, `publication_device_flushes`, `publication_directory_syncs`).
- **Device flushes**: on macOS a sync window (a group, or the directory alone) is one `F_FULLFSYNC`; elsewhere each file and the directory is one `fsync` call (not `fdatasync`), so a group of n files counts n + 1 (`src/detent_local/agent_store.cc`, `sync_group`).

The 100K arms run under `exclusive`, so their times compare. The 5M arms count files and flushes only and run under `light`, because counters do not depend on load; four earlier `exclusive` 5M runs (Dense and S2 round 1, the A/A pair) count the same way and are listed with them.

| Arm | Tree | What it holds |
| --- | --- | --- |
| Dense alone | `Detent:9852a3c` | Dense pages (DTPAGE03) and the sync counters, per-file syncs |
| S2 alone | `Detent:35296ba` | Grouped syncs (S1/S2) on the flat image |
| Stacked | `Detent:d04794d` | The first tree with both |
| Main | `Detent:ea1fddc` | `main` at the quest's start: both, plus the agent store |
| Branch head | `Detent:9f4f681` | The landing code: `main`'s agent store plus this quest's tests |

Dense, S2 and stacked read the 2026-10-23 fixtures (`.scratch/capacity-trials/100000-r3`, and `5000000-flat-r1` at 5M). Main cannot read a state seeded before 2026-11-12, so main and the branch head run on fixtures of the same scenarios seeded at main's tree (`100000-main`, `5000000-flat-main`; [fixtures](../measurements/dense-sync-stack-20261118/README.md)). Main's times therefore compare with nothing else here; its counters do.

Builds are x86_64 release builds (`file` is checked in every job) on an Intel Core i9-9980HK (16 threads, 32 GiB), macOS. At 100K the A/A is each arm's range over three interleaved rounds; at 5M it is the stacked tree again in its own slot.

## 100K

Times are seconds; the range is min to max over the rounds.

| Arm | Rounds | Boot median | Boot range | Restart median | Files | Groups | Device flushes | Directory syncs |
| --- | ---: | ---: | --- | ---: | ---: | ---: | ---: | ---: |
| Dense alone | 3 | 6.65 | 6.59 to 6.90 | 0.069 | 17 | 0 | 35 | 18 |
| S2 alone | 3 | 8.98 | 8.35 to 10.24 | 0.067 | 63 | 2 | 3 | 3 |
| Stacked (`d04794d`) | 3 | 5.78 | 5.72 to 6.39 | 0.068 | 17 | 2 | 3 | 3 |
| Main (own fixture) | 3 | 3.82 | 3.77 to 3.89 | 0.067 | 11 | 2 | 4 | 3 |
| Branch head (own fixture, counters only) | 1 | | | | 11 | 2 | 4 | 3 |

Reading the table:

1. **The counters stack exactly.** The stacked tree publishes Dense's 17 files with S2's 2 groups, 3 directory syncs and 3 device flushes. Every round of every arm reports the same counters.
2. **The landing code issues 4 device flushes, not 3.** `main` and the branch head sync the descriptor table alone (`agent_store.cc`), one window more than `d04794d`, which synced the descriptor in the payload group.
3. **Stacked beats S2 alone by 3.20 s (36%)** in median cold bootstrap; the ranges are 5.72 to 6.39 against 8.35 to 10.24. S2 publishes 63 files to stacked's 17 for the same records.
4. **Stacked beats Dense alone by 0.88 s (13%)**; the ranges (5.72 to 6.39 against 6.59 to 6.90) do not overlap. Grouping removes 32 device flushes; at the 25 ms per flush that `sync-groups.md` estimated at 100K, that is 0.8 s, the size of the gain.
5. **Restart is within noise** in every arm (0.067 to 0.069 s medians, single restarts 0.062 to 0.086 s).
6. **Main reports groups at 100K**: "publication synced 11 files in 2 groups, 3 directory syncs, 4 device flushes" (A1's log line on `main`).

## 5M

Counters only, two rounds per arm; every round of an arm reports the same counters. Times: not measured here; see `dense-sync-stack-5m-times`.

| Arm | Rounds | Files | Groups | Device flushes | Directory syncs |
| --- | ---: | ---: | ---: | ---: | ---: |
| Dense alone | 2 | 371 | 0 | 743 | 372 |
| S2 alone | 2 | 2,653 | 22 | 23 | 23 |
| Stacked (`d04794d`) | 2 | 371 | 4 | 5 | 5 |
| A/A (stacked tree, own slot) | 2 | 371 | 4 | 5 | 5 |
| Main (own fixture) | 2 | 243 | 2 | 4 | 3 |

1. **The counters stack at 5M too.** The stacked tree publishes Dense's 371 files in 4 groups with 5 device flushes: 738 fewer than Dense alone, and S2's 2,653 files become 371.
2. **The landing code keeps 2 groups and 4 device flushes** at 5M, as at 100K: the flush count no longer grows with the records. The stacked tree's 4 groups and 5 flushes are `d04794d`'s grouping, not the landing code's.

## Runs

Every run passed. `Boot` is cold bootstrap; `Restart` is the median of three restarts. 5M times are not measured here (see `dense-sync-stack-5m-times`).

| Records | Arm | Round | Level | Commit | Job | Boot (s) | Restart (s) | Files | Groups | Device flushes |
| --- | --- | ---: | --- | --- | --- | ---: | ---: | ---: | ---: | ---: |
| 100K | Dense alone | 1 | exclusive | `Detent:9852a3c` | `261118-054306-b5fe77` | 6.90 | 0.069 | 17 | 0 | 35 |
| 100K | S2 alone | 1 | exclusive | `Detent:35296ba` | `261118-054307-5a3cb1` | 10.24 | 0.074 | 63 | 2 | 3 |
| 100K | Stacked | 1 | exclusive | `Detent:d04794d` | `261118-054308-15108e` | 6.39 | 0.074 | 17 | 2 | 3 |
| 100K | Dense alone | 2 | exclusive | `Detent:9852a3c` | `261118-054310-9848a1` | 6.65 | 0.069 | 17 | 0 | 35 |
| 100K | S2 alone | 2 | exclusive | `Detent:35296ba` | `261118-054311-2e72e9` | 8.35 | 0.063 | 63 | 2 | 3 |
| 100K | Stacked | 2 | exclusive | `Detent:d04794d` | `261118-054312-c42fcf` | 5.78 | 0.068 | 17 | 2 | 3 |
| 100K | Dense alone | 3 | exclusive | `Detent:9852a3c` | `261118-054314-792ca7` | 6.59 | 0.068 | 17 | 0 | 35 |
| 100K | S2 alone | 3 | exclusive | `Detent:35296ba` | `261118-054315-78f04e` | 8.98 | 0.067 | 63 | 2 | 3 |
| 100K | Stacked | 3 | exclusive | `Detent:d04794d` | `261118-054316-9f93de` | 5.72 | 0.067 | 17 | 2 | 3 |
| 100K | Main | 1 | exclusive | `Detent:ea1fddc` | `261118-070747-d672ef` | 3.77 | 0.069 | 11 | 2 | 4 |
| 100K | Main | 2 | exclusive | `Detent:ea1fddc` | `261118-070751-839960` | 3.82 | 0.067 | 11 | 2 | 4 |
| 100K | Main | 3 | exclusive | `Detent:ea1fddc` | `261118-070752-576dde` | 3.89 | 0.067 | 11 | 2 | 4 |
| 100K | Branch head | 1 | light | `Detent:9f4f681` | `261118-181141-48bd7b` | not timed | not timed | 11 | 2 | 4 |
| 5M | Dense alone | 1 | exclusive | `Detent:9852a3c` | `261118-064403-1e5071` | not measured here | | 371 | 0 | 743 |
| 5M | S2 alone | 1 | exclusive | `Detent:35296ba` | `261118-064414-eebae8` | not measured here | | 2,653 | 22 | 23 |
| 5M | Stacked | 1 | light | `Detent:d04794d` | `261118-223833-b8f344` | not measured here | | 371 | 4 | 5 |
| 5M | A/A (stacked tree) | 1 | exclusive | `Detent:d04794d` | `261118-064421-90a1aa` | not measured here | | 371 | 4 | 5 |
| 5M | Main | 1 | light | `Detent:ea1fddc` | `261118-223852-5bc14b` | not measured here | | 243 | 2 | 4 |
| 5M | Dense alone | 2 | light | `Detent:9852a3c` | `261118-223846-fbe1b6` | not measured here | | 371 | 0 | 743 |
| 5M | S2 alone | 2 | light | `Detent:35296ba` | `261118-223849-9abd84` | not measured here | | 2,653 | 22 | 23 |
| 5M | Stacked | 2 | light | `Detent:d04794d` | `261118-223851-2251bb` | not measured here | | 371 | 4 | 5 |
| 5M | A/A (stacked tree) | 2 | exclusive | `Detent:d04794d` | `261118-064427-8657da` | not measured here | | 371 | 4 | 5 |
| 5M | Main | 2 | light | `Detent:ea1fddc` | `261118-223854-772604` | not measured here | | 243 | 2 | 4 |
