> Status: CURRENT (2026-11-09) Cold-bootstrap and restart timings before and after sync groups (S1/S2), with the publication sync counters and the ext4 primitive screen. Mechanism: [sync-groups.md](../mechanisms/sync-groups.md). Maintainer: Detent campaign.

# Sync groups: cold-bootstrap and restart timings

Sync groups cut cold bootstrap by about 25% at 100K and 5M records and by about 31% at 10K, and leave restart time within noise at 10K and 100K. At 5M the restart median falls from 1.354 s to 0.446 s, from one run per arm whose own spread is of the same size. The saving comes from a publication that syncs in two groups of windows instead of one device flush per file ([mechanism](../mechanisms/sync-groups.md)): the 5M publication issues 25 device flushes where the per-file shape issued 5,775.

## Method

Each run is one `docs/measurements/sync-groups-20261108/arm.sh ARM RECORDS ROUND` exclusive job. It restores three authority voters from a fixed fixture (`.scratch/capacity-trials/10000-r3`, `100000-r3`, `5000000-flat-r1`), starts one relay and one agent, and measures:

- **Cold bootstrap**: seconds from starting the relay and agent until the agent selects the generation that holds the fixture's revision (`agent_selected`).
- **Restart**: seconds from starting `detent-agent` on its existing store until it logs `store ready`, three restarts in a row after the bootstrap. The run's restart figure is the median of the three.
- **Sync counters**: the agent's "publication synced" line and the relay's status JSON (`sync_*`, `publication_*`), read by `summarize.py --require-counters`.

Every job runs the harness at `Detent:35296ba`. Before builds `Detent:327ca58`, the tree before S1/S2. After builds a tree with no difference in `CMakeLists.txt`, `cmake`, `include`, `src` or `tests` from `Detent:35296ba`, the landed S1/S2 code (`Detent:dbf4f8d` on `main`). Both are x86_64 release builds on an Intel Core i9-9980HK (16 threads, 32 GiB) under an exclusive hold, on macOS, where a `SyncFiles` window is one `F_FULLFSYNC`.

10K and 100K have three runs per arm. 5M has one run per arm: its spread is unmeasured, and each 5M figure is a single observation.

## Results

Times are seconds. The spread is the range (max - min) across the three runs of an arm, the A/A for that arm; the median is the median of the three runs. A run's restart figure is the median of its three restarts.

| Records | Measure | Before median | Before spread | After median | After spread | Change |
| --- | --- | ---: | ---: | ---: | ---: | --- |
| 10K | Cold bootstrap | 2.05 | 0.17 | 1.42 | 0.14 | -0.63 s (-31%) |
| 10K | Restart | 0.067 | 0.004 | 0.069 | 0.004 | within noise |
| 100K | Cold bootstrap | 12.70 | 0.15 | 9.54 | 0.48 | -3.16 s (-25%) |
| 100K | Restart | 0.071 | < 0.001 | 0.070 | 0.002 | within noise |
| 5M | Cold bootstrap | 619.5 | one run | 463.2 | one run | -156.3 s (-25%) |
| 5M | Restart | 1.354 | one run | 0.446 | one run | -0.91 s, indicative only |

Reading the table:

1. **Cold bootstrap is a real gain at 10K and 100K.** The arms' ranges do not overlap (10K: 1.89 to 2.06 against 1.32 to 1.46; 100K: 12.61 to 12.76 against 9.15 to 9.63), and each gain is several times the larger spread.
2. **The 5M bootstrap gain matches the 100K ratio** (-25% in both) and is 156 s against spreads of 0.15 s and 0.48 s at 100K, but it is one run per arm and carries no measured A/A.
3. **Restart at 10K and 100K is within noise.** The medians differ by 0.002 s and 0.001 s, below the 0.004 s and 0.002 s spreads.
4. **Restart at 5M is not established.** The medians are 1.354 s before and 0.446 s after, but the three restarts inside one run span 0.659 to 1.595 s before and 0.427 to 0.828 s after; the ranges overlap, and there is no second run.
5. **The first restart is faster at 10K and 100K; later restarts are not.** The first restart takes 0.083, 0.084 and 0.088 s before and 0.069, 0.065 and 0.065 s after at 10K, and 0.091 s in all three runs before against 0.069, 0.072 and 0.072 s after at 100K: about 0.02 s, with non-overlapping ranges. Restarts two and three take 0.063 to 0.072 s in both arms. These runs do not isolate why the first restart differs.

The bootstrap gain divided by the device flushes avoided is similar across sizes, which suggests the saving is the flushes: 0.63 s over 20 flushes at 10K (about 32 ms each), 3.16 s over 124 at 100K (about 25 ms) and 156.3 s over 5,750 at 5M (about 27 ms). The arithmetic assumes the whole difference is flush time; the runs do not isolate it.

## Runs

Every run passed. `Boot` is cold bootstrap; `Restarts` lists the three restarts in order; `Median` is their median.

| Records | Arm | Round | Job | Boot (s) | Restarts (s) | Median (s) |
| --- | --- | ---: | --- | ---: | --- | ---: |
| 10K | before | 1 | `261109-125239-019c9c` | 2.06 | 0.083, 0.064, 0.063 | 0.064 |
| 10K | before | 2 | `261109-121537-aa665e` | 2.05 | 0.084, 0.068, 0.067 | 0.068 |
| 10K | before | 3 | `261109-121537-59e880` | 1.89 | 0.088, 0.067, 0.065 | 0.067 |
| 10K | after | 1 | `261109-125239-e9ce13` | 1.46 | 0.069, 0.070, 0.068 | 0.069 |
| 10K | after | 2 | `261109-121537-ef207f` | 1.32 | 0.065, 0.069, 0.070 | 0.069 |
| 10K | after | 3 | `261109-125238-294dec` | 1.42 | 0.065, 0.065, 0.065 | 0.065 |
| 100K | before | 1 | `261109-125239-90fffe` | 12.76 | 0.091, 0.071, 0.071 | 0.071 |
| 100K | before | 2 | `261109-125238-d9860a` | 12.61 | 0.091, 0.071, 0.070 | 0.071 |
| 100K | before | 3 | `261109-125239-2750d0` | 12.70 | 0.091, 0.071, 0.071 | 0.071 |
| 100K | after | 1 | `261109-125238-e2ca32` | 9.54 | 0.069, 0.070, 0.071 | 0.070 |
| 100K | after | 2 | `261109-125239-826187` | 9.63 | 0.072, 0.072, 0.071 | 0.072 |
| 100K | after | 3 | `261109-121537-2817a2` | 9.15 | 0.072, 0.070, 0.066 | 0.070 |
| 5M | before | 1 | `261109-125238-dffa9c` | 619.53 | 1.595, 1.354, 0.659 | 1.354 |
| 5M | after | 1 | `261109-125239-dd7342` | 463.22 | 0.828, 0.427, 0.446 | 0.446 |

The log of each job (`campaign job show ID`) ends with a `SUMMARY` line for the timed run, after an earlier one for the 10K smoke on the same binaries.

## Sync counters

The relay's counters are identical across the repetitions of an arm. The publication columns are the last published generation; the total adds the startup barrier (2 device flushes in every run).

| Records | Arm | Files | Groups | Directory syncs | Device flushes | Total device flushes |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| 10K | before | 11 | 0 | 12 | 23 | 25 |
| 10K | after | 11 | 2 | 3 | 3 | 5 |
| 100K | before | 63 | 0 | 64 | 127 | 129 |
| 100K | after | 63 | 2 | 3 | 3 | 5 |
| 5M | before | 2,887 | 0 | 2,888 | 5,775 | 5,777 |
| 5M | after | 2,887 | 24 | 25 | 25 | 27 |

Before, a publication syncs each file and its directory, one flush each, and counts no groups. After, a window of up to 128 files is one `F_FULLFSYNC`: the 10K and 100K publications fit one window in each of the two groups, and the 5M publication takes 12 windows in each (see [Flush counts](../mechanisms/sync-groups.md#flush-counts)).

## Publication operation counts

The native power-cut fixture records completion's actual sync seam for a seven-file publication. `Detent:522452a` separates startup and publication counters.

| Window | Publication primitives | Synced files | Groups | Directory syncs | Device flushes (macOS) |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 | 37 | 7 | 7 | 8 | 8 |
| 2 | 34 | 7 | 4 | 5 | 5 |
| 128 | 32 | 7 | 2 | 3 | 3 |

These counts describe the fixture; the real publication counts are in the table above.

## Ext4 primitive screen

The Linux VM runs kernel `6.8.0-147-generic` with ext4 over virtio, mounted `rw,relatime,discard,errors=remount-ro,commit=30`, under an exclusive host hold. Each file is 1 MiB; the descriptor limit is 8,192. The 128-file case has nine rounds and the 1,445-file case has three. The [raw output](../measurements/sync-groups-20261105/ext4-probe.txt) includes each minimum and maximum.

| Primitive | 128 files, median ms | 1,445 files, median ms |
| --- | ---: | ---: |
| File sync and directory flush per file | 371.0 | 3,911.5 |
| Full sync per file | 370.9 | 3,888.4 |
| `IO::sync_files` | 88.6 | 778.5 |
| Concurrent fsync threads | 91.3 | 886.5 |
| fdatasync per file | 366.5 | 3,943.3 |
| One syncfs | 67.6 | 557.5 |

The helper builds `bench/sync_probe.cc` from the staged completion worktree (`completion:560c288`) with GNU C++20 and `-O2`. The VM executable carries no embedded source identifier. This virtual-disk screen is not a production bootstrap, cost or power result, and this page holds no Linux bootstrap or restart timing.
