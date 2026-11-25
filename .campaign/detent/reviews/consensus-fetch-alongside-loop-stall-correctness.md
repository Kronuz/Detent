# consensus-fetch-alongside-loop-stall review: correctness

Commit: consensus:4fa72a5
Verdict: CHANGES
Date: 2026-11-24

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

## Evidence I relied on

- Suite 261124-215723-0d3fbd (exit 0) and CI 45557132080, green on every platform including
  macos-x86_64 (fetch-alongside 5.68 s, rpc_host fetch 20.14 s) and linux-tsan.
- Groups: A2 261124-215719-47582c (SLOW_READ=6000 passes at the fix: 16 of 16 exact, 0 peers
  gone, slowest commit 290 ms); fails-without 261124-212704-2b98ad (done); lags at main
  261124-212720-d82038 (serving loops 8-27 ms) and at the fix 261124-212804-eb47f1 (1-4 ms);
  A3 261124-215720-0be582 (0 of 25 failed beside 4 burners).
- rpc_host: A1 261124-215634-5e9c39 (3500) and 261124-215638-369ffc (1500) show CI's line: gap
  3546 or 1549 ms, reply exact, links 2+2, none ended, host 2's link loop's turn equal to the hold;
  A2 261124-215646-b613dc passes at the fix, fails-without 261124-215649-56b54a; lags at main+probe
  261124-215714-ba0954 (host 2's longest turn 13-25 ms) and at the fix 261124-215644-81f10e (3-32
  ms); A3 261124-215716-07805b (0 of 25 failed).
- Not relied on: 261124-212647-bda88f, groups A1, **failed** (F1).

## What I checked myself

Scripts are in `Detent/.scratch/detent/review-consensus-fetch-alongside-loop-stall-correctness/`.

- `moved-out-mutants.sh`, job 261124-220716-08aef6: removes the `pop_back` after each reader moves
  a ready reply out. Groups catches it: 8 misses, "0 bytes of 8388608". rpc_host does not catch it:
  "five 4 MiB at once: 1 ok, 4 refused ok" (F2).
- `dump-stall.sh`, job 261124-222251-e201f1, at consensus:10747e0 with SLOW_READ=6000. With the
  real `sample`, the process stall is 5,692 ms, 0 peers are gone and the dump runs 7,160 ms. With a
  no-op `sample` first on PATH (`shim/sample`), the stall is 5 ms, 4 peers are gone and 3 calls end
  connection_aborted: CI's shape. This root-causes F1.
- CI log run-45372452058: the sample has node 2's loops 0 and 1 (threads 64396 and 64397) and node 3's
  loop 3 (thread 64407) inside `fetch_alongside()::$_1` under serve_fetch, 6.7 s after the rounds
  began (sample at 10:47:03.873, rounds from 10:46:57.15). The dump trigger to sample start took
  ~5 s. The footprint was 29.1 MiB, which fits three 8 MiB builds in progress. Nothing in the log
  contradicts the cause.
- Read the diffs. Bounds are unchanged: the 2'000 bound, the leads and term rule, and the asserts
  in groups; `1000 + lagged`, `exact` against an independent `expected`, and D8's table
  (missing, throws, huge, busy + slow_ok == 5) in rpc_host. Groups checks each fetch against a fresh
  `pattern()`.
- Turns (rpc_host_test.cc:812-851) is stopped before `one.stop()`/`two.stop()`. It is declared after
  the hosts, so it is destroyed first, and nothing between its construction and `turns.stop()`
  returns early. The posted lambda holds `probes_` by shared_ptr, so a handler that runs late, or a
  queued handler destroyed with the io_context, is safe. The load is one post per 5 ms per loop, at
  most one outstanding, and it feeds no bound.
- Transport: serve_fetch (host_transport.h:1597) calls the reader on the connection's loop and
  returns a std::string by value. The requester appends without reserving (host_transport.h:1486),
  while Peers reserves (peers.h:408).

## Answers

1. Groups: proven. The CI sample has both held loops in the reader's build, and the knob sleeps in
   that same reader frame. Why the build took seconds on the runner (CPU starvation or paging) is
   not settled; the new per-loop CPU and state line settles it on the next dump. rpc_host:
   sufficient but not proven (F3).
2. No weakening: no bound or check changed. Groups' moved-out reader is correct and pinned by the
   mutant. rpc_host's is correct by reading but not pinned (F2).
3. Turns is safe; see above. It can under-report a worst by a lost update (F5).
4. Yes, it is a product gap (F4), and the owner's call, not this quest's.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | yes | gap | A1 for groups has no passing job. The signature fails on "the process stalled 5650 ms", and that stall comes from the 1.5 s dump's own `sample`, which Stalls counts. The dump also changes the run: with real `sample`, 0 peers are gone; with a no-op one, CI's 4 peers gone and 3 aborted. CI's "2,600 ms stall" is probably the dump's own as well. | 261124-212647-bda88f (failed); 261124-222251-e201f1 (real 5,692 ms, shim 5 ms) | Keep the dump's window out of the stall the case reports (pause Stalls around `print_stacks`, or print the dump's ms and subtract it), or have signature.sh check the stall before the dump. Rerun the signature at consensus:10747e0 plus that change: it must pass, and it fails today. |
| F2 | no | gap | rpc_host's fetch never pins the moved-out reader: a reader that leaves the moved-from reply in place still passes when only one "slow" is served (1 ok, 4 refused). slow_ok checks size, not bytes. The fallback path's code is correct (pop under the mutex at rpc_host_test.cc:899-902), and groups pins the same shape. | 261124-220716-08aef6 | Compare each slow reply with `expected.substr(0, 4 << 20)` (values, not counts). Optionally fetch one name twice in the case. |
| F3 | no | gap | The rpc_host cause is not proven: CI's logs do not show which end held. Host 1's link loop also does a monolithic copy (the requester's unreserved `append`, up to half the reply at the last doubling). The fix removes host 2's 16 MiB copy only, and the fix's lags overlap main's (3-32 ms against 13-25 ms). | host_transport.h:1486; ba0954 and 81f10e | Do not record main's 3 rpc_host CI failures as root-caused. The new Turns line names the end on the next macos-x86_64 failure. |
| F4 | no | gap | Product gap: FetchReader runs on the loop that carries Control and returns bytes by value, so even a host that serves from memory copies the whole file on that loop. The requester's buffer grows by doubling there; Peers reserves instead (two shapes). | host_transport.h:1597, 1486; peers.h:408 | The owner's call. Options: keep as is with the peer-fetch.md advice; a reader returning shared bytes; a reader run off the loop with the reply posted back; reserve the claimed total on the first chunk. |
| F5 | no | nit | Turns and LoopLags update `worst` with a load, max and store from two threads, so a lost update can under-report. LoopLags also stops probing every loop while it dumps: CI's node 3 shows 1/1/1/1 while the sample has its loop 3 in the reader (log lines 880 and 984). | rpc_host_test.cc:827-840; groups_test.cc LoopLags | Use a CAS fetch-max, and keep probing (or mark the dump's window) while dumping. |
| F6 | no | nit | At the fix, groups builds 4 x 8 MiB on the main thread per round, outside every timed window. On a runner that runs CPU-bound threads ~400x slower, that time counts only against the 60 s cap. | groups_test.cc:2707-2711 | None needed; watch the "rounds ... in N ms" figure. |

## Not run

- No macOS x86_64 runner locally: whether the runner's slow build was a thread that was runnable
  but not run, or paging, is unsettled; the new CPU and state line answers it.
- No local TSan; CI's linux-tsan is green at this commit.
- No probe of host 1's requester-side copy under a knob (F3).
- Session: a subagent of the lead, not a titled `detent · reviewer` session.

## Round 2

Commit: consensus:0d8263d
Verdict: APPROVE
Date: 2026-11-25

Reviewed consensus:4fa72a5..0d8263d. The test changes are consensus:019f63c, 4898f1c and 0d8263d.
The merge from main (e17d9d8) brings the Driver simulation, which is outside this lens.

Evidence: A1 261124-232245-034e6c passed at consensus:10747e0 plus the dump change. Run 1's dump
suspended the process 6,877 ms and no link ended; run 2's dump took 6 ms and showed 4 peers gone
and 3 calls ended connection_aborted, with 2 threads in the reader. Other jobs at consensus:0d8263d:

- Groups A2: 7d11cf.
- Groups A3: 6b4d7b, 0 of 25 failed.
- rpc_host A2: 8bc466 ("the 16 MiB again exact yes").
- rpc_host A3: 0a3ff3, 0 of 25 failed.
- F2 fails-without: 6e4d93 ("again exact NO FAILED").
- Suite: fe7353.

CI 45567716437 is green on every Linux and FreeBSD job, linux-tsan included. macos-x86_64 and
macos-arm64 were still running when I wrote this, and A4 needs them green at the landed commit.

- F1 closed. Stalls keeps any nap that overlaps a dump in `dump_stall` (an even or odd counter
  around `system()`), and the case prints both values. The signature reruns a run only when the
  dump suspended the process 1 s or more and no link ended. Probe e201f1 showed that a suspended
  process keeps its links. Every other miss, including a passing case, fails at once, so the rerun
  hides only the dump's own effect, at most 3 runs.
- F2 closed. slow_ok compares bytes with `expected.substr(0, 4 << 20)`. A second "big" is served
  from the ready queue and must arrive exact, and 6e4d93 shows the moved-from mutant fails.
- F3 recorded as not root-caused, as asked. F4 is assigned to rc1-integration.

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| R2-1 | no | nit | The case's bound no longer counts a dump's suspension (`stalled` excludes `dump_stall`), so a commit that spans a multi-second dump is no longer allowed for it. That is a new way for the diagnostic itself to fail a run. Not reproduced: with a 1.7 s sync firing the dump, 4 of 4 runs passed at the fix (one dump of 6,041 ms) and 4 of 4 at 4fa72a5. | 261124-232514-900e48, 261124-232517-9e036e (`slow-sync-dump.sh`) | Optional: add `dump_stall` to the bound's host time while keeping it apart in the print, or skip the dump when the process itself stalled. |
| R2-2 | no | nit | F5 (lost-update `worst`, and LoopLags probing nothing while it dumps) and F6 stand as written. | round 1 | As in round 1. |

Not run: macOS CI at consensus:0d8263d (still running), and the Driver simulation merged from main.
