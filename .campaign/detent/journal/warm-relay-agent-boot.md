# warm-relay-agent-boot: journal

State: working
Quest: .campaign/detent/quests/warm-relay-agent-boot.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Harness in docs/measurements/warm-relay-agent-boot/ (boot.py one run warm|cold; row.sh in-job
driver; seed.sh 1M seed; submit.sh queues smoke, seed, rows). Jobs: smoke 261120-041206-eee476 (strict, 10K warm+cold); seed
261120-041208-23e7e4 (light, 1M to capacity-trials/1000000-main); rows (exclusive, --after both):
100K 3 rounds 261120-042634-382bae; 1M r1 261120-042636-86edd1, r2 261120-042641-7f6992, r3
261120-042647-d964b3; 5M r1 warm 261120-042638-33dce4, cold 261120-042639-2a0b48. Per D85 (lead's message), 5M is one
confirmation round: r2 and r3 (261120-042643-d21172, -042645-6308e2, -042648-8b762d,
-042650-a9e03a) cancelled; spread comes from 100K (3 rounds in one job) and 1M (3 jobs).

## Next

1. Push; CI macos-arm64 (D57 gate) green at the head, then ready for review.

## Blocked on

Nothing.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| A1 | 261120-144419-5e7242, 261120-144420-73a018, 261120-144420-48ab68, 261120-144420-f0a573, 261120-144421-d16a3a, 261120-144421-5a223a (all exit 0, exclusive, at 131bde6; no "ran beside" record) |
| A2 | docs/perf/warm-relay-agent-boot.md; rows in docs/measurements/warm-relay-agent-boot/rows-131bde6.jsonl |
| Suite | 261120-144425-4edf28 (exit 0 at 131bde6); gate CI macos-arm64 |

## Result

docs/perf/warm-relay-agent-boot.md. Warm-relay agent bootstrap, medians: 0.57 s (100K, 3 rounds),
3.92 s (1M, 3), 19.22 s (5M, 1 per D85); relay-plus-agent cold total 3.93 / 33.51 / 162.78 s, so
the agent is 15% / 12% / 12% of it (cold-bootstrap-5m.md estimated 32-38%). Cold total minus relay
ready equals the warm agent time at every size. Agent CPU 0.31 / 2.88 / 14.40 s (about 2.9 us per
record, same in both arms); 11 / 53 / 243 files, 4 device flushes; 0.22 / 2.0 / 10.0 MB in.
Open: fetch vs decode vs build are one span (the agent logs no mark between them).

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
- Not merged main again: since 131bde6 main changed docs, campaign files and two tests
  (tests/detent_local), none in src, include or the built targets; another merge would only rerun
  the six exclusive rows. The lead's land --merge-main takes it.
- table.py also reads a committed rows file (rows-131bde6.jsonl), so the doc's tables survive job
  pruning.
