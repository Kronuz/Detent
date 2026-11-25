# consensus-inline-sync-loop: journal

State: ready for review (fix round 1; jobs queued: 261125-022317-c35d52 261125-022319-78e2a5 261125-022321-d39023 261125-022322-8743dc 261125-022324-925f03 261125-022326-2ffaad 261125-022327-3106af 261125-022329-f14798 261125-022338-e1ff83 261125-022340-33344a; CI 45594592100)
Quest: .campaign/detent/quests/consensus-inline-sync-loop.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Fix round 1 (review consensus-inline-sync-loop-correctness, CHANGES) done at consensus:9f14194,
pushed (CI 45594592100). Commits: consensus:d2ef9a1, consensus:7a9ae86 (round 0),
consensus:9f14194 (round 1: native completion in groups_test and netlab; SyncThread gone).

Findings:
- F1 + F3 (one fix): SyncThread deleted. groups_test's journal runs on
  `AsioCompletionDriver<SlowQueue>`, SlowQueue wrapping `NativeCompletionQueue` and holding a
  reaped completion `delay` ms (slow-sync's 6 s sync, the slot held, the loop turning). Class: an
  operation completed off its owner; the only off-owner completer was SyncThread, so every
  transition in the class (each journal operation's `complete`: turns, Bulk, reclaim, recovery)
  now completes on the loop through the one native driver. Tests: the TSan run of driver,
  joiner, relayout, fetch-alongside, install, slow-sync (261125-022338-e1ff83) and its
  fails-without at 7a9ae86 (261125-022340-33344a); CI linux-tsan at 9f14194. Local TSan
  (macOS, CI's flags, halt_on_error): all six cases exit 0, 0 reports. DRIVER.md's table keeps
  native and InlineCompletion; the rule reads "syncs off the loop: the native completion". The
  13 us / 0 us-idle numbers below describe SyncThread, which no longer exists.
- F2: see "F2's cause" below.
- F5: bench/netlab/node.cc on `AsioCompletionDriver<NativeCompletionQueue>` (default policy;
  reset on the loop at shutdown). Builds (node.cmake, no CMake change). Its local smoke on macOS
  fails at startup with "node pins: the local certificate is not the local node's key", and so
  does main's node.cc built the same way (pre-existing; netlab runs in Linux guests). Not run in
  a guest.
- F4: ruled by the lead (documented rule; Xapiand told). Nothing to do.
- F6, F7: nits, no change.

## F2's cause

The quiet-commit miss (1 of 25, 261125-014258-ab633e, groups_test.cc:2802) at 7a9ae86 is
attributed to F1's race: CI's linux-tsan at 7a9ae86 reports the race in fetch-alongside 55 ms
after "the group elects and commits", that is in the quiet commits, the phase that missed:
StoreReclaim::complete runs Journal::finish_reclaim and admission credit on SyncThread while the
loop's Worker steps the same journal (store.h:288 against worker.h:518). In Release that is UB on
the journal and admission state a proposal and its commit go through. Evidence at the head:
the TSan job and its fails-without (above: the fails-without runs fetch-alongside and driver at
7a9ae86 and names the phase of the first report), and A5's 4 x 25 strict runs. The quiet
commits now print, on a miss, which node lacks the command, each node's role, term, committed,
applied and last, its loops' turns, its longest journal operations and the process stall, then
assert (the 2 s bound unchanged, D152). Not shown: the miss itself reproduced at 7a9ae86 with that
diagnosis (1 in 25; a rerun of 7a9ae86 would carry no print). If A5 misses at the head, the
print names the cause and F2 reopens.

## Next

1. Read the round-1 jobs and CI 45594592100; fold in the next review.

## Blocked on

Nothing. For the lead:
- CMakeLists.txt is outside the set, so slow-sync has no ctest entry of its own: the `driver`
  entry runs it after its three cases; `slow-sync` also runs alone.
- bench/netlab's node fails at startup on macOS at main and at the head ("node pins: the local
  certificate is not the local node's key"); netlab's own runs are in Linux guests, which I did
  not run. docs/perf/netlab-consensus.md (outside the set) holds numbers taken with
  InlineCompletion; they predate the native completion there.

## The choice (round 0, by measurement; superseded by F1/F3: the native completion)

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

At consensus:9f14194 (round 1). Round 0's jobs at 7a9ae86 are history only.

| Criterion | Evidence |
|---|---|
| A1 | 261125-022317-c35d52 (slow-sync on the native completion); fails without: 261125-022319-78e2a5 (mutant `sync-on-loop`, consensus:6680278 on 9f14194: InlineCompletion on the loop, main's shape). Local at the head: held 6,000 ms, every loop turned within 1 ms of 1,001 allowed, 0 peers lost, leader kept term 1, 20 commits within 2 s each (slowest 320 ms) |
| A2 | 261125-022321-d39023 (ctest groups_test, driver*, blocking_entry, completion_asio); 261125-022338-e1ff83 (TSan: driver, joiner, relayout, fetch-alongside, install, slow-sync); fails without: 261125-022340-33344a (TSan at 7a9ae86, SyncThread). Local: driver (recover pin 2 == 2 for both groups, slow-sync again) and fetch-alongside pass; TSan six cases, 0 reports. No library file changed |
| A3 | [artifact] docs/DRIVER.md "Where syncs run" (consensus:9f14194): native and InlineCompletion, where a sync runs and whether the loop turns; a Driver on a link loop syncs off the loop on the native completion (detentd, groups_test, bench/netlab); what an inline sync there costs. docs/mechanisms/host-transport.md Placement points to it |
| A5 | 261125-022322-8743dc, 261125-022324-925f03, 261125-022326-2ffaad, 261125-022327-3106af (strict, 25 runs each beside 4 burners, native completion). Local: 16 of 16 exact, loops turned within 3 ms, longest journal operation 94 ms |
| A6 | 261125-022329-f14798 (reader) |
| A4 | `ci:ci` (CI run 45594592100 at 9f14194) |

## Result

groups_test's and bench/netlab's journals run on the native completion
(`AsioCompletionDriver<NativeCompletionQueue>`), as detentd's do: a sync runs off the loop and
completes on it, so a 6 s sync on a follower whose links share the group's loop no longer stops
its heartbeats (slow-sync). groups_test wraps the queue (`TimedNative`'s SlowQueue) to hold a
reaped completion `delay` ms, the slot held and the loop turning. docs/DRIVER.md "Where syncs
run" states the rule. No production host pairs InlineCompletion with HostTransport (review Q1).

Lead's strata constraint (state(id)->last right after recover returns is the recovered log
end): the driver case records `state(g)->last` in recover's completion, on the loop, and asserts
it equals the log end the node stopped at; on the native completion, 2 == 2 for both groups.

## Rulings I made

- Round 1 (lead's ruling on F1/F3): one completion shape, the native one; no test-only
  completion. The slow sync is a queue wrapper (held reaped completion), not a sleep: the
  operation stays in flight and holds the driver's slot, as a slow F_FULLFSYNC does.
- A1's test runs on the native completion (the quest text says InlineCompletion; the lead ruled
  the native one); its fails-without is the mutant with InlineCompletion on the loop.
- F1/F2's TSan jobs carry `--for A2` (job criteria take A ids only); they are F1's test.
- F2: the quiet commits' 2 s bound stays (D152); a miss prints its diagnosis first.
- The transport does not refuse the pairing: it cannot see a Driver's completion (F4, ruled).
- fetch-alongside: the term allowance is a process stall past 1,000 ms only (`stalled`).
- Reused groups_test's LoopLags and Stalls (no new probe); Lag (lag.h) in slow-sync and reader.
- Session: a subagent of the lead, not a session of my own, so no `claude -n` title.
