# warm-relay-agent-boot: journal

State: working
Quest: .campaign/detent/quests/warm-relay-agent-boot.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Harness in docs/measurements/warm-relay-agent-boot/ (boot.py one run warm|cold; row.sh in-job
driver; seed.sh 1M seed; submit.sh queues smoke, seed, rows). Smoke (10K) and 1M seed submitted.

## Next

1. When smoke and seed pass: `bash docs/measurements/warm-relay-agent-boot/submit.sh rows SMOKE SEED`.
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
