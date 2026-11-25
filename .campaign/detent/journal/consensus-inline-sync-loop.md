# consensus-inline-sync-loop: journal

State: ready for review (jobs queued: 261125-014238-f65c96 261125-014240-315596 261125-014250-e5f9bb 261125-014254-56d5d1 261125-014258-ab633e 261125-014320-2d75b5; CI 45588135687)
Quest: .campaign/detent/quests/consensus-inline-sync-loop.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Code done at consensus:7a9ae86, pushed (CI 45588135687). Commits:
- consensus:d2ef9a1 SyncThread (groups_test's journal completion, one per loop), slow-sync case
  (A1), fetch-alongside back on a shared link loop (A5), reader + Lag (A6), driver's recover pin
  (the lead's strata constraint, below), DRIVER.md "Where syncs run" (A3), mutant `sync-on-loop`.
- consensus:7a9ae86 fetch-alongside's line names each loop's longest journal operation.

## Next

1. Read the queued jobs and CI; fold in review findings.

## Blocked on

Nothing. For the lead:
- The file set names `DRIVER.md`; the file is `docs/DRIVER.md`, so `campaign status` lists it as
  outside the set. Please correct the set.
- bench/netlab/node.cc (outside the set) pairs InlineCompletion with HostTransport on its one
  loop: the pairing DRIVER.md now says not to make. A slow sync there holds its links. Not a
  test; a follow-up quest if it matters.
- CMakeLists.txt is outside the set, so slow-sync has no ctest entry of its own: the `driver`
  entry runs it after its three cases (~25 s total locally); `slow-sync` also runs alone.

## The choice (by measurement)

The blocking entry itself (`consensus::run` over `task::blocking_scheduler`) never carries links:
no reactor, and every peer call throws `needs_peer`. The stall exists only where a Driver on a
transport loop is handed InlineCompletion: groups_test's Node (and bench/netlab). So "the
blocking entry's syncs" are, in practice, an inline completion on a reactor loop.

Probe ~/.scratch/detent/consensus-inline-sync-loop/syncprobe.cc (an asio loop thread; 4 KiB
pwrite + F_FULLFSYNC; n=400, two rounds; quiet strict; macOS x86_64), as the loop sees it:

| | p50 | p90 | p99 |
|---|---|---|---|
| hop only, on the loop | 14.5-15.1 us | 18-32 us | 36-64 us |
| hop only, via a sync thread | 27.5-28.0 us | 41-69 us | 67-102 us |
| sync, on the loop | 21.01-21.03 ms | 21.9-23.0 ms | 37.2-38.8 ms |
| sync, via a sync thread | 21.02-21.03 ms | 22.3-23.1 ms | 37.2-39.2 ms |

Idle: the parked sync thread used 0 us of CPU in 5 s (blocked on a condvar; one per loop).

- Off-loop syncs (chosen): +13 us per operation at p50 (0.06% of a 21 ms sync, inside the
  sync's own spread); 0 idle CPU, one parked thread per loop. Works at any L, one loop included
  (slow-sync, netlab).
- Placement (rejected): sync latency as inline (21 ms, no hop), but it needs a loop no link
  uses. Links place by peer id over the same 4,096 slots, so with peers >= loops there is often
  none, and with L = 1 never; the journal places groups by that slot (outside the set), so
  keeping groups off link loops changes both placements, and every frame of such a group
  crosses loops (one post, ~15 us, per read batch). It also drops the coverage A5 restores.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | 261125-014238-f65c96 (slow-sync at 7a9ae86); fails without: 261125-014240-315596 (mutant `sync-on-loop`, consensus:dfbe708 on 7a9ae86: the sync back on the loop, main's shape). Local: head held 6,001 ms, loops turned within 1 ms, 0 peers lost, term kept, 28 commits each within 262 ms; mutant: loop held 6,043 ms, 2 peers lost, FAILED |
| A2 | 261125-014250-e5f9bb (ctest groups_test, driver*, blocking_entry, completion_asio). No library file changed: include/ untouched |
| A3 | [artifact] docs/DRIVER.md "Where syncs run" (consensus:d2ef9a1): per completion, where a sync runs and whether the loop turns; what an inline sync on a link loop costs (heartbeats, dead_after, elections). docs/mechanisms/host-transport.md Placement points to it |
| A5 | 261125-014254-56d5d1, 261125-014258-ab633e (strict, 25 runs each beside 4 burners). The group on the loop of the links to node 1 (nodes 2 and 3 serve node 1's fetches there; asserted). Local with FETCH_ALONGSIDE_SLOW_SYNC=6000: node 2's operation 6,036 ms, its loops turned within 2 ms, 0 peers gone, 16 of 16 exact, term kept |
| A6 | 261125-014320-2d75b5 (reader). Bound 1,000 + Lag; local: gap 103 ms of 1,003 allowed |
| A4 | CI 45588135687 at 7a9ae86 |

## Result

groups_test's journal syncs off its loop: SyncThread (test/transport/groups_test.cc) drives each
operation as InlineCompletion does, on a thread of the loop's, and resumes the caller on its
loop. A 6 s sync on a follower whose links share the group's loop no longer stops its heartbeats
(slow-sync; fetch-alongside with the knob). docs/DRIVER.md "Where syncs run" states the rule for
each completion. Cost: +13 us per journal operation at p50 against a 21 ms F_FULLFSYNC, 0 idle
CPU.

Lead's strata constraint (state(id)->last right after recover returns is the recovered log
end): the driver case records `state(g)->last` in recover's completion, on the loop, and asserts
it equals the log end the node stopped at (local: 2 == 2 for both groups). Recovery now runs
through SyncThread (it suspends), and the pin holds.

## Rulings I made

- Shape: off-loop syncs, in the harness (SyncThread), not a library type: no production
  embedder pairs InlineCompletion with HostTransport except bench/netlab (flagged above);
  detentd uses native. A library sync-thread completion would belong in the completion repo.
- The transport does not refuse the pairing: it cannot see a Driver's completion. DRIVER.md
  says so.
- fetch-alongside: the term allowance is now a process stall past 1,000 ms only (`stalled`),
  no longer the longest journal operation: a slow sync must not cost the term.
- Reused groups_test's LoopLags for slow-sync's loop turns (no new probe); Lag (lag.h) for the
  host's lag in slow-sync and reader, as A6 asks.
- A1's fails-without runs at a mutant on the head (main has no slow-sync case): the mutant
  restores main's on-loop sync exactly.
