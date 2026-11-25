# consensus-inline-sync-loop review: correctness

Commit: consensus:7a9ae865ed05ac18e93d583202d49066007ec6ed
Verdict: CHANGES
Date: 2026-11-25

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

## Evidence I relied on

- 261125-014238-f65c96 (A1, slow-sync at 7a9ae86, Release with `-UNDEBUG`, CMakeLists.txt:370,
  so the asserts are live): node 2's operation held 6,001 ms, every loop turned within 2 ms,
  0 peers lost, leader kept term 1, 27 commits within 2 s each.
- 261125-014240-315596 (fails without, consensus:dfbe708 = 7a9ae86 plus only the `sync-on-loop`
  mutant, checked with `git diff`): held 6,051 ms, loops turned within 6,049 ms of 1,022 allowed,
  2 peers lost, `Assertion failed: (ok)` at groups_test.cc:2700, exit 134. It fails for A1's
  reason (loop held, peers lost). The leader kept its term in the mutant too, so the term half of
  A1 does not discriminate (F7).
- 261125-014320-2d75b5 (A6, reader): longest gap 103 ms of 1,001 allowed (1,000 + 1 ms Lag). The
  base stays 1,000; Lag spans the measured 5 s window, as rpc_host_test does.
- 261125-014250-e5f9bb (A2, ctest groups_*, driver*, blocking_entry, completion_asio): 45 of 45
  passed. The driver case includes the recover pin (asserts live).
- 261125-014254-56d5d1 (A5 shard 1): 0 of 25 failed. 261125-014258-ab633e (A5 shard 2): 1 of 25
  failed (F2).
- CI run 45588135687 at 7a9ae86, job linux-tsan 139114930288: 5 groups_test cases fail with data
  races (F1). linux-asan-ubsan passed. The base 217481b's CI (run 45577085586) succeeded, tsan
  included, so the races are this branch's.

## What I checked myself

Scripts under `.scratch/detent/review-consensus-inline-sync-loop-correctness/` in the home
(`native_patch.py`, `native_probe.sh`; the TSan log `ci-tsan.log`; A5's failed run
`a5-shard2-run13.log`).

- Q1, production pairings of InlineCompletion with HostTransport links (main and every branch of
  Detent, Xapiand, strata; the in-flight worktrees):
  - detentd builds no consensus Driver and no HostTransport; its storage runs on
    `AsioCompletionDriver<NativeCompletionQueue>` (Detent src/detent_authority/service_runtime.cc:182).
  - strata src takes a `completion_ref` (include/strata/group/group.hh:100) and constructs none;
    InlineCompletion appears only under strata/test.
  - Xapiand main uses no consensus. The journal-replaces-wal worktree's `ShardGroups` runs
    InlineCompletion on a `task::blocking_scheduler` thread of its own with no transport
    (src/database/shard_group.h:206-217, shard_group.cc:589-590); the cluster worktree uses
    HostTransport for RPC (src/cluster/rpc.h) but constructs no Driver there. No branch of any
    repository constructs a HostTransport in src.
  - consensus examples/demo.cc uses the core only. bench/netlab/node.cc:172,208-216 pairs them
    (F5). The blocking entry refuses peer calls (`needs_peer`, driver.h:523,926).
  So the claim holds: no production pairing today. The transport cannot refuse it at
  construction: HostTransport takes `HostLoop`s only, and `completion_ref` is type-erased with no
  way to ask whether it completes on the caller (completion seam.h:49-70). See F4.
- The fix's mechanism. `SyncThread::serve` runs `drive_synchronously` on its own thread, and that
  calls `operation.complete()` there (completion operation.h:197-203). Every driver of the seam
  completes on the owner: InlineCompletion on the caller, AsioCompletionDriver in `drive()` on its
  owner scheduler (asio_completion.h:288-293), HeldCompletion on delivery. A journal operation's
  `complete` mutates journal state (`StoreReclaim::complete` calls `finish_reclaim` and
  `credit_durable_reclaim`, journal store.h:280-290) while the loop's Worker reads `done()` and
  steps the journal (worker.h:518). CI's TSan shows exactly that (F1).
- Probe 261125-015153-819352 (fresh checkout of 7a9ae86, patched by `native_patch.py`): groups_test
  with `AsioCompletionDriver<NativeCompletionQueue>` in SyncThread's place, behind a queue wrapper
  that holds the next reaped completion `delay` ms. slow-sync: held 6,022 ms, every loop turned
  within 1 ms, 0 peers lost, term kept, 22 commits within 2 s; the driver case (restart, recover
  pin, quiescent, quiescent fence, slow-sync again) passed; exit 0. The one existing off-loop
  completion fits groups_test's loops (`task::asio_scheduler`, groups_test.cc:121-123).
- Q3 (A5): fetch-alongside places the group on `loop(slot(1), L)` and asserts on nodes 2 and 3 that
  the link to node 1, which serves node 1's fetches, shares it (groups_test.cc:2721, 2769-2770).
  The term rule tightened to `kept || stalled >= 1'000`. Coverage is right; the 25-run gate is not
  met (F2).
- Q4 (strata's constraint): the Node records `driver->state(g)->last` in recover's completion on
  the loop (groups_test.cc:1231-1234); groups open one at a time (`done.wait()`), so the map has
  one writer; `driver()` asserts it equals the log end read before the restart (1468-1478). Live in
  Release, and A2 and my native probe passed it. It cannot tell `last` from `committed` here (F6).

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | yes | defect | SyncThread completes journal operations on its own thread, not on the loop: `StoreReclaim::complete` (journal store.h:288) writes journal and admission state on the sync thread while the loop's Worker reads `done()` (worker.h:518) and steps the same journal. A data race in every groups_test case that reclaims. | CI 45588135687 linux-tsan (job 139114930288): driver, joiner, relayout, fetch-alongside-consensus, install fail, each "data race ... store.h:288/292 StoreReclaim"; the base 217481b's CI run 45577085586 succeeded, tsan included. | Use the native completion (`AsioCompletionDriver<NativeCompletionQueue>`), which completes on the owner; probe 261125-015153-819352 shows slow-sync and driver pass with it. Test: CI linux-tsan green on those five groups_test cases at the fixed commit, and A1's slow-sync plus its fails-without rerun there. |
| F2 | yes | gap | A5's gate fails: 1 of 25 runs aborted at groups_test.cc:2802 (a "quiet" commit on all three within 2 s, before any fetch) and the case prints nothing about why. 0 of 200 failed in the 8 earlier 25-run jobs with the group on a link-free loop. Not root-caused; F1's race (UB in Release) is a candidate. | 261125-014258-ab633e; run 13's log saved as `a5-shard2-run13.log`. | After F1, rerun A5's two strict 25-run jobs. If a quiet commit still misses, print which node lacks it and the host's Lag before asserting, and root-cause from that. |
| F3 | yes | defect | A second off-loop completion where one exists: SyncThread duplicates the native completion (one worker per loop, FIFO by whole operation, no Turn-before-Bulk priority), and docs/DRIVER.md presents a test-file class as a host option ("natively or on a sync thread", DRIVER.md:358, 364) that no host can use. groups_test, the one Driver-over-HostTransport suite, then exercises a completion production never runs. | groups_test.cc:1072-1145; DRIVER.md:355-367; probe 261125-015153-819352 (native fits). | The F1 fix removes SyncThread; DRIVER.md's table keeps native and InlineCompletion, and the rule reads "syncs off the loop: the native completion". The 13 µs and 0 µs-idle numbers then describe nothing shipped; drop them or measure native (its 1 ms bounded poll runs while an operation is out, asio_completion.h:310-312). |
| F4 | no | gap | The quest's fallback (the transport refuses the pairing at construction) is not done and cannot be done in the transport: it never sees the completion. Today no production host pairs them (Q1), so it is documented only; Xapiand's `ShardGroups` (InlineCompletion on its own blocking thread) is the likeliest place for it to come back once its groups get peers. | seam.h:49-70; host_transport.h:398; Xapiand journal-replaces-wal shard_group.h:206-217. | Owner's call. Options: (a) keep the documented rule as it is; (b) `completion_ref` says whether it completes on the caller and the Driver refuses peer traffic over it when its scheduler is a reactor (completion and driver.h, a follow-up quest); (c) a check in Xapiand's cluster work when it gives shard groups peers. |
| F5 | no | gap | bench/netlab/node.cc pairs InlineCompletion with HostTransport on its one loop (172, 208-216): the pairing DRIVER.md now says not to make. Its commit-latency, throughput and election numbers include syncs holding the node's links. | bench/netlab/node.cc:172-216. | A follow-up quest: the native completion there, as detentd and the F1 fix use. |
| F6 | no | nit | The recover pin compares `state().last` to the log end with nothing uncommitted, so it cannot tell `last` from `committed`. The tail cases are already pinned on InlineCompletion in driver_test.cc:894-952. | groups_test.cc:1468-1478. | None required. |
| F7 | no | nit | A1's "leader keeps its term" holds in the mutant too: the fails-without is carried by peers lost and the loop-turn bound, not the term. | 261125-014240-315596 ("node 1 keeps term 1 ... FAILED"). | None required; A1's other two checks discriminate. |

## Not run

- TSan locally (CI's); the native probe ran Release on macOS x86_64 only, slow-sync and driver
  cases only, once each; not fetch-alongside 25x on the native completion.
- No rate measurement of F2's quiet-commit miss on the old shape beyond the existing 200 runs.
- CI's other platforms at 7a9ae86 were still running when I wrote this; only linux-tsan (failed)
  and linux-asan-ubsan (passed) had finished.
- No probe of FETCH_ALONGSIDE_SLOW_SYNC or the slow-read knob.

## Round 2

Commit: consensus:9f14194
Verdict: APPROVE
Date: 2026-11-25

### Evidence I relied on

- CI 45594592100 at 9f14194: linux-tsan, linux-asan-ubsan, freebsd, macos-arm64, linux gcc/clang
  and arm64 green. macos-x86_64 failed in `consensus_transport_connection` (connection_test.cc:988,
  `coarse_writer`): a file and a library this branch does not touch (`git diff 217481b 9f14194 --
  include/ test/transport/connection_test.cc` is empty), and the same assertion is in job
  261118-221115-3049dd, a week before this quest. A4 needs that job rerun, not a fix (R2-N1).
- 261125-022338-e1ff83 (TSan, CI's flags, at 9f14194): driver, joiner, relayout,
  fetch-alongside-consensus, install, slow-sync: 0 reports each. Its fails-without
  261125-022340-33344a at 7a9ae86: fetch-alongside and driver each abort on the store.h:288
  `StoreReclaim::complete` race. F1 is closed with a fails-without.
- 261125-022317-c35d52 (A1 at 9f14194): held 6,000 ms, loops turned within 1 ms, 0 peers lost.
  Fails-without 261125-022319-78e2a5 (consensus:6680278 = 9f14194 plus only the mutant, checked):
  InlineCompletion on the loop, loops held 6,059 ms, 2 peers lost, assert at groups_test.cc:2665.
- 261125-022321-d39023 (A2): 45 of 45. 261125-022329-f14798 (A6): 103 ms of 1,002 allowed.
- A5 at 9f14194: 261125-022322-8743dc, -022324-925f03, -022326-2ffaad, -022327-3106af: 0 of 100.

### What I checked myself

- The whole mechanism: `TimedNative` (groups_test.cc:1076-1112) is `AsioCompletionDriver` over a
  `SlowQueue` that holds a reaped completion `delay` ms. The queue is touched only from its
  owner loop (the driver polls it in `drive()`); completion reaches the operation on the owner
  (asio_completion.h:288-293), so F1's off-loop `complete` is gone. A delay of 0 releases at
  once. Shutdown: each Driver and turn driver ends before its `PerLoop`, which is destroyed after
  the loops join and before their `io_context`. The mutant is main's shape (inline on the loop).
  DRIVER.md now lists only native and InlineCompletion (F3 closed). It is the shape of my
  round-1 probe 261125-015153-819352.
- F2, ruled under V5. The 2 s quiet-commit miss was never reproduced, and TSan at 7a9ae86 showed
  its first race after the quiet phase, so the race-to-miss path is inferred, not observed. A run
  can still settle it. I added 8 strict shards of 25 at 9f14194 (review-r2 jobs
  261125-024338-2c7f43, -024340-597df1, -024342-624cab, -024345-f99ee8, -024348-74cced,
  -024351-3baa63, -024353-d3d818, -024356-2d9a14; script `repro.sh`, a copy of the contributor's):
  0 of 200. With the contributor's 100 that makes 0 of 300 at the fix, against 1 of 50 at 7a9ae86.
  If the old rate had held (1 in 50), 0 of 300 has probability 0.98^300 = 0.2%. So the miss
  belonged to 7a9ae86's SyncThread, whose one proven defect (F1) is fixed with a fails-without.
  A recurrence now prints each node's state, loop turns and journal times (groups_test.cc:2768-2799)
  instead of a bare assert. F2 is closed.
- F5: netlab is on the native completion (`completion` is declared after `scheduler`, and is
  released on the loop before it stops). netlab is not in CI and I did not build or run it;
  the contributor's report of a pre-existing node-pins failure on macOS is unverified (R2-N2).

### Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | no | defect | Closed: syncs complete on the owner loop; TSan clean on all six cases, the race reproduced at 7a9ae86. | 261125-022338-e1ff83, 261125-022340-33344a, CI linux-tsan at 9f14194. | Done. |
| F2 | no | gap | Closed: 0 of 300 strict runs at 9f14194 against 1 of 50 at 7a9ae86 (0.2% if unchanged); a miss is now self-diagnosing. The exact path from race to miss was not observed. | The 4 contributor and 8 review shards above. | Done; a recurrence's printed state reopens it. |
| F3 | no | defect | Closed: SyncThread deleted; one off-loop completion, the native one. | groups_test.cc:1076-1112; DRIVER.md:355-367. | Done. |
| F4 | no | gap | Unchanged: refusing the pairing needs a seam change outside this quest; owner's call (round 1's options). | seam.h:49-70. | Owner's call. |
| R2-N1 | no | gap | macos-x86_64 CI failed in connection_test's `coarse_writer`, untouched by this branch and seen before (261118-221115-3049dd). A4 is not green until that job reruns green. | CI 45594592100, job 139137463833. | Rerun the job; the flake is its own quest if it repeats. |
| R2-N2 | no | gap | netlab's native-completion change is neither built nor run by any job, Linux (its target) included. | bench/netlab/node.cc:174-176, 306. | A follow-up quest with netlab's node-pins startup failure. |

### Not run

- netlab, on any platform. Local TSan beyond the contributor's macOS job (CI's linux-tsan stands
  for Linux). FETCH_ALONGSIDE_SLOW_SYNC on the native completion.
