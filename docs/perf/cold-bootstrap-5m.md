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
