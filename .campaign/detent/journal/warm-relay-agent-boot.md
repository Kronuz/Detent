# warm-relay-agent-boot: journal

State: ready for review (doc and rows at abae2b31; e5fdf9ca reruns queued as landing evidence, ids in Evidence)
Quest: .campaign/detent/quests/warm-relay-agent-boot.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Rows at abae2b31 (14 exclusive jobs, all passed, no "ran beside", no time overlap) tabulated into
docs/measurements/warm-relay-agent-boot/rows-abae2b31.jsonl (job field and relay RSS before/peak
derived from each run's marks.json trace: before = last relay sample at t <= 0.1 s, peak = max;
the same derivation reproduces rows-3f0a5b59's values). docs/perf/warm-relay-agent-boot.md
rewritten on them. The lead requeued the 14 rows at e5fdf9ca as landing evidence (ids in
Evidence); none had ended when the doc was written. When they end: table.py their ids; if a median
leaves its abae2b31 range (100K warm 0.64-1.01 s, 1M warm 3.38-3.83 s, 1M cold total 5.75-6.22 s,
100K cold total 1.74-2.21 s; 5M is one round, treat +-10% as the spread) say so in the doc.

Doc's headline: warm agent 0.68 / 3.47 / 13.67 s (100K / 1M / 5M), cold relay+agent 1.93 / 6.17 /
27.87 s, so the agent is 36% / 56% / 49%. Against the earlier mirror-relay run at 3f0a5b59 the warm
agent is slower at 100K (+0.10 s) and 1M (+0.35 s), both outside the earlier range, faster at 5M
(-1.76 s, one round each); cause unmeasured (main's agent, reader, local, testing changed between
the heads). Against the bundle relay, cold totals -49% / -81% / -82%; warm agent 100K +0.15 s,
1M inside the one before run's noise, 5M about -23% (one round).

CI on the branch's last run failed on macos-x86_64 only and its log is gone; the push of e5fdf9ca
runs it again (run id under Evidence).

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

Cause (harness): fresh seed 261123-151051-06b0df (100000-mirror, at 15951a88) and probe
261123-151055-68ee44 still timed out; its marks show the agent selected load0..3 at 205/206/207/208
(D3 per-namespace positions) while boot.py waited for 784, /v2/read's group `version`. Fixed at
b0e09b72: wait for `namespace_revision`. Same probe: the relay never settled under the 0.01 s
rule (20.5 s CPU in 120 s, the mirror polls every 10 ms); now 0.1 s per half-second sample.
Note: a resubmit at a new commit returned the failed 68ee44 (identity covers row.sh, not boot.py);
--force gave 8f37e3.

Probes at b0e09b72 select (light, indicative): 261123-164944-8f37e3 fresh 100K warm agent 1.42 s,
cold relay-start-to-selected 2.33 s; 261123-164928-adf267 old 100K warm 1.18 s, relay ready 1.35 s,
settled 3.5 s, agent 7 files, 4 device flushes, 0.18 MB in. Old fixtures work: no reseed.

Earlier rounds (history):

Harness in docs/measurements/warm-relay-agent-boot/ (boot.py one run warm|cold; row.sh in-job
driver; seed.sh 1M seed; submit.sh queues smoke, seed, rows). Jobs: smoke 261120-041206-eee476 (strict, 10K warm+cold); seed
261120-041208-23e7e4 (light, 1M to capacity-trials/1000000-main); rows (exclusive, --after both):
100K 3 rounds 261120-042634-382bae; 1M r1 261120-042636-86edd1, r2 261120-042641-7f6992, r3
261120-042647-d964b3; 5M r1 warm 261120-042638-33dce4, cold 261120-042639-2a0b48. Per D85 (lead's message), 5M is one
confirmation round: r2 and r3 (261120-042643-d21172, -042645-6308e2, -042648-8b762d,
-042650-a9e03a) cancelled; spread comes from 100K (3 rounds in one job) and 1M (3 jobs).

## Next

0. State: ready for review. The e5fdf9ca reruns (Evidence) are the landing evidence; compare them to the doc's ranges when they end.
1. (done at 794fe37f) Suite 261124-084506-9cfdb6 and harness probe 261124-084513-df11ea at the merged head 794fe37f
   (light; the probe checks boot.py still runs with main's ManagedCohort changes, not a row). When
   both pass: set "ready for review", push (Kronuz token).
2. Deferred, the lead decides: nothing needs a rerun (src, include, cmake untouched by the merge).
   Optional new row: two agents against one warm relay (does the relay keep its page builds?).

## Blocked on

Nothing.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| A1 | Landing evidence, exclusive at e5fdf9ca (queued, one mode per job): 261124-202039-aa80ba, 261124-202039-9c9088, 261124-202042-42c0f3, 261124-202041-4379bb, 261124-202044-26b532, 261124-202043-4374a0, 261124-202040-e47e90, 261124-202041-d8961e, 261124-202043-39475a, 261124-202042-2dd756, 261124-202045-e28f51, 261124-202046-1fe8d9, 261124-202045-76410f, 261124-202046-64d8d4. Measured, all passed at abae2b31 (exclusive, no "ran beside", no overlap): 261124-141813-b9846a, 261124-141813-5de11a, 261124-141816-b1f146, 261124-141815-19936e, 261124-141818-3ca37e, 261124-141818-0904a8, 261124-141814-202283, 261124-141815-2ac21a, 261124-141817-af06a9, 261124-141816-f2247f, 261124-141819-f892ee, 261124-141820-725a5e, 261124-141820-694a39, 261124-141821-5f0af0; earlier at 3f0a5b59: 261123-165617-0de0e5 .. 261123-165703-83f18d |
| A2 | docs/perf/warm-relay-agent-boot.md; rows in docs/measurements/warm-relay-agent-boot/rows-abae2b31.jsonl (earlier: rows-3f0a5b59.jsonl; before: rows-3a57661b.jsonl); the e5fdf9ca reruns above confirm or correct it |
| Suite | 261124-084506-9cfdb6 (exit 0 at the merged head 794fe37f) |

## Result

docs/perf/warm-relay-agent-boot.md (mirror relay at abae2b31; earlier run 3f0a5b59; before = bundle
relay at 3a57661b). Warm agent medians 0.68 s (100K, 3 rounds, range 0.64-1.01), 3.47 s (1M, 3,
3.38-3.83), 13.67 s (5M, 1 per D85); relay-plus-agent cold totals 1.93 / 6.17 / 27.87 s, so the agent
is 36% / 56% / 49% of it (14% / 12% / 11% on the bundle relay). Against the bundle relay: cold total
-49% / -81% / -82% (disjoint ranges at 100K, far apart at 1M and 5M); warm agent +0.15 s at 100K,
unseparated at 1M (3.72 s is inside the new range), about -23% at 5M (one round each, known to about
10%). Against the earlier run at 3f0a5b59 the warm agent is +17% at 100K and +11% at 1M (disjoint
ranges) and -11% at 5M; cause unmeasured. Agent CPU 0.21 / 1.45 / 6.22 s (about half the bundle-relay
agent); the cost moved to the relay: CPU during the warm agent 0.51 / 3.47 / 13.99 s (was 0.01 / 0.07 /
0.40), peak RSS 48 / 295 / 1,112 MiB (was 27 / 181 / 878); at 5M the agent waits 8.6 s after its first
publication for the relay's serial page builds (logs, not a profile). Open: second-agent cost on one
relay (unmeasured); the e5fdf9ca reruns.

## Rulings I made

- Relay settled = four half-second samples each adding at most 0.1 s CPU (was 0.01 s): the mirror's
  10 ms poll is its resting cost; catch-up runs near a full core.

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
