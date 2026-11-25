> Status: CURRENT (2026-11-24) The time and cost of one cold agent joining a mirror relay that already holds the namespaces, at 100K, 1M and 5M records on `main` merged at `Detent:abae2b3`, against the relay-plus-agent total from the same fixture, the earlier run of the same harness (`Detent:3f0a5b5`) and the bundle relay it replaced (`Detent:3a57661`). Harness: [warm-relay-agent-boot](../measurements/warm-relay-agent-boot/). Maintainer: Detent campaign.

# Agent bootstrap against a warm relay

A new agent joining a warm mirror relay selects the fixture's revision in 0.68 s at 100K records, 3.47 s at 1M and 13.67 s at 5M (medians; 5M is one round). The relay-plus-agent cold total on the same fixtures is 1.93 s, 6.17 s and 27.87 s, so the agent is 36%, 56% and 49% of it. On the bundle relay the agent was 14%, 12% and 11% of a total five to six times larger than today's at 1M and 5M (3.79 s, 32.18 s, 157.20 s): the relay's catch-up from the authority fell from 139 s to 13 s at 5M, and the agent is now about half.

Against the bundle relay (`Detent:3a57661`, same fixtures, same host, same harness except the relay's readiness line and settle rule):

| Records | Warm agent wall, before to after | Change | Cold total, before to after | Change |
| --- | --- | --- | --- | --- |
| 100K | 0.53 to 0.68 s | +0.15 s (+28%) | 3.79 to 1.93 s | -49% |
| 1M | 3.72 to 3.47 s | -0.25 s (-7%) | 32.18 to 6.17 s | -81% |
| 5M | 17.83 to 13.67 s | -4.16 s (-23%) | 157.20 to 27.87 s | -82% |

The cold totals are the result: the ranges do not overlap at 100K (3.70 to 3.84 s before, 1.74 to 2.21 s after), and at 1M and 5M the totals differ by 5x and 6x, far beyond any spread here. The warm agent is a smaller effect, and the rows do not show it faster at every size. At 100K the after runs are 0.64, 0.68 and 1.01 s (the first of its job, as the before set's 0.83 s was) against 0.52, 0.53 and 0.83 s: the agent is about 0.15 s slower, and the two sets overlap through their first runs, so call 100K unchanged to slightly slower. At 1M the one before run (3.72 s) sits inside the after range (3.38 to 3.83 s), so the rows do not separate them; the earlier bundle-relay set (3.87 to 3.93 s, `rows-131bde6.jsonl`) is 10 to 12% above the after median. At 5M each side is one round: 13.67 s against 17.83 s (19.22 s in the earlier bundle-relay set), 23 to 29% faster, but the earlier mirror-relay run on the same fixture took 15.43 s, so one 5M round is known to about 10%: treat the 5M warm change as roughly a fifth.

Against the earlier run of this harness (`Detent:3f0a5b5`, `rows-3f0a5b59.jsonl`) the warm agent got slower at 100K and 1M and faster at 5M:

| Records | Warm agent wall, 3f0a5b5 to abae2b3 | Agent CPU, s | Cold total, s |
| --- | --- | --- | --- |
| 100K | 0.58 (0.58 to 0.58) to 0.68 (0.64 to 1.01) | 0.16 to 0.21 | 1.93 to 1.93 |
| 1M | 3.12 (2.90 to 3.27) to 3.47 (3.38 to 3.83) | 1.29 to 1.45 | 5.30 to 6.17 |
| 5M | 15.43 to 13.67 | 6.69 to 6.22 | 30.46 to 27.87 |

The 100K and 1M warm ranges are disjoint (+0.10 s, +17%, and +0.35 s, +11%), and the 1M cold totals are disjoint too (5.29 to 5.63 s, then 5.75 to 6.22 s); the 5M change is one run against one (-11%). The cause is not measured. `main` between the two heads changed `src/detent_agent/main.cc`, `src/detent_reader/core.cc`, `src/detent_local` and `src/detent_testing` among others; no profile attributes the difference to any of them. The reruns at `Detent:e5fdf9c` (jobs below) show whether it repeats.

The agent does about half the CPU work of the bundle-relay agent: 0.21 s, 1.45 s and 6.22 s (2.1, 1.45 and 1.24 µs per record) against 0.28 s, 2.66 s and 13.27 s (2.8, 2.7 and 2.7 µs). It no longer builds the generation: it hashes 0 bytes to build and 1.2 MB, 12.1 MB and 60.6 MB to check, against 4.9 MB, 48.5 MB and 242.6 MB to build. It writes 7, 17 and 65 files (11, 53 and 243 before) with the same 4 device flushes and downloads 9 to 15% fewer bytes (0.19 MB, 1.82 MB, 8.98 MB against 0.22, 2.00 and 10.0 MB).

The cost moved to the relay. During the warm agent's run the relay spends 0.51 s, 3.47 s and 13.99 s of CPU (0.01 s, 0.07 s and 0.40 s before), and its resident memory rises from 19, 91 and 185 MiB to 48, 295 and 1,112 MiB at peak (the bundle relay held 26, 181 and 878 MiB throughout). The mirror relay is cheap to bring up and expensive to first use. Over a whole cold run (catch-up plus serving one agent) it still spends less CPU than the bundle relay: 0.56, 3.88 and 16.67 s against 0.66, 6.29 and 31.36 s.

## Method

Each A1 job builds `detent-authority`, `detent-relay` and `detent-agent` (x86_64 release) at the quest's merged head, `Detent:abae2b3`, and runs `row.sh`, which runs `boot.py` once for the job's one arm. One run clones the fixture's three authority voters (`cp -c`), starts them, then:

- **Warm**: starts the relay alone (`ManagedCohort`, agent count 0) and waits outside the clock until it logs "the namespace mirror is ready", its health is ready with the mirror ready, and four half-second `ps` samples in a row add at most 0.1 s of relay CPU (the mirror polls every 10 ms, so a settled relay is not at zero; bounded at 120 s, and every warm run settled). Then the clock starts with one cold agent.
- **Cold**: starts the relay and the agent under one clock, as `coldboot.py` does (the relay-plus-agent total of [dense-sync-stack.md](dense-sync-stack.md) and [cold-bootstrap-5m.md](cold-bootstrap-5m.md)).

The clock stops when the agent has selected every namespace at the position the authority reports for it (`namespace_revision` per namespace, D3: namespaces advance independently, so no single group version exists). The relay's health carries no revision, so the agent's convergence is the revision check. Both arms share one configuration (relay session limit 48, one agent session). Per-process CPU and RSS come from `ps`, wire bytes from `nettop -P`, files and device flushes from the agent's "publication synced" line, and the relay's resident memory from the run's `ps` trace (derived fields in the rows files).

All 14 jobs ran under `quiet exclusive`, one arm per job, no two overlapped in time, and none carries a "ran beside" record. 100K and 1M are three rounds each with the arm order alternating (warm then cold, cold then warm, warm then cold); 5M is one round, one job per arm, per D85 (5M confirms, it does not repeat). The earlier light probes (`261123-164928-adf267` at 100K, `261123-165613-3cde33` at 5M) gated the harness, and `261124-084513-df11ea` showed it still runs after main's `ManagedCohort` changes.

The before rows (`Detent:3a57661`) are the same harness at the bundle relay, with differences: warm waited for "durable bundles ready", counted a relay settled at 0.01 s of CPU per half-second sample, and read one group version. They are three 100K rounds in one job and one round each at 1M and 5M (`rows-3a57661b.jsonl`). An earlier bundle-relay set at `Detent:131bde6` (three 1M rounds, `rows-131bde6.jsonl`) agrees with it within 8% on every warm and cold figure.

Fixtures, cloned and never written: `100000-main` (revision 784), `1000000-main` (revision 7,816) and `5000000-flat-main` (revision 39,064), seeded before the namespace mirror (`Detent:ea1fddc` for 100K and 5M, `Detent:78d69fa` for 1M); they read unchanged. Hardware: Intel Core i9-9980HK (8 cores, 16 threads), 32 GiB, PCIe SSD, macOS 15.8.1; every process on loopback.

## Time

Medians, range in brackets. Agent wall runs from the agent's start to selection; in the cold arm it includes waiting for the relay.

| Records | Arm | Runs | Agent wall, s | Agent CPU, s | Relay ready, s | Relay plus agent, s |
| --- | --- | ---: | --- | --- | --- | --- |
| 100K | warm | 3 | **0.68** (0.64 to 1.01) | 0.21 (0.20 to 0.22) | 1.31 (1.27 to 2.48), outside the clock | |
| 100K | cold | 3 | 1.90 (1.72 to 2.19) | 0.22 (0.19 to 0.22) | 1.31 (1.25 to 1.69) | **1.93** (1.74 to 2.21) |
| 1M | warm | 3 | **3.47** (3.38 to 3.83) | 1.45 (1.45 to 1.79) | 4.01 (3.03 to 5.72), outside the clock | |
| 1M | cold | 3 | 6.14 (5.73 to 6.19) | 1.49 (1.41 to 1.53) | 2.87 (2.55 to 2.94) | **6.17** (5.75 to 6.22) |
| 5M | warm | 1 | **13.67** | 6.22 | 13.11, outside the clock | |
| 5M | cold | 1 | 27.86 | 6.55 | 12.99 | **27.87** |

The warm and cold agent ranges do not overlap at any size with three rounds. Cold total minus relay ready (0.52 s, 3.23 s, 14.89 s, medians of per-run differences) is within 0.25 s of the warm agent's time at 100K (0.68 s) and 1M (3.47 s) and 1.2 s above it at 5M (13.67 s): the agent's work is about the same with or without a head start from the relay. The relay's ready time moves with load (1M warm ranges 3.03 to 5.72 s against 2.55 to 2.94 s cold, the same code; 100K warm 1.27 to 2.48 s), so a relay-ready figure is known to about 1 s at 1M, not better.

## Phases

The agent logs one mark for the first generation, "publication synced" (the first publication durable), then selects the remaining namespaces as their pages arrive; this harness records no process-start mark. Medians of the warm runs; before is the bundle relay at `Detent:3a57661`.

| Records | Start to publication synced, s (before) | Synced to selected, s (before) | Agent CPU / wall (before) | Agent CPU at synced, s | Bytes hashed to check (to build before) |
| --- | --- | --- | --- | --- | --- |
| 100K | 0.37 (0.55) | 0.32 (0.00) | 32% (53%) | 0.03 (two runs) | 1.21 MB (4.85 MB) |
| 1M | 1.39 (3.71) | 2.08 (0.01) | 43% (72%) | 0.27 | 12.1 MB (48.5 MB) |
| 5M | 5.05 (17.86) | 8.61 (-0.03) | 46% (74%) | 1.24 | 60.6 MB (242.6 MB) |

The first publication arrives 1.5 to 3.5x sooner, and then the agent waits. At 5M the log shows page transfers 2, 3 and 4 completing 2.8 s and 3.0 s apart (cumulative 61.8, 92.6 and 123.5 MB decoded), the last at 13.7 s, while the relay's CPU climbs from 2.4 s to 16.3 s and its resident memory from 185 MiB to 1.11 GiB; the mirror counters report 4 page builds. The reading is that the relay builds each namespace's pages on first request and the agent takes them one after another, so the wait is the relay's serial page build. This comes from the logs and counters, not a profile, and from one 5M round; a second agent against the same relay, which would show whether the builds are kept, is not measured.

Only 46% of the 5M wall is agent CPU, against 74% before: the agent is no longer bound by its own build, the rest is waiting on the relay. Wire is not the bound: 9 MB at 5M is 0.7 s even at 100 Mbit/s.

## Files, flushes and wire

| Records | Arm | Files published | Groups | Device flushes (publication; since start) | Agent bytes in | Agent bytes out | Relay bytes out serving | Relay CPU during the agent, s | Agent RSS peak, MiB |
| --- | --- | ---: | ---: | --- | ---: | ---: | ---: | --- | ---: |
| 100K | warm | 7 | 2 | 4; 5 | 186,656 | 3,780 | 186,649 | 0.51 (0.48 to 0.52) | 16 |
| 100K | cold | 7 | 2 | 4; 5 | 190,051 | 12,396 | 190,052 | 0.56 (0.47 to 0.56) | 16 |
| 1M | warm | 17 | 2 | 4; 5 | 1,816,193 | 4,300 | 1,813,997 | 3.47 (3.43 to 3.98) | 66 |
| 1M | cold | 17 | 2 | 4; 5 | 1,804,850 | 13,830 | 1,808,947 | 3.88 (3.76 to 4.07) | 70 |
| 5M | warm | 65 | 2 | 4; 5 | 8,976,873 | 4,310 | 8,976,873 | 13.99 | 204 |
| 5M | cold | 65 | 2 | 4; 5 | 8,984,365 | 18,605 | 8,988,468 | 16.67 | 211 |

Bytes are medians. Files, groups and flushes are identical in every run of a size. In the cold arm "relay CPU during the agent" is the relay's whole CPU (it starts with the agent). The relay fetches the authority's namespaces once (3.9 MB, 39.9 MB and 203.9 MB in 4 files, in every run of a size) and serves the agent 21x, 22x and 23x less than it holds.

| Records | Relay RSS before the agent, warm, MiB (bundle relay) | Relay RSS peak, warm, MiB (bundle relay) | Relay RSS peak, cold, MiB (bundle relay) |
| --- | --- | --- | --- |
| 100K | 19 (26) | 48 (27) | 46 (26) |
| 1M | 91 (181) | 295 (181) | 316 (181) |
| 5M | 185 (878) | 1,112 (878) | 1,099 (878) |

## What it means

- **Relay start**: a 5M relay is ready in 13 s, not 139 to 148 s, and a 1M relay in 2.9 to 4.0 s, not 28 to 32 s.
- **Fleet join**: a joining agent costs 13.7 s at 5M, 6.2 s of agent CPU and 9 MB of relay uplink. A thousand agents cost 1.7 core-hours of agent CPU (3.7 before) and 9 GB of uplink. The relay's side is unmeasured past the first agent: if every agent costs the relay what the first did (14 s of CPU, 0.9 GiB), a thousand cost 3.9 core-hours; if the page builds are shared, almost nothing. One more row, two agents against one warm relay, decides it.
- **Where the time went**: at 5M the agent's 13.7 s is 5 s to the first publication and 8.6 s waiting for the other namespaces' pages.

## Caveats

- **5M is one round** (D85): its warm change from the bundle relay (-23%) is one run against one, and the earlier mirror-relay run (15.43 s) differs from this one by 11%; the cold total change (-82%) is far beyond any such gap.
- **Before rows are thinner**: 1M and 5M before are one round each at `Detent:3a57661`, with the earlier three-round 1M set at `Detent:131bde6` (3.87 to 3.93 s) as the spread.
- **Harness differences**: the after harness settles at 0.1 s of CPU per half-second sample and waits for per-namespace positions; the before harness did neither. The warm "relay start to selected" column of `table.py` includes the settle wait and measures nothing in either arm.
- **Rows at `Detent:abae2b3`, head at `Detent:e5fdf9c`**: the merge since changed `src/detent_authority` (state code) and tests, not the relay, agent or reader sources. The 14 reruns at `Detent:e5fdf9c` (below) are the landing evidence; where they differ from a line here beyond its stated range, they win and this page is updated.
- **First 100K run of a job**: the first run of a 100K set was the slowest in both the before set (0.83 s) and this one (1.01 s); the median is not affected, the range is.
- **Loopback**: wire time on a real network adds bytes over link speed, 9 MB per agent at 5M.

## Jobs

Rows in [rows-abae2b31.jsonl](../measurements/warm-relay-agent-boot/rows-abae2b31.jsonl); the earlier mirror-relay rows in [rows-3f0a5b59.jsonl](../measurements/warm-relay-agent-boot/rows-3f0a5b59.jsonl) and the bundle-relay rows in [rows-3a57661b.jsonl](../measurements/warm-relay-agent-boot/rows-3a57661b.jsonl) (`table.py ROWS` prints the tables above from any). All `quiet exclusive`, one arm per job, at `Detent:abae2b3` (`abae2b313e71`).

| Size | Round | Warm | Cold |
| --- | --- | --- | --- |
| 100K | 1 | `261124-141813-b9846a` | `261124-141813-5de11a` |
| 100K | 2 | `261124-141816-b1f146` | `261124-141815-19936e` |
| 100K | 3 | `261124-141818-3ca37e` | `261124-141818-0904a8` |
| 1M | 1 | `261124-141814-202283` | `261124-141815-2ac21a` |
| 1M | 2 | `261124-141817-af06a9` | `261124-141816-f2247f` |
| 1M | 3 | `261124-141819-f892ee` | `261124-141820-725a5e` |
| 5M | 1 | `261124-141820-694a39` | `261124-141821-5f0af0` |

Reruns at `Detent:e5fdf9c` (`e5fdf9cacd7b`), the landing evidence: 100K `261124-202039-aa80ba` (warm 1), `-202039-9c9088` (cold 1), `-202042-42c0f3` (warm 2), `-202041-4379bb` (cold 2), `-202044-26b532` (warm 3), `-202043-4374a0` (cold 3); 1M `261124-202040-e47e90` (warm 1), `-202041-d8961e` (cold 1), `-202043-39475a` (warm 2), `-202042-2dd756` (cold 2), `-202045-e28f51` (warm 3), `-202046-1fe8d9` (cold 3); 5M `261124-202045-76410f` (warm), `-202046-64d8d4` (cold). Queued when this page was written; not in a rows file yet.

Earlier mirror-relay run (`Detent:3f0a5b5`): `261123-165617-0de0e5`, `-165620-51800c`, `-165624-e3b117`, `-165628-f15efc`, `-165631-3bc1e4`, `-165634-7ba37a`, `-165638-b3fd52`, `-165643-4f1b40`, `-165646-b31273`, `-165649-7c0f14`, `-165653-3ebe6a`, `-165656-0c6a5e`, `-165700-7beee0`, `-165703-83f18d`.

Before (`Detent:3a57661`): `261121-130739-53a9e5` (100K, three rounds), `261121-130739-c9c5d4` (1M), `261121-130739-ea2ed5` (5M warm), `261121-130740-155781` (5M cold).
