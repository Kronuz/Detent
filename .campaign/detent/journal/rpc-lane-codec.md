# rpc-lane-codec: journal

State: waiting on jobs 261117-050434-95ef3e 261117-050435-5170c5 261117-050439-c01d2b 261117-050443-3fb2a3 261117-050447-ac8101 261117-050452-8737e6 261117-050457-ef5f13 261117-050501-e5a32f 261117-050506-7d6860 261117-050511-765547 261117-050516-fffbe9 261117-050521-7a5f49 261117-050526-8966c1
Quest: .campaign/detent/quests/rpc-lane-codec.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Head consensus:8db3ce3 (0fe4d15 the lane, 8db3ce3 the fuzz harness fix); its evidence jobs are
queued. At 0fe4d15 the suite, rpc test, TSan, ASan+UBSan and all eight mutant jobs passed; the
fuzz job 261117-045748-3c9de3 failed on the harness (below), so everything reruns at 8db3ce3.

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
| A1 | queued at 8db3ce3: rpc test + replay 261117-050435-5170c5; ASan+UBSan 261117-050443-3fb2a3; fuzz 20 min 261117-050447-ac8101; suite 261117-050434-95ef3e |
| A2 | queued: 261117-050435-5170c5, TSan 261117-050439-c01d2b, ASan 261117-050443-3fb2a3 |
| A3 | queued: 261117-050435-5170c5, TSan 261117-050439-c01d2b (cases pools-threads, pipes) |
| A4 | queued, --fails-without at mutants of 8db3ce3 (test/transport/rpc_mutants.py): forget-is-violation 261117-050452-8737e6, grant-without-reserve 261117-050457-ef5f13, no-release-on-close 261117-050501-e5a32f, no-regrant 261117-050506-7d6860, grant-on-receive 261117-050511-765547, split-release 261117-050516-fffbe9, drain-ignored 261117-050521-7a5f49, id-at-call 261117-050526-8966c1 |

Review probes (security review, scripts in the home checkout's `.scratch/review-rpc-lane-codec-security/`): 261117-051541-d571c4 (mutants by case, v1: clean runs not rebuilt), 261117-051544-a4d308 and 261117-052700-54817b (failed to build, superseded), 261117-055231-96b757 (oracle, flood, overrun, reenter), 261117-055301-75aff3 (each mutant fails its named case), 261117-055239-13601d (10-min fuzz at max_len 70000 with rpc.h coverage).

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
- R11 Fuzz job 261117-045748-3c9de3 crashed after 7,293 runs on the target's own server, which
  sent "ok" (2 bytes) against a 1-byte window; `Lane::send` refusing it is correct. The harness
  now sends what credit allows, and the input is `fuzz/corpus/rpc/crash-send-past-credit`.
