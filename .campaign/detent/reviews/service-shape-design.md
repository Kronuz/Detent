# service-shape review: design

Commit: task:f5d7580 (`docs/design/service-shape.md`)
Verdict: APPROVE
Date: 2026-11-12

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

Re-review of the fix task:5f07955..ed76e4f only. The first review (at task:5f07955) found F1-F13;
the Xapiand fit review (`~/code/Kronuz/Xapiand/.campaign/xapiand/reviews/detent-service-shape-fit.md`)
found B1, B2, G1-G7. Every one of those I was asked to check is answered in the text, and the new
`stop_signals` does what F1 asked. Two gaps remain in what `serve` specifies: where `flush()` runs
outside the third signal (F14), and a stop that arrives while a `start()` is still waiting (F15).
Both are a sentence or a parameter in the design; neither changes the shape.

Final round, task:ed76e4f..f5d7580: the concept's `start(cancel_token stop)` and step 1's
`start(signals.stop())` close F15. Step 5 closes F14: every path flushes each started service in
reverse before `serve` returns, and later drains still run after a throw. The tests line names
both. With only nits left, APPROVE.

## Evidence I relied on

- Job 261112-101111-197ad6 (the contributor's, exit 0; source `.scratch/service-shape-signals/`):
  I read the probe and its log. Its handler matches D6 and is async-signal-safe (saves `errno`, a
  lock-free atomic `fetch_add`, one `write`; `stop_signals_probe.cc:28-35`). The third SIGTERM
  kills with the loop idle, wedged (0.61 s, before it unwedges) and past `io.run()`; with a 1 s
  flush it kills at 1.41 s after the flush's marker; two signals do not kill; a burst of three
  killed 2 of 5 (POSIX merging, in Limits :232-233). Not rerun: the log and source agree.
- Docs check 261112-102139-05709b at task:ed76e4f: formatting only.

## What I checked myself

- `run_v2.py` + `v2_probe.cc` in `.scratch/review-service-shape-design/`, job 261112-102631-ea340f
  (exit 0, at task:ed76e4f): the contributor's model with two modes it lacked. Loop wedged 3 s and
  a 1 s flush: killed by the third SIGTERM at 1.41 s with the marker written, before the loop
  unwedged. A flush that hangs 60 s: still alive 2 s after a fourth and fifth SIGTERM, ended only
  by SIGKILL, as Limits :231 states.
- Detent's empty `flush()` (risk 2), at Detent:833c118c and the cited repos: journal README
  "Every append returns only after its barriers" (journal:9c87929); the consensus pump appends
  before it steps `Persisted` (consensus:3d988d1 README :68-78); the authority's write replies come
  from the node's result (`service.cc:1868-1872`), the rest are `outcome_unknown` or `stopping`
  (`:2699-2857`); `agent_store.cc:494-506` writes, syncs, renames, then syncs the directory. The
  authority runs `async_storage` (`service.cc:1507`), which changes when an append completes, not
  when a reply is sent. Nothing Detent acknowledges waits on a later write, so the claim holds.
- D8 against deadlock (risk 3): every `stop()` is "at once" (:96) and refuses only outside work;
  "whatever touches admitted work belongs in a drain" (:170-171). No mapped `stop()` ends a loop
  or blocks: HTTP's closes listeners, the authority's answers held polls and refuses commands
  while its runtime keeps applying, the agent's closes the coordinator, and autocommit's debounce
  ends in its own drain (:196). A drain needing a loop drained earlier is today's order in Xapiand
  (`manager.cc:1090-1131`). I found no deadlock; F18 asks the text to say which `stop()`s are empty.
- The 5 s grace (risk 4): cancel at 30 s, abandon at 35 s, durable work after, unbounded. That
  matches D80's "finish inside one 35 s bound", "the 2nd cancels requests" and "kills only after
  that durable flush". The one gap is F19.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | no | resolved | The third signal kills a wedged process, and kills after teardown starts. | D6 :216; jobs 261112-101111-197ad6, 261112-102631-ea340f | - |
| F2 | no | resolved | `flush()` is outside the bound and the hurry; `stop_bound` replaces the time argument, and D7 argues that HTTP needs no reserve once Stop 3 is outside the bound. | :87-98, :195, D7, D9 | - |
| F3 | no | resolved | D13 and order step 3; the authority, relay and agent rows remapped. See F17 for one token that has no name. | :160-162, :42-45 | - |
| F4 | no | resolved, but see F15 | `ready`, `signals.fail()` and `served.failure` exist, and the agent, relay and detentd rows say where each goes. | :105-120, :43-45 | - |
| F5 | no | resolved | D8: every `stop()` comes before any drain, and the authority's `stop()` answers held polls 503. | :167-171, D8 | - |
| F6-F13 | no | resolved | D12 keeps the next tick after a throw. The Signals row covers stdin, the "you insisted" escalation and SA_RESTART. The agent's 20 ms read is stated, `teardown` is gone, D14 gives one time shape, `main` assigns `result` and catches a throw, D6 drops `signal_set`, and step 1 takes the D10 test. | :74-77, :202, :44, :143, D14, :133-134, :253-254 | - |
| B1, B2, G2, G4, G5, G7 | no | resolved | B1: HTTP's drain runs its own phases 2 and 3 on a D11 thread. B2: the WAL is `flush()`. G2: the registry drain cancels its timer entries. G4: the registry's tasks are its `group`. G5: SA_RESTART, the signal name, stdin and the log flush before `_Exit`. G7: autocommit has its own source. | :195-202, :183, :139-141 | - |
| F14 | no | resolved at task:f5d7580 (was blocking) | `flush()` "runs on every path" (D9), but the order that `serve` follows names only two places it runs: the third signal, and the services after an unfinished drain (:156, :163-164). The order does not say that `serve` flushes on the drained path, after a start that throws, or after a drain that throws, or whether later drains still run after a throw. On Xapiand's normal stop the WAL join (Stops 4-5, :195) is only "its `flush()`"; if the registry's drain throws, nothing says the WAL is joined. `main`'s comment "every flush() has returned" sits on the unfinished branch only (:138). | :156-165, :138, D9 :219 | One sentence in step 4. After the drains, on every path (drained, start failure, a drain that throws, unfinished), `serve` runs every started service's `flush()` in reverse on a D11 thread. Also say whether a throwing drain stops later drains (recommend: they still run). Step 3 test: a start that throws and a drain that throws each still run every flush. |
| F15 | no | resolved at task:f5d7580 (was blocking) | `start()` takes no token (:95), so a first or second signal while a start waits does nothing until the start returns. detentd's lone-quorum wait (:43) has no timeout; today the first signal ends it (`detentd/main.cc:198`, `:205`). The agent's preparation (:44) runs to its timeout. Under this design only the third signal (flush, then kill) ends either. "A stop before `ready` when detached stays a failure exit" (:43) has no mechanism for the start to see the stop. | :95, :156-157, :43-44; Detent:833c118c `src/detentd/main.cc:194-205` | `start(cancel_token stop)` (`serve` passes `signals.stop()`). A start that sees the stop returns early or throws; `serve` then skips later starts and `ready`, and stops and drains the started ones. detentd's start throws "stopped before startup completed" when detached. Step 4 test: SIGTERM during an endless lone-quorum wait exits at once, a failure when detached. |
| F16 | no | nit | The third signal runs `flush()` on the signal thread while `serve` may still be draining or flushing (:153). "Idempotent" (:98) does not say it is safe concurrently with itself and with a drain, or that it needs no loop (the loop may be wedged). For "durable on return" to cover the kill, a flush should first refuse new WAL appends, so that no request is acknowledged after the flush starts. | :98, :153 | Add "concurrent-safe, needs no loop, refuses further appends first" to the concept's comment. |
| F17 | no | nit | The signal pipe's write end should be `O_NONBLOCK \| O_CLOEXEC`, so a full pipe never blocks the handler. A hung flush ignores a fourth signal (job 261112-102631-ea340f). A handler that sets `SIG_DFL` and raises at count 4 (both async-signal-safe) would end it without SIGKILL. The lead's call: Limits :231 accepts SIGKILL as it stands. | D6 :216; Limits :231 | State the flags in D6; optionally add the fourth-signal escape. |
| F18 | no | nit | The authority row's `sleep_until(at, stop)` (:42) runs during HTTP's drain. It must be the authority's own source, requested by its drain, not `signals.stop()`, or D13 breaks. The registry's and autocommit's `stop()` (:195-196) are not named; both are presumably empty. | :42, :195-196 | Name the token; say that those two `stop()`s do nothing. |
| F19 | no | nit | "Durable work still completes" (:22-23) overstates one path. When cancelled requests outlast the grace, the later services only `flush()` (:163-164), so Xapiand's index commits are skipped and the WAL replay covers them. That is today's abandoned path, and it is sound, but D80's text lists index commits as durable work. | :22-24, :163-165; PLAN D80 | Say it in the D80 section or in Limits. |
| F20 | no | nit | An orchestrator sends one SIGTERM, then SIGKILL after its grace (Kubernetes defaults to 30 s), never a third signal. So the WAL flush runs only if that grace exceeds 35 s plus the durable work. | Limits | Add a Limits line. |
| F21 | no | nit | D80 names Detent's "generation publication, journal fsync" as durable work for the third signal. The design makes Detent's `flush()` empty because both finish before acknowledgement; this review's code read confirms it (above). The owner's call: keep it as is (recommended), or have the flush await an in-flight append. | PLAN D80; :30-35 | Options as stated. |

| F22 | no | nit | Step 5 flushes each *started* service. A service whose `start()` throws, or one a stop ended mid-start, may already hold acknowledged writes, and the text does not say whether its flush runs. The third signal covers it, because step 1 registers every flush. | task:f5d7580 :156-170 | Say "each service whose start began". |

## Not run

- No Linux run of the signal model (macOS only, both jobs).
- No build of `serve` or any service; F14 and F15 are readings of the design text against
  `detentd/main.cc` and D9.
- Detent's relay and agent HTTP paths were not traced for D8 beyond the design's rows.
