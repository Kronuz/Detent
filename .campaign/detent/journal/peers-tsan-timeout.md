# peers-tsan-timeout: journal

State: waiting on jobs 261119-095751-3b68ef 261119-095753-bf2aed 261119-095756-73a0e2 261119-095815-401d23 261119-095801-b6cc19 261119-095803-4214f8, CI 44582638852 (quest), probe 7 44582684934
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

**What throws: not caught yet.** Probe 4 (run 44405919972, consensus:998120b = fix + probe
ci.yml, beside the connection_test load): 0 of 420 runs failed (2 full shards x 30, 2 shards x 180
of hung-resolver and slow-resolver alone), so nothing threw. Before the fix the hang rate beside
the load was 2 of 140 full runs (probe 1: 1 of 20, probe 2: 1 of 120). Probe 2's stacks do not
tell which call threw: epoll_wait's timeout=-1 is asio's timerfd mode, not an idle context.
Suspect, unproven: the Peers constructor's bind (peers.h:118) fails with EADDRINUSE because a
port free_port picked was taken before the voter bound it (by the load's own bind(0), which
also prefers odd ports). Probe 5 (run 44535268814, consensus:373b4ca = merged head e62cc77 +
probe ci.yml, 8 full shards x 40 beside the load) prints the exception if it recurs.

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

### What throws, named (A1)

Probe 5 (run 44535268814, consensus:373b4ca, 8 full shards x 40 beside the load): shard 8 run
17 printed `outage: threw: bind: Address already in use` before outage's first line, so a
start() (Peers' acceptor bind, peers.h:118) found its port taken. CI 44535229542's linux-asan-ubsan
aborted groups lagging with the same `bind: Address already in use` (groups_test.cc:1036 picks
the same way). The other probe 5 failures (shard 8 run 23, shard 6 run 1, rc 134) are
peers_test.cc:1172's round-trip bound in fetch-alongside-consensus: timing, not this.

**The cause:** free_port bound port 0, read the port and closed the socket. The port went back
to the ephemeral pool until the voter bound it, and again over every restart. Any bind(0) or
connect() beside it (the connection_test load, a voter redialing a refused voter, which can
self-connect on Linux) could be handed it. The thrown bind then hung the run before the
~Cluster fix, so this is the hang's origin.

**The fix** (consensus:abb7276, then consensus:015a7ea): one picker in test/support/ports.h,
used by peers_test and groups_test. Ports come from below the ephemeral range the system
reports (Linux ip_local_port_range, BSD net.inet.ip.portrange), where bind(0) and connect()
never reach, each claimed for the process by a UDP socket on [::]:port without SO_REUSEADDR,
so tests side by side never share one. No retry on EADDRINUSE.
- groups_test: Cluster's ports (picked by bind 0 and close, rebound on restart) and refill's
  restarted host (its port read from a destroyed acceptor) now use free_port.
- links_test and connection_test read ports back from listeners they keep open and never
  rebind: left as they are.
- Test: case `taken-ports` (ctest consensus_transport_peers_taken_ports, TIMEOUT 20; also in the
  full run): a listening contender on every picked port the system could hand out, then the
  voters start, commit, restart; each port is outside the range and unclaimable.
- Mutants (old picker, bind 0 and close): consensus:a858934 at abb7276
  (refs/mutants/peers-tsan-timeout/old-port-picker), consensus:621becf at 015a7ea
  (refs/mutants/peers-tsan-timeout/old-port-picker-shared). Their ports are ephemeral, so the
  contender takes them and start() throws EADDRINUSE.

### The other rc 134: peers_test.cc:1172 (lead's message, item 2)

Not this cause, and not named. Probe 5 shard 8 run 23 and shard 6 run 1 (2 of 320) failed
fetch-alongside-consensus's round-trip bound under TSan beside the load: the quiet phase's slowest
bucket was 16 ms as usual, but while fetching one append round trip landed in the 1 s bucket
against 256 ms, with 0 timeouts, slowest commits 329 and 175 ms, and the fetch phase 5.8 and
4.1 s, no slower than passing runs (whole case 7.5-8.3 s). One round trip stalled; the run as a
whole did not slow. That is the symptom peers-busy-roundtrip-bound already files (256 ms against
64 ms outside TSan on a loaded host, same check), whose A1 names the cause from samples and whose
A2 derives the bound from loaded runs. The probe prints only failing runs, so no passing
distribution exists to derive a ratio from; widening the bound now would be the blind widening
the lead ruled out. Ruling 8.

## Next

1. Read jobs: 3b68ef (A2 taken-ports at 015a7ea), bf2aed (fails-without at 621becf: expect
   `taken-ports: threw: bind: Address already in use`), 73a0e2 + 401d23 (A3 20 of 20 TSan),
   b6cc19 (ASan+UBSan groups lagging x20), 4214f8 (suite). At abb7276 A2 passed (df6d37) and
   its fails-without exited 8 with the EADDRINUSE report (92ae93).
2. Read probe 7 (`gh run view 44582684934 -R Kronuz/consensus --log | grep -E 'probe: |threw|rc [1-9]'`):
   no `threw`; an rc 134 at :1172 is peers-busy-roundtrip-bound's (above). Probe 6 cancelled.
3. CI 44582638852; paste `campaign brief`'s table; ready for review.

Merged main (consensus:c64b5cd) at consensus:ba66583, no conflicts. Earlier: A2 for the hang at
e62cc77 is 3bb236, fails-without 37e35d at mutant 0a85773. A3 runs as two strict batches of 10.
Suite c0444c timed out only in consensus_mutants (1800 s) at load ~170: load, rerun as 4f7912.

## Blocked on

Nothing. (File set widened by the lead; groups_test moved onto the shared picker.)

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| A1 [manual] | The detent lead checked: the cause is the tests' pick-then-close ports. Probe 5 (CI run 44535268814) threw `bind: Address already in use` in the outage case; CI 44535229542 aborted groups_lagging under ASan with the same error; the hang itself is the harness dropping running voters without stop() (correctness review, job 6362c5 brings it back without the new ~Cluster). The mutant bf2aed fails with that bind error at a858934. |

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
4. The fails-without test is tagged A2, the quest file's criterion for "a test that fails without
   the fix"; A1 is [manual] and keeps its journal row (the lead's message called it A1).
5. A3's TSan runs are strict holds (2 CPUs, the test's busy threads): light runs at nice 19 fail
   peers' timing checks under host load (batch b), which would test the host, not the fix.
6. The claim is a UDP socket, not the TCP port held open: holding a bound TCP socket beside
   Peers' listener needs SO_REUSEPORT on both on macOS, which Peers must not set, and a listening
   hold would accept during a restart and break the refused-connection cases.
7. The picker is header-only in test/support/ports.h, included by relative path, so no CMake
   change; it keeps peers_test's free_port(io, address) and bindable(io, endpoint) shapes.
8. peers_test.cc:1172's rc 134 stays peers-busy-roundtrip-bound's work: same check and symptom,
   cause unnamed, and no passing-run distribution to derive a ratio from (see above).
