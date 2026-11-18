# transport-idle-budget: journal

State: waiting on jobs 261118-073615-da90ae
Quest: .campaign/detent/quests/transport-idle-budget.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

A1 diagnosis. The fit's loops=4 slope is 49156-49160 B against a 49152 B (48 KB) cap: a few
bytes over, so anything that moves per-end bytes by >0.01% flips it. Spec diff f0eff6 (pass) vs
1fb596 (fail): pass built `-DCMAKE_BUILD_TYPE=Release` (Makefiles, slot build); fail built with no
build type (Ninja, fresh `build-idle`). Both compiled against /usr/local/include/openssl (4.1).
Connections live on loop_of_node(peer), so at L=4 the loops touched grow with P (1, 2, up to 4):
per-thread state (OpenSSL thread-locals, Asio's per-thread recycler) lands in the slope.
Probe 261118-073615-da90ae: Release vs no build type, Ninja, fresh, x3 each.

## Next

1. Read 261118-073615-da90ae; if build type flips it, A1 is named. Then fix the fit: warm every
   loop's per-thread state before `s1`, so it counts in the intercept, not per connection end.

## Blocked on

Nothing.

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
