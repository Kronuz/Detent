# peers-tsan-timeout: journal

State: waiting on jobs 261118-135201-486193 261118-135207-6362c5, CI 44405920724 (quest), probe 4 44405919972
Quest: .campaign/detent/quests/peers-tsan-timeout.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Step 1, reproduce on macOS TSan (RelWithDebInfo, -fsanitize=thread, target
consensus_transport_peers_test in .scratch/build-tsan of the quest slot) at consensus:42019eb.
Runner: ~/.scratch/peers-tsan-timeout/loop.sh BUILD N LIMIT OUT [CASE] runs the binary N times
through a pty (line-buffered, so in-case printfs survive a kill), samples a run past LIMIT s
(`sample`, stacks in OUT/sample-i.txt), kills it, and writes OUT/summary.txt with per-case
max/mean. Exits 0 unless STRICT=1, so chained diagnostic jobs run even after a hang.
- 261118-125526-323c20: full x10, limit 75 s -> ~/.scratch/peers-tsan-timeout/main-full-a
- 261118-125534-d50a24: full x10 (after a) -> main-full-b
- 261118-125551-0df91a: refused-first alone x50, limit 30 s (after b) -> main-refused

CI facts (run 44379638851): stdout is a pipe, so a case's own printf stays buffered until run()
flushes it; the kill at 30.01 s lost whatever refused-first printed. No CHECK fired (stderr is
unbuffered and empty), so the run neither failed a check nor ended: refused-first or a later case
ran past ~21.7 s.

Linux probe (lead's msg, ci.yml granted for the probe branch only): consensus:1498386 on
probe/peers-tsan-timeout, which is never merged and never lands. It replaces .github/workflows/ci.yml
with a workflow_dispatch job on ubuntu-24.04 that uses CI's TSan flags (clang -O1 -g -fsanitize=thread).
It runs the peers binary 20 times, in two shards: alone, and beside a looping connection_test (CI's
ctest --parallel 2). Output is line-buffered through `script` with per-case times. A run past 120 s
gets `sudo gdb thread apply all bt`, then SIGQUIT. The workflow's source is
~/.scratch/peers-tsan-timeout/probe-ci.yml. Run: 44397775687 (dispatched 2026-11-18T21:05Z).

### The hang, named (A1)

Probe 2 (run 44401370811, consensus:4a45e15): of 120 runs beside the connection_test load, 1 hung
(shard 2, run 5) inside slow-resolver, after refused-first ok. gdb stacks of the peers process:
- The main thread is idle in epoll_wait inside run()'s io.run() for slow-resolver
  (peers_test.cc:451).
- Two lookup threads sit in FakeDns::resolve's Slow wait (:148).
- There are no other threads.

Every wait in a scenario is bounded (until() pumps against a deadline) and CHECK aborts loudly,
so a still-running scenario prints or aborts within seconds. The lookups are still parked
because FakeDns::quiesce never ran: the scenario did not reach its end. It left by an exception.

**The cause:** run() spawned the scenario and collected its exception only after io.run()
returned. A throwing scenario unwinds its Cluster, which dropped its Peers without stop(). Each
Peers lives on through its own tasks (shared_from_this), so the voters keep redialing and
looking up forever, io.run() never returns, and the exception is never reported. The result is
a silent hang at whichever case threw: hung-resolver in probe 1, slow-resolver in probe 2, and
refused-first or slow-resolver in CI.

**The fix** (consensus:1867287, test/transport/peers_test.cc):
- ~Cluster stops every running voter, so they drain and io.run() returns.
- run() prints `<case>: threw: <what>`, and the test exits with a failure.
- Test: the `throwing` case (by name; ctest consensus_transport_peers_throwing, TIMEOUT 20,
  PASS_REGULAR_EXPRESSION on the report) throws with three voters connected.
- Jobs: 261118-135201-486193 at the fix (passes); 261118-135207-6362c5 with --fails-without at
  mutant consensus:a1eb72a (no ~Cluster stop; refs/mutants/peers-tsan-timeout/no-cluster-stop),
  which must time out.

**Still open: what throws.** Probe 4 (run 44405919972, consensus:998120b = fix + probe ci.yml)
prints the exception. Suspect: binding a port picked earlier by free_port fails with EADDRINUSE,
because the load or the voters' own dials took it as an ephemeral source port. Fix that next,
or the hang becomes a reported failure instead.

Budget: consensus:c81a075 sets TIMEOUT 50 for consensus_transport_peers (CMakeLists.txt, granted).

### Findings so far (consensus:42019eb)

1. **Linux TSan hang (a defect, cause not yet named).** Probe 1, run 44397775687:
   - probe-alone: 20 of 20 pass.
   - probe-beside: 19 of 20 pass. Run 16 printed system-localhost ok, then nothing for 120 s, so
     it was stuck inside hung-resolver before that case's first line. Load average was 0.16, so
     the process was blocked, not spinning.
   - The probe's stacks are of the wrong process: `pgrep -x consensus_trans` matched the
     connection_test load, whose name truncates the same way. Probe 2 (consensus:4a45e15, run
     44401370811: 4 shards x 30 beside the load) matches the full binary path instead.
   - CI 44379638851 hung at another case boundary, after several-addresses. A blocked io thread
     at varying cases points to one shared mechanism, not one case.
2. **Budget: no headroom.** A whole passing run takes 21.6-22.1 s on Linux TSan (probe 1, 39
   runs) and 21.7-24.7 s on macOS TSan (job 323c20, 10 of 10). The sums of per-case maxima are
   22.3 s (Linux) and 23.6 s (macOS), against CTest's TIMEOUT 30. The biggest cases are
   fetch-alongside-consensus (max 4.2 s Linux, 5.0 s macOS), slow-resolver (3.6 s) and outage
   (2.9 s). Applying the 2x rule to the slowest passing run (24.7 s) gives TIMEOUT 50.
3. **macOS batch b (job d50a24): host load, not peers.** 9 of 10 runs failed, all on timing
   checks while the host ran at load ~170 on 16 CPUs (light jobs run at nice 19):
   - 5 runs failed at peers_test.cc:1155, `busy.slowest_ns <= round_trip_bound_ns`. The append
     round trip reached the 1 s bucket against a 256 ms bound; fetch-alongside-consensus took
     14-18 s against 3.8 s on Linux.
   - 4 runs failed at :1321, late-append's `committed`, with the slowest commit 2.0 s against a
     2 s limit.
   - None hung. This is the same load sensitivity for which macOS x86_64 CI already skips
     fetch-alongside-consensus.
4. **refused-first alone (job 0df91a): 50 of 50 pass** on macOS TSan, max 919 ms.

## Next

1. Read probe 4 (`gh run view 44405919972 -R Kronuz/consensus --log`): which exception, in which case; fix that cause with a test.
2. A3: peers 20 of 20 under TSan (STRICT=1 loop.sh job), `campaign verify`, CI on the quest branch (44405920724 is at 1867287).
3. Merge main (after consensus-close-fenced lands; it also registers a test in CMakeLists.txt) before landing.

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

1. TIMEOUT 50 applies to consensus_transport_peers on every build, not only TSan builds, matching
   consensus_simulation's unconditional sanitizer-sized TIMEOUT 300. The cases are mostly wall-clock
   waits, so a plain build is not much faster.
2. The hang fix lives in the test harness (~Cluster), not in Peers. Peers' contract is "retain
   until drained", and the harness broke it on unwind.
3. macOS batch b's timing failures (load ~170) are not this quest's work, per the lead; they are
   noted above.
