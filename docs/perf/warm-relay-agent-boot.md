> Status: CURRENT (2026-11-24) The time and cost of one cold agent joining a mirror relay that already holds the namespaces, at 100K, 1M and 5M records on `main` (`Detent:3f0a5b5`), against the relay-plus-agent total from the same fixture and against the bundle relay it replaced (`Detent:3a57661`). Harness: [warm-relay-agent-boot](../measurements/warm-relay-agent-boot/). Maintainer: Detent campaign.

# Agent bootstrap against a warm relay

A new agent joining a warm mirror relay selects the fixture's revision in 0.58 s at 100K records, 3.12 s at 1M and 15.43 s at 5M (medians; 5M is one round). The relay-plus-agent cold total on the same fixtures is 1.93 s, 5.30 s and 30.46 s, so the agent is 30%, 59% and 51% of it. On the bundle relay the agent was 14%, 12% and 11% of a total five to six times larger at 1M and 5M (3.79 s, 32.18 s, 157.20 s): the relay's catch-up from the authority fell from 139 s to 14 s at 5M, and the agent is now the larger half.

Against the bundle relay (`Detent:3a57661`, same fixtures, same host, same harness except the relay's readiness line and settle rule):

| Records | Warm agent wall, before to after | Change | Cold total, before to after | Change |
| --- | --- | --- | --- | --- |
| 100K | 0.53 to 0.58 s | +0.05 s (+9%) | 3.79 to 1.93 s | -49% |
| 1M | 3.72 to 3.12 s | -0.60 s (-16%) | 32.18 to 5.30 s | -84% |
| 5M | 17.83 to 15.43 s | -2.40 s (-13%) | 157.20 to 30.46 s | -81% |

The cold totals are the result: the ranges do not overlap at any size (100K 3.70 to 3.84 s before, 1.92 to 1.97 s after), and at 1M and 5M the totals differ by 6x and 5x, far beyond any spread here. The warm agent is a smaller effect. At 100K the three after runs are 0.575 to 0.577 s and the before runs 0.52, 0.53 and 0.83 s: the agent is about 0.05 s (9%) slower than the typical before run, and the 0.83 s run (the first of its job) shows noise at that size larger than the difference, so call 100K unchanged. At 1M the after range (2.90 to 3.27 s) sits below the one before run (3.72 s) and below the three runs of the earlier bundle-relay set (3.87 to 3.93 s, `rows-131bde6.jsonl`): 16 to 26% faster. At 5M each side is one round: 15.43 s against 17.83 s (19.22 s in the earlier bundle-relay set), 13 to 20% faster, a gap about twice the 7% between those two bundle-relay runs; the light probe `261123-165613-3cde33`, not timed alone, took 15.91 s. Treat the 5M warm change as about 15%, not a precise figure.

The agent does half the CPU work: 0.16 s, 1.29 s and 6.69 s (1.6, 1.3 and 1.3 µs per record) against 0.28 s, 2.66 s and 13.27 s (2.8, 2.7 and 2.7 µs). It no longer builds the generation: it hashes 0 bytes to build and 1.2 MB, 12.1 MB and 60.6 MB to check, against 4.9 MB, 48.5 MB and 242.6 MB to build. It writes 7, 17 and 65 files (11, 53 and 243 before) with the same 4 device flushes and downloads 10 to 15% fewer bytes (0.19 MB, 1.80 MB, 8.98 MB).

The cost moved to the relay. During the warm agent's run the relay spends 0.39 s, 3.26 s and 16.11 s of CPU (0.01 s, 0.07 s and 0.40 s before), and its resident memory rises from 18, 90 and 224 MiB to 46, 315 and 1,067 MiB at peak (the bundle relay held 26, 181 and 878 MiB throughout). The mirror relay is cheap to bring up and expensive to first use. Over a whole cold run (catch-up plus serving one agent) it still spends less CPU than the bundle relay: 0.47, 3.46 and 18.34 s against 0.66, 6.29 and 31.36 s.

## Method

Each A1 job builds `detent-authority`, `detent-relay` and `detent-agent` (x86_64 release) at the quest's merged head, `Detent:3f0a5b5` (main merged at `0a247a7`), and runs `row.sh`, which runs `boot.py` once for the job's one arm. One run clones the fixture's three authority voters (`cp -c`), starts them, then:

- **Warm**: starts the relay alone (`ManagedCohort`, agent count 0) and waits outside the clock until it logs "the namespace mirror is ready", its health is ready with the mirror ready, and four half-second `ps` samples in a row add at most 0.1 s of relay CPU (the mirror polls every 10 ms, so a settled relay is not at zero; bounded at 120 s, and every warm run settled). Then the clock starts with one cold agent.
- **Cold**: starts the relay and the agent under one clock, as `coldboot.py` does (the relay-plus-agent total of [dense-sync-stack.md](dense-sync-stack.md) and [cold-bootstrap-5m.md](cold-bootstrap-5m.md)).

The clock stops when the agent has selected every namespace at the position the authority reports for it (`namespace_revision` per namespace, D3: namespaces advance independently, so no single group version exists). The relay's health carries no revision, so the agent's convergence is the revision check. Both arms share one configuration (relay session limit 48, one agent session). Per-process CPU and RSS come from `ps`, wire bytes from `nettop -P`, files and device flushes from the agent's "publication synced" line, and the relay's resident memory from the run's `ps` trace (a derived field in the rows files).

All 14 jobs ran under `quiet exclusive`, one arm per job, and none carries a "ran beside" record. 100K and 1M are three rounds each with the arm order alternating (warm then cold, cold then warm, warm then cold); 5M is one round, one job per arm, per D85 (5M confirms, it does not repeat). A light probe gated each size (`261123-164928-adf267` at 100K, `261123-165613-3cde33` at 5M).

The before rows (`Detent:3a57661`) are the same harness at the bundle relay, with differences: warm waited for "durable bundles ready", counted a relay settled at 0.01 s of CPU per half-second sample, and read one group version. They are three 100K rounds in one job and one round each at 1M and 5M (`rows-3a57661b.jsonl`). An earlier bundle-relay set at `Detent:131bde6` (three 1M rounds, `rows-131bde6.jsonl`) agrees with it within 8% on every warm and cold figure.

Fixtures, cloned and never written: `100000-main` (revision 784), `1000000-main` (revision 7,816) and `5000000-flat-main` (revision 39,064), seeded before the namespace mirror (`Detent:ea1fddc` for 100K and 5M, `Detent:78d69fa` for 1M); they read unchanged, as a fresh 100K seed did in the probe `261123-164944-8f37e3`. Hardware: Intel Core i9-9980HK (8 cores, 16 threads), 32 GiB, PCIe SSD, macOS 15.8.1; every process on loopback.

## Time

Medians, range in brackets. Agent wall runs from the agent's start to selection; in the cold arm it includes waiting for the relay.

| Records | Arm | Runs | Agent wall, s | Agent CPU, s | Relay ready, s | Relay plus agent, s |
| --- | --- | ---: | --- | --- | --- | --- |
| 100K | warm | 3 | **0.58** (0.58 to 0.58) | 0.16 (0.15 to 0.16) | 1.47 (1.47 to 1.49), outside the clock | |
| 100K | cold | 3 | 1.91 (1.91 to 1.91) | 0.17 (0.16 to 0.18) | 1.34 (1.28 to 1.46) | **1.93** (1.92 to 1.97) |
| 1M | warm | 3 | **3.12** (2.90 to 3.27) | 1.29 (1.23 to 1.41) | 2.69 (2.48 to 3.70), outside the clock | |
| 1M | cold | 3 | 5.28 (5.28 to 5.62) | 1.24 (1.21 to 1.28) | 2.56 (2.47 to 2.58) | **5.30** (5.29 to 5.63) |
| 5M | warm | 1 | **15.43** | 6.69 | 17.30, outside the clock | |
| 5M | cold | 1 | 30.42 | 6.49 | 13.89 | **30.46** |

The warm and cold agent ranges do not overlap at any size with three rounds. Cold total minus relay ready (0.59 s, 2.82 s, 16.57 s, medians of per-run differences) is within 0.3 s of the warm agent's time (0.58 s, 3.12 s, 15.43 s) at every size: the agent's work is the same with or without a head start from the relay. The relay's ready time moves with load (1M warm ranges 2.48 to 3.70 s; 5M warm 17.30 s against 13.89 s cold, the same code), so 5M's relay ready is known to about 3 s, not better.

## Phases

The agent logs one mark for the first generation, "publication synced" (the first publication durable), then selects the remaining namespaces as their pages arrive; this harness records no process-start mark. Medians of the warm runs; before is the bundle relay at `Detent:3a57661`.

| Records | Start to publication synced, s (before) | Synced to selected, s (before) | Agent CPU / wall (before) | Agent CPU at synced, s | Bytes hashed to check (to build before) |
| --- | --- | --- | --- | --- | --- |
| 100K | 0.29 (0.55) | 0.29 (0.00) | 28% (53%) | | 1.21 MB (4.85 MB) |
| 1M | 1.32 (3.71) | 1.80 (0.01) | 41% (72%) | 0.28 | 12.1 MB (48.5 MB) |
| 5M | 5.98 (17.86) | 9.46 (-0.03) | 43% (74%) | 1.69 | 60.6 MB (242.6 MB) |

The first publication arrives 2 to 3x sooner, and then the agent waits. At 5M the log shows page transfers 2, 3 and 4 completing 3.1 s apart (cumulative 61.8, 92.6 and 123.5 MB decoded), the last at 15.4 s, while the relay's CPU climbs from 2.56 s to 18.67 s and its resident memory from 229 MiB to 1.09 GiB; the mirror counters report 4 page builds and 4 full copies. The reading is that the relay builds each namespace's pages on first request and the agent takes them one after another, so the wait is the relay's serial page build. This comes from the logs and counters, not a profile, and from one 5M round; a second agent against the same relay, which would show whether the builds are kept, is not measured.

Only 43% of the 5M wall is agent CPU, against 74% before: the agent is no longer bound by its own build, the rest is waiting on the relay. Wire is not the bound: 9 MB at 5M is 0.7 s even at 100 Mbit/s.

## Files, flushes and wire

| Records | Arm | Files published | Groups | Device flushes (publication; since start) | Agent bytes in | Agent bytes out | Relay bytes out serving | Relay CPU during the agent, s | Agent RSS peak, MiB |
| --- | --- | ---: | ---: | --- | ---: | ---: | ---: | --- | ---: |
| 100K | warm | 7 | 2 | 4; 5 | 185,565 | 3,819 | 185,565 | 0.39 (0.37 to 0.40) | 14 |
| 100K | cold | 7 | 2 | 4; 5 | 190,097 | 12,396 | 191,960 | 0.47 (0.45 to 0.47) | 16 |
| 1M | warm | 17 | 2 | 4; 5 | 1,799,863 | 4,300 | 1,799,864 | 3.26 (2.91 to 3.34) | 63 |
| 1M | cold | 17 | 2 | 4; 5 | 1,804,847 | 13,830 | 1,804,848 | 3.46 (3.30 to 3.57) | 57 |
| 5M | warm | 65 | 2 | 4; 5 | 8,976,878 | 4,310 | 8,976,878 | 16.11 | 179 |
| 5M | cold | 65 | 2 | 4; 5 | 8,984,862 | 19,558 | 8,987,062 | 18.34 | 171 |

Bytes are medians. Files, groups and flushes are identical in every run of a size. In the cold arm "relay CPU during the agent" is the relay's whole CPU (it starts with the agent). The relay fetches the authority's namespaces once (3.9 MB, 39.9 MB and 203.9 MB in 4 files, in every run of a size) and serves the agent 21x, 22x and 23x less than it holds.

| Records | Relay RSS before the agent, warm, MiB (bundle relay) | Relay RSS peak, warm, MiB (bundle relay) | Relay RSS peak, cold, MiB (bundle relay) |
| --- | --- | --- | --- |
| 100K | 18 (26) | 46 (27) | 47 (26) |
| 1M | 90 (181) | 315 (181) | 315 (181) |
| 5M | 224 (878) | 1,067 (878) | 972 (878) |

## What it means

- **Relay start**: a 5M relay is ready in 14 to 17 s, not 139 to 148 s, and a 1M relay in 2.5 to 2.7 s, not 28 to 32 s.
- **Fleet join**: a joining agent costs 15.4 s at 5M, 6.7 s of agent CPU and 9 MB of relay uplink. A thousand agents cost 1.9 core-hours of agent CPU (3.7 before) and 9 GB of uplink. The relay's side is unmeasured past the first agent: if every agent costs the relay what the first did (16 s of CPU, 0.8 GiB), a thousand cost 4.5 core-hours; if the page builds are shared, almost nothing. One more row, two agents against one warm relay, decides it.
- **Where the time went**: at 5M the agent's 15.4 s is 6 s to the first publication and 9.5 s waiting for the other namespaces' pages.

## Caveats

- **5M is one round** (D85): its warm change from the bundle relay (-13%) is one run against one run; the cold total change (-81%) is far beyond any such gap.
- **Before rows are thinner**: 1M and 5M before are one round each at `Detent:3a57661`, with the earlier three-round 1M set at `Detent:131bde6` (3.87 to 3.93 s) as the spread.
- **Harness differences**: the after harness settles at 0.1 s of CPU per half-second sample and waits for per-namespace positions; the before harness did neither. The warm "relay start to selected" column of `table.py` includes the settle wait and measures nothing in either arm.
- **Merged head**: the rows are at `Detent:3f0a5b5`. Main has since moved only tests, campaign files, a workflow and the website (no `src`, `include` or `cmake`), so the built binaries are the measured ones; the `ManagedCohort` support code (`tests/detent_system`) did change, and is checked by a light probe, not re-timed.
- **Loopback**: wire time on a real network adds bytes over link speed, 9 MB per agent at 5M.

## Jobs

All at `Detent:3f0a5b5`, `quiet exclusive`, one arm per job, rows in [rows-3f0a5b59.jsonl](../measurements/warm-relay-agent-boot/rows-3f0a5b59.jsonl); the bundle-relay rows are in [rows-3a57661b.jsonl](../measurements/warm-relay-agent-boot/rows-3a57661b.jsonl) (`table.py ROWS` prints the tables above from either).

| Size | Round | Warm | Cold |
| --- | --- | --- | --- |
| 100K | 1 | `261123-165617-0de0e5` | `261123-165620-51800c` |
| 100K | 2 | `261123-165634-7ba37a` | `261123-165631-3bc1e4` |
| 100K | 3 | `261123-165646-b31273` | `261123-165649-7c0f14` |
| 1M | 1 | `261123-165624-e3b117` | `261123-165628-f15efc` |
| 1M | 2 | `261123-165643-4f1b40` | `261123-165638-b3fd52` |
| 1M | 3 | `261123-165653-3ebe6a` | `261123-165656-0c6a5e` |
| 5M | 1 | `261123-165700-7beee0` | `261123-165703-83f18d` |

Before (`Detent:3a57661`): `261121-130739-53a9e5` (100K, three rounds), `261121-130739-c9c5d4` (1M), `261121-130739-ea2ed5` (5M warm), `261121-130740-155781` (5M cold).
