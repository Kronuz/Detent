# consensus-fetch-macos review: correctness

Commit: consensus:6efc34bde48773d220d73678bff43b8b28fb8f38 (re-review of the fixes since consensus:d322889)
Verdict: CHANGES
Date: 2026-11-16

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

## Evidence I relied on

- CI 43921692140 at 6efc34b: green on all ten jobs. linux-asan-ubsan builds and passes
  `consensus_transport_deadline` (0.66 s), so F10's test runs under ASan in CI. The pipe entry
  (`completed_read_after_stop`) passes on every platform.
- F10/F11 mutants at c676998. `session.h` and `peers.h` are unchanged since then
  (`git diff c676998..6efc34b` touches only peers_test.cc). These mutants still count:
  - 261116-075838-18b79c: a raw-pointer wait gives ASan heap-use-after-free in `owed`, reached from `dropped_timer`.
  - 261116-075841-ac7334: without the cancel check, `cancelled_wait` takes 600 ms.
  - 261116-075845-6213af: without the cancel check, `completed_read_after_stop` returns 401 ms after resuming.
- ASan suite 261116-075819-d191ee (c676998): 26 of 26.
- Pre-existing failures, not from the branch:
  - `consensus_worker` timed out in suite 090705-9ef515. It also timed out at main in
    091746-638010 (1 of 3, 180 s; the others took 168 s and 147 s).
  - `consensus_retained_state` hits a TSan stack overflow in 085702-ace127. Main fails the same
    way in 091753-223f76 (3 of 3, `_tlv_get_addr`).
  - Both tests link only `consensus::consensus` (CMakeLists.txt:66-69). No non-transport header
    includes `transport/`, and `git diff 771e1bb..6efc34b` touches only `include/consensus/transport/`,
    `test/transport/`, CMakeLists, CHANGELOG and ci.yml.
  - The TSan suite 090713-a40815 passes every other entry: deadline, peers, peers_fetch, tls, pipe.
- Probes for 6efc34b's test changes:
  - F12 passes-after 090729-506c4f: 450 ms stop in the drain, 5 of 5.
  - D2 090736-314495: x30 beside 16 hogs.
- F16 probe 090743-9828cb: TSan, stalls entry, x10. `reopened` timed out in 2 of 10 runs, after
  505 ms and 523 ms.

## What I checked myself

The scripts are in `.scratch/review-consensus-fetch-macos-correctness/r3/`.

| Probe | Job | Result |
|---|---|---|
| W1 `wake.sh`: the timer of an `async_deadline` wait expires during a 600 ms stop. In the first handler batch after the stop, something wakes the wait in one of three ways. *slot*: a cancellation-slot emit (the `\|\|` path, F11's fix). *same-pass*: `timer->cancel()` from a completion in the same reactor pass, as a reply that ends or `close()` does in `await_reply`. *posted*: `timer->cancel()` posted from another thread during the stop, as `resolved()` does with the resolver's answer. Exit 0 iff no wake waits out the extension | 261116-095049-326a05 | **exit 1**. slot: 3 of 3 `operation_aborted` at 0 ms. same-pass: 3 of 3 complete with **success after 592-598 ms**. posted: 3 of 3 complete with **success after 592-600 ms**. Each waited the whole extension |
| M1 `drain-mutant.sh`, the no-drain mutant: a failed fetch keeps its entry when its late reply ends, so the bulk lane always closes at the second deadline. It runs every untimed transport entry and `fetch-failures` x5, at CI flags. Exit 0 iff killed | 261116-095107-3f85ff | **survived**. 10 of 10 untimed entries pass. Every `fetch-failures` run passes, printing `drain: 500 ms, the bulk lane closed at its deadline`, `retried fetch: Socket is not connected in 0 ms (... lane closed in the drain, bounded)` |
| F16a `f16.sh tsan 20 0`: `reopened` instrumented. It prints when voter two's reader served it, the bytes and first byte at any fetch deadline, and the stall meanwhile | 261116-095058-dc6ca9 | 40 of 40 pass (stalls 20, untimed 20). Voter two serves the request at **+1-2 ms** every time. `reopened` takes 173-378 ms in the stalls entry (median ~285) and 158-245 ms in the untimed one, with 0 ms stalled |
| F16b `f16.sh tsan 15 12`: the same beside 12 hogs | 261116-100908-f28132 | **Reproduced, and it is a transfer, not a lost request.** `reopened` timed out in 9 of 15 stalls runs and 13 of 15 untimed runs, after 505-558 ms. Every time, voter two served at +2-4 ms and the first byte came at +20-65 ms. At the deadline **3.0-7.7 MiB of 8 had arrived** (~7-16 MiB/s), with 4 lanes and a stall measure of 0-117 ms. Of the other failures, the covered retried fetch at :1312 failed in 2 runs (5.6-5.7 MiB at its deadline). The stalls entry's silent check at :1326 failed in 5; I did not trace those, and the timing entry is off CI. 1 untimed run passed. Under contention, a TSan executor cannot carry 8 MiB in 500 ms, and the stall measure does not see starvation |

## Answers to the lead's questions

1. **F10 is fixed at the cause. F11 is fixed only for the `||` caller.**
   - F10: `async_deadline` owns a `shared_ptr` timer, and all three callers pass one:
     `timed` (session.h:270-279), `await_reply` (peers.h:338-341) and the lookup (peers.h:566-569).
     A completion that runs after its owner dropped the timer touches only memory the wait owns.
     `dropped_timer` under CI's ASan fails with the raw-pointer mutant (18b79c).
   - F11: the cause is that a wake landing after the expiry is queued is invisible to the
     extension step. The fix sees one kind of wake, a cancellation-slot emit (`||` in `timed`).
     The other three callers wake by `timer->cancel()`: `resolved()` and `abandon_lookups()`
     (peers.h:585, :600), and in `await_reply` the reply's end and `close()` (peers.h:403, :327).
     Once the expiry is queued, `cancel()` finds no pending wait. The step then sees success and
     re-arms for the whole lateness. W1 measures it: 6 of 6 waited ~595 ms of a 600 ms stop.
   - Reach of the gap:
     - The lookup is new in this quest (main's had no extension). After a stop during which the
       DNS answer arrived, the dial waits up to `resolve_deadline` (1 s) more, and the stop
       counts as a `lookup_timeout`.
     - `abandon_lookups()` at shutdown, and a reply that ends or a `close()` in the deadline's
       batch, keep `drained()` false for up to the lateness. That is at most `resolve_deadline`
       or `fetch_deadline` (30 s default).
     - The bound is the same as F11's, so F17 is blocking on the same grounds.
2. **F16 is a wall-clock artifact of TSan under CPU contention, not a defect.**
   - With the timeline printed (F16b), every timeout shows the reopened request served at
     +2-4 ms. Its reply was flowing (3.0-7.7 MiB of 8 at the deadline) on 4 established lanes.
     It is not a lost request, a missing redial, or a lane waiting on an extension.
   - The deadline fired on time, so the lateness was a few ms and the extension added that
     much. At ~7-16 MiB/s, an 8 MiB reply needs more than the fixed 500 ms.
   - Without hogs, 40 of 40 TSan runs pass (F16a). In those runs `reopened` takes 158-378 ms;
     the stalls entry is slower (median ~285 ms vs ~165 ms).
   - At CI flags, the untimed entry is clean beside 16 hogs (D2 314495, 30 of 30).
   - It is **not** confined to the `timing` entry. Under TSan beside 12 hogs, the untimed
     `fetch-failures` fails 14 of 15, and CI's linux-tsan runs that entry. The same starvation
     also fails the covered retried fetch (:1312), because the stall measure cannot see it.
     CI linux-tsan has passed it so far (43921692140, 43888069939).

3. **Yes, the drain-outcome assertion hides a real failure (F18).** The closed outcome is
   accepted whenever the drain took ≥ 350 ms. A transport that never drains a failed fetch's late
   reply therefore passes the whole untimed suite (M1). That regression costs the bulk lane on
   every fetch timeout. The scenario's own header (peers_test.cc:1240-1241, "the late reply
   drains into the failed fetch over the same lanes") is no longer asserted anywhere. At d322889
   the covered branch required it.
   `bulk_ready` itself hides nothing. It accepts only `not_connected` while it retries, fails on
   any other error, and requires a redial within 3 s. Polling a small fetch is the right probe,
   since `connections()` counts a closing lane. The never-early check (`drained >= deadline -
   margin`) does catch a premature close.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F10 | no | resolved | The wait owns its timer; all three callers pass a `shared_ptr`. | session.h:112-128; 18b79c (mutant killed under ASan); CI 43921692140 linux-asan-ubsan | — |
| F11 | no | resolved | For the `\|\|` caller, a slot cancel after the expiry is queued completes with `operation_aborted`, with no extension. Other wakes are under F17. | session.h:119-120; ac7334, 6213af; W1 slot 0 ms | — |
| F12 | no | resolved | `fetch-failures` reads the drain outcome. A lane the drain closes makes the retried fetch bounded, and the close is never early. The CI :1281 signature now passes. | 506c4f, 314495, CI 43921692140 | — (but see F18) |
| F13 | no | gap | Accepted per D100: no CI test exercises the Peers-level fetch-deadline extension. | ci.yml; CMakeLists.txt:128-132 | — |
| F14 | no | resolved | CHANGELOG now says "when its timer fires late" and names the stop before the deadline. | CHANGELOG.md:13-21 | — |
| F15 | no | resolved | The dial loop measures lateness from `deadline`. | peers.h:652 | — |
| F16 | no | gap | `reopened` timeouts are a slow transfer: TSan's executor starved by contention moves 8 MiB at ~7-16 MiB/s, under a fixed 500 ms deadline. Served at +2-4 ms, the reply flowing at the deadline, lanes up, no lateness. Not a transport defect. It also hits the untimed `--fetch` entry under TSan with hogs (14 of 15), so it is a flake risk for CI linux-tsan, not only for the timing entry. | 9828cb; F16a dc6ca9 (40 of 40 without hogs); F16b f28132 (3.0-7.7 MiB at the deadline) | Scale the scenario's transfer to the executor, one shape with the round-trip rule (64/256 ms by measured MiB/s). For example, size the file or the `fetch_deadline` from a measured speed, or use a smaller file in the must-succeed fetches (`retried`, `reopened`). Or accept it under D100 while CI linux-tsan stays green |
| F17 | yes | defect | F11's cause remains for wakes by `timer->cancel()`. When the lookup's answer (`resolved`), `abandon_lookups`, a reply's end or `close()` lands in the first batch after the timer fired late, the step sees success and waits out the whole extension. The lookup then delays its dial by up to `resolve_deadline` and counts a timeout. `drained()` stays false for up to the lateness (≤ `fetch_deadline`). New relative to main: the lookup and reply waits had no extension there. | W1 326a05 (same-pass 592-598 ms, posted 592-600 ms, slot 0 ms); peers.h:327, :403, :585, :600; session.h:116-123 | One wake mechanism the step can see. Options: (a) owners wake through the wait's cancellation slot (`route.waiting` and `Fetching` hold a `cancellation_signal`), one shape with `\|\|`, and the lookup then reads `operation_aborted`; (b) owners wake with `expires_at(time_point::max())`, so `owed` returns zero; (c) the caller passes a "still waiting" predicate checked before extending. Test: W1's same-pass and posted cases in `deadline_test` (complete within ~20 ms of the stop). They must fail at 6efc34b |
| F18 | yes | gap | The drain-outcome assertion accepts a lane closed at the second deadline whenever the drain took ≥ 350 ms. So nothing asserts that a late reply drains over the same lanes. The no-drain mutant (every failed fetch costs its bulk lane) survives every untimed entry. | M1 3f85ff (survived; drain 500-507 ms, lane closed, 5 of 5 pass); peers_test.cc:1240-1241, :1296-1298, :1304-1313 | Assert the kept outcome where it does not depend on load. For example, a second frozen fetch with a small late reply (e.g. 64 KiB, which drains in milliseconds even under TSan) must keep the lanes and let the retried fetch succeed. The 8 MiB case keeps either outcome. Test: M1 must be killed by `ctest -LE timing` |
| F19 | no | nit | `completed_read_after_stop` runs on CI (pipe entry) with a wall-clock bound of 150 ms after a 1000 ms sleep. A runner stop of more than 150 ms after `resumed` fails it, the D100 class. It passed on all platforms in 43921692140. | tls_test.cc:342-427 | Keep, or widen the bound toward the 400 ms extension (it still separates the 400 ms defect) |

F1-F5 resolved earlier; F6-F9 as in the earlier rounds.

## Not run

- A Peers-level reproduction of F17: a resolver answering during a stop, or a reply ending in
  the deadline's batch. W1 reproduces the helper's contract with the same wake orderings
  `resolved()` and `received()` produce. The asio scheduler queues the reactor's expired timers
  after a handler posted during the stop.
- M1 against the `timing` entry. CI configures that entry out (D100), so it cannot be what
  kills the mutant.
- I did not rerun the F10/F11 mutants at 6efc34b. The code under test is byte-identical to c676998.
- The 5 silent-check failures (:1326) in the stalls entry under TSan beside 12 hogs (F16b). That
  entry is `timing` and off CI. I read them as the same starvation, but did not trace them.
