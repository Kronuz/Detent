# consensus-rpc-host-stream-gap review: correctness

Commit: consensus:d428b377eb75f888cd38bed532dc6f996f9cda80
Verdict: APPROVE
Date: 2026-11-24

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

## Evidence I relied on

- 261124-014521-c32cc2 (probe consensus:97d862d, SIGSTOP 1.5 s at 7 offsets): in each of the 4
  failing runs the delivery gap equals the posting gap (1,527-1,574 ms), the lag is 1,510-1,517 ms,
  and post->send and send->deliver take 0-1 ms. The heartbeat was late because its posting thread
  was frozen. No frame waited in the transport.
- 261124-015744-1b1ee9 (head): 5 stopped runs where the gap exceeds 1,000 ms by an amount within
  100 ms of the printed lag. 261124-014655-15443a (fix) passes 7 of 7 stops; 261124-014707-1319b1
  and 261124-015141-a3fef6 (main) fail them.
- 261124-014657-c7b211 (A3): 50 runs beside 4 burners, 0 failed, longest gap 127 ms.
- CI run 45370136336 at consensus:d428b37: green, macos-x86_64 included.

## What I checked myself

Scripts are in `.scratch/detent/review-consensus-rpc-host-stream-gap-correctness/`: `build.sh`,
`patch.py` (env-driven mutants in `HostTransport::send`/`call` plus a `PROBE_TAIL_MS` heartbeat
tail in the stream case) and `run.sh`. Probe job **261124-031642-88f679** (strict, head
consensus:d428b37, 16 CPUs) produced these results:

| Run | Result |
|---|---|
| Baseline x15 | lag 1-3 ms, so the bound is 1,001-1,003 ms; gaps 104-110 ms |
| Control held 1,000/1,500/2,000 ms from the call's start, no tail | **passes** at every delay; longest gap 0-4 ms |
| The same mutants with a 2.5 s tail | 1,500 ms and 2,000 ms **fail** (gap 1,499-2,001 ms, lag 2-4 ms); 1,000 ms passes at 999-1,000 ms of 1,002 |
| Sending loop spins 1,500 ms, with a tail | fails, gap 1,500 ms, lag 2 ms |
| 32 spinner threads (2x the cores) for 1,500 ms | lag 20-23 ms |
| 32 spinners plus a 1,500 ms hold, with a tail | fails, gap 1,411-1,444 ms against a bound of 1,020-1,023 ms |

I also fetched the CI logs into `ci/`. The failing log is `fail-a1.log`: run 45299903223, attempt 1,
job 138129577299, macos-15-intel, `ctest --parallel 2`. `durations.txt` holds the
`rpc_host_stream` durations from 20 recent macos-x86_64 runs.

Answers to the lead's questions:

1. **Does the evidence prove a host stall?** Locally, yes, for the reproduction. The stages job
   (c32cc2) shows the heartbeat's own thread frozen and zero transport wait.
   - **Can a lane delay hide inside the allowance?** Only one that coincides with a stall. Unstalled,
     the allowance is 1-3 ms. With 2x the cores busy-spinning it is 20-23 ms, so a transport CPU hog
     cannot buy itself a meaningful allowance.
   - **Can a transport stall inflate Lag?** A loop that blocks or spins delays heartbeats without
     moving Lag (2 ms). Such a transport stall still fails the test.
2. **Is it one shape with the other cases?** Yes. It uses the same `Lag` (rpc_hosts.h:187), built
   after `beats.worst = 0` as in fetch, and the same `1000 + lag.worst` arithmetic as fetch
   (rpc_host_test.cc:886) and group-full (`250 +`, :311). With no stall it is 1-3 ms looser than
   `< 1000`.
3. **Does a 1,000-2,000 ms Control delay still fail?** No. It passes at the head, and the same holds
   at main because main's check is the same. The cause is not the allowance; see F1.
4. **Is CI's run consistent with a stall?** Yes. Normal `rpc_host_stream` time on macos-x86_64 is
   0.53-1.16 s (median about 0.7 s); the failure took 1.68 s. That is about 1.0 s of extra time
   against a 1,170 ms gap, so Bulk stopped too.
   - A Control-only delay would not lengthen the stream.
   - The log cannot tell a host stall from a whole-transport freeze, because no Lag existed then.
     Either way the new bound does not excuse a freeze, since Lag stays near 0.
   - `local-throw`, which ran inside the stream's window (01:34:09.94-10.22), took a normal 0.27 s.
     The stall therefore fell after it, beside only `stalled-peer`, whose 4 s sleep absorbs it.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | no | gap | The stream case measures only closed gaps. It checks `beats.worst` the moment the call ends (rpc_host_test.cc:427-436), so a Control delay longer than the remaining stream (about 0.5 s) is never seen. This predates the quest: main has the same check. | Job 261124-031642-88f679: a Control hold of 1,000-2,000 ms passes with a gap of 0-4 ms and fails only with a tail. Contributor jobs c32cc2 (offset 0.45: 1,933 ms stream, gap 108 ms) and 15443a (offset 0.60: 2,075 ms, gap 103 ms) miss a 1.5 s stop the same way. | Before the check, either heartbeat for a tail of 1-2 s, or fold `now - beats.last` into `worst`, or require a beat count as call-full does (:366). The test: rerun `patch.py`'s `MUTANT_DELAY_MS=1500` without a tail, which must fail. Lead's call: fold it in here (the file is in the set) or open a follow-up. |
| F2 | no | gap | `lag.worst` is read while the sampler still runs (:434). A stop that ends just before the check can be missing from Lag even though a beat delivered after resume already closed the gap, which is a residual false failure. The same read exists in fetch (:886) and group-full (:311). | c32cc2 offset 0.45 and 1b1ee9 offset 0.60: a 1.5 s stop inside the run reports lag 1-3 ms. The gap was open in those runs too, so the probes observed no false failure. | Stop and join the sampler before reading (for example `Lag::stop()` returning `worst`, in rpc_hosts.h), once for all three cases. |
| F3 | no | nit | `call-full` (:366) is labelled `timing` and keeps a flat `beats.worst < 1000` with no Lag, so it is exposed to the same stall. groups_test.cc:586 is the same, with its own copy of `Beats` (:331); the journal already notes it. | Code reading | Give it the same allowance when the next flake shows up, or now (rpc_host_test.cc is in the set). |

## Not run

- macOS x86_64 or 4-vCPU timing locally. The spinner probe stands in for CPU starvation on this
  16-core host only.
- A rerun at main of the mutant. Main's stream check (main's rpc_host_test.cc:434) differs from the
  head's only in the bound, which I established by reading the diff.
- TSan or sanitizers, which are CI's.
- groups_test.cc, which is outside the file set.
