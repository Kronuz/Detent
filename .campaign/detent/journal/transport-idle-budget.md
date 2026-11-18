# transport-idle-budget: journal

State: waiting on jobs 261118-073615-da90ae 261118-074432-acdca6
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
Probe 261118-073615-da90ae (at main 5923b6c): Release vs no build type, Ninja, fresh, x3 each.
Candidate fix consensus:d2eeb0d (bench only): draw RAND_bytes/RAND_priv_bytes on every loop after
open, before s1, so each loop's thread-local DRBGs count in the host intercept. Probe
261118-074432-acdca6: same A/B at d2eeb0d, printing per-end OpenSSL/C++ at L=1 vs L=4.

## Next

1. Read 261118-073615-da90ae (does build type flip it?) and 261118-074432-acdca6 (does the
   DRBG warm-up bring L=4 per-end OpenSSL back to L=1's, ~45.1 KB?). If C++ per end still sits
   ~500 B higher at L=4, that is Asio's per-thread recycler; decide whether to warm it too.
2. A2 mutant: perl-insert `[[maybe_unused]] char mutant_pad_[4096] = {};` after `class Connection {`
   in include/consensus/transport/connection.h, run transport_idle, `--fails-without --for A2`.
3. A3: `campaign verify transport-idle-budget --for A3`, push, CI.

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

- Docs go under docs/perf/transport-idle* only (lead, mid-task).
- Per-thread OpenSSL state is a per-loop host cost, counted like the timer wheels already are:
  in the host, not per connection end.
