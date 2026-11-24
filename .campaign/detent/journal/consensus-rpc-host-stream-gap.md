# consensus-rpc-host-stream-gap: journal

State: waiting on jobs 261124-032545-3c3ebf 261124-032548-9e4ff0 261124-032552-5023a3 261124-032555-cf6897 and CI of consensus:0e000db (A4); then ready for review
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
| A1 | 261124-015744-1b1ee9 (head: gap over 1,000 ms equals the host's lag); fails without: 261124-014521-c32cc2 (probe 97d862d: stages, post->send 0 ms, send->deliver 0-1 ms), 261124-014707-1319b1, 261124-015141-a3fef6 (main 512e3ef: 6 of 7 stops fail at 1,521-1,573 ms); a1.sh at the probe: 261124-015310-731798; unstalled gaps 103-222 ms under 16-24 burners: 261124-013928-c5f6e7, 261124-014534-123e12 |
| A2 | 261124-014655-15443a (7 stops, 0 failed, gaps 1,517-1,604 ms within 1,000 plus lag); fails without: 261124-014707-1319b1 (main) |
| A3 | 261124-014657-c7b211 (50 runs beside 4 burners, 0 failed) |
| A4 | `ci:macos-x86_64`, `ci:ci` (CI run 45370136336 at consensus:d428b37, green) |

## Result

Cause: a host stall, not lane scheduling. A process stopped 1.5 s mid-stream shows the heartbeat gap
equal to the host's lag (1,527-1,574 ms against 1,510-1,517 ms); the heartbeat thread itself was
frozen, and post->send and send->deliver took 0-1 ms: nothing waited behind Bulk. Unstalled gaps
stay 103-222 ms. CI run 45299903223's 1,170 ms gap in a 1,593 ms stream fits a stall; no CPU load
on this 16-core host starved the process, so the stop is the deterministic reproduction.
Fix: the stream case's bound is 1,000 ms plus the Lag sampler's worst, as the fetch and group-full
cases (consensus:303455c, :93df08c); the 1,000 ms base is unchanged. Docs: docs/mechanisms/rpc-lane.md
stream row. Open: groups_test.cc:586 `beats_2.worst < 1000` has no lag allowance (outside the file set).

## Rulings I made

- Lag allowance, not a wider base bound: one shape with fetch and group-full.
- A1's reproduction is SIGSTOP at 7 offsets (scripts in ~/.scratch/detent/consensus-rpc-host-stream-gap/), since load alone did not starve a 16-core host.
- Review findings F1-F3 (Detent:1480fcad), consensus:0e000db: F1 the stream posts a heartbeat after the call and waits until every posted one arrived (Control is in order) before reading the gap; F2 `Lag::stop()` joins the sampler and returns its worst, read once after each case's last beat (group-full, stream, fetch, call-full); F3 call-full takes `1000 + lag`. F2 needed `Lag` in test/transport/rpc_hosts.h (not in the file set; Lag's only user is rpc_host_test.cc, the edit is additive).
- Mutant fails-without: build.sh/patch.py copied from the review dir into ~/.scratch/detent/consensus-rpc-host-stream-gap/; mutant.sh holds Control 1,500 and 2,000 ms with no probe tail; job 261124-032545-3c3ebf passes when both fail at the fix.
- Evidence rows to cite after the jobs: A1 261124-032555-cf6897; A2 261124-032548-9e4ff0, fails without 261124-032545-3c3ebf; A3 261124-032552-5023a3; A4 CI at 0e000db.
