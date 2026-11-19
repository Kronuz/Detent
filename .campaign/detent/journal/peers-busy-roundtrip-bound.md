# peers-busy-roundtrip-bound: journal

State: waiting on jobs 261119-155313-833fb6 261119-155314-762e16 261119-155316-53e971 261119-155317-278c71
Quest: .campaign/detent/quests/peers-busy-roundtrip-bound.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Step 1 (A1). First probes (consensus:7beb709, 6 workers x N beside 16 niced spinners, ~/.scratch/peers-busy-roundtrip-bound/{rel,tsan}-1):
- RelWithDebInfo, d7f473: 67 of 72 fail peers_test.cc:1210 (the bound). Fetching slowest bucket
  <=256 ms in 67, <=64 ms in 5; quiet phase <=16 ms in 72 of 72. Pump gap while fetching 88-163 ms
  (quiet 1-3 ms); watchdog thread host stall 0-1 ms in every run. Buckets while fetching:
  ~35 in 4 ms, ~75 in 16 ms, ~8 in 64 ms, ~5 in 256 ms of ~120 round trips.
- TSan, bacf48: 3 of 36 fail (1 s bucket), 33 pass at the 256 ms bound; quiet <=16 ms in 31, <=64 ms in 5.
- So the executor stops for 100+ ms while fetching, the oversleeping watchdog does not see a host
  stall (a sleeper is favoured over a busy thread, so it proves little). Next probe separates
  executor work from time off the CPU with thread CPU time per pump gap (af3d2b1):
  The af3d2b1 probes (3024fe 1465fa 8569a4 62a3e2) failed to compile (my `auto` declaration
  error, fixed in a34b8ed); rerun at a34b8ed with spinners capped at the light hold's CPUs
  (4 workers + 4 spinners): 833fb6 rel loaded x48, 762e16 TSan loaded x32, 53e971 rel quiet x20
  (strict), 278c71 TSan quiet x15 (strict).

## Next

1. Read them: the worst gap's CPU vs wall time (work or off CPU), and the quiet-host baseline.
2. Derive the bound with stated headroom, keeping a 100 ms transport delay failing; mutant =
   writer pauses 100 ms before sending an append (refs/mutants/peers-busy-roundtrip-bound/append-delay-100ms),
   job --fails-without; the failing CHECK must be the bound's line.
3. Suite (consensus, --minutes 50) and CI.

## Blocked on

<What, and who decides. "Nothing" when nothing.>

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

<Decisions inside the quest's scope, so the reviewer can check them.>
