# relay-delta-shutdown-hang: journal

State: waiting on jobs 261111-165851-1889f8 261111-165916-d794c9
Quest: .campaign/detent/quests/relay-delta-shutdown-hang.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Fix and tests committed (9c8cc070); waiting on the mutant (A2) and the A1 stack job at the diagnostic commit
(07be9bf0). Then paste `campaign brief`'s table and set State to ready for review.

## Next

1. When both jobs pass: `campaign brief`, paste the Evidence table, write the Result, set ready for review, push.

## Blocked on

scripted_upstream.hh is outside the quest's file set, and the fix lives there (the hang is a lifetime bug of
`run_phase`, not product code): I changed it (9c8cc070: drops the options copy and the coordinator before the
bundle; adds `Server::delta_delay_ms` for the receiving case). Lead: accept it into the set, or tell me to move
the change elsewhere.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| A1 | pending |

## Result

Cause: the hang is in the test harness, not in the relay. `run_phase` (scripted_upstream.hh) waits on
`cache_domain->close()` while it still holds the relay `Bundle`: the `CoordinatorOptions` copy in `config`, the
coordinator's own copy and the `CountingSink` all share it (use count 3, DIAG in 261111-165134-1c6ce4 and
261111-165916-d794c9). A bundle that took a delta keeps the delta edge in `UpdateCache::edges_` after
`fence()` (fence drops only `snapshot_`; edges go through `release_stale`, run by the relay host's turn), so
that edge is a resident of the cache domain until the bundle is destroyed. A snapshot-only run has no edge,
which is why the other relay probes drain. The stack (261111-155630-eb5f86): main thread in
`RetirementDrain::wait` from `run_phase`, every other thread idle.
The relay host is not affected: `run_host_owned` resets the coordinator, then the bundle, before it closes the
cache domains, and moves the sink into the coordinator without a copy.
Fix: `run_phase` drops the options' sink copy after `make_coordinator` and the coordinator before the sink and
the bundle. Tests: `test_delivery_relay_delta_shutdown_{published,receiving,building,publishing}` (1-2 s each,
261111-165634-dbace0), the close landing in the phase the test waits for (it fails when the phase is not seen).

## Rulings I made

- The probe was already on main (namespace-s4 landed it); no restore needed.
- No product change: the relay host's teardown order is already right, and a coordinator change would not release the harness's copies.
- The unreleased-edge-after-fence is left as is (the host's cleanup turn releases it); fence could drop the edges too, which would make a leaked bundle harmless. Not done: outside the file set and not needed.
- Closing in `receiving` uses a 1 s server-side hold of the delta reply, so the close is deterministic; the other phases poll once per coordinator turn.
