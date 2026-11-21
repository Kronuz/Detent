> Status: CURRENT (2026-11-20) The time and cost of one cold agent joining a relay that already holds the bundle, at 100K, 1M and 5M records on `main` (`Detent:131bde6`), against the relay-plus-agent total from the same fixture. Harness: [warm-relay-agent-boot](../measurements/warm-relay-agent-boot/). Maintainer: Detent campaign.

# Agent bootstrap against a warm relay

A new agent joining a warm relay selects the fixture's revision in 0.57 s at 100K records, 3.92 s at 1M and 19.22 s at 5M (medians; 5M is one round). That is 15%, 12% and 12% of the relay-plus-agent cold total on the same fixtures (3.93 s, 33.51 s, 162.78 s). The rest of the cold total is the relay's own catch-up from the authority: it reports "durable bundles ready" at 3.4 s, 29.5 s and 144.4 s, and cold total minus relay ready (0.5 s, 4.0 s, 18.4 s) matches the warm agent's time at every size. [cold-bootstrap-5m.md](cold-bootstrap-5m.md) estimated the agent's share at 32% (100K) and 38% (5M, r12 binaries) from a phase split of one clock; measured directly on today's code it is under a sixth.

The agent's work does not change between the arms: its CPU is 0.31 vs 0.34 s, 2.88 vs 3.07 s and 14.40 vs 14.64 s (warm vs cold), and it writes the same files with the same 4 device flushes. A warm join costs about 2.9 µs of agent CPU per record at every size (0.31 s / 100K, 2.88 s / 1M, 14.40 s / 5M), plus 0.70 s of relay CPU at 5M. A thousand agents joining a 5M relay cost 4.0 core-hours of agent CPU and 0.19 core-hours on the relay, and 10 GB from the relay's uplink.

## Method

Each A1 job builds `detent-authority`, `detent-relay` and `detent-agent` (x86_64 release) at the quest's merged head, `Detent:131bde6`, and runs `row.sh`, which runs `boot.py` once per arm in the order given. One run clones the fixture's three authority voters (`cp -c`), starts them, then:

- **Warm**: starts the relay alone (`ManagedCohort`, agent count 0) and waits outside the clock until it logs "durable bundles ready", its health shows the fixture's revision with `snapshot_ready`, and four half-second `ps` samples in a row add at most 0.01 s of relay CPU (bounded at 120 s). Then the clock starts with one cold agent.
- **Cold**: starts the relay and the agent under one clock, as `coldboot.py` does (the relay-plus-agent total of [dense-sync-stack.md](dense-sync-stack.md) and [cold-bootstrap-5m.md](cold-bootstrap-5m.md)).

The clock stops when the agent's `selected` file holds the fixture's revision. Both arms share one configuration (relay session limit 48, one agent session). Per-process CPU and RSS come from `ps`, wire bytes from `nettop -P`, files and device flushes from the agent's "publication synced" line.

All six jobs ran under `quiet exclusive`, and none carries a "ran beside" record. 100K is one job of three interleaved rounds (warm, cold, cold, warm, warm, cold); 1M is one job per round with the arm order alternating (warm-cold, cold-warm, warm-cold); 5M is one round, one job per arm, per D85 (5M confirms, it does not repeat). A gate smoke at 10K (`261120-041206-eee476`, strict) ran before them.

Fixtures, cloned and never written: `100000-main` (revision 784) and `5000000-flat-main` (revision 39,064), seeded at `Detent:ea1fddc`; `1000000-main` (revision 7,816), seeded by this quest's `seed.sh` at `Detent:78d69fa` (job `261120-041208-23e7e4`). Hardware: Intel Core i9-9980HK (8 cores, 16 threads), 32 GiB, PCIe SSD, macOS 15.8.1; every process on loopback.

## Time

Medians, range in brackets. Agent wall runs from the agent's start to selection; in the cold arm it includes waiting for the relay.

| Records | Arm | Runs | Agent wall, s | Agent CPU, s | Relay ready, s | Relay plus agent, s |
| --- | --- | ---: | --- | --- | --- | --- |
| 100K | warm | 3 | **0.57** (0.52 to 0.58) | 0.31 (0.26 to 0.33) | 3.21 (3.19 to 3.28), outside the clock | |
| 100K | cold | 3 | 3.91 (3.70 to 4.15) | 0.34 (0.32 to 0.34) | 3.42 (3.18 to 3.60) | **3.93** (3.72 to 4.17) |
| 1M | warm | 3 | **3.92** (3.87 to 3.93) | 2.88 (2.81 to 2.88) | 29.14 (28.68 to 31.53), outside the clock | |
| 1M | cold | 3 | 33.49 (32.81 to 34.02) | 3.07 (2.84 to 3.43) | 29.50 (28.91 to 29.78) | **33.51** (32.85 to 34.03) |
| 5M | warm | 1 | **19.22** | 14.40 | 147.80, outside the clock | |
| 5M | cold | 1 | 162.74 | 14.64 | 144.40 | **162.78** |

The warm and cold ranges do not overlap at any size with three rounds. The 5M cold total, 162.78 s, matches main's 162.7 s in [dense-sync-stack.md](dense-sync-stack.md) on the same fixture.

## Phases

The agent logs three marks: "managed delivery running" (process up), "publication synced" (generation built and durable), and the selection. Fetch, decode and build share one span; the agent logs nothing between them.

| Records | Start to running, s | Running to publication synced (fetch, decode, build, publish), s | Synced to selected, s | Agent CPU / wall | Bytes hashed to build |
| --- | --- | --- | --- | --- | --- |
| 100K | 0.10 | 0.46 | 0.00 | 54% | 4.85 MB |
| 1M | 0.10 | 3.82 | -0.01 | 73% | 48.5 MB |
| 5M | 0.10 | 19.13 | -0.02 | 75% | 242.6 MB |

Medians of the warm runs. Synced to selected is zero within the marks' resolution (-0.02 to 0.02 s in every run: the log stamps milliseconds, the selection is polled). At 1M and 5M three quarters of the wall is agent CPU on one core, building and hashing the generation (no SHA extensions on this host); the rest is the publication's syncs and the fetch. Wire is not the bound: 10 MB at 5M is 0.8 s even at 100 Mbit/s.

## Files, flushes and wire

| Records | Arm | Files published | Groups | Device flushes (publication; since start) | Agent bytes in | Agent bytes out | Relay bytes out serving | Relay CPU during the agent, s | Agent RSS peak, MiB |
| --- | --- | ---: | ---: | --- | ---: | ---: | ---: | --- | ---: |
| 100K | warm | 11 | 2 | 4; 5 | 218,963 | 3,028 | 220,306 | 0.03 (0.02 to 0.03) | 19 |
| 100K | cold | 11 | 2 | 4; 5 | 224,878 | 21,788 | 228,710 | 0.63 (0.54 to 0.68) | 19 |
| 1M | warm | 53 | 2 | 4; 5 | 2,000,590 | 2,490 | 2,005,967 | 0.15 (0.13 to 0.16) | 126 |
| 1M | cold | 53 | 2 | 4; 5 | 2,069,819 | 170,258 | 2,076,909 | 5.73 (5.60 to 5.87) | 127 |
| 5M | warm | 243 | 2 | 4; 5 | 9,995,045 | 2,490 | 10,019,560 | 0.70 | 605 |
| 5M | cold | 243 | 2 | 4; 5 | 10,257,535 | 829,538 | 10,281,782 | 27.85 | 604 |

Bytes are medians. Files, groups and flushes are identical in every run of a size. The cold agent sends 7 to 330 times more bytes than the warm one: it keeps its session open while the relay catches up, and the relay's CPU in that window is its own catch-up, not serving.

## Caveats

- **Relay settle**: in two of three 100K warm runs and the 5M warm run the relay did not meet the four-quiet-samples rule within 120 s; it had logged "durable bundles ready" and reached the revision, and its CPU during the agent's run (0.03 s at 100K, 0.70 s at 5M, against 0.63 s and 27.85 s in the cold arm) shows it was only serving. The warm "relay start to selected" column of `table.py` includes that settle wait and measures nothing.
- **5M is one round** (D85): it confirms the 100K and 1M spread (under 15% between rounds) and is not repeated.
- **Loopback**: wire time on a real network adds bytes over link speed, 10 MB per agent at 5M.

## Jobs

All at `Detent:131bde6`, `quiet exclusive`, rows in [rows-131bde6.jsonl](../measurements/warm-relay-agent-boot/rows-131bde6.jsonl) (`table.py rows-131bde6.jsonl` prints the tables above from it).

| Size | Rounds | Job |
| --- | --- | --- |
| 100K | 1-3 (warm, cold, cold, warm, warm, cold) | `261120-144419-5e7242` |
| 1M | 1 (warm, cold) | `261120-144420-73a018` |
| 1M | 2 (cold, warm) | `261120-144421-d16a3a` |
| 1M | 3 (warm, cold) | `261120-144421-5a223a` |
| 5M | 1, warm | `261120-144420-48ab68` |
| 5M | 1, cold | `261120-144420-f0a573` |
