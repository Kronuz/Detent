# rpc-lane-codec: async-drivers 5e: the rpc lane codec, call table, pools with atomic renew, Drain

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status rpc-lane-codec`.

## Outcome

consensus gains `transport/rpc.h`, the request/response lane's codec and call machinery of Xapiand's consensus-rpc-lane (consensus:0fd8a7f docs/design/rpc-lane.md revision 2, approved at Xapiand:a3b4b4903), over `async_pipe.h` and not yet on Bulk: frames, the call table, a call's life, credit pools and `Drain`.

## Acceptance criteria

- A1 The codec (D2, D3 rules 1-5) and the full decoder validity list: unknown kind, reserved bits, length per kind, direction, error codes, window 0, end iff the total is known, overflow-safe offset+n; `Drain` is kind 6, 24 bytes, carrying the last accepted call id, which has the receiver's parity or is 0: a decoder test per rule with values, a fuzz target with a replayed corpus, a fuzz job
- A2 The call table (rules 6-10) and a call's life (D4): tests with values
- A3 Credit (D5, F2): three thread-safe pools outside Bulk's connection window, small frames FIFO, an atomic `ByteBudget::renew`, and a transfer that moves a call's consumed bytes into its new grant in one step so no other call can starve it between release and acquire: tests under TSan
- A4 The model's eight mutants (Xapiand:a3b4b4903) as unit tests, each killed: mutant jobs

## Contracts

Provides `consensus:rpc-lane` (the codec and call machinery); consensus-transport puts it on Bulk.

## Decisions

Split from consensus-transport by D106 (the contributor's plan, journal Detent:417daa75 B1). D96, D97, D102.

## May land early

Tests and internal code behind them; nothing on the wire before the review.
