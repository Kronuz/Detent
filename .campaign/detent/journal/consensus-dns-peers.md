# consensus-dns-peers: journal

State: working
Quest: .campaign/detent/quests/consensus-dns-peers.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Implementation written (consensus worktree): `include/consensus/transport/resolve.h` (PeerAddress,
PeerResolver, system_resolver, interleave, Lookup thread handoff), `peers.h` (routes, lookup wait,
staggered dial race, dial pins only the dialed peer), `test/transport/peers_test.cc` (3 Workers over
TLS, scenarios per criterion), `test/transport/dns_mutants.sh`, `docs/mechanisms/peer-dns.md`.
Building and running the test locally.

## Next

1. Green locally; commit; run `campaign verify` (A1-A6 suite) and the two mutant jobs (A2).
2. CHANGELOG, README line.

## Blocked on

Nothing. msg (lead, A6 added): folded into the design (staggered dial race, whole-set cache).

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
