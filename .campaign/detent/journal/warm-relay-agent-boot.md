# warm-relay-agent-boot: journal

State: waiting on jobs 261123-151051-06b0df 261123-151055-68ee44
Quest: .campaign/detent/quests/warm-relay-agent-boot.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Rows at dc0315b5 (261122-211123-e68717 100K, -211123-7d24e2 1M, -211124-af8242 5M) timed out from a
hang, not their size: main (30c2d65b) serves the relay from its namespace mirror only, so it logs
"the namespace mirror is ready" and never "durable bundles ready", and boot.py waited 1800 s for
the old line. From each job's start in those logs: authorities listening at 6.0 / 9.8 / 54 s
(100K / 1M / 5M), mirror ready at 7.9 / 13.0 / 68 s (the relay 1-14 s after it starts, versus
3.4 / 32 / 148 s for the old bundle relay). The old relay's rows at 3a57661b, per mode: 100K
total 9-24 s (one warm run hit the 120 s settle bound), 1M 44-51 s, 5M 214-234 s (authority 47-55
s, relay 139-148 s, warm agent 17.8 s); each fit 10 minutes, 5M warm with 6 minutes to spare.

At 0a247a7e (main merged; boot.py waits for the mirror line and a ready health, reads each
namespace's position (D3), records mirror counters and the agent's publication files and device
flushes; selection bound 300 s): light probes 261123-141834-bbcc84 (100K warm+cold) and
261123-141838-0c7abd (5M warm, at 10474914) gate the rows. Rows, exclusive, one mode per job,
10 minutes each, interleaved: 100K and 1M rounds 1 warm,cold / 2 cold,warm / 3 warm,cold (after
the 100K probe), 5M round 1 warm, cold (after the 5M probe, D85): 261123-142309-4755f7, -142315-5d9d85, -142322-7fc115, -142327-c26a47, -142332-844c26, -142337-6f5b0b, -142341-8b6e26, -142346-eaa46d, -142350-34cc60, -142354-d2245b, -142359-6c39a3, -142404-b2697f, -142408-98fe9f, -142412-9734c4.
Worst case per 5M job: clone + authority ~60 s, mirror ~15 s, settle <=120 s, agent <=300 s, so
no job needs --long.

Probes at 10474914 on the old fixtures: 261123-141834-bbcc84 (100K) and 261123-141838-0c7abd (5M)
never converged (wait_selected timeout). 100K: mirror ready 2.4 s after relay start; relay never
idled (21 s CPU over the 120 s settle bound; the mirror syncs every 10 ms); the agent logged
"1 namespaces selected", two publications and 4 page transfers (2.47 MB) within 0.4 s of start,
then nothing, while the authority answered /v2/read version 784 for each of load0..load3. The 14
rows were cancelled (12 skipped while queued, 2 cancelled).

Earlier rounds (history):

Harness in docs/measurements/warm-relay-agent-boot/ (boot.py one run warm|cold; row.sh in-job
driver; seed.sh 1M seed; submit.sh queues smoke, seed, rows). Jobs: smoke 261120-041206-eee476 (strict, 10K warm+cold); seed
261120-041208-23e7e4 (light, 1M to capacity-trials/1000000-main); rows (exclusive, --after both):
100K 3 rounds 261120-042634-382bae; 1M r1 261120-042636-86edd1, r2 261120-042641-7f6992, r3
261120-042647-d964b3; 5M r1 warm 261120-042638-33dce4, cold 261120-042639-2a0b48. Per D85 (lead's message), 5M is one
confirmation round: r2 and r3 (261120-042643-d21172, -042645-6308e2, -042648-8b762d,
-042650-a9e03a) cancelled; spread comes from 100K (3 rounds in one job) and 1M (3 jobs).

## Next

1. Fresh 100K seed at main 261123-151051-06b0df (fixture 100000-mirror, light) then probe
   261123-151055-68ee44 (light, warm+cold, at 15951a88: marks keep the agent's selected table).
   Agent selects: the old fixtures are stale; reseed 1M (light) and 5M (--long "5M seed takes
   ~4,700 s"), then resubmit the rows. Agent does not select: product regression, report to the
   lead at once with the logs.
2. When the rows finish: tabulate, commit the rows file, rewrite docs/perf/warm-relay-agent-boot.md
   for the mirror relay with the bundle-relay rows at 3a57661b as the before, then ready for review.

## Blocked on

Nothing.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| A1 | pending: 261123-142309-4755f7, -142315-5d9d85, -142322-7fc115, -142327-c26a47, -142332-844c26, -142337-6f5b0b, -142341-8b6e26, -142346-eaa46d, -142350-34cc60, -142354-d2245b, -142359-6c39a3, -142404-b2697f, -142408-98fe9f, -142412-9734c4 (exclusive at 0a247a7e). Superseded: 261122-211123-e68717, 261122-211123-7d24e2, 261122-211124-af8242 (timeout: the harness waited for a log line main removed) |
| A2 | docs/perf/warm-relay-agent-boot.md; rows in docs/measurements/warm-relay-agent-boot/rows-131bde6.jsonl |
| Suite | 261120-144425-4edf28 (exit 0 at 131bde6); CI run 44831410902 at 131bde6, all jobs green incl. macos-arm64 (D57); later commits are docs-only, paths-ignored by CI |

## Result

docs/perf/warm-relay-agent-boot.md. Warm-relay agent bootstrap, medians: 0.57 s (100K, 3 rounds),
3.92 s (1M, 3), 19.22 s (5M, 1 per D85); relay-plus-agent cold total 3.93 / 33.51 / 162.78 s, so
the agent is 15% / 12% / 12% of it (cold-bootstrap-5m.md estimated 32-38%). Cold total minus relay
ready equals the warm agent time at every size. Agent CPU 0.31 / 2.88 / 14.40 s (about 2.9 us per
record, same in both arms); 11 / 53 / 243 files, 4 device flushes; 0.22 / 2.0 / 10.0 MB in.
Open: fetch vs decode vs build are one span (the agent logs no mark between them).

## Rulings I made

- A1's "durable bundles ready" maps to the mirror relay's "the namespace mirror is ready" plus a
  ready /v1/health (main removed bundles in 30c2d65b); the relay health has no revision, so the
  agent's convergence to every namespace's position is the revision check. Lead confirmed; A1
  amended on home main.
- One mode per exclusive job (lead's direction); A2's relay "publication files" become the
  agent's own publication files and device flushes, plus the mirror's fetched files and bytes.

- New harness boot.py instead of coldboot.py: main renamed detentd to detent-authority, which
  coldboot.py and seed.py still name; boot.py starts the relay through ManagedCohort with count 0,
  then the agent with relay None (relay ports kept), so warm and cold share one configuration. The
  relay's session limit is 48 instead of coldboot's 49 (it counts agents at relay start); one agent
  uses one session.
- Warm means: relay logged "durable bundles ready", its health shows the fixture's revision and
  snapshot_ready, and four half-second ps samples in a row add at most 0.01 s of relay CPU
  (bounded at 120 s; marks record whether it settled).
- The cold arm (relay and agent under one clock, as coldboot.py) runs in the same jobs on the
  same fixture, for the relay-plus-agent comparison.
- Rows run without coldboot's restarts and per-job 10K smoke: one strict smoke job gates them.
- Fixtures: 100000-main and 5000000-flat-main (seeded at Detent:ea1fddc, 2026-11-18); a new
  1000000-main seeded at this quest's tree by seed.sh.
- Not merged main again: since 131bde6 main changed docs, campaign files and two tests
  (tests/detent_local), none in src, include or the built targets; another merge would only rerun
  the six exclusive rows. The lead's land --merge-main takes it.
- table.py also reads a committed rows file (rows-131bde6.jsonl), so the doc's tables survive job
  pruning.
