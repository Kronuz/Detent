# consensus-driver-pressure-rearm review: correctness

Commit: consensus:e61ac8f
Verdict: APPROVE
Date: 2026-11-24

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

## Evidence I relied on

- A1 261124-172806-e68a0a at e61ac8f: `hold`, 9 checks, 0 failures.
- A1 fails-without 261124-172810-68a7c2 at f5069e0. `git diff e61ac8f f5069e0` is exactly
  driver.h back to main's: the bool, its two writes and the arm() branch. The job fails with
  "no arm at or before the clock: 1000" and "at most one step per retry_ns ...: 1000". "No
  pre-vote" and "Core's deadline unchanged" both pass at main. So the test fails for the stated
  reason: at main the refused Tick is re-stepped on every pass at a past deadline.
- A2 261124-172812-a595f1 at e61ac8f: `clears`, 6 checks, 0 failures.
- The suite job 261124-172820-a9d732 was still running when I wrote this. CI was also running.
  Both stay landing gates; I relied on neither.

## What I checked myself

Probe script: `.scratch/detent/review-consensus-driver-pressure-rearm-correctness/probe.sh`
(`probe.cc`). It includes the quest's test fixture, is built in Debug with live asserts, and
runs every case. Job 261124-175049-9eeef4 at e61ac8f passed:

- `hold` and `clears` in Debug: 0 failures.
- `after_clear`: after the release, the step re-arms at Core's next election deadline, 194 ms
  ahead. Over the next 500 ms there are 3 arms, none retry-shaped, `pressured` is 0, and
  pre-votes go from 1 to 4. The bool does not linger, and later deadlines still act.
- `long_hold`: 1 s of pressure gives 1000 arms, 0 in the past, and Core's deadline is unchanged.
  On release the election runs once.

The first run of the same script, 261124-173831-ef7362, failed `after_clear` with 0 arms and no
second pre-vote. Root cause: the probe's own window (100 ms) was shorter than the compact
timing's 100-200 ms election timeout; the deadline was armed 194 ms ahead. The 500 ms rerun
above settles it. It was a probe error, not a code defect.

1. **Is every release a step of the group?** Yes in effect. The pool is the Store's own
   `Admission`; it refills only through that Store's own permits and reclaims (journal
   admission.h `refill` callers: `settle`, `release`, `credit_durable_reclaim`). A shared host
   adds no cross-group pool. A release either runs inside a step (synchronous appends,
   maintenance) or in a completion that marks the group ready:
   - IO completions: `launch`, driver.h:1184-1196.
   - Turn barriers: `watch`, driver.h:1202-1209. StoreAppend settles at the barrier.
   - `cancel_checkpoint`, followed by `mark_ready`: driver.h:706-711 and 1081.

   I found no release that leaves the group unwoken, except after shutdown, when `watch` is
   not re-armed. 1 ms is the right floor: `retry_ns` equals the production wheel's slot
   (wheel.hh:67, `tick = 1'000'000`). So "within one retry_ns" is "within one wheel pass" in
   production, and Core's clock is in ms anyway.
2. **Can `pressured` go stale?** It is cleared at the top of each step (driver.h:792). It is set
   by each turn's `run_one` (797) and read only by `arm()`, whose only caller is the end of
   `step()` (819). No event, call, Start, fence or close reads a value from an earlier step.
   The only stale read is the exception path; see N3. It can delay a due deadline by at most
   `retry_ns`, and only when the last turn was Pressure for something other than the Tick (N2).
   It never hides a deadline beyond that.
3. **The test.** `-fno-access-control` is sound here:
   - CI builds with GCC and Clang only (cpp-libraries cmake.yml:102-106), and both accept the flag.
   - `consensus` is INTERFACE (CMakeLists.txt:20), and the flag is on one TU, so no
     instantiation is shared with differently-checked code.
   - The repository has no test seam that reaches a Worker's Store through the Driver.
   - worker_test's route (scarce limits on a per-group PosixIO Store) does not carry over: in
     Host mode `used` counts artifacts only.

   A1 and A2 check values: arm counts bounded on both sides, past arms, Core's deadline, and the
   exact pre-vote count.
4. **Busy, Fenced and admit() are unchanged.** The diff touches only the bool, its two writes
   and the branch at driver.h:839-842. Busy and Fenced results set `pressured` false. admit()'s
   Pressure (driver.h:892, 909, 926) never sets it. The `waiting` min at 844 is unchanged.

**consensus-recovered-over-limit-start.** This fix does not make its liveness worse; the group
still never starts. It does change the cost. A group whose `run_one` returns Pressure with Core's
deadline at `maximum` used to sleep. It now steps once per `retry_ns`: up to 1000 steps/s per
stuck group, by driver.h:839-842 (not run). That is the poll N1 would remove.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| N1 | no | gap | The rule also polls when Core's deadline is ahead or `never`: `at > retry` → `at = retry` (journal ruling 2). By the doc's own argument (every release wakes the group, check 1), no release needs that poll. It costs up to 1000 steps/s per group under sustained capture-funding Pressure (`fund_capture`, worker.h:1001; a capture whose growth exceeds the replacement pool never clears) and for the unstarted over-limit group. At main these slept until Core's deadline. Unmeasured. | driver.h:839-842 | Options, lead's call: (a) narrow to the past-deadline case, `if (g.pressured && at <= now) at = now + retry_ns;`, which leaves A1 and A2 unchanged; (b) keep it as a liveness floor and state its cost in deadlines.md. |
| N2 | no | nit | The doc says each release "is a step of that group". IO and turn completions release outside a step and then `mark_ready` the group. The doc also scopes the rule to "a Tick the Worker refuses", but the bool keys on any Pressure turn: `submit_held`'s refused Receive (Normal pool, worker.h:612), capture funding, and initialization. In those cases a due Tick that would fit waits up to one `retry_ns`. | deadlines.md "What the Driver re-arms"; worker.h:539-626 | Say "is followed by a step of that group" and "a turn the Worker ends with Pressure". |
| N3 | no | nit | A throw after a Pressure turn leaves `pressured` set. A group fenced mid-step (driver.h:809-813) is then armed `retry_ns` ahead instead of disarmed: one extra step, which sees Fenced and disarms. | driver.h:797, 809-819 | Optional: clear `pressured` in the catch. |
| N4 | no | nit | `-fno-access-control` reaches `Driver::groups_` and `Worker::store_`. It is sound on the CI compilers, but it couples the test to private names. | CMakeLists.txt (test target); driver_pressure_test.cc | Keep. A named seam is one more mechanism for one test. |

## Not run

- The suite (261124-172820-a9d732) and CI at e61ac8f had not finished.
- I ran no shared-mode test with real in-flight IO releasing Control permits. Check 1 rests on
  reading `launch`, `watch` and `cancel_checkpoint`, not on a run.
- I did not measure N1's poll cost, and I did not reproduce the over-limit stall.
- No sanitizer run; that is CI's.
- This review ran as a subagent, not a session titled `detent · reviewer
  consensus-driver-pressure-rearm correctness`.

## Round 2

Commit: consensus:c724c58
Verdict: APPROVE
Date: 2026-11-24

What changed since e61ac8f: driver.h, the test (`ahead`, `rest`), CMakeLists.txt and deadlines.md.

- The pressured branch now fires only when the deadline is behind the clock:
  `if (g.pressured && at <= now) at = now + retry_ns;`. A deadline ahead of the clock, or
  `never`, is armed as it stands. This takes N1's option (a).
- The fence catch clears `pressured` (N3).
- The doc now says "a turn the Worker ends with Pressure" and "is a step of that group or is
  followed by one" (N2).

**Is the past-deadline case still bounded?** Yes. The branch is the same one that bounded it in
round 1, so a due deadline still arms at most one `retry_ns` ahead.

My probe script, unchanged, at c724c58 in Debug: job 261124-192909-5b3b7a passed.

| Case | Result |
|---|---|
| `hold` | 0 failures |
| `clears` | 0 failures |
| `after_clear` | release step armed 194 ms ahead; 3 arms in 500 ms, none retry-shaped; pre-votes 1 → 4 |
| `long_hold` | 1000 arms in 1 s, 0 in the past; the election runs once on release |

**Can a pressured group with a future deadline miss a release from outside a step?** Only until
its own deadline. It then sleeps until that deadline or until any wake, as at main.

- Every production release wakes the group (round 1, check 1):
  - `launch`'s IO completion (driver.h:1184-1196)
  - `watch`'s turn barrier (driver.h:1202-1209)
  - `cancel_checkpoint` followed by `mark_ready`
- The only unwoken releases I found:
  - a test dropping permits
  - a turn barrier after shutdown, when `watch` is not re-armed (D8: the group is stopping)
- In both, the group runs at its next deadline. If the pool is still short then, that deadline
  is behind the clock and gets the 1 ms floor. Nothing is lost, and the wait is bounded by
  Core's own deadline.
- With `maximum` (at rest), Core has nothing to time. A later event steps the group as it would
  at main.

**The new tests do not pass vacuously.** Each pressure turn is shown to happen by its
fails-without at 4dc1fd1. `git diff 4dc1fd1 c724c58` is only driver.h, so the mutant is c724c58's
test with e61ac8f's rule.

| Test | At c724c58 | At 4dc1fd1 (e61ac8f's rule) |
|---|---|---|
| `ahead` | 261124-180709-310fda: arms once, at Core's deadline | 261124-180717-874feb: 199 arms, fails |
| `rest` | 261124-180713-57d06f: 0 arms, no timer | 261124-180721-a113e5: 2 arms, fails |

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| R2-1 | no | nit | Under the narrowed rule, the `retry_ns` floor in the doc covers only a due deadline. An unwoken release, which I found only in tests and after shutdown, waits for the group's next deadline, not 1 ms. The doc's "The `retry_ns` is the floor for a release from outside a step" overstates it for a deadline ahead of the clock. | deadlines.md "What the Driver re-arms", second paragraph | Optional: "for a due deadline; one ahead waits for itself". |

Not run in round 2: the suite at c724c58, and CI. Both remain landing gates.
