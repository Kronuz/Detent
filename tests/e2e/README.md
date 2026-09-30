# End-to-end harness

Every real bug found in the Raft integration (bootstrap-latency window,
dead-follower liveness, a correlation-id collision across restarts) was
invisible to `ctest`'s `test_raft_cluster` and only surfaced by running
real `detentd` processes by hand: real OS processes, real signals, real
timing, real network sockets. The in-process C++ suite constructs
`Distributor` objects directly in one binary, with one hardcoded fast
`RaftConfig`, and only ever stops a node via a clean destructor call -- it
structurally cannot exercise a CLI invocation, a `SIGKILL`, or
production-scale timing.

This harness exists to close exactly that gap. It is not a replacement for
`test_raft_cluster`: that suite is fast, runs on every build, and is the
right place for logic that doesn't depend on being a real process. This
harness is slower and spawns real subprocesses; run it after any change to
`src/detentd/` or `cluster`'s Raft wiring, and before a release.

## What it checks

Every scenario asserts one or both of the two properties a consensus
system actually has to uphold:

- **Safety**: every currently-running node's `GET /v1/snapshot` agrees --
  no two nodes ever disagree about what a given version's content is
  (no split-brain / fork).
- **Liveness**: given a live majority, a write eventually succeeds within
  a bounded time.

## Running it

```sh
cmake --build build --target detentd
python3 tests/e2e/detent_e2e.py                      # fast scenarios only
python3 tests/e2e/detent_e2e.py --production-timing   # + real Raft timing (slow)
python3 tests/e2e/detent_e2e.py --chaos-rounds 30      # + randomized fault injection
python3 tests/e2e/detent_e2e.py --scenario dead_follower_does_not_block_writes
```

No dependencies beyond the Python 3 standard library and a built
`detentd` binary (found via `--binary`, or searched for under `build*/`).

## Scenarios

| Scenario | What it would have caught |
|---|---|
| `fresh_bootstrap_fast` | Basic election sanity, fast timing |
| `fresh_bootstrap_production_timing` | The bootstrap-latency bug (3x election_max applied to a first-ever launch) |
| `put_to_follower_forwards` | Forwarding/replication basics |
| `leader_failover` | Re-election keeps writes working |
| `dead_follower_does_not_block_writes` | The `is_alive()` liveness bug (a permanently-dead follower freezing all future commits) |
| `kill_mid_request` | No false-positive acknowledgment when a node dies mid-write |
| `restart_is_not_immediately_reelected` | Restart-safety window, via a real process restart |
| `chaos` | Randomized combinations of the above, run repeatedly -- found the correlation-id collision documented below |

## Correlation-id collisions across restarts

`chaos` found this in its own early runs: a restarted node's
`command_counter_` resets to 0, so a write it originates right after
restarting can regenerate the exact id an earlier (pre-crash) write used.
That old entry is often still replaying from the Raft log during the
node's catch-up, and a matching id lets it steal the new write's pending
HTTP request, handing it a stale result while the real write commits
unseen. Fixed in `detentd.cc`/`.hh` by folding a random per-process
`session_id_` into every correlation id; see `test_raft_cluster.cc`'s
"a node's own write right after its restart..." test for the fast,
deterministic regression coverage.

## Adding a scenario

Write a function `scenario_<name>(cluster, harness)`, register it in
`SCENARIOS`, and use `harness.assert_safety(cluster)` /
`harness.assert_eventually_writable(cluster)` rather than hand-rolling
checks -- new scenarios should compose the same two properties, not invent
a third kind of check.
