# transport-connection: journal

State: working
Quest: .campaign/detent/quests/transport-connection.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Commit 1: `transport/deadline.h` and its test.

## Next

Plan (D106; consensus-transport journal Detent:417daa75 row 1). Code worktree
`consensus-wt/detent/transport-connection`; each commit lands a complete behavior with its tests.

1. `transport/deadline.h` (A1): `late_deadline` on the loop's wheel (`scheduler_ref::arm`): a
   token requested at the final expiry; `wait()` resumed by `wake()`, expiry or the parent token.
   A fire later than one wheel tick extends once by the lateness, capped at one timeout; the
   object owns its entry (F10); `wake()` cancels an armed extension, so a wake in the batch after
   a late fire completes at once (F11, F17). `deadline_test` on `inline_scheduler` with injected
   stops; mutants: no extension, uncapped, twice, wake-ignores-extension, entry left armed.
2. `transport/node_hello.h` (A3): DTHL0002, 64 bytes, its decoder refusing each field;
   `negotiate_rpc` (T1); `hello_test` with values; `fuzz_decoders` case 9 and seeds.
3. `transport/connection.h` (A2, A4): `NodePins` (one certificate per node, pinned once per host),
   the `task` connection (OpenSSL on a non-blocking socket, `SSL_MODE_RELEASE_BUFFERS`, readiness
   by `await_asio` on `async_wait`), length-prefixed frames, a keepalive per connection (a
   zero-length frame) and a dead-peer deadline through (1), graceful close (flush, then
   `close_notify`); `connection_test` over loopback with the TLS fixture.
4. `transport/links.h` (A1, A4): dial (lookup and connect through (1)) and accept, with the
   duplicate-dial tie-break before any frame; `links_test`, the tie-break mutant, TSan.
5. `docs/mechanisms/host-connection.md`, CHANGELOG; jobs: suite, TSan, fuzz, mutants.

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
