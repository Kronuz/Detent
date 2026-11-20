# reactor-shared-accept-bind-flake: journal

State: waiting on jobs 261120-032337-0144b3 261120-032338-5d9280 261120-032344-c78468 (all queued behind an exclusive hold)
Quest: .campaign/detent/quests/reactor-shared-accept-bind-flake.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Fix committed and pushed (reactor:c8930ed): test/test.cc starts every server on port 0 and reads
service.port() (`listen_any`), replacing `free_port()` (bind 127.0.0.1:0, close, server binds later).
The old shape leaves a window (the server binds after spawning its reactor threads) in which any other
process binding that port makes start() throw EADDRINUSE; uncaught, it ends the test.

Probes on this host (scratch: ~/.scratch/reactor/rsab/*.c) ruled out kernel-side causes: bind(127.0.0.1:0)
never returned a port held by 12000 wildcard listeners, 8000 LAN-address listeners, 8000 v6 listeners or
8000 established LAN connections, and ESTABLISHED/TIME_WAIT sockets never block a wildcard SO_REUSEADDR
bind. Ephemeral ports are handed out ascending. So the only way free_port returns a port the server
cannot bind is a third party binding it inside the window. CI-side collider not observable here.

Repro job: job.sh runs the test under a neighbor (`sweep`) that binds/releases ports across the range, with
a DYLD interposer printing the failing bind's port, errno and netstat/lsof state.

## Next

1. Read mutant job 261120-032337-0144b3 (base commit, expected to fail with BIND FAILED output): A1 and A2 mutant.
2. Read 261120-032338-5d9280 (fix, 200 runs, expected 0 failures): A2. If the mutant does not fail, raise the sweeper ring (arg of sweep) or iterations.
3. Suite 261120-032344-c78468 (A3); CI on the pushed branch (A3).

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

- Fix in the test (no reactor defect found: acceptor retirement is not involved; every failing bind is a fresh port).
- Replaced all 15 free_port() uses in test.cc, not only the failing case: same race everywhere.

<Decisions inside the quest's scope, so the reviewer can check them.>
