# transport-connection: journal

State: waiting on jobs 261117-012645-510cf4 261117-012648-223239 261117-012653-af6f61 261117-012657-190319 261117-012701-805798 261117-012705-7fdd18 261117-012148-792d04
Quest: .campaign/detent/quests/transport-connection.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Built at consensus `35ebaba` (8 commits on main `57745e8`): `deadline.h`, `node_hello.h`,
`node_pins.h`, `connection.h`, `links.h`, their tests, `docs/mechanisms/host-connection.md`,
CHANGELOG. At `c1270f0`: suite and the four test jobs passed, all 15 mutants killed (exit 8),
TSan failed on a test bug (the lookup case's abandoned resolver thread wrote a counter on a
dead stack; fixed at `35ebaba`). `575671e` makes `~Links` abandon a refreshing lookup.

## Next

1. Read the jobs at `35ebaba` (suite, four tests, TSan) and the fuzz job; fix what fails.
2. Push for CI (the lead's), then "ready for review" (security lens).

## Blocked on

Nothing.

## Evidence

At `35ebaba`: suite 261117-012645-510cf4; TSan 261117-012705-7fdd18. At `c1270f0`: suite
261117-011918-3232f8 passed. Mutants are commits on no branch from
`test/transport/mutant_commit.py c1270f01d233 NAME`; each job is `--fails-without`, all exit 8.

| Criterion | Jobs |
|---|---|
| A1 | `deadline` 261117-012648-223239; `links` (lookup) 261117-012701-805798; mutants no-extension 261117-012000-205e0e, uncapped 261117-012004-35b740, twice 261117-012009-0d6a0f, wake-ignores-extension 261117-012013-dbd982, entry-left-armed 261117-012021-795811, lookup-no-wake 261117-012118-209628 |
| A2 | `connection` 261117-012653-af6f61; mutants pin-any-node 261117-012027-32b241, pin-self 261117-012033-e1cceb, pin-unpinned 261117-012039-9c2239 |
| A3 | `node_hello` 261117-012657-190319; fuzz (FUZZ_DECODER=9, 20 min, at `c1270f0`; the decoder is unchanged since) 261117-012148-792d04 |
| A4 | `links` 261117-012701-805798, `connection` 261117-012653-af6f61; TSan 261117-012705-7fdd18; mutants close-drops-queue 261117-012045-b8df9d, no-close-notify 261117-012052-5bfa60, no-keepalive 261117-012059-910de1, refuse-when-offered 261117-012105-586789, accept-when-offered 261117-012109-4f9a7a, draining-counts 261117-012114-7d6e0c |

## Result

<When ready for review: what changed, its measured effect with numbers and the jobs behind them,
and what it leaves open. Results and how it works go in the docs of the repository it changed
(docs/perf/, docs/mechanisms/, a library's own); name them here. `campaign map` shows this.>

## Rulings I made

- "No DTHL0001 reader kept" applies to the new connection: `node_hello.h` reads only DTHL0002.
  `Peers` keeps its DTHL0001 (`hello.h`) until worker-pump-removal deletes `Peers` (D106).
- DTHL0002 is 64 bytes: node, lane, flags (bit 0 `refused`, the acceptor's tie-break answer),
  cluster, connection id, rpc_min, rpc_max (1..9,999, the lane magic's four digits). Group,
  epoch and profile leave the hello for the group-open exchange (transport-identity).
- Late means more than one wheel tick (1 ms) past the deadline: the wheel's own granularity is
  not a stop, so on-time fires never re-arm.
- The keepalive is a zero-length frame; the dead-peer deadline (`dead_after`, 5 s) restarts on
  every read wait, and a write stuck for `dead_after` fails the connection.
- Tie-break: a dial counts once its hello is sent; the lower node's dial wins. A dial that
  finds a link established before its hello gives up quietly (asymmetric reachability cannot
  make an unsent dial refuse a working one).
- Wall-clock tests assert only bounds a runner's process stop cannot break (never early; open
  under keepalives) and print the timings, so no new entry needs the `timing` label (D100).
  `deadline_test` injects stops on `inline_scheduler`'s manual clock, with no wall clock.
- `consensus_transport` links `task::asio` when the target exists (the connection awaits Asio).
- `~Links` abandons in-flight lookups (`575671e`): a cached name's refresh runs with no task, so
  the drained assertion alone does not stop its thread posting later. No test: it needs an
  io_context destroyed under a live resolver thread, which is not deterministic.
