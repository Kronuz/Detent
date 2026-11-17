# transport-connection: async-drivers 5a: the task connection, one late-deadline rule, node pin, hello DTHL0002, dial tie-break, keepalive

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status transport-connection`.

## Outcome

consensus gains the connection the host transport is built on, additive beside `Peers`: a `task` connection doing OpenSSL on a non-blocking socket (D10: `SSL_set_fd`, `SSL_MODE_RELEASE_BUFFERS`), the node certificate pinned once per host (Q7 (a)), the DTHL0002 hello, dial and accept with the duplicate-dial tie-break, a keepalive per connection, a graceful close, and `transport/deadline.h`: the one late-deadline rule every transport wait uses.

## Acceptance criteria

- A1 One late-deadline rule in one place (`transport/deadline.h`, D102 from consensus-fetch-macos): a deadline that fires late after a process stop extends once by the lateness, capped at one timeout; the wait owns its timer (F10); every wake (cancel, resolve, reply end, close) is seen by a wait whose timer already fired (F11, F17); dial and lookup use it; unit tests on `inline_scheduler` with injected stops, and a mutant per rule killed; stop-injecting scenarios carry the ctest label `timing`, off CI (D100)
- A2 Node authentication (Q7 (a)): one certificate per node pinned once per host; a peer presenting another node's certificate, an unpinned one or none is refused before any frame: tests with values, reviewed by the security lens
- A3 The hello is DTHL0002 carrying rpc_min and rpc_max (T1), no DTHL0001 reader kept; the decoder refuses each malformed field: decoder tests with values, and a fuzz job over the hello decoder
- A4 Duplicate dials resolve to one connection before any frame, and a draining connection doesn't count in the tie-break (T6, R3); a keepalive once per connection, lane-only connections included (T4, A5's keepalive); a graceful close sends the flushed frames, then close_notify (T8's transport half): tests, a mutant for the tie-break killed, the connection tests under TSan

## Contracts

Provides `consensus:transport`, additive beside `Peers`; consumes consensus-driver's DRIVER.md (consensus:57745e8).

## Decisions

Split from consensus-transport by D106 (the contributor's plan, journal Detent:417daa75 B1). D96, D97, D102.

## May land early

Tests and internal code behind them; nothing on the wire before the review.
