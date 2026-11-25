# post-w2: journal

State: landed
Quest: .campaign/detent/quests/post-w2.md (the lead's; never edit it)

History: landed at `Detent:43d1ce9` on 2026-11-03, before this campaign; recorded with
`campaign land --record --earlier`.

## Doing

Nothing.

## Next

1. Nothing; landed.

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | open: the ledger leaves the cluster and consensus pins to Next 1 (`docs/status/post-w2.md`); `m1-pins` took them later (`Detent:3e8bdbc`, `docs/status/m1-pins.md`) |
| A2 | `docs/status/post-w2.md`: "Bound ports (`Detent:6683e63`, note `docs/mechanisms/bound-port-report.md`): `detentd --port 0`" |
| A3 | `docs/status/post-w2.md`: "CR2 (`Detent:20b9c29`, `tests/detent_authority/test_service_wire.cc`, `test_authority_service_wire`)" |
| A4 | `docs/status/post-w2.md`: "Mutants, each failing its test: `request.keep_alive` replaced by `true`" |
| A5 | `docs/status/post-w2.md`: "83/83. cluster (bus, raft 2/2) and consensus (transport, completion_asio 9/9) pass" |

## Result

Landed through `Detent:43d1ce9` on 2026-11-03.
CI 41757189321 green on every job; native 83/83 of the touched suites; four mutants fail their tests.
The bound-port report also closes the salvage follow-ups' item 1 for the daemons.
No separate review; it carries the W2 review's CR2. Docs: `docs/mechanisms/bound-port-report.md`.
Left open: A1 (cluster and consensus pins, blocked on a denied push; closed by `m1-pins` on 2026-11-04).

## Rulings I made

None recorded.
