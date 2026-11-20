# warm-relay-agent-boot: journal

State: waiting on jobs 261120-041206-eee476 261120-041208-23e7e4 261120-042634-382bae 261120-042636-86edd1 261120-042638-33dce4 261120-042639-2a0b48 261120-042641-7f6992 261120-042643-d21172 261120-042645-6308e2 261120-042647-d964b3 261120-042648-8b762d 261120-042650-a9e03a
Quest: .campaign/detent/quests/warm-relay-agent-boot.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Harness in docs/measurements/warm-relay-agent-boot/ (boot.py one run warm|cold; row.sh in-job
driver; seed.sh 1M seed; submit.sh queues smoke, seed, rows). Jobs: smoke 261120-041206-eee476 (strict, 10K warm+cold); seed
261120-041208-23e7e4 (light, 1M to capacity-trials/1000000-main); rows (exclusive, --after both):
100K 3 rounds 261120-042634-382bae; 1M r1 261120-042636-86edd1, r2 261120-042641-7f6992, r3
261120-042647-d964b3; 5M r1 warm 261120-042638-33dce4, cold 261120-042639-2a0b48; r2 cold
261120-042643-d21172, warm 261120-042645-6308e2; r3 warm 261120-042648-8b762d, cold
261120-042650-a9e03a.

## Next

1. If smoke or seed fails: fix, resubmit (rows replace themselves at the new head).
2. Tabulate rows into docs/perf/warm-relay-agent-boot.md (A2); check no "ran beside" record.

## Blocked on

Nothing.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| <A1> | <from `campaign brief`> |

## Result

<When ready for review: what changed, its measured effect with numbers and the jobs behind them,
and what it leaves open. Results and how it works go in the docs of the repository it changed
(docs/perf/, docs/mechanisms/, a library's own); name them here. `campaign map` shows this.>

## Rulings I made

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
