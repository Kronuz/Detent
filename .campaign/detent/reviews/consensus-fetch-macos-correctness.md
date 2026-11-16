# consensus-fetch-macos review: correctness

Commit: consensus:d32288992df0c0a215d0412cc5d62e0a6d1d169f (re-review; diff against consensus:38f2022)
Verdict: CHANGES
Date: 2026-11-16

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

## Evidence I relied on

- 261116-063848-502982: suite `ctest -LE timing`, CI flags, at d322889: 25 of 25 pass.
- 261116-063855-f9b06f: `ctest -L timing` x10 under local TSan: 10 of 10 pass.
- 261116-063852-11f15e: `ctest -L timing` x20 beside 8 hogs: 19 of 20. Run 3 failed at :1281 with
  `retried fetch: Socket is not connected in 0 ms (stopped 1100 ms placed, 0 ms more: covered, must
  succeed)`. This is the CI :1281 signature, now locally in the stalls variant.
- CI 43888069939 at d322889: green. linux-tsan `consensus_transport_peers` 13.57 s and `_peers_fetch`
  7.94 s, both under 30 s (F1). macos-x86_64 `_peers_fetch` 7.21 s.
- CI 43881340661 attempt 1, the macos log (`~/.scratch/fetch-macos/ci-43881340661/m1.log:453-459`).
  The runner fetched at 49 MiB/s, its busiest iteration was 176 ms, and the scenario before had
  stalled 192 ms. Then: `frozen peer: fetch failed after 503 ms`, `retried fetch: Socket is not
  connected in 1 ms`.
- Mutant jobs from rounds 2-3 (late_deadline, uncapped, fetch-wall, no-cpu, wait-only) for F3, F5
  and F6. The code under test has not changed since 2998cbf/746c6f4: `git diff 2998cbf..d322889`
  touches only peers_test fetch_failures, CMakeLists and ci.yml.

## What I checked myself

The scripts are in `.scratch/review-consensus-fetch-macos-correctness/r2/`.

| Probe | Job | Result |
|---|---|---|
| U1 `uaf.sh`: `detail::async_deadline` on a timer that a handler queued ahead destroys after it has expired (the `await_reply` shape) | 261116-064445-14b4ec | **ASan heap-use-after-free at session.h:117** (`owed` reads the freed timer) |
| D1 `drain.sh 5 0 450`: a 450 ms stop inside fetch-failures' drain window, ending before the second fetch deadline | 261116-064419-0b494b | **5 of 5 reproduce the CI signature**: drain ~500 ms (450 ms of it stalled), the lane closed (connections 3, reconnects +1; one run 4 lanes already redialed), `retried fetch: Socket is not connected in 0 ms (... 0 ms more: covered, must succeed)` |
| D2 `drain.sh 30 16 0`: fetch-failures instrumented (drain ms, stall in the drain, lanes, reconnects) beside 16 hogs | 261116-064422-68a9fe | 27 of 30 pass. Drain min 124, median 201, max 502 ms. Run 19 shows the CI signature **with no injected stop**: drain 502 ms, 21 ms stalled, lane closed (connections 3, reconnects +1). Contention alone can push the drain past one deadline. Runs 8 and 26 fail at :1325 `!reopened->error` (the fetch after voter two restarts); the error is not printed (F16) |
| C1 `cancel.sh 10`: a 4 KiB frame's bytes arrive during a 600 ms stop of the reader; how long receive() takes to return after resuming | 261116-064753-dbad0d | **10 of 10: receive() returns 200-201 ms after resuming**, the whole capped extension (timeout 200 ms), though the frame was complete on resume |
| C2 the same at 38f2022, the coroutine `expiry` (baseline) | 261116-064756-fa9d59 | 10 of 10 return 0 ms after resuming: the regression is new in this diff |

## Answers to the lead's questions

1. F1-F5 are resolved (rows below), and the deadline rule is in one place. `owed` and
   `async_deadline` serve the lane `timed`, `await_reply` and the lookup. The dial loop uses
   `owed` inline. Every extension is capped at one deadline and applied once, so nothing waits
   unbounded. But `async_deadline` keeps only a reference to the timer, and that is new: in
   `await_reply` the timer is owned by the `Fetching` entry. When the timer has expired and its
   completion is queued, `close()` (peers.h:326-330) or the reply's end (peers.h:403-412)
   cancels the timer and erases the entry. The cancel cannot abort a completion that is already
   queued. The step then calls `owed(timer)` and `expires_after`/`async_wait` on freed memory
   (U1, F10). The parent's handler never touched the timer. There is also a second, bounded
   cost: the step re-arms without checking `self.cancelled()`. If `||` cancels after the expiry
   was dequeued, a lane whose op completed waits out the extension, up to one timeout. The
   38f2022 coroutine threw at that `co_await` instead (asio `throw_if_cancelled`,
   awaitable.hpp:192). C1/C2 measure it (F11).
2. :1281 is not a lane that was closed and then picked. `not_connected` comes only from
   `!connection` in `fetch()` (peers.h:215-217), so the bulk lane was gone when the fetch was
   asked. The one path in this scenario that closes it is the designed second deadline of
   `await_reply` (peers.h:353-355): the frozen peer's 8 MiB late reply did not finish within one
   `fetch_deadline` after the first. On the CI runner the drain alone needs about 165 ms of
   running time at 49 MiB/s. A stop or deschedule that ends *before* the second deadline steals
   that time with no extension, because lateness cannot see it (the journal's Open). The test
   takes `lost` only after the drain (peers_test.cc:1267), so it reads that loss as "0 ms more:
   covered, must succeed". So the transport behaves as designed, and the test is wrong to call
   the case covered. The same signature appeared locally beside hogs (11f15e run 3). D1/D2 were
   still running when I wrote this. They should show `connections < 4` and a reconnect with the
   placed stop, and the drain-time distribution under load. Read them before closing F12. F12
   itself is not blocking: the stack holds without D1/D2.
3. The `deadline/4` threshold is sound for stops it measures: an 8 MiB fetch needs ≤ 210 ms of the
   500. The bounded branch accepts any error with `ran ≤ deadline + margin`. A no-extension
   defect therefore passes in runs that take that branch. The fetch-wall and late-deadline
   mutants are killed only because most runs take the covered branch. The real hole is the one
   in answer 2: stops before `lost` is taken are counted in neither branch (F12).
4. Nothing else is hidden. `CONSENSUS_TIMING_TESTS` removes only `consensus_transport_peers_stalls`
   (stall-accounting, fetch-alongside-stalls, fetch-failures-stalls). All three inject or measure
   stops, and their non-stall halves still run in `--fetch`. Two side effects follow. CI no
   longer exercises the F2 fetch-deadline extension or the Peers-level cap; only local jobs and
   the tls pipe `late_deadline` do (F13). And `late_deadline` in `consensus_transport_pipe` is
   wall-clock too (a 600 ms sleep, 150-300 ms window), yet it stays on CI.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | no | resolved | linux-tsan timeout: the peers test is split into entries, all under 30 s. | CI 43888069939 linux-tsan 13.57 s / 7.94 s | — |
| F2 | no | resolved | The fetch reply, lookup and dial deadlines now use the late-timer rule. | peers.h:341, :569, :649-653; fetch-wall mutant 261116-051644-3f2fc9 | — |
| F3 | no | resolved | Deterministic session-level `late_deadline` over pipes; the late-deadline mutant fails 10 of 10. | 261116-035757-247722 | — |
| F4 | no | resolved | `stall_floor` is 20 ms: with no stop the bound stays 64 ms (6 of 6). | 261116-035758-6930b0; peers_test.cc:53, :384 | — |
| F5 | no | resolved | The extension is capped at one deadline (`owed` clamps to `cap`); the uncapped mutant fails. | session.h:103-106; 261116-035758-c40bdd, 261116-051638-3e833e | — |
| F10 | yes | defect | Use-after-free in `async_deadline` via `await_reply`. The fetch timer is owned by `fetching[id]`, and `async_deadline` keeps only `&timer`. If `close()` or the reply's end erases the entry after the timer expired but before its completion runs, the step reads and re-arms the freed timer. Reachable whenever a reply ends, or the lane closes (e.g. another fetch's second deadline), within one handler batch of a fetch deadline. | U1 261116-064445-14b4ec (ASan, session.h:117); peers.h:326-330, :338-345, :403-412 | Give the wait ownership. Capture the `shared_ptr<steady_timer>` in `await_reply`'s handler, or make `async_deadline` take a `shared_ptr`. Test: U1's shape as a suite test (timer destroyed after expiry is queued). It must pass under CI's linux-asan-ubsan and fail without the fix. |
| F11 | yes | defect | `async_deadline` re-arms without checking `self.cancelled()`. When `||` cancels after the expiry was dequeued, a lane op that completed waits out the extension, up to one `timeout_`. Bounded, but new: the 38f2022 coroutine threw at its re-arm. It most likely hits after a stop, when the bytes and the late timer are ready in one reactor pass. | C1 261116-064753-dbad0d (10 of 10 wait 200 ms, one whole timeout) vs C2 261116-064756-fa9d59 at 38f2022 (0 ms); awaitable.hpp:192; session.h:113-121 | `if (self.cancelled() != asio::cancellation_type::none) return self.complete(asio::error::operation_aborted);` before re-arming. Test: C1's shape (bytes arrive during the stop; receive() returns within ~20 ms of resuming). |
| F12 | no | gap | :1281 (CI attempt 1, and 11f15e run 3) is the designed lane close: the late reply was not done one deadline after the first. A stop or slow drain *before* `lost` is taken is invisible to `covered`, so the test calls it "must succeed". It is a test defect plus the stated Open (lateness misses stops that end before the deadline), not a transport defect. | m1.log:453-459; 11f15e run 3; peers.h:215-217, :353-355; peers_test.cc:1263-1281; D1/D2 pending | Take `lost` before `frozen = false`, and either require the drain to finish within one deadline of running time or skip the retried check when the lane closed. Or move `fetch-failures` under `timing`. Owner's call. |
| F13 | no | gap | With the stalls entry off CI, no CI test exercises the Peers-level fetch-deadline extension or the cap. Only local jobs do. | ci.yml; CMakeLists.txt:128-132 | Accept (D100), or add a deterministic Peers-level late-fetch-timer test. |
| F16 | yes | gap | The untimed `--fetch` entry fails 2 of 30 under 16 hogs at peers_test.cc:1325: the `reopened` fetch, after voter two restarts and `fully_connected`, returns an error. The error is not printed, and it is not the :1281 mechanism. It is unexplained, so I cannot tell whether it is a runner artifact or a defect (F10 or F11 are candidates). | D2 261116-064422-68a9fe runs 8, 26 | Print `reopened->error` and rerun D2. Name the mechanism. If it is a defect, fix it with a test. |
| F14 | no | nit | The CHANGELOG headline "no longer fails when the process itself did not run past it" overclaims. A stop that ends before the deadline still costs the peer that time (journal Open). | CHANGELOG.md:13 | Say "when its timer fires late". |
| F15 | no | nit | The dial loop's `owed(race->wake, …)` measures lateness from `next_start` when the wake was a stagger wake. It over-extends, still within the cap. | peers.h:646-653 | Measure from `deadline`. |

F6-F9 from the earlier review: F6 is resolved (no-cpu mutant 261116-035759-7a3624), F8 is resolved
(wait-only mutant), and F7 and F9 stay non-blocking as before.

## Not run

- D1 finished after I handed back and confirms F12: a stop inside the drain window closes the lane
  through the second deadline, and the test calls it covered.
- D2 finished later. Its run 19 shows that CPU contention alone, with no stop, closes the lane
  (F12). Runs 8 and 26 add an unexplained :1325 failure (F16, blocking gap).
- C1 and C2 finished later. They make F11 deterministic: every frame complete on resume is held one whole timeout (0 ms at 38f2022), so F11 is blocking. F11's delay and F12's mechanism rest on
  the code reading and the matching signatures above until those jobs are read. Their scripts are
  ready to rerun.
- 261116-063859-5287f5 (TSan suite) and 261116-063903-d4c64d (fetch x20 with hogs) were still queued.
- U1 shows the defect in the helper's contract. I did not reproduce it end to end inside Peers.
  That takes a reply or close() landing in the same handler batch as a fetch deadline.
- The U1 job log keeps only its end. The plain `async_wait` control line is not in the retained
  output.
