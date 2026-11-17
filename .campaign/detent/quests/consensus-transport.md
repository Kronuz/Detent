# consensus-transport: async-drivers 5: host transport, Control and Bulk per node pair, fetch lane, node identity (D97 Q7)

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status consensus-transport`.

## Outcome

consensus gains the host transport of `docs/design/async-drivers.md` (Detent) Build plan row 5 (Track C D16, D10) beside today's `Peers`: one Control and one Bulk connection per peer node, each on one loop, frames batched by destination loop with per-group credit and control first; node identity is one certificate per node pinned once per host with per-group checks in the group-open exchange (Q7 (a)); the fetch lane (consensus:a197efb) moves onto the shared connections.

## Acceptance criteria

- A1 D16's tests: frames batched by destination loop and posted once per read, per-group credit, control before bulk; two hosts of 4 loops each under TSan; a group with full admission never delays another group's heartbeat (`reader_test`): tests
- A2 A maximal upload (chunk commands at the 1 MiB command and 2 MiB RPC caps, types.h) on the shared connection never delays any group's heartbeat or vote past its election timeout, and a fetch shares the bulk budget without delaying consensus frames (Xapiand F5): a test, plus a mutant putting bulk ahead of control, killed
- A3 Identity (Q7 (a)): node authentication, the per-frame rule (a group the host stores, a destination incarnation it holds, the source node matching the connection's node where membership names it, R7), the hello's checks in the group-open exchange; a joiner installs and catches up as a learner over shared connections (`frame_admission`, its voter-only control failing): tests, reviewed by the security lens
- A4 Idle cost: per idle group at most 64 bytes and 0 descriptors, frames or timers; an idle connection end within 48 KB; per host `29 KB + 2P x 41 KB` within 10%; the hop's CPU per frame and per idle wake measured on the Linux gate host: idle-cost and measurement jobs
- A5 A host restarted with another L serves every group from its new loop; keepalives once per connection: tests
- A6 Process stops (D102, from consensus-fetch-macos): every transport deadline (lane, fetch reply, lookup, dial) that fires late after a process stop follows one rule in one place (extend once by the lateness, capped at one timeout), the wait owns its timer (F10), and every wake (cancel, resolve, reply end, close) is seen by a wait whose timer already fired (F11, F17); fetch-alongside-consensus passes on macOS x86_64 CI three runs in a row with its skip in `peers_test.cc` removed, and a placed SIGSTOP inside a drain proves a late reply drains over the same lanes (F18: a never-drain mutant killed); timing scenarios that inject or measure stops carry the ctest label `timing`, off CI (D100)
- A7 The request/response lane serves Xapiand's consensus-rpc-lane (its design, consensus:dbf559b docs/design/rpc-lane.md, table at line 205): (T1) the hello carries rpc_min and rpc_max (DTHL0002; no DTHL0001 reader kept); (T2) Bulk routes by the first 8 bytes, with a small-frames queue, alternating chunk classes and the lane's budgets as their own ByteBudgets; (T3) `Transport::hold(node)`/`release(node)` keep a Bulk connection to a node outside every group; (T4) a keepalive on every connection, lane-only Bulk included; (T5) the reader hands each lane frame over synchronously from a per-connection buffer, never waiting on a shared budget; (T6) duplicate dials resolve to one connection before any lane frame; (T7) A6's late-deadline rule covers call deadlines. Fetch is lane method 1 and DTFT0001 is deleted; today's `Peers` fetch is not ported (worker-pump-removal deletes Peers): tests per item, a mutant for T5 and T6

## Contracts

Provides the host transport and the request/response lane on it (fetch moves over; streamed request bodies beyond it are Xapiand's consensus-rpc-lane, after this quest). Consumes consensus-driver.

## Decisions

D96, D97 (owner: Q2 Sink, Q3 one directory per host, Q4 quiescence quest, Q7 node identity). Xapiand cluster-release (Xapiand:15c3e05ac) orders after this quest by this id. consensus-fetch-macos lands first (its fetch defect).

## May land early

Tests; the transport is additive beside Peers.
