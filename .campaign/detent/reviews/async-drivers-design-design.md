# async-drivers-design review: design

Commit: Detent:128df91045cf9c434927a87831e22ec76107723f
Verdict: CHANGES
Date: 2026-11-09

APPROVE: lands as is, at this commit's code. CHANGES: lands after each blocking finding is fixed
and confirmed by a job at the fixed commit. REJECT: the approach is wrong; the quest is re-planned.
An APPROVE lists no blocking finding; a CHANGES lists at least one.

The shape is right: one driver frame per loop over the existing machines, one wheel per loop as
the only timer, `task::deadline` on the token for triage #18, a blocking entry over the same
task, and a build plan that deletes the pump last. What is missing is the threading contract
that makes those pieces composable (who may touch a wheel, a group, a turn log, a waiter, from
which thread) and the transport's share of an idle group. Those are the decisions the build
quests cannot settle locally without diverging.

## Evidence I relied on

- 261109-230223-1f63c8 (strict, Detent:87df68b, exit 0): idle bytes, descriptors and threads.
  Every number in the bytes tables matches the log; 170,902 - 6,262 = 164,640 = 163,840 + 800
  (the channel block plus its object), so the 7.9 KB D9 column (6,262 + 800 + 800 + 40) is a
  correct sum of measured parts, labelled "designed".
- 261109-230225-963cb8 (exclusive, Detent:87df68b, exit 0): frames, resumes, wheel arm/re-arm,
  `run_one`, idle CPU. Every cited number matches; 26.7 us x 10^6 = 26.7 cores, and
  (1.540 - 0.027) x 8,760 h = 13,254 core-hours, as stated.
- 261109-232231-bced6c (light, Detent:646cfb7, exit 0): `SnapshotChannel` 800 B, `Channel`
  66,088 B, input array 65,688 B, 2 peers 132,176 requested / 163,840 usable.

These ran other code than 128df91 (bench unchanged since, per the journal; `campaign status`
asks for reruns, which is A7's business, not this review's).

## What I checked myself

- Read the design, the bench (`wheel.hh`, `driver_cost.cc` idle section) and the library code
  it rests on: task:bdafa5f `task.hh` (`deadline`, `cancel_callback`, `sync_wait`,
  `inline_scheduler`), task's `docs/mechanisms/cancellation.md`, journal:e5f6ff1
  `turn_driver.h` and `store.h` (shared backend), consensus:ab05507 `transport/peers.h`,
  `core.h:1196`, Detent `service.cc:2594-2606`.
- Probe 261109-233203-d822ec, `.scratch/review-async-drivers-design-design/run.sh` (home
  checkout) over `wheel_reentry.cc`: two entries due in one slot, the first fire cancels
  (mode `cancel`) or re-arms (mode `rearm`) the other, as a fired parent deadline does to a
  linked child's. Result (exit 0): mode `cancel` dies with SIGSEGV (exit 139); mode `rearm`
  survives here (the re-armed entry is not due, so the walk into its new slot fires nothing).

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | yes | gap | The wheel cannot yet serve `task::deadline` as specified. (1) `deadline<S>` takes `std::chrono::steady_clock::time_point` and `start_timer(at, fire)` (`task.hh:1118-1136`), the clock D4 rejects for Kronuz/scheduler because the simulator cannot drive it; D4/D5 never say how a wheel on the manual clock of `inline_scheduler::now()` accepts a `steady_clock` deadline. (2) task's contract is that a deadline "may be destroyed on any thread" (`cancellation.md`, deadline), which holds for an Asio timer; the wheel is owner-thread only (`wheel.hh:1-3`), and `~deadline` unlinking a frame-resident entry from another thread corrupts the wheel, while posting the unlink to the owner leaves it pointing into a destroyed frame. D8's cross-loop fan-out makes this the normal case, not an edge. (3) `expire` runs `fire`, and `fire` runs cancel callbacks synchronously, which may destroy linked child deadlines, so `expire` must tolerate cancel and re-arm of any entry during iteration; the prototype does not (F2). (4) Deadlines beyond 4,096 ms: the prototype keeps them in their slot and wakes the reactor once per round; the design states neither that nor a hierarchical level, and request deadlines are often longer. | `task.hh:1113-1146`; task `docs/mechanisms/cancellation.md` "deadline"; `wheel.hh:44-58`; D4, D5, D8 | State in D4: the wheel's clock type and how `deadline` reaches it (a clock parameter on `deadline`, or the wheel converting); the thread rule (arm, cancel and destroy only on the wheel's loop, Debug-asserted with `runs_here()`, and a child on another loop arms on its own loop's wheel), or a cross-thread cancel that does not leave a dangling entry; `expire` re-entrancy (detach the due list before firing); long-deadline behaviour. Add each as a `task-wheel` (quest 1) test criterion. |
| F2 | no | defect | The prototype `expire` dereferences null when a fire cancels the next entry of its slot: `following` is saved, the fire unlinks it (`pprev = nullptr`), and the loop then calls `unlink` on it because its deadline is due. A re-arm instead makes the loop continue down another slot's list (harmless in the probe, where nothing there is due). The measured numbers are unaffected (the bench never cancels from a fire). | probe 261109-233203-d822ec (`cancel`: SIGSEGV, exit 139); `wheel.hh:50-54`, `:81-85` | Fix in quest 1's real wheel (F1 item 3); the probe is its regression test. |
| F3 | yes | gap | D8 does not say who owns a waiter, so a token firing during completion has a use-after-free path. The cancel callback "runs on the firing loop's thread and only marks the waiter and posts its resume to the waiter's scheduler"; the group's loop also resumes that waiter when the proposal places or the IO completes. Nothing states (a) a single claim so exactly one of completion and cancellation resumes the frame, and (b) that the waiter is unlinked from the group's pending list on the group's loop before the caller's frame (which holds the `cancel_callback` node and the result slot) can be destroyed. A caller on loop B, a group on loop A and a deadline firing on loop C, all allowed by D8, otherwise leave A's list pointing at B's freed frame; a later `Placement` writes into it. | D8 last paragraph; `task.hh` `cancel_callback` (unlink waits only for a running callback, not for the group's reference) | Specify the waiter protocol in D8: an atomic claim word in the waiter; the cancel callback posts to the group's driver loop (not the caller's), which unlinks the waiter and then posts the caller's resume; completion claims first or skips. Add a deterministic test per interleaving to quest 4's "deadline table tests". |
| F4 | yes | defect | D8's table conflicts with its own rule "never undoes a state-machine step" for IO the machine owns. "An IO primitive not yet queued: not issued" is right for a caller-owned operation, but a log append Core requested for an accepted proposal, or a journal append already staged into the core's turn (`TurnDriver::stage`, which has no unstage, `turn_driver.h:52-56`), must still be written: skipping it stalls Core or leaves a sequence gap. D2's `completion_ref::run(op, cancel_token)` does not say which token machine-owned IO carries. | D8 table rows 4-5; D2 `completion_ref`; `turn_driver.h:52-75` | State that machine-owned IO (Core's storage actions, staged turn records) runs under the loop's shutdown token only, never a caller's; add the rows "staged in a turn: written, caller released" and "a read awaiting apply" and the `create`/`recover`/`close` outcomes. |
| F5 | yes | gap | Thread affinity of the Driver API and the blocking entry is unstated. (1) D2's `propose`, `read`, `receive`, `applied` touch a group's admission and ready list; D8 assumes callers on other loops, but nothing says the call hops to the group's loop first (`schedule_on`) and resumes the caller on its own. (2) D5 says `sync_wait(store.append(..., inline_io))` "never suspends": false for the shared backend the cost table uses, where `Store` stages into the core's `TurnDriver` and completes only at that turn's barrier (`store.h:328-333`, `turn_driver.h:24-26`); with no `TurnDriver::run` and no scheduler, `sync_wait` blocks forever. (3) A WorkPool device chore running D5's entry on a group, or a turn log, that a reactor's Driver also owns is a data race; D5 does not restrict it to groups no loop has open. | D2, D5; `task.hh:744-792` (`sync_wait` blocks on a cv; its own comment warns against the needed thread); journal `store.h:328-339`, `turn_driver.h` | State in D2 that every Driver call runs on the group's loop and how it gets there; in D5 that the blocking entry owns its groups and their core's `TurnDriver` exclusively (Debug assert) and commits turns inline (`TurnDriver::commit` through `InlineCompletion`); add a shared-backend `sync_wait` test to quest 3. |
| F6 | yes | gap | The idle cost omits the transport, which is per group. `consensus::Peers` is constructed per group: its own TLS context, a bound listening acceptor, and a Control and a Bulk connection per peer, each with a suspended read loop and retry timers (`transport/peers.h:53-91`). A three-voter member therefore holds about five more descriptors, TLS state and live `asio::awaitable` frames while idle, none of it in the 7.9 KB / 3 fds / 0 frames row, the 7.9 GB million-group row, or the 7b note to Xapiand. D2 moves the transport onto task but never says it becomes shared per loop. | `consensus/transport/peers.h:53-91` at ab05507; Cost tables; journal A8 note item 4 | Either decide a per-loop (or per-process) transport multiplexing groups over one connection pair per peer host, and cost it, or measure and add today's per-group `Peers` to the tables and to quest 4's idle-cost criterion. Either way, say it before the note goes to Xapiand. |
| F7 | no | gap | "1.54 cores today" is 1,000 one-millisecond timers on one `io_context` (`driver_cost.cc:271-284`), not Detent's layout, which the same document counts as 2,000 threads (one runtime per group). At its own 1-group row (17,670 us) Detent today costs about 17.7 cores per 1,000 groups, so the savings are understated by about 11x, not overstated; the baseline is mislabelled. | job 963cb8; Cost "Idle CPU"; `driver_cost.cc:266-284` | Name the baseline ("a shared 1 ms loop"), or add the per-runtime row and restate the core-hours. |
| F8 | no | nit | "About 27 cores" for a million leaders extrapolates a 1,000-group run whose state is cache-resident and a `run_one` measured on one hot group; at a million groups (6.5 GB) every step is cold. It is a lower bound, and the conclusion (quiescence, Q4, is required for CPU) stands; say "at least". | job 963cb8; Limits | Word it as a lower bound. |
| F9 | no | nit | Build plan: quest 4 removes `asio::awaitable` from consensus's `include/`, so it replaces the transport rather than adding "beside the existing APIs"; Detent's `AuthorityPeers` (`peers.hh:9`) keeps working only through its pin, and quest 5 takes the pin bump and the transport migration together. Each state still works; the text should say so. | Build plan; `src/detent_authority/peers.hh:9` | One sentence in the build plan. |
| F10 | no | nit | D5's "advancing the wheel by the caller's clock when the queue empties first" leaves open whether the blocking entry sleeps real time to `wheel.next()` (a single voter's first election) or jumps a manual clock. | D5 | Say which. |

Blocking: yes or no. Kind: defect (wrong behavior), gap (unproven claim or missing test), nit. A
finding that would remove or narrow user-visible behavior offers options, including keeping it,
and is never blocking.

## Not run

- No rerun of the three cost jobs at 128df91, and no Linux numbers (the design defers both).
- No measurement of today's per-group `Peers` cost (F6); its size is read from code, not measured.
- No interleaving model of D8 (F3); the race is read from the stated protocol.
- A8 (the Xapiand lead's review) is outside this lens.
