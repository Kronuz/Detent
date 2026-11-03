# Agent restart and cold bootstrap attribution

Branch `explore/agent-startup`, base `github/main` 09e6930. Host: Intel i9-9980HK, macOS 15.8.1, 32 GiB, no SHA-NI (the `sha256` kernel falls back to `compress_portable`). Release build, `detent-agent`/`detent-relay`/`detentd`. All timed numbers ran under `quiet exclusive` (restarts) or `quiet strict`, which on macOS is exclusive. `purge` needs sudo, so every number is warm page cache (the first run after a fresh clone is also 121 s; the 5M store does not fit in a cold-vs-warm difference here).

## Answers

1. A restart over a prepared 5M store takes 118-122 s on current main. It is two sequential O(N) phases, not one:
   - 52 s semantic recovery: re-hashes every byte six times (SHA-256, portable kernel, 9.1 GB at 223 MB/s = 40.7 s) and decodes 5M definitions (about 11 s).
   - 55 s startup census: `F_FULLFSYNC` on each of the 2,882 segment files (1,441 packs + 1,441 owner records), 18.9 ms each, even though nothing is dirty.
   - the rest (about 8 s) is process start, inventory, opens, reads.
   Nothing is page-fault, registry, or index rebuild; the `.read` metadata is only compared at the end.
2. Cold bootstrap at 5M takes 722.6 s (12.0 min), not the 17 min extrapolation. Breakdown below: authority export about 170 s overlapped with relay decode, relay build and verify, then 240 s of `F_FULLFSYNC` across relay and agent.
3. Scaling is linear in records; see the table.
4. The on-disk format is binary, fixed-width big-endian, mmap'd. Restart reads all of it anyway (below).
5. Delta from an arbitrary old cursor: no. The authority keeps 4096 transactions / 16 MiB in memory, reset on every restart. The relay serves only an edge whose base is exactly the asked cursor, from 64 retained edges. See the answer section.

## Restart, 5M prepared store (managed-5m-r3 clone, cursor 39064)

Wall to "managed local store ready". Same binary family; arms are env switches on the branch.

| Arm | What | Runs (s) | User CPU s |
| --- | --- | --- | --- |
| M | main | 121.24, 122.06, 121.27, 121.89, 118.49, 118.45 | 48.4-48.9 |
| S | skip per-file startup sync | 56.16, 62.32 | 48.6-53.6 |
| R | skip the second hash pass (graph validator) | 105.53, 105.10 | 32.0-32.4 |
| B | S + R | 36.77, 37.23 | 29.2-29.7 |
| A | B + skip all digests (structure walk only) | 17.73, 17.80 | 10.5-10.7 |

M/M spread: 118.4-122.1 s (3%, direction is run order and background load, no trend). Direction of every ratio: lower wall is better. S is 2.0x faster than M, B 3.3x, A 6.8x. S's two runs differ by 6 s, so S is 56-62 s.

Identity per arm run: `selected` byte-identical to the base (no republish), and 40 fresh prepared-reader probes of the benchmark cases (`benchmarks/prepared-pack-access-5m-20261025.json`) return the expected values with 0 failures in every run (see `.scratch/restart/ab1.out`, `ab2.out`). The recovery's counts must equal the `.read` metadata (`PreparedReader::metadata_ready`, `src/detent_delivery/coordinator.cc:208`); every arm reports "ready", so they matched. Caveat: arms R and A deliberately skip digest checks, so they prove speed, not safety; they would accept a corrupted pack.

### Profile (`sample`, 5 ms, main, 22,389 samples)

| Where | Samples | Seconds | File:line (main) |
| --- | ---: | ---: | --- |
| `F_FULLFSYNC` per file in startup census | 10,910 | 54.5 | `src/detent_local/disk_store.cc:132` (after read) and `:171` (after open) request `Kind::Sync`; journal `PosixFile::sync` is `F_FULLFSYNC` on macOS |
| SHA-256, segment hash in discovery | 2,674 | 13.4 | `src/detent_local/paged_dependency_discovery.cc:117-131` |
| SHA-256, page hash (physical + logical), discovery walk | 2,190 | 11.0 | `paged_dependency_walk.cc:106` -> `paged_page_validator.cc:74-131` |
| SHA-256, page hash, graph validation | 2,134 | 10.7 | `paged_graph_validator.cc:266` |
| SHA-256, segment hash again, graph validation | 1,133 | 5.7 | `paged_graph_validator.cc:201-221` |
| decode and graph walk, rest of recovery | about 2,300 | about 11 | `paged_graph_validator.cc:222-330` |

The worker thread's recovery (`RecoveryEngine::recover`, `src/detent_local/generation.cc:201`) runs first and the census second; the two phases sum to the restart wall time. The 6 hash passes are: discovery segment, discovery page x2 (physical and logical digests), graph segment, graph page x2. Each page is hashed twice per walk and each segment once per pass.

## Cold bootstrap

`capacity_bootstrap_selected`-equivalent: harness cohort start (relay + agent, empty stores) to the agent's `selected` file at the authority revision. Preserved authority states, current main, one trial each. 5M and 100K had `sample` attached (about 10 ms); 10K matches the recorded 2.7 s, 100K is 15.8 s (recorded 17.9 s).

| Records | Total s | Relay ready s | Agent after relay s | us/knob |
| ---: | ---: | ---: | ---: | ---: |
| 10,000 | 2.72 | 2.03 | 0.69 | 272 |
| 100,000 | 15.81 | 11.42 | 4.39 | 158 |
| 5,000,000 | 722.6 | 492.3 | 230.3 | 145 |

5M timeline (process CPU seconds from the harness samples, wall in s from cohort start):

| Window | What runs | Evidence |
| --- | --- | --- |
| 0-170 | authority snapshot export on one voter: 78 CPU s | authority-0 CPU 36.7 to 115.1 |
| 170-313 | relay decodes and builds the image: 86 CPU s | relay CPU 13.7 to 87.7 |
| 313-440 | relay durable publication: about 2 CPU s per 30 s, i.e. waiting on fsync | relay CPU flat 87.7 to 94.5 |
| 440-492 | relay recovery/verification (hashing): 51 CPU s | relay CPU 94.5 to 144.9; ready at 492.3 |
| 492-574 | agent receives, decodes, builds image: 63 CPU s | agent CPU 2.5 to 66 |
| 574-722 | agent durable publication and census: about 1.3 CPU s per 30 s, waiting on fsync | agent CPU 66 to 72 |

Whole-run thread totals (10 ms samples): relay `F_FULLFSYNC` 120.4 s (file sync 61.9 s + `sync_directory` 58.5 s), agent 118.9 s (61.4 + 57.5). Together 239 s, 33% of the 722.6 s wall. SHA-256: relay 78 s (recovery 36 s, snapshot build 20 s, candidate validation 22 s), agent 34 s (build and candidate validation). The wire transfer and msgpack decode are small: the agent's decoder frames total 2.3 s of its 72 s CPU. The authority export was not sampled (its sampler did not attach); its CPU curve is above. Authority export, wire and relay decode overlap (0-313 s), so the table is not additive; transfer cost cannot be separated from decode with this data.

Per-stage split of the 722.6 s, by what the critical path is waiting on: export+decode+build 313 s (43%), `F_FULLFSYNC` 239 s (33%) plus hashing 112 s (15%), remainder 58 s (8%, polling, handoff).

## Scaling (warm)

| Records | Restart M s | Restart B (S+R) s | Bootstrap s |
| ---: | ---: | ---: | ---: |
| 10,000 | 0.38, 0.39, 0.50 | 0.14, 0.15, 0.15 | 2.72 |
| 100,000 | 2.54, 2.50, 2.46 | 0.78, 0.81, 0.81 | 15.81 |
| 1,000,000 | about 26 (interpolated) | about 8 (interpolated) | about 145 (interpolated) |
| 5,000,000 | 118-122 | 37 | 722.6 |

No 1M store is preserved and re-seeding costs about 17 minutes of fsync-bound writes, so the 1M row is the linear fit through the 100K and 5M points (restart slope 24 us/knob; bootstrap about 145 us/knob). It is not measured. 10K and 100K are within a few percent of the fit, so the per-knob cost is flat. The 10K and 100K stores hold 3 and 40 packs, so the per-file `F_FULLFSYNC` is small there; restart is already below 3 s at 100K.

## What the agent keeps on disk, and what a restart reads

Binary, fixed-width, big-endian, with magic words; no JSON anywhere in the store. Packs are `mmap`'d `PROT_READ, MAP_SHARED` (`generation.cc` recovery, `MAP_FAILED` check).

| File | Size | Magic | Notes |
| --- | ---: | --- | --- |
| `selected` | 176 B | `DTSELECT` | descriptor: generation, cursor, image length, SHA-256 |
| `generation-<id>.image` | 320 B | root | SHA-256 checked against descriptor |
| `generation-<id>.lease` | 128 B | | ownership record, `flock` |
| `generation-<id>.read` | 192 B | `DTREAD01` | prepared metadata: storage id, generation, checksum, counts, physical bytes |
| `segment-<id>.pack` | about 1 MB each, 1,441 at 5M, 1.51 GB total | `DTPAGE02` | pages, Merkle-style physical and logical digests |
| `segment-<id>.owner` | small | | per-segment ownership and checksum |
| `storage.meta`, `owner.lock` | 64 B, 0 B | | |

A restart reads or recomputes, in order: `selected`, `storage.meta`, lease, image (SHA-256), then for every segment the owner record, an `mmap` of the pack and its full SHA-256 against the owner record, then the whole page tree twice (physical and logical page digests, definition decoding, key ordering, counts), then a census of the directory (readdir, `fstatat` of each of 2,890 entries, open and read of each owner record, open of each pack) with `F_FULLFSYNC` of each, then a directory barrier. It compares the recomputed counts with `.read`, writes nothing, and republishes nothing. Everything checked is immutable data that was already verified when it was published.

The reader fast path (0.7 ms per fresh SDK process, `docs/reviews/prepared-managed-qualification-2026-10-26.md`) skips all of this because it trusts `.read` plus the publisher's prior verification. The publisher does not trust its own prior work.

## Delta from an old cursor

Not available. The code bound:

- Authority: `ChangeHistory` in-memory ring, `HistoryOptions{transactions=4096, payload_capacity=16 MiB, transaction_bytes=256 KiB}` (`src/detent_authority/change_history.hh:8-10`), built with defaults at `src/detent_authority/service.cc:1396`. `covers()` (`change_history.cc:413`) answers yes only if `floor <= base <= target <= through` in the same cluster/group/restore epoch. `reset()` (`change_history.cc:364`) sets `floor = through = revision` on any context change, and attach after recovery (`service.cc:1631`) starts with an empty history. A restart of the authority therefore invalidates every older cursor. Eviction by count or bytes raises `floor`. Anything uncovered is a full snapshot (`delivery_export.cc:67-86`).
- Relay: `UpdateCache` retains 64 edges by default, 256 maximum (`include/detent_relay/bundle.hh:11`, `src/detent_relay/configuration.cc:123`). `offer()` (`src/detent_distribution/update_cache.cc:50`) returns an update only if `edge.update->base() == *base` exactly; otherwise `SnapshotRequired`. An agent that is more than 64 publications behind, or at a base the relay never held as an edge base, gets a full snapshot. A gap or relay restart clears the lineage (`update_cache.cc:11`).

Consequence for the "ship a snapshot in the base image, pull the delta on start" idea: it does not work as built. After days, the authority's 4096-transaction window has rolled past the image's cursor, and any authority or relay restart in between drops it. The agent would fall back to the full snapshot, which is the 12-minute (5M) bootstrap on this host. The idea needs a durable, time-bounded history (retained for the image age plus margin) or a compacted "changes since cursor" index in the authority; that is a design, not a tuning. Separately, the baked image must contain a prepared store (the `.read`), otherwise the first start pays the restart cost above before it can attach.

## Biggest fixes and effect

1. `F_FULLFSYNC` on clean files (restart census and publication barriers). Skipping the per-file startup sync cut restart from 121 s to 56-62 s (S arm; 2.0x). The same sync pattern accounts for 239 s of the 722.6 s bootstrap (33%). The identity check passes. This is a macOS cost (18-42 ms per `F_FULLFSYNC`); on Linux `fsync` of a clean file is microseconds, so the production gain will be smaller and must be measured there. The correct form keeps the contract: `fsync` each file (cheap when clean, pushes dirty pages if any) and issue one `F_FULLFSYNC` at the end. That needs a journal-library change (a data-only sync request kind plus a coalesced device flush), not a Detent-only edit; the branch switch skips the per-file sync and therefore also drops the push of dirty pages left by a crashed writer, which is the one risk to review.
2. Hash each byte once, not six times. R (skip the second pass) saves 16 s; hashing is 41 s on this CPU because there is no SHA-NI (Apple Silicon and Ice Lake and newer are 5-10x faster). A (no digests) shows the structural floor is 17.7 s at 5M.
3. A prepared, immutable store that passed once should not be re-hashed at all on restart: verify lazily on first page touch, or trust (`.read` + per-file inode/size/mtime identity). That is the O(1)-ish restart; it is a design change (it moves integrity checking from startup to first use) and belongs with the designer.

Combined S+R is 3.3x (121 s to 37 s). Not shipped: the switches are exploration only (`src/detent_local/explore_switch.hh`, environment `DETENT_EXPLORE_SKIP_STARTUP_SYNC`, `..._SKIP_REHASH`, `..._SKIP_ALLHASH`).

## Method notes

- A/A: main repeated six times (118.4-122.1 s); S and B and A each twice.
- Restart store: APFS clone of `managed-5m-r3/generations`, fresh per run; originals untouched. The ca file was regenerated by the build's TLS fixture and the upstream port is closed (connection refused), as in the prepared qualification.
- Bootstrap: `tests/detent_system` harness adapted from `capacity-trials/restore_5m.py` (cloned authority state, one relay, one agent, no verification oracle); the cursor is checked via the agent's `selected` file only.
- Not done: Linux numbers, cold cache (no sudo for `purge`), a measured 1M, the authority export sample at 5M, a transfer/decode split.
