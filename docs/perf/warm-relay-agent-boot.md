> Status: CURRENT (2026-11-25) The time and cost of one cold agent joining a mirror relay that already holds the namespaces, at 100K, 1M and 5M records on `main` merged at `Detent:e5fdf9c`, against the relay-plus-agent total from the same fixture, two earlier runs of the same harness (`Detent:abae2b3`, `Detent:3f0a5b5`) and the bundle relay it replaced (`Detent:3a57661`). Harness: [warm-relay-agent-boot](../measurements/warm-relay-agent-boot/). Maintainer: Detent campaign.

# Agent bootstrap against a warm relay

A new agent joining a warm mirror relay selects the fixture's revision in 0.58 s at 100K records, 2.79 s at 1M and 13.90 s at 5M (medians; 5M is one round). The relay-plus-agent cold total on the same fixtures is 1.83 s, 5.08 s and 26.98 s, so the agent is 32%, 55% and 52% of it. On the bundle relay the agent was 14%, 12% and 11% of a total about six times larger than today's at 1M and 5M (3.79 s, 32.18 s, 157.20 s): the relay's catch-up from the authority fell from 139 s to 13 s at 5M, and the agent is now about half.

Against the bundle relay (`Detent:3a57661`, same fixtures, same host, same harness except the relay's readiness line and settle rule):

| Records | Warm agent wall, before to after | Change | Cold total, before to after | Change |
| --- | --- | --- | --- | --- |
| 100K | 0.53 to 0.58 s | +0.05 s (+9%) | 3.79 to 1.83 s | -52% |
| 1M | 3.72 to 2.79 s | -0.93 s (-25%) | 32.18 to 5.08 s | -84% |
| 5M | 17.83 to 13.90 s | -3.93 s (-22%) | 157.20 to 26.98 s | -83% |

The cold totals are the result at 1M and 5M, where they differ by 6x, far beyond any spread here. At 100K the median is 52% lower but the ranges touch: two of the three cold runs took 1.79 and 1.83 s, the third 3.84 s (its relay took 3.29 s to be ready, against 1.25 and 1.34 s for the other two), which is the top of the bundle relay's 3.70 to 3.84 s. The warm agent is a smaller effect, and the rows do not show it faster at every size. At 100K the after runs are 0.575, 0.576 and 0.581 s against 0.52, 0.53 and 0.83 s: the agent is about 0.05 s slower, and the two sets overlap through the before set's slow first run, so call 100K unchanged to slightly slower. At 1M the one before run (3.72 s) is above the whole after range (2.79 to 2.86 s), and the earlier bundle-relay set (3.87 to 3.93 s, `rows-131bde6.jsonl`) is 28 to 29% above the after median. At 5M each side is one round: 13.90 s against 17.83 s (19.22 s in the earlier bundle-relay set), 22 to 28% faster, but the three mirror-relay runs on this fixture took 13.67, 13.90 and 15.43 s, so one 5M round is known to about 10%: treat the 5M warm change as roughly a fifth.

Three runs of this harness against the mirror relay, at three heads (median, range in brackets):

| Records | Warm agent wall, s: 3f0a5b5 / abae2b3 / e5fdf9c | Cold total, s: 3f0a5b5 / abae2b3 / e5fdf9c | Agent CPU, s: 3f0a5b5 / abae2b3 / e5fdf9c |
| --- | --- | --- | --- |
| 100K | 0.58 (0.58 to 0.58) / 0.68 (0.64 to 1.01) / 0.58 (0.58 to 0.58) | 1.93 (1.92 to 1.97) / 1.93 (1.74 to 2.21) / 1.83 (1.79 to 3.84) | 0.16 / 0.21 / 0.17 |
| 1M | 3.12 (2.90 to 3.27) / 3.47 (3.38 to 3.83) / 2.79 (2.79 to 2.86) | 5.30 (5.29 to 5.64) / 6.17 (5.75 to 6.22) / 5.08 (5.03 to 5.18) | 1.29 / 1.45 / 1.25 |
| 5M | 15.43 / 13.67 / 13.90 | 30.46 / 27.87 / 26.98 | 6.69 / 6.22 / 6.33 |

The `Detent:abae2b3` set was the slow one at 100K and 1M, and the `Detent:e5fdf9c` jobs, the landing evidence, moved outside its ranges: the 100K warm agent from 0.68 (0.64 to 1.01) to 0.58 s (-16%), the 1M warm agent from 3.47 (3.38 to 3.83) to 2.79 s (-19%) and the 1M cold total from 6.17 (5.75 to 6.22) to 5.08 s (-18%). The 100K cold total median (1.83 s) stays inside its earlier range (1.74 to 2.21 s), with one run at 3.84 s above it. The 5M rounds agree within 2% (warm) and 3% (cold), inside the 10% one round is known to. Against `Detent:3f0a5b5` the warm agent is the same at 100K, 11% faster at 1M (disjoint ranges, 2.79 to 2.86 s against 2.90 to 3.27 s) and 10% faster at 5M (one round each).

The cause of the move is not measured. The merge between `Detent:abae2b3` and `Detent:e5fdf9c` split `src/detent_authority` (service and state code) and changed tests, and no relay, agent or reader source; the two sets ran about eight hours apart; no profile attributes the difference. The three 1M warm medians (3.12, 3.47 and 2.79 s) span 24%, so a 1M warm figure here is known to about 20%, not better.

The agent does about half the CPU work of the bundle-relay agent at 1M and 5M and three fifths at 100K: 0.17 s, 1.25 s and 6.33 s (1.7, 1.25 and 1.27 µs per record) against 0.28 s, 2.66 s and 13.27 s (2.8, 2.7 and 2.7 µs). It no longer builds the generation: it hashes 0 bytes to build and 1.2 MB, 12.1 MB and 60.6 MB to check, against 4.9 MB, 48.5 MB and 242.6 MB to build. It writes 7, 17 and 65 files (11, 53 and 243 before) with the same 4 device flushes and downloads 10 to 15% fewer bytes (0.19 MB, 1.80 MB, 8.98 MB against 0.22, 2.00 and 10.0 MB).

The cost moved to the relay. During the warm agent's run the relay spends 0.34 s, 2.76 s and 14.31 s of CPU (0.01 s, 0.07 s and 0.40 s before), and its resident memory rises from 19, 83 and 204 MiB to 39, 339 and 1,013 MiB at peak (the bundle relay held 26, 181 and 878 MiB throughout). The mirror relay is cheap to bring up and expensive to first use. Over a whole cold run (catch-up plus serving one agent) it still spends less CPU than the bundle relay: 0.44, 3.24 and 16.55 s against 0.66, 6.29 and 31.36 s.

## Method

Each A1 job builds `detent-authority`, `detent-relay` and `detent-agent` (x86_64 release) at the quest's merged head, `Detent:e5fdf9c` (`e5fdf9cacd7b`), and runs `row.sh`, which runs `boot.py` once for the job's one arm. One run clones the fixture's three authority voters (`cp -c`), starts them, then:

- **Warm**: starts the relay alone (`ManagedCohort`, agent count 0) and waits outside the clock until it logs "the namespace mirror is ready", its health is ready with the mirror ready, and four half-second `ps` samples in a row add at most 0.1 s of relay CPU (the mirror polls every 10 ms, so a settled relay is not at zero; bounded at 120 s, and every warm run settled). Then the clock starts with one cold agent.
- **Cold**: starts the relay and the agent under one clock, as `coldboot.py` does (the relay-plus-agent total of [dense-sync-stack.md](dense-sync-stack.md) and [cold-bootstrap-5m.md](cold-bootstrap-5m.md)).

The clock stops when the agent has selected every namespace at the position the authority reports for it (`namespace_revision` per namespace, D3: namespaces advance independently, so no single group version exists). The relay's health carries no revision, so the agent's convergence is the revision check. Both arms share one configuration (relay session limit 48, one agent session). Per-process CPU and RSS come from `ps`, wire bytes from `nettop -P`, files and device flushes from the agent's "publication synced" line, and the relay's resident memory from the run's `ps` trace (derived fields in the rows files, written by `rows.py`).

All 14 jobs ran under `quiet exclusive` at `Detent:e5fdf9c`, one arm per job, each done with exit 0; no two overlapped in time, and none carries a "ran beside" record. 100K and 1M are three rounds each, with the arm order differing between rounds as the queue ran them; 5M is one round, one job per arm, per D85 (5M confirms, it does not repeat). The earlier light probes (`261123-164928-adf267` at 100K, `261123-165613-3cde33` at 5M) gated the harness, and `261124-084513-df11ea` showed it still runs after main's `ManagedCohort` changes.

The before rows (`Detent:3a57661`) are the same harness at the bundle relay, with differences: warm waited for "durable bundles ready", counted a relay settled at 0.01 s of CPU per half-second sample, and read one group version. They are three 100K rounds in one job and one round each at 1M and 5M (`rows-3a57661b.jsonl`). An earlier bundle-relay set at `Detent:131bde6` (three 1M rounds, `rows-131bde6.jsonl`) agrees with it within 8% on every warm and cold figure.

Fixtures, cloned and never written: `100000-main` (revision 784), `1000000-main` (revision 7,816) and `5000000-flat-main` (revision 39,064), seeded before the namespace mirror (`Detent:ea1fddc` for 100K and 5M, `Detent:78d69fa` for 1M); they read unchanged. Hardware: Intel Core i9-9980HK (8 cores, 16 threads), 32 GiB, PCIe SSD, macOS 15.8.1; every process on loopback.

## Time

Medians, range in brackets. Agent wall runs from the agent's start to selection; in the cold arm it includes waiting for the relay.

| Records | Arm | Runs | Agent wall, s | Agent CPU, s | Relay ready, s | Relay plus agent, s |
| --- | --- | ---: | --- | --- | --- | --- |
| 100K | warm | 3 | **0.58** (0.58 to 0.58) | 0.17 (0.16 to 0.17) | 1.38 (1.37 to 3.45), outside the clock | |
| 100K | cold | 3 | 1.81 (1.78 to 3.82) | 0.17 (0.17 to 0.18) | 1.34 (1.25 to 3.29) | **1.83** (1.79 to 3.84) |
| 1M | warm | 3 | **2.79** (2.79 to 2.86) | 1.25 (1.24 to 1.25) | 2.47 (2.29 to 2.47), outside the clock | |
| 1M | cold | 3 | 5.06 (5.01 to 5.17) | 1.28 (1.27 to 1.32) | 2.37 (2.34 to 2.37) | **5.08** (5.03 to 5.18) |
| 5M | warm | 1 | **13.90** | 6.33 | 14.50, outside the clock | |
| 5M | cold | 1 | 26.96 | 6.25 | 13.20 | **26.98** |

The warm and cold agent ranges do not overlap at any size with three rounds. Cold total minus relay ready (0.54 s, 2.71 s, 13.79 s, medians of per-run differences) is within 0.11 s of the warm agent's time at every size (0.58 s, 2.79 s, 13.90 s): the agent's work is the same with or without a head start from the relay. The relay's ready time agrees between arms at 1M (2.29 to 2.47 s warm, 2.34 to 2.37 s cold) and at 5M it differs by 1.3 s in one round each. At 100K one warm run (3.45 s, `261124-202039-aa80ba`) and one cold run (3.29 s, `261124-202043-4374a0`) took about 2 s longer than the others of their arm (1.37 and 1.38 s warm, 1.25 and 1.34 s cold) with the same code, so a 100K relay-ready figure is known to about 2 s, and the 3.84 s cold total above is that cold run.

## Phases

The agent logs one mark for the first generation, "publication synced" (the first publication durable), then selects the remaining namespaces as their pages arrive; this harness records no process-start mark. Medians of the warm runs; before is the bundle relay at `Detent:3a57661`.

| Records | Start to publication synced, s (before) | Synced to selected, s (before) | Agent CPU / wall (before) | Agent CPU at synced, s | Bytes hashed to check (to build before) |
| --- | --- | --- | --- | --- | --- |
| 100K | 0.29 (0.55) | 0.29 (0.00) | 30% (53%) | 0.04 | 1.21 MB (4.85 MB) |
| 1M | 1.09 (3.71) | 1.70 (0.01) | 45% (72%) | 0.21 | 12.1 MB (48.5 MB) |
| 5M | 5.09 (17.86) | 8.81 (-0.03) | 46% (74%) | 1.18 | 60.6 MB (242.6 MB) |

The first publication arrives 1.9 to 3.5x sooner, and then the agent waits. At 5M the log shows page transfers 2, 3 and 4 completing 2.9 s, 2.9 s and 3.0 s after the one before (cumulative 61.8, 92.6 and 123.5 MB decoded), the last at 13.9 s, while the relay's CPU climbs from 2.5 s to 16.7 s and its resident memory from 204 MiB to 1.01 GiB; the mirror counters report 4 page builds. The reading is that the relay builds each namespace's pages on first request and the agent takes them one after another, so the wait is the relay's serial page build. This comes from the logs and counters (`261124-202045-76410f`), not a profile, and from one 5M round; a second agent against the same relay, which would show whether the builds are kept, is not measured.

Only 46% of the 5M wall is agent CPU, against 74% before: the agent is no longer bound by its own build, the rest is waiting on the relay. Wire is not the bound: 9 MB at 5M is 0.7 s even at 100 Mbit/s.

## Files, flushes and wire

| Records | Arm | Files published | Groups | Device flushes (publication; since start) | Agent bytes in | Agent bytes out | Relay bytes out serving | Relay CPU during the agent, s | Agent RSS peak, MiB |
| --- | --- | ---: | ---: | --- | ---: | ---: | ---: | --- | ---: |
| 100K | warm | 7 | 2 | 4; 5 | 185,564 | 3,819 | 183,662 | 0.34 (0.33 to 0.37) | 16 |
| 100K | cold | 7 | 2 | 4; 5 | 190,599 | 12,839 | 190,599 | 0.44 (0.44 to 0.52) | 15 |
| 1M | warm | 17 | 2 | 4; 5 | 1,799,859 | 4,300 | 1,797,663 | 2.76 (2.73 to 2.81) | 63 |
| 1M | cold | 17 | 2 | 4; 5 | 1,804,350 | 12,877 | 1,804,855 | 3.24 (3.13 to 3.40) | 61 |
| 5M | warm | 65 | 2 | 4; 5 | 8,976,884 | 4,310 | 8,976,587 | 14.31 | 147 |
| 5M | cold | 65 | 2 | 4; 5 | 8,984,862 | 19,558 | 8,984,862 | 16.55 | 162 |

Bytes are medians. Files, groups and flushes are identical in every run of a size. In the cold arm "relay CPU during the agent" is the relay's whole CPU (it starts with the agent). The relay fetches the authority's namespaces once (3.9 MB, 39.9 MB and 203.9 MB in 4 files, in every run of a size) and serves the agent 21x, 22x and 23x less than it holds.

| Records | Relay RSS before the agent, warm, MiB (bundle relay) | Relay RSS peak, warm, MiB (bundle relay) | Relay RSS peak, cold, MiB (bundle relay) |
| --- | --- | --- | --- |
| 100K | 19 (26) | 39 (27) | 47 (26) |
| 1M | 83 (181) | 339 (181) | 346 (181) |
| 5M | 204 (878) | 1,013 (878) | 1,078 (878) |

## What it means

- **Relay start**: a 5M relay is ready in 13.2 to 14.5 s, not 139 to 148 s, and a 1M relay in 2.3 to 2.5 s, not 28 to 32 s.
- **Fleet join**: a joining agent costs 13.9 s at 5M, 6.3 s of agent CPU and 9 MB of relay uplink. A thousand agents cost 1.8 core-hours of agent CPU (3.7 before) and 9 GB of uplink. The relay's side is unmeasured past the first agent: if every agent costs the relay what the first did (14.3 s of CPU, 0.8 GiB), a thousand cost 4.0 core-hours; if the page builds are shared, almost nothing. One more row, two agents against one warm relay, decides it.
- **Where the time went**: at 5M the agent's 13.9 s is 5.1 s to the first publication and 8.8 s waiting for the other namespaces' pages.

## Caveats

- **5M is one round** (D85): its warm change from the bundle relay (-22%) is one run against one, and the three mirror-relay runs (13.67, 13.90 and 15.43 s) span 13%; the cold total change (-83%) is far beyond any such gap.
- **Before rows are thinner**: 1M and 5M before are one round each at `Detent:3a57661`, with the earlier three-round 1M set at `Detent:131bde6` (3.87 to 3.93 s) as the spread.
- **Harness differences**: the after harness settles at 0.1 s of CPU per half-second sample and waits for per-namespace positions; the before harness did neither. The warm "relay start to selected" column of `table.py` includes the settle wait and measures nothing in either arm.
- **Run-to-run spread**: the `Detent:abae2b3` set (`rows-abae2b31.jsonl`) and this one differ by 16 to 19% at 100K warm, 1M warm and 1M cold, outside each other's ranges, with no source change in the relay, agent or reader between them and no attributed cause. Numbers here are the `Detent:e5fdf9c` jobs; the earlier set is kept for the comparison above.
- **100K relay start**: one warm and one cold run of the six waited about 2 s longer for the relay to be ready; the cold one sets the 3.84 s top of the 100K cold range.
- **Loopback**: wire time on a real network adds bytes over link speed, 9 MB per agent at 5M.

## Jobs

Rows in [rows-e5fdf9ca.jsonl](../measurements/warm-relay-agent-boot/rows-e5fdf9ca.jsonl) (`rows.py JOB... > ROWS` writes one from jobs; `table.py ROWS` prints the tables above from any rows file or job id). All `quiet exclusive`, one arm per job, at `Detent:e5fdf9c` (`e5fdf9cacd7b`):

| Size | Round | Warm | Cold |
| --- | --- | --- | --- |
| 100K | 1 | `261124-202039-aa80ba` | `261124-202039-9c9088` |
| 100K | 2 | `261124-202042-42c0f3` | `261124-202041-4379bb` |
| 100K | 3 | `261124-202044-26b532` | `261124-202043-4374a0` |
| 1M | 1 | `261124-202040-e47e90` | `261124-202041-d8961e` |
| 1M | 2 | `261124-202043-39475a` | `261124-202042-2dd756` |
| 1M | 3 | `261124-202045-e28f51` | `261124-202046-1fe8d9` |
| 5M | 1 | `261124-202045-76410f` | `261124-202046-64d8d4` |

Earlier set at `Detent:abae2b3` (`abae2b313e71`, [rows-abae2b31.jsonl](../measurements/warm-relay-agent-boot/rows-abae2b31.jsonl); warm and cold per round): 100K `261124-141813-b9846a` and `-141813-5de11a`, `-141816-b1f146` and `-141815-19936e`, `-141818-3ca37e` and `-141818-0904a8`; 1M `261124-141814-202283` and `-141815-2ac21a`, `-141817-af06a9` and `-141816-f2247f`, `-141819-f892ee` and `-141820-725a5e`; 5M `261124-141820-694a39` and `-141821-5f0af0`.

Earlier mirror-relay run (`Detent:3f0a5b5`, [rows-3f0a5b59.jsonl](../measurements/warm-relay-agent-boot/rows-3f0a5b59.jsonl)): `261123-165617-0de0e5`, `-165620-51800c`, `-165624-e3b117`, `-165628-f15efc`, `-165631-3bc1e4`, `-165634-7ba37a`, `-165638-b3fd52`, `-165643-4f1b40`, `-165646-b31273`, `-165649-7c0f14`, `-165653-3ebe6a`, `-165656-0c6a5e`, `-165700-7beee0`, `-165703-83f18d`.

Before (`Detent:3a57661`, [rows-3a57661b.jsonl](../measurements/warm-relay-agent-boot/rows-3a57661b.jsonl)): `261121-130739-53a9e5` (100K, three rounds), `261121-130739-c9c5d4` (1M), `261121-130739-ea2ed5` (5M warm), `261121-130740-155781` (5M cold).
