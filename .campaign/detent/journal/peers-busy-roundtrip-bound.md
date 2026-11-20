# peers-busy-roundtrip-bound: journal

State: waiting on jobs 261119-161259-366f8f 261119-161300-7ea583 261119-161302-bf6ec3 261119-161315-9fcd66, CI at consensus:16d10b0
Quest: .campaign/detent/quests/peers-busy-roundtrip-bound.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Fix at consensus:16d10b0 (test/transport/peers_test.cc); proofs queued: A2 passes 366f8f (RelWithDebInfo
loaded x48, STRICT) and 7ea583 (TSan loaded x32); A2 mutant bf6ec3 (--fails-without at
consensus:3e52869, refs/mutants/peers-busy-roundtrip-bound/append-delay-100ms: the append writer
pauses 100 ms before every send; its log must fail the `slow * slow_round_trips_share <= busy.count`
CHECK, not another); A3 suite 9fcd66 (--minutes 50) and CI.

## Next

1. Read the four jobs; check the mutant's failing line is the share CHECK.
2. If all green: fill Evidence and Result, ready for review.

## Blocked on

<What, and who decides. "Nothing" when nothing.>

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | Cause: the one executor runs 20-35 ms of fetch work per pump gap (quiet host, 100% CPU: rel-quiet 53e971 gap 19-23 ms, cpu equal), so an append round trip spans one to two such gaps (quiet: 18 of 20 runs <=16 ms, 2 <=64 ms), and a loaded host takes the executor off the CPU for tens of ms more. Loaded RelWithDebInfo 833fb6 (48 runs, 4 workers + 4 spinners, consensus:a34b8ed): 5 failed the 64 ms bound; their longest gap was off the CPU 42-84 ms (wall 85, cpu <=37), the 41 passing ones <=47 ms; each failing run had 1-3 of ~120 round trips (2.3% at most) in the 256 ms bucket. Not the transport: quiet-phase round trips <=16 ms in 72 of 72 runs (7beb709 probe d7f473), and the watchdog thread saw no host stall (0-6 ms) because a sleeper is favoured over a busy thread. Heavier load (d7f473: 16 spinners + 6 workers) put up to 8% past 64 ms (67 of 72 runs failed the old bound); TSan 762e16 loaded: 4 of 32 reach 256 ms bucket, <=0.6% of round trips; bacf48 heavy TSan: up to 18.5% past 64 ms, 3 of 36 in the 1 s bucket. Passing distributions in ~/.scratch/peers-busy-roundtrip-bound/{rel,tsan}-{1,loaded,quiet}. |
| A2 | see jobs |
| A3 | see jobs |

## Result

peers_test fetch-alongside-consensus bounds the share of append round trips past the bucket edge
(64 ms; 256 ms under TSan), one in eight at most, instead of the slowest bucket. Headroom: loaded
runs peak at 2.3% (RelWithDebInfo) and 0.6% (TSan), so 12.5% is 5x and 20x; a 100 ms delay on every
append puts nearly all past 64 ms. The edge under TSan is unchanged (a 100 ms delay passes it there).
Not fixed in the transport: the executor's 20-35 ms work gaps are the design of the fetch lane.

## Rulings I made

1. Share, not slowest: a bucket maximum names the one stalled round trip, the host's; the share
   names whether appends are held back. Timeouts stay 0, commits and term checks unchanged.
2. Kept the TSan/non-TSan edge split (an existing shape); a 64 ms TSan edge fails heavy overload (21 of 36 at 1/8).
3. Removed the stall instrumentation (watchdog, CPU per gap) from the test after naming the cause; its numbers are in A1.
