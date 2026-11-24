# consensus-rpc-host-stream-gap: journal

State: waiting on jobs 261124-041209-359b5d (fetch probe), 261124-033258-c0b16a, 261124-033301-bd7ae6 and CI of consensus:0d5f362 (A4: red on rpc_host_fetch and seeds_rate)
Quest: .campaign/detent/quests/consensus-rpc-host-stream-gap.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Cause found (host stall, not lane scheduling): the stream test stopped 1.5 s mid-run (SIGSTOP, job
261124-014521-c32cc2) shows gap 1530/1574/1527/1574 ms = the host's lag (1510-1517 ms), the
heartbeat's own posting thread frozen (post gap = delivery gap), post->send 0 ms and send->deliver
0-1 ms: nothing waited behind Bulk. Unstalled, gaps stay 103-222 ms (16-24 burners, niced, 4 at a
time: jobs 261124-013928-c5f6e7, 261124-014534-123e12). CI's failure fits: 1,170 ms of a 1,593 ms
stream, the rest at 76 MB/s against 55 MB/s unloaded. Fix: consensus:303455c/93df08c give the stream
case the fetch and group-full cases' allowance, 1,000 ms plus the Lag sampler's worst.
A1 job 261124-015310-731798 (a1.sh at the probe 97d862d: passes when the gap equals the lag and the stages are under 5 ms); fails-without runs at main: 261124-014707-1319b1, 261124-015141-a3fef6. Jobs for A2 (stopped run at fix 261124-014655-15443a, at main 261124-014707-1319b1 fails-without) and
A3 (261124-014657-c7b211).

## Next

1. When 731798 and CI finish: paste `campaign brief`'s table; write Result (rpc-lane.md's stream row still says 1,000 ms flat: outside my file set, lead to edit); State ready for review.

## Blocked on

Nothing

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | 261124-032555-cf6897 (gap over 1,000 ms equals the host's lag); fails without: 261124-014521-c32cc2 (probe 97d862d: stages, post->send 0 ms, send->deliver 0-1 ms), 261124-014707-1319b1, 261124-015141-a3fef6 (main: 6 of 7 stops fail at 1,521-1,573 ms); earlier head 261124-015744-1b1ee9; unstalled gaps 103-222 ms under 16-24 burners: 261124-013928-c5f6e7, 261124-014534-123e12 |
| A2 | 261124-032548-9e4ff0 (7 stops at the fix); fails without: 261124-014707-1319b1 (main, stopped process), 261124-032545-3c3ebf (the review's Control-delay mutant at the fix: 1,500 ms gap 1,501 of 1,005 allowed and 2,000 ms gap 1,999 of 1,003 both fail; 1,000 ms passes at 999) |
| A3 | 261124-032552-5023a3 (50 runs beside 4 burners, 0 failed, longest gap 109 ms at run 50) |
| A4 | CI of consensus:0e000db (running; `campaign verify consensus-rpc-host-stream-gap --for A4` submitted) |

## Result

Cause: a host stall, not lane scheduling. A process stopped 1.5 s mid-stream shows the heartbeat gap
equal to the host's lag (1,527-1,574 ms against 1,510-1,517 ms); the heartbeat thread itself was
frozen, and post->send and send->deliver took 0-1 ms: nothing waited behind Bulk. Unstalled gaps
stay 103-222 ms. CI run 45299903223's 1,170 ms gap in a 1,593 ms stream fits a stall; no CPU load
on this 16-core host starved the process, so the stop is the deterministic reproduction.
Fix: the stream case's bound is 1,000 ms plus the Lag sampler's worst, as the fetch and group-full
cases (consensus:303455c, :93df08c); the 1,000 ms base is unchanged. Docs: docs/mechanisms/rpc-lane.md
stream row. Review F1-F3 folded in (consensus:0e000db): the stream waits for its heartbeat tail, `Lag::stop()` joins the sampler before each read, call-full takes the lag allowance; a Control delay of 1,500-2,000 ms now fails. Open: groups_test.cc:586 `beats_2.worst < 1000` has no lag allowance (outside the file set).

## Rulings I made

- Lag allowance, not a wider base bound: one shape with fetch and group-full.
- A1's reproduction is SIGSTOP at 7 offsets (scripts in ~/.scratch/detent/consensus-rpc-host-stream-gap/), since load alone did not starve a 16-core host.
- Review findings F1-F3 (Detent:1480fcad), consensus:0e000db: F1 the stream posts a heartbeat after the call and waits until every posted one arrived (Control is in order) before reading the gap; F2 `Lag::stop()` joins the sampler and returns its worst, read once after each case's last beat (group-full, stream, fetch, call-full); F3 call-full takes `1000 + lag`. F2 needed `Lag` in test/transport/rpc_hosts.h (not in the file set; Lag's only user is rpc_host_test.cc, the edit is additive).
- Mutant fails-without: build.sh/patch.py copied from the review dir into ~/.scratch/detent/consensus-rpc-host-stream-gap/; mutant.sh holds Control 1,500 and 2,000 ms with no probe tail; job 261124-032545-3c3ebf passes when both fail at the fix.
- Evidence rows to cite after the jobs: A1 261124-032555-cf6897; A2 261124-032548-9e4ff0, fails without 261124-032545-3c3ebf; A3 261124-032552-5023a3; A4 CI at 0e000db.
- F4 (Detent:5a71c17e), consensus:0d5f362: the stream case requires the tail wait to succeed (`arrived` joins `ok`). Mutant drop.sh (patch.py MUTANT_MODE=drop: Control frames dropped after the call starts) fails at the fix: job 261124-033248-45a895. Jobs at 0d5f362: A1 261124-033301-bd7ae6; A2 261124-033255-d1fab5, fails without 261124-033248-45a895 (drop) and 261124-033251-9931b1 (delay 1,500/2,000 ms); A3 261124-033258-c0b16a; A4 CI at 0d5f362.
- CI 45385145255 at 0d5f362, rpc_host_fetch on macos-x86_64: gap 2125 ms of 1024 (lag 24 ms), fetch 4131 ms (the passing macos run took 3.35 s: Bulk did not stop for 2.1 s). My F1-F4 edits do not touch the fetch case (F2 reads lag at the same point, after the same work), and xapiand's b92b6c8 (without them) fails fetch too (aborted connection), so it predates the quest. Probe: fetchprobe.py/fbuild.sh/frun.sh in ~/.scratch/detent/consensus-rpc-host-stream-gap/ time each heartbeat's post, loop send and delivery; job 261124-041209-359b5d runs 16 runs 8 at once beside 16 burners. Hypothesis to confirm: busy loop threads wait while the 1 ms sleeper (Lag) keeps waking, so Lag under-reports starvation of loop threads; a transport loop held under Bulk belongs to consensus-fetch-alongside-loop-stall.
- Lag moved to test/transport/lag.h (self-contained, included by rpc_hosts.h) for consensus-seeds-rate-flake: consensus:e416bb0, local only, pushed with the next push (a push restarts CI; the fetch probe 261124-041209-359b5d is still queued). A1 261124-033301-bd7ae6 and A3 261124-033258-c0b16a are done at 0d5f362.
