> Status: CURRENT (2026-11-18) Cold bootstrap, restart and publication sync counters of Dense pages and grouped syncs, alone and stacked, at 100K and 5M records. Mechanism: [sync-groups.md](../mechanisms/sync-groups.md). Maintainer: Detent campaign.

# Dense pages with grouped syncs: the stacked effect

Dense pages and grouped syncs stack. At 100K records the stacked tree publishes Dense's 17 files in S2's 2 groups with 3 device flushes, where Dense alone issues 35 and S2 alone publishes 63 files. The stacked agent selects its first generation in 6.09 s (median), 0.56 s (8%) faster than Dense alone and 2.89 s (32%) faster than S2 alone; the stacked runs do not overlap either single-change arm. Restart stays at 0.07 s in every arm, within noise. The 5M rows are pending (see [5M](#5m)).

## Method

Each run is one `exclusive` job: it builds the arm's tree in its own slot, smoke-tests it at 10K, then runs `docs/measurements/sync-groups-20261107/coldboot.py` with `--stage=confirm` on a fixed fixture and summarizes it with `docs/measurements/sync-groups-20261108/summarize.py`. The harness is `main`'s at `Detent:ea1fddc` for every arm; each arm builds only its own `detentd`, `detent-agent` and `detent-relay`. The fixture is restored by `cp -cR`, never written.

- **Cold bootstrap**: seconds from starting the relay and agent until the agent selects the generation holding the fixture's revision (`agent_selected`).
- **Restart**: the median of three restarts of `detent-agent` on its existing store, each until `store ready`.
- **Sync counters**: the agent's "publication synced" line and the relay's status (`publication_files`, `publication_groups`, `publication_device_flushes`, `publication_directory_syncs`).

| Arm | Tree | What it holds |
| --- | --- | --- |
| Dense alone | `Detent:9852a3c` | Dense pages (DTPAGE03) and the sync counters, per-file syncs |
| S2 alone | `Detent:35296ba` | Grouped syncs (S1/S2) on the flat image |
| Stacked | `Detent:d04794d` | The first tree with both |
| Main | `Detent:ea1fddc` | `main` at the quest's start: both, plus the agent store |

Dense, S2 and stacked read the 2026-10-23 fixtures (`.scratch/capacity-trials/100000-r3`, and `5000000-flat-r1` at 5M). Main cannot read a state seeded before 2026-11-12 (its authority fences while replaying the old journal), so it runs on fixtures of the same scenarios seeded at its own tree (`100000-main`, `5000000-flat-main`; [fixtures](../measurements/dense-sync-stack-20261118/README.md)). Main's times therefore compare with nothing else here; its counters do.

Builds are x86_64 release builds (`file` is checked in every job) on an Intel Core i9-9980HK (16 threads, 32 GiB), macOS, where a sync window is one `F_FULLFSYNC`. At 100K the A/A is each arm's range over three interleaved rounds; at 5M it is a fourth arm, the stacked tree again in its own slot.

## 100K

Times are seconds; the range is min to max over the rounds.

| Arm | Rounds | Boot median | Boot range | Restart median | Files | Groups | Device flushes | Directory syncs |
| --- | ---: | ---: | --- | ---: | ---: | ---: | ---: | ---: |
| Dense alone | 3 | 6.65 | 6.59 to 6.90 | 0.069 | 17 | 0 | 35 | 18 |
| S2 alone | 3 | 8.98 | 8.35 to 10.24 | 0.067 | 63 | 2 | 3 | 3 |
| Stacked | 2 | 6.09 | 5.78 to 6.39 | 0.071 | 17 | 2 | 3 | 3 |
| Main (own fixture) | 2 | 3.80 | 3.77 to 3.82 | 0.068 | 11 | 2 | 4 | 3 |

Reading the table:

1. **The counters stack exactly.** Stacked publishes Dense's 17 files with S2's 2 groups, 3 directory syncs and 3 device flushes. Every round of every arm reports the same counters.
2. **Stacked beats S2 alone by 2.89 s (32%)** in median cold bootstrap; the ranges are 5.78 to 6.39 against 8.35 to 10.24. S2 publishes 63 files to stacked's 17 for the same records.
3. **Stacked beats Dense alone by 0.56 s (8%)**; the ranges (5.78 to 6.39 against 6.59 to 6.90) do not overlap, but the gap is about the size of S2's own spread. Grouping removes 32 device flushes; at the 25 ms per flush that `sync-groups.md` estimated at 100K, that is 0.8 s, the same order as the gain.
4. **Restart is within noise** in every arm (0.067 to 0.071 s medians, single restarts 0.062 to 0.085 s).
5. **Main reports groups at 100K**: "publication synced 11 files in 2 groups, 3 directory syncs, 4 device flushes" (A1's log line on `main`).

## 5M

Pending: Dense, S2 and stacked rounds 1 and 2 and the A/A round 2 are queued `exclusive` jobs, and main's two rounds wait for its fixture's seed (`261118-070640-2fb122`). The first A/A round (stacked tree, `261118-064421-90a1aa`) selected in 137.2 s and published 371 files in 4 groups with 5 device flushes; on 2026-11-07 Dense alone published 609 files in 0 groups with 1,219 device flushes and selected in 350 to 361 s ([screen](../measurements/dense-cold-bootstrap-20261107/README.md)).

## Runs

Every run passed. `Boot` is cold bootstrap; `Restart` is the median of three restarts.

| Records | Arm | Round | Job | Boot (s) | Restart (s) | Files | Groups | Device flushes |
| --- | --- | ---: | --- | ---: | ---: | ---: | ---: | ---: |
| 100K | Dense alone | 1 | `261118-054306-b5fe77` | 6.90 | 0.069 | 17 | 0 | 35 |
| 100K | S2 alone | 1 | `261118-054307-5a3cb1` | 10.24 | 0.074 | 63 | 2 | 3 |
| 100K | Stacked | 1 | `261118-054308-15108e` | 6.39 | 0.074 | 17 | 2 | 3 |
| 100K | Dense alone | 2 | `261118-054310-9848a1` | 6.65 | 0.069 | 17 | 0 | 35 |
| 100K | S2 alone | 2 | `261118-054311-2e72e9` | 8.35 | 0.063 | 63 | 2 | 3 |
| 100K | Stacked | 2 | `261118-054312-c42fcf` | 5.78 | 0.068 | 17 | 2 | 3 |
| 100K | Dense alone | 3 | `261118-054314-792ca7` | 6.59 | 0.068 | 17 | 0 | 35 |
| 100K | S2 alone | 3 | `261118-054315-78f04e` | 8.98 | 0.067 | 63 | 2 | 3 |
| 100K | Stacked | 3 | `261118-054316-9f93de` | pending | | | | |
| 100K | Main | 1 | `261118-070747-d672ef` | 3.77 | 0.069 | 11 | 2 | 4 |
| 100K | Main | 2 | `261118-070751-839960` | 3.82 | 0.067 | 11 | 2 | 4 |
| 100K | Main | 3 | `261118-070752-576dde` | pending | | | | |
| 5M | A/A (stacked tree) | 1 | `261118-064421-90a1aa` | 137.20 | 0.117 | 371 | 4 | 5 |
