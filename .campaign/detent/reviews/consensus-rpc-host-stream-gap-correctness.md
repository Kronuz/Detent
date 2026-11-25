# consensus-rpc-host-stream-gap review: correctness

Commit: consensus:e416bb002c0b (first reviewed at consensus:d428b37, then at 0e000db)
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

## Re-review at consensus:0e000db

I reviewed `git diff d428b37 0e000db` (rpc_host_test.cc, and rpc_hosts.h's additive
`Lag::stop()`).

- **The wait cannot hang.** `until(beats.count >= posted, 10s)` (rpc_host_test.cc:439) has a 10 s
  deadline. `posted` is written and read only on the main thread, and `beats.count` is atomic. It
  never timed out in practice: A3 (5023a3) ran 50 runs in 43 s, about 0.86 s a run, so every
  warm-up beat arrived.
- **The sampler is joined once on every path.** `stop()` sets `stopped`, joins only while the
  thread is `joinable()`, and returns `worst`. The destructor's second call is a no-op. Every
  early return comes before `Lag` exists (stream's warm-up), or the destructor joins.
  group-full, call-full, stream and fetch each read the value `stop()` returned, so F2 is closed.
- **F1 is fixed.** My Control-delay mutant at the fix (3c3ebf): 1,500 ms and 2,000 ms fail, and
  1,000 ms passes at 999 ms of 1,004. The stopped runs (9e4ff0) now measure the gap at offset
  0.60, 1,571 ms where d428b37's run showed 103 ms, and all 7 pass within the lag.
- **F3 is fixed.** call-full has `Lag` and a `1000 + lagged` bound (:369-370).
- **A2:** 261124-032548-9e4ff0. **A3:** 261124-032552-5023a3 (50 runs, 0 failed). CI at 0e000db
  was still running when I wrote this.

## Re-review at consensus:e416bb0

I reviewed `git diff 0e000db e416bb0`.

- **F4 is fixed.** `arrived` now takes the wait's result and joins `ok`, and the log line prints
  `arrived` or `LOST`.
  - The drop-Control mutant 261124-050014-f52514 fails as required, logging "the last LOST" and
    FAILED.
  - A1 261124-050028-665c4f, A2 261124-050027-dd85d6 and A3 261124-050028-760936 pass. CI at
    e416bb0 is green, per the lead.
- **`Lag` moved to test/transport/lag.h without a change in behavior.** The struct, `stop()`
  and the destructor are unchanged. To make the header self-contained, `steady::now()` became
  `std::chrono::steady_clock::now()` and `1ms` became `std::chrono::milliseconds(1)`, and the
  comment was reworded. rpc_hosts.h includes it.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | no | gap | **Fixed at 0e000db** (jobs 3c3ebf, 9e4ff0, 5023a3). The stream case measures only closed gaps. It checks `beats.worst` the moment the call ends (rpc_host_test.cc:427-436), so a Control delay longer than the remaining stream (about 0.5 s) is never seen. This predates the quest: main has the same check. | Job 261124-031642-88f679: a Control hold of 1,000-2,000 ms passes with a gap of 0-4 ms and fails only with a tail. Contributor jobs c32cc2 (offset 0.45: 1,933 ms stream, gap 108 ms) and 15443a (offset 0.60: 2,075 ms, gap 103 ms) miss a 1.5 s stop the same way. | Before the check, either heartbeat for a tail of 1-2 s, or fold `now - beats.last` into `worst`, or require a beat count as call-full does (:366). The test: rerun `patch.py`'s `MUTANT_DELAY_MS=1500` without a tail, which must fail. Lead's call: fold it in here (the file is in the set) or open a follow-up. |
| F2 | no | gap | **Fixed at 0e000db** (`Lag::stop()`, all four cases). `lag.worst` is read while the sampler still runs (:434). A stop that ends just before the check can be missing from Lag even though a beat delivered after resume already closed the gap, which is a residual false failure. The same read exists in fetch (:886) and group-full (:311). | c32cc2 offset 0.45 and 1b1ee9 offset 0.60: a 1.5 s stop inside the run reports lag 1-3 ms. The gap was open in those runs too, so the probes observed no false failure. | Stop and join the sampler before reading (for example `Lag::stop()` returning `worst`, in rpc_hosts.h), once for all three cases. |
| F3 | no | nit | **Fixed at 0e000db** for call-full; groups_test.cc:586 remains out of the set. `call-full` (:366) is labelled `timing` and keeps a flat `beats.worst < 1000` with no Lag, so it is exposed to the same stall. groups_test.cc:586 is the same, with its own copy of `Beats` (:331); the journal already notes it. | Code reading | Give it the same allowance when the next flake shows up, or now (rpc_host_test.cc is in the set). |

| F4 | no | gap | **Fixed at e416bb0** (f52514 fails as required; 665c4f, dd85d6, 760936 pass). At 0e000db the stream case ignores the result of the 10 s wait (:439). A heartbeat lost outright, or later than 10 s, leaves `beats.worst` untouched and passes. Errors should not pass silently. | Code reading. The wait never times out normally (5023a3: 0.86 s a run), so failing on it costs nothing. | `const bool arrived = until(...)` and `&& arrived` in `ok`. The test: a mutant that drops Control frames after the call starts must fail. |

## Not run

- macOS x86_64 or 4-vCPU timing locally. The spinner probe stands in for CPU starvation on this
  16-core host only.
- A rerun at main of the mutant. Main's stream check (main's rpc_host_test.cc:434) differs from the
  head's only in the bound, which I established by reading the diff.
- TSan or sanitizers, which are CI's.
- groups_test.cc, which is outside the file set.
