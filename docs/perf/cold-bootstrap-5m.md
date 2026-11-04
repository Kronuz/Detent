# Cold bootstrap at 5M: where the time goes

Status: explored, partial. The 5M re-run on main did not happen (token budget; it was queued behind other holds and cancelled before it started). The 5M phase split below comes from the preserved `5000000-restore-r12` run (Detent binaries of 2026-10-25). 100K was re-measured on main (Detent:5475baf). No prototype was built.

## Method

- Harness: `.scratch/coldboot.py` (this branch's worktree; not committed), a trimmed copy of `.scratch/capacity-trials/restore_5m.py`. It clones the three authority voters (`cp -c`), restores them, starts one relay and one cold agent through `tests/detent_system/managed.py`, and stops when the agent's `selected` file reaches the authority's revision. It records wall marks, per-process CPU (`ps`) and per-process socket bytes (`nettop -P`).
- 100K: source `capacity-trials/100000-r3`, release build of main (`-DDETENT_BUILD_RELAY=ON`), under `quiet strict`. One run, so no spread.
- 5M: `capacity-trials/5000000-restore-r12/events.jsonl` (CPU samples every 0.25 s, relay/agent log lines) and `5000000-restore-r12-relay.sample.txt` (macOS `sample` of the relay in its idle phase, parsed per thread with `.scratch/sample_parse.py`).
- Pack sizes, zstd sizes and the page-byte split: `.scratch/sizes.py`, `.scratch/zstd_packs.py` (Python 3.14 `compression.zstd`, one thread), `.scratch/page_bytes.py` (walks every `DTPAGE02` page header in every pack).

## Phase table

Measured unless marked. The finer phases D12 asks for (frame decode, record validation, key sort, page encode, SHA-256, pack writes, `.read`/`.live`) have no counters on main, and the `sample` profile of the main build never ran, so they are not split here.

| Phase | 100K, main | 5M, r12 binaries |
| --- | --- | --- |
| Relay: authority export, decode, build, publish (until "durable bundles ready") | 10.3 s (68%) | 467 s (62%) |
| Agent: fetch from relay, decode, build, publish, select | 4.9 s (32%) | 287 s (38%) |
| Total, cohort start to agent selected | 15.2 s | 754 s |
| Relay CPU / relay wall | 2.7 s / 10.3 s (26%) | 156 s / 467 s (33%) |
| Agent CPU / agent wall | 1.5 s / 4.9 s (31%) | 125 s / 287 s (44%) |
| Off-CPU share of total wall | 72% | 63% |
| Relay idle-phase thread split (5M sample, 724 samples on the I/O thread) | not sampled | `F_FULLFSYNC` on files 296 (41%), directory sync 278 (38%), queue wait 146 (20%) |

Both tiers are mostly off-CPU at both scales, and neither uses more than about half a core on average: the bootstrap is serial and waits on storage. The 5M CPU trace (wall from cohort start) shows the shape:

| Window | What runs | Evidence |
| --- | --- | --- |
| 0-150 s | Authority export; relay receives (relay RSS 8 MB to 777 MB from 73 s) | authority-0 CPU +70 s |
| 150-258 s | Relay decode and build | relay CPU 10 to 82 s, RSS to 2.8 GB |
| 258-412 s (154 s) | Relay nearly idle, about 0.1 core | relay CPU 82 to 96 s; the `sample` at 314 s finds the I/O thread in `fcntl` |
| 412-467 s | Relay finishes (validation, publication) | relay CPU 96 to 150 s |
| 467-541 s | Agent fetch, decode and build | agent CPU 3 to 67 s, RSS to 2.2 GB |
| 541-690 s (149 s) | Agent nearly idle | agent CPU 67 to 74 s |
| 690-754 s | Agent finishes | agent CPU 78 to 125 s |

D12's split (239 s sync, 112 s hashing, 313 s overlapped export/decode/build) stays an estimate. What these numbers support: the two flat windows are about 303 s, 40% of 754 s, and the one sample taken in them is file and directory syncs; export alone is about 150 s; the tiers run one after the other.

## Top three costs

1. Per-file `F_FULLFSYNC` and per-step directory syncs in publication, on both tiers. Measured on r12 (the relay I/O thread's non-idle samples are 79% `fcntl`); on main the calls are `completion::file_sync` (`_deps/completion-src/include/completion/posix.h:169-171`, `F_FULLFSYNC` on macOS) and the directory barriers issued per step in `src/detent_local/disk_store.cc:404,1047,1393` and `src/detent_local/paged_segment_collection.cc:720,726`. At 5M the flat-CPU windows are 154 s on the relay and 149 s on the agent, 303 s or 40% of wall (attributing them all to sync is an estimate from one relay sample, not a counter; the agent was not sampled). D11's one barrier per publication is the fix.
2. Serial tiers and serial build. Export (~150 s), relay build (~110 s plus ~55 s finish) and agent build (~75 s plus ~65 s finish) run one after another, each on about one core; the agent cannot start until the relay has published (agent CPU stays at 3 s until t = 467 s). Streaming the relay's build to the agent (D12.3) overlaps them. Not split further on main.
3. Trie overhead in the bytes everyone writes, syncs and hashes: 84% of pack bytes are page headers and child references at both scales, and the trie has 1.4 pages per record (7,000,007 pages and 7,000,006 child references for 5,000,000 records; 140,007 and 140,006 at 100K). See "Page bytes" below. This is also why the full pack stream compresses only to 35%.

## Page bytes (shape B input)

A page is an 80-byte header (magic, kind, lengths, key count, 32-byte logical digest), the prefix, 96 bytes per child (label plus an 88-byte reference: 16-byte segment id, offset, length, 32-byte physical and 32-byte logical digest) and the body (`include/detent_local/paged_layout.hh:7-28`).

| Bytes | 100K, main agent store | 5M, r12 agent store |
| --- | --- | --- |
| Pack total | 30,102,539 (29 packs) | 1,509,994,805 (1,441 packs) |
| Page headers | 11,200,560 (37.2%) | 560,000,560 (37.1%) |
| Child references | 13,440,576 (44.6%) | 672,000,576 (44.5%) |
| Prefixes | 90,029 (0.3%) | 4,500,029 (0.3%) |
| Bodies | 4,750,954 (15.8%) | 242,500,032 (16.1%) |
| Pack framing (outside pages) | 620,420 (2.1%) | 30,993,608 (2.1%) |

Hashes, references, headers and framing are 83.6% of the 5M packs (1,262,994,744 bytes); prefixes and bodies are 16.4% (247,000,061 bytes). Within that 83.6%, the digests alone are 32 bytes per header plus 64 per child, 672,000,608 bytes (44.5%). A placement-free page stream (shape B) drops the segment id, offset and length from each reference (24 of 96 bytes, 168,000,144 bytes at 5M) and the pack framing (31.0 MB), and keeps the digests and the rest of each header: about 1,311 MB, 87% of the packs. Without digests (receiver recomputes) it would be about 639 MB, 42%.

## Shipping the relay's prepared files instead of records (estimate, not built)

The relay's and the agent's packs are not byte-identical today: same count and same total size (1,441 packs, 1,509,994,805 bytes at 5M; 29 packs, 30,102,539 bytes at 100K on main), but no pack has the same SHA-256 on both sides. A paired diff of five 100K packs finds 11-13% of bytes differing, starting at the first child reference: the random segment ids and the digests that cover them. Ordinal-0 canonical files (T6) would need deterministic segment identities; shipping the relay's own files to a cold agent under the relay's names works without that.

| | Records full copy | Prepared pack files |
| --- | --- | --- |
| Wire bytes, 100K (measured, `nettop`) | 566 KB agent in, 574 KB relay out | 30.1 MB raw |
| Wire bytes, 5M | ~28 MB (100K scaled linearly; estimate) | 1,510 MB raw; 529.9 MB zstd-1; 527.9 MB zstd-3 (measured) |
| Agent-side work at 5M | 287 s (r12, measured) | ~10 s (estimate: write 1.5 GB, one SHA-256 pass, zstd decode, one barrier, `.read`/`.live`) |

Records compress about 8x on the wire because they are the semantic content (bodies, 16% of the packs, and highly regular in this corpus); pack files compress only to 35% because 82% of them are digests and headers. Shipping files costs about 500 MB more per cold agent and saves about 277 s of agent time today.

Crossover: 500 MB / 277 s = 1.8 MB/s, about 15 Mbit/s. Above that link speed, shipping files is faster for the agent. Once D11 removes the agent's sync time (about 150 s), the saving drops to about 115 s and the crossover rises to about 4.3 MB/s, 35 Mbit/s. Fleet bandwidth is the real cost: 19x the bytes per cold agent, paid by the relay's uplink.

## Not done, and what to do next

1. The 5M re-run on main with `sample` on both tiers (`coldboot.py ... sample`, under `quiet exclusive`, about 15 min). It would replace the r12 column and split CPU time into decode, build, hash and write.
2. Counters for the finer phases: `DETENT_READ_COUNTERS` covers bytes hashed and pages verified, but nothing times decode, sort or encode.
3. The wire bytes of the 5M record copy measured, not scaled.
4. The prototype: D11's single barrier per publication behind a switch, A/B against main at 100K and 5M.

## Round 2: sampled 5M on main, wire bytes, single-barrier prototype

Detent:5475baf plus two prototype probes, release build, `quiet exclusive` for 5M (runs A1, B1, A2 in that order, all sampled with `sample` at 5 ms on both relay and agent) and `quiet strict` for 100K (A1 B1 A2 B2 A3 B3 interleaved). A is main's sync; B is the prototype on the agent only (`DETENT_STAGED_SYNC=detent-agent`). Probes print at exit with `DETENT_PROBE_SYNC` / `DETENT_PROBE_WIRE`.

### Prototype

`docs/perf/cold-bootstrap-5m-completion.patch` (applied to the fetched completion sources; not a branch of completion). Each pack and owner file is an image preparation (`completion/image.h`: create, write, `Sync`, `DirectorySync`, close), and on macOS `Sync` is `F_FULLFSYNC` (`posix.h:168-171`) and `DirectorySync` is `fsync` of the directory plus `F_FULLFSYNC` (`posix.h:625-633`): two device flushes per file. With the switch on, a file sync stages (`fsync`, no device flush) and the preparation skips its directory barrier; the publication's own `DirectorySync` (`image.h:392`) is then the one device flush that makes every staged file and entry durable, which is the shape `files_sync_devices` already uses (`posix.h:189-236`).

### Results

| | 100K A (3 runs) | 100K B (3 runs) | 5M A1 / A2 | 5M B1 |
| --- | --- | --- | --- | --- |
| Cohort start to agent selected | 14.10 / 13.87 / 14.10 s | 11.47 / 11.42 / 11.42 s | 699.1 / 701.5 s | 574.0 s |
| Agent starts working (CPU > 3 s) | n/a | n/a | 473.9 / 471.9 s | 470.9 s |
| Agent phase (selected minus start) | ~4.9 s | ~2.3 s | 225.2 / 229.6 s | 103.1 s |
| Agent `F_FULLFSYNC` per publication | 130 | 1 | 5,778 | 1 |
| Relay `F_FULLFSYNC` (unchanged) | 130 | 130 | 5,778 | 5,778 |
| Relay CPU / agent CPU | 2.7 / 1.5 s | 2.6 / 1.35 s | 139.4 / 73.1 s | 135.5 / 68.5 s |

- A/A spread: 100K 0.23 s (1.7%); 5M 2.4 s (0.35%). B spread at 100K 0.05 s.
- B/A, total: 100K 11.44 / 14.02 = 0.816 (-2.6 s, -18%); 5M 574.0 / 700.3 = 0.820 (-126 s, -18%). Agent phase at 5M: 103 / 227 = 0.45 (-55%).
- 5,778 = 2 flushes x 2,889 images (1,441 packs, 1,441 owners, the rest generation files).
- Identity: every run, A and B, at each scale has the same root logical digest (100K `f4993127...ac569`, 5M `5b4b5934...7e2f2`), the same record, page and byte counts (5M: 5,000,000 records, 7,000,007 pages, 1,479,001,197 bytes, revision 39,064), and the same multisets of page logical digests and of page prefix+body hashes (`.scratch/identity.py`). Pack bytes are not compared because segment ids are random per run.
- The relay has the same 5,778 flushes; applying the switch there should save about the same again (estimate, about 126 s from the relay profile below; not run).

### Profile, 5M A1 (samples at about 5.9 ms each over about 700 s)

| Thread | Agent | Relay |
| --- | --- | --- |
| I/O thread in `fcntl` (file `F_FULLFSYNC` + directory flush) | 11,289 + 9,186 = 20,475 (about 120 s) | 11,303 + 9,990 = 21,293 (about 126 s) |
| Builder thread, non-idle | 9,096 (about 53 s) | 16,004 (about 94 s) |
| of which portable SHA-256 (`kronuz::sha256::detail::compress_portable`) | 5,861 (about 34 s, 64%) | 10,707 (about 63 s, 67%) |
| of which re-validating its own build (PageValidator, GraphValidator, DependencyDiscovery) | 2,943 (about 17 s) | 7,722 (about 45 s) |

So the idle windows are syncs, by profile: the agent's 120 s of `fcntl` is what B removed (126 s). The builder is single-threaded on both tiers and two thirds of it is SHA-256 in the portable path; this host (i9-9980HK) has no SHA extensions, so the share is host-specific.

### Wire bytes of the record full copy

| | 100K | 5M |
| --- | --- | --- |
| Fragments | 75 | 3,988 |
| As sent (zstd-3 fragments plus 96-byte headers) | 542,302 B (5.4 B/record) | 25,646,713 B (5.1 B/record) |
| Raw (decoded) | 4,894,345 B (48.9 B/record) | 261,337,255 B (52.3 B/record) |
| `nettop`, agent bytes in | 565,767 B | 26,628,188 B (B1) |

Every fragment is compressed; the relay receives exactly the same bytes from the authority. Raw is 52 B per record, as the design's model assumes; this corpus compresses about 10x because its records are mostly defaults with regular names. The earlier 28 MB estimate holds (25.6 MB measured), so the shipping-files crossover above stands: about 500 MB more per cold agent.

### Revised top costs at 5M on main (700 s)

1. Device flushes: 5,778 `F_FULLFSYNC` per publication per tier, about 120 s on the agent and 126 s on the relay. The prototype removes the agent's (-126 s, -18% of total); the relay's is the same change.
2. Single-threaded build with portable SHA-256: 34 s of the agent's 53 s and 63 s of the relay's 94 s builder time is hashing, 17 s and 45 s of it re-validating the tier's own build (D10).
3. Serial tiers: the agent starts at about 472 s, when the relay publishes.

Worth finishing: yes. The single barrier is a completion-level change (a staged-sync image preparation and a group flush at publication), byte-identical in content, and it needs a crash-consistency test before it lands (a power-cut between staging and the publication flush must leave the previous selection intact).
