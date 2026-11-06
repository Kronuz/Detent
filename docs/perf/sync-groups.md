> Status: DRAFT (2026-11-05) Grouped-sync qualification; bootstrap timings and startup-ordering proof remain open. Maintainer: Detent campaign.

# Grouped publication syncs

The seven-file publication fixture uses three device flushes with a 128-file window, compared with eight at window size one. The ext4 VM screen records the primitive costs below. Full receiver bootstrap and restart timings remain unmeasured.

## Publication operation counts

The native power-cut fixture on the S1/S2 candidate records completion's actual sync seam. `Detent:522452a` separates startup and publication counters. The positive test passes [7,186 power-cut/restart cases](../measurements/sync-groups-20261105/power-cut-positive.txt), but [removing the startup barrier](../measurements/sync-groups-20261105/startup-mutant.txt) still passes 5,493 cases. Qualification remains blocked on explaining or closing that gap. The [negative-check log](../measurements/sync-groups-20261105/proof-recovery.txt) records the surviving mutant.

| Window | Publication primitives | Synced files | Groups | Directory syncs | Device flushes (macOS) |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 | 37 | 7 | 7 | 8 | 8 |
| 2 | 34 | 7 | 4 | 5 | 5 |
| 128 | 32 | 7 | 2 | 3 | 3 |

These counts describe the fixture, not the projected 5M publication. The 5M flush counts need the real bootstrap run.

## Ext4 primitive screen

The November 5 queue run completes under an exclusive host hold. The Linux VM uses kernel `6.8.0-147-generic`, ext4 over virtio, with `rw,relatime,discard,errors=remount-ro,commit=30`. Each file is 1 MiB; the descriptor limit is 8,192. The 128-file case has nine rounds and the 1,445-file case has three. The [raw output](../measurements/sync-groups-20261105/ext4-probe.txt) includes each minimum and maximum.

| Primitive | 128 files, median ms | 1,445 files, median ms |
| --- | ---: | ---: |
| File sync and directory flush per file | 371.0 | 3,911.5 |
| Full sync per file | 370.9 | 3,888.4 |
| `IO::sync_files` | 88.6 | 778.5 |
| Concurrent fsync threads | 91.3 | 886.5 |
| fdatasync per file | 366.5 | 3,943.3 |
| One syncfs | 67.6 | 557.5 |

The helper builds `bench/sync_probe.cc` from the staged completion worktree (`completion:560c288`) with GNU C++20 and `-O2`. The existing VM executable has no embedded source identifier; its exact binary/source provenance needs confirmation before final platform qualification. This virtual disk screen supplies no production bootstrap, cost or power result.

## Remaining qualification

1. Explain or close the surviving startup-barrier mutant and rerun the restored positive test.
2. Qualify the consistent completion/journal/consensus pin closure and keep the unchanged manifest checker green.
3. Run the bounded all-arm smoke, then staged A/A and interleaved bootstrap/restart comparisons at 10K, 100K and 5M. Preserve both code-tree and harness identities with each result.
4. Confirm the VM executable provenance, record the accepted platform result in completion's `docs/PLATFORM_FINDINGS.md`, and complete the architect durability review.

The candidate's [workstream ledger](https://github.com/Kronuz/Detent/blob/namespace/s1-s2/docs/status/namespace-s1-s2.md) records the commands and gates. The [handoff](../handoffs/detent-codex-2026-11.md) retains the landing requirements.
