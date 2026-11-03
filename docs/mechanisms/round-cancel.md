> Status: CURRENT (2026-11-03) how the delivery coordinator ends a network call in flight with a `task::cancel_source` it renews each round, read from `src/detent_delivery/coordinator.cc` at Detent:2fc6269.

# The coordinator's per-round cancel source

The coordinator's candidate loop makes network calls that can wait a long time: open a session,
wait for the next event of a subscription, receive an update. `close()` and `cancel_update()`
(both owner-side) must end the call in flight at once. A request must also end with that round: the
next round's calls start clean.

## Mechanism

`Coordinator::Impl` holds `task::cancel_source network_cancel` (`coordinator.cc:98`).

- Each pass of `candidate_loop` (`while (!closing)`, `:811`) starts with
  `candidate_cancellation = cancellation; network_cancel = task::cancel_source{};` (`:814-815`).
  The assignment drops the previous source and installs a fresh, unrequested one.
- The round's network calls take `network_cancel.token()` as an argument: `open_session`
  (`:846`), `next_session` (`:866`), `receive` (`:905`). Each is a `task<T>` in
  `detent_delivery/client.hh`. `perform` (`client.cc:285`) links a `task::deadline` for the call
  to that token (`:291`), and every Asio operation of the exchange takes the deadline's token,
  through `task::await_asio` or `transport::await_io` (the latter throws the error). A requested
  token never starts an operation, and a request while one is pending cancels it through its
  cancellation slot (task's `docs/mechanisms/await-asio.md`): the call ends with
  `operation_aborted`.
- `close()` (`:690`) and `cancel_update()` (`:1207`) each increment `cancellation` and call
  `network_cancel.request()`, then wake the candidate and disk waits. The round's checkpoints use
  `cancelled()` (`closing || candidate_cancellation != cancellation`, `:124`) and throw
  `operation_aborted` (`:826`).

```cpp
while (!closing) {
    candidate_cancellation = cancellation;
    network_cancel = task::cancel_source{};        // this round's token source
    ...
    session = co_await open_session(request, force_snapshot, network_cancel.token());
}
```

## Why a fresh source

A `cancel_source` is first-wins and permanent: once requested, every token from it is requested
for good. One source for the coordinator's life would end every call after the first
`cancel_update()`. Replacing it each round gives `cancel_update()` the meaning "end the update in
progress": it ends the calls holding the old token (each holds its own copy, so the old state
lives until they finish), and the next round holds a new token.

`close()` is permanent anyway: it sets `closing`, which the loop tests before it renews the
source, so no round starts after it.

## Ordering

The renewal runs on the owner between suspension points, right after the `closing` test, so a
`close()` cannot fall between the test and the renewal. `close()` and `cancel_update()` must run
on the owner (`coordinator.hh:70`): a cross-thread `request()` could race the assignment. A
`cancel_update()` that arrives between rounds hits the spent source and the counter before the
next round reads it, so it does not reach the next round; this is the intended meaning.

## What the token does not cover

`close_session` in `release_session` (`:770`) takes no token; its timeout is capped at 1000 ms
(`:771`). `when_all(disk_loop, candidate_loop)` ends when both loops see `closing`.

## Invariants

1. A round's network calls take the token of the round's source, never an older one.
2. The source is replaced only on the owner, after the `closing` test, before the first call.
3. `close()` ends the call in flight and no later round starts.
4. `cancel_update()` ends the call in flight and does not end the next round's calls.

## Tests

`tests/detent_delivery/test_coordinator.cc` through ctest `test_delivery_coordinator_<mode>`:

- `cancel-selected` and `sink-cancel-selected`: `cancel_update()` after the selection boundary
  (`:245-251`) must leave the update unacknowledged at once, and the coordinator then goes on to
  acknowledge one (4).
- `observe` (`:316-326`): after `close()`, the coordinator drains within 10 s with a network call
  in flight, rather than waiting out the call's own poll (3).
- The `test_relay_network_*` modes (`--subscription-relay-network-*`), including
  `test_relay_network_failure_drainage`, drain through `close()` with calls in flight (3).
