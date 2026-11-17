# rpc-lane-codec: journal

State: waiting on jobs 261117-045655-128e01 261117-045727-831947 261117-045731-952efa 261117-045736-6254de 261117-045748-3c9de3 261117-045802-b15bed 261117-045808-2d42af 261117-045813-e45d39 261117-045819-a9a7f2 261117-045825-90b1dc 261117-045831-8f2ed2 261117-045836-0de6ee 261117-045841-f3bb33
Quest: .campaign/detent/quests/rpc-lane-codec.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Built consensus:0fe4d15 (one commit on 677c864); its evidence jobs are queued.

## Next

1. Read the jobs below; fix any failure with a job that fails without the fix.
2. Paste `campaign brief`'s Evidence table; set State to "ready for review".

## Blocked on

Nothing. For the lead: the change is ~1,800 lines (rpc.h 764, rpc_test.cc 762, fuzz 117,
mutant script 65, docs 75), over the ~1,500 budget, about 880 of it tests. If one review cannot
hold it, it splits cleanly at the codec (`encode`/`decode`/`violation`, decoder tests, fuzz mode
0) versus the call table and pools.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | queued: rpc test + replay 261117-045727-831947; ASan+UBSan 261117-045736-6254de; fuzz 20 min 261117-045748-3c9de3; suite 261117-045655-128e01 |
| A2 | queued: 261117-045727-831947, TSan 261117-045731-952efa, ASan 261117-045736-6254de |
| A3 | queued: 261117-045727-831947, TSan 261117-045731-952efa (cases pools-threads, pipes) |
| A4 | queued, --fails-without at mutant commits from test/transport/rpc_mutants.py: forget-is-violation 261117-045802-b15bed, grant-without-reserve 261117-045808-2d42af, no-release-on-close 261117-045813-e45d39, no-regrant 261117-045819-a9a7f2, grant-on-receive 261117-045825-90b1dc, split-release 261117-045831-8f2ed2, drain-ignored 261117-045836-0de6ee, id-at-call 261117-045841-f3bb33 |

## Result

consensus:0fe4d15 adds `include/consensus/transport/rpc.h` (codec, `Lane` call table, `ByteBudget`
pools, `Charge`), `test/transport/rpc_test.cc` (19 cases), `test/transport/rpc_mutants.py`,
`fuzz/rpc_fuzz.cc` with 12 seeds in `fuzz/corpus/rpc`, and `docs/mechanisms/rpc-lane.md` (how it
works). Not on Bulk: no I/O, no hello, no `hold`; consensus-transport wires it.

## Rulings I made

- R1 `Cancel source_failed` is code 7 (the design names it without a number; 7 is the first after
  the lane's six). `Error` stays 1-6 or >= 256.
- R2 A `Call`'s inline bytes charge `rpc_outgoing_bytes` like a chunk; headers and small frames do
  not. So a `Call` with no inline bytes never waits on it (this is what makes `id-at-call`
  observable: a call waiting for `outgoing` takes its id after one that did not wait).
- R3 `renew(returned, wanted)` returns at least `min(wanted, returned)` and grows past `returned`
  only while no one waits on the pool, so renewals cannot starve FIFO waiters.
- R4 Regrant when outstanding credit falls to half a window, as `consumed + renew(held, min(window,
  total - consumed))`; a stream's holding covers everything the peer may still send.
- R5 Outcomes: `cancel`/`error` carry the frame's code (a caller's deadline is `cancel 3`, a
  server's `error 3`); `timed_out` when the deadline passes before the `Call` queues;
  `not_processed` for calls never queued at a `Drain` or `close()`; `not_connected` after close.
- R6 The server's call ends `ok` when its response end is queued; unread request bytes are then
  discarded (the early end costs nothing extra). The caller's ends `ok` when its consumer takes
  the response's last byte.
- R7 `CallOptions::total` unset means the inline bytes are the whole request.
- R8 `rpc::ByteBudget` is new, in its own namespace; session.h's single-executor `ByteBudget`
  stays for `Peers`, which worker-pump-removal deletes.
- R9 Callbacks run on a `shared_ptr<Events>` copy, because a callback that ends its own call
  (a server sending its end from `body`) destroyed the running closure; the `pipes` case crashed
  on it before the fix (local run, not a job).
- R10 Each mutant job runs the whole rpc test at the mutant, so the first failing case kills it;
  each case of the mutant's name was checked locally to fail on its own.
