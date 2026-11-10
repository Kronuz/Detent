# async-drivers-design review: design

Commit: Detent:73e96d686701841b771bf0213a2583455b4f7489
Verdict: CHANGES
Date: 2026-11-10

APPROVE: lands as is, at this commit's code. CHANGES: lands after each blocking finding is fixed
and confirmed by a job at the fixed commit. REJECT: the approach is wrong; the quest is re-planned.
An APPROVE lists no blocking finding; a CHANGES lists at least one.

Re-review of fix round 1 (Detent:128df91..73e96d6) for F1-F10 of the review at 128df91, plus the
new D10, Q7 and quest 5. The fixes are real: the wheel's detached-list `expire`, the clock and
thread rules, the claim-word waiter, machine-owned IO under the shutdown token, Driver calls on
the group's loop, the inline turn commit, the measured `Peers` and the corrected baseline all
hold up. What remains is narrower: one contradiction in D4 about `asio_scheduler`, a blind spot
in `waiter_test` that a mutant survives, the blocking entry's termination, and D10's routing and
read-side isolation, which the new mechanism leaves unstated.

## Evidence I relied on

All at Detent:35ed97d (the bench is unchanged since; `git diff 35ed97d..73e96d6 --
docs/design/async-drivers-bench` is empty), pinned consensus:ab05507, journal:e5f6ff1,
completion:560c288, task:bdafa5f, exit 0:

- 261110-000902-6240b4 `wheel_test` (assertions on, `-UNDEBUG`): reentry cancel, rearm, self,
  arm-due; model 20,000 rounds, 12,065 fires, 4,099 mutations from fires; long 4,095-300,000 ms
  with `floor(d / 4,096)` early wakes and 999,877 ns late; manual 5,000 ms simulated in 34 us;
  chain both orders; loops 2,000 x 8.
- 261110-000905-c1d3b9 (ASan+UBSan) and 261110-000906-2730b5 (TSan): the same `wheel_test`
  plus `waiter_test all`, clean. So the sanitizer jobs do run re-entrant expire, cross-thread
  child deadlines created and destroyed on B while A fires, the manual clock and far deadlines
  (risk 2).
- 261110-000910-2d7f1a: my earlier probe, cancel and rearm both exit 0 (it crashed at 128df91).
- 261110-000904-28d8d8: `explore` 17 / 2,910 / 938,978 schedules clean; naive control 12/19 and
  2,308/2,860 schedules reach a freed frame; threads 3,000 rounds.
- 261110-000908-5a484f: a staged shared-backend append is neither done nor in flight; one inline
  `TurnDriver::commit` completes it, sequence 0 to 1, 0 threads added.
- 261110-000909-400d3c (strict): `Peers` 511,947 B / 5 fds (3 voters, 4 connections), 982,918 B /
  9 fds (5 voters, 8): slope 117,743 B per connection, intercept 40,975 B, so `41 KB + 2H x
  118 KB` and `1 + 2H` fit both points.
- 261110-000911-f87d0f (exclusive): 1 ms loop 16,305.67 us/s at 786 wakes, `run_one` 1,547.9 ns,
  so 16,306 + 1,217 = 17,523 us per runtime; (17.523 - 0.026) x 8,760 = 153,300 core-hours,
  shared loop (1.519 - 0.026) x 8,760 = 13,100. Every cited number matches the log.
- Bytes: 261109-230223-1f63c8 and 261109-232742-fdfe40 (6,262; 170,902; 176,208; 6,880; 10,992).
  170,902 + 511,947 = 683 KB, 3 + 5 = 8 fds; 6,262 + 1,600 + 48 + 16 = 7,926 B (risk 5: backed).

## What I checked myself

- Read waiter.hh, waiter_test.cc, wheel.hh, wheel_test.cc, loops.hh, blocking_entry.cc, the
  design's D2-D10, Cost and Build plan, task:bdafa5f `task.hh` (`deadline`, `post_call`,
  `inline_scheduler`, `event`) and `task_asio.hh:125-136`, consensus:ab05507 `core.h`,
  `transport/peers.h`, `transport/hello.h`, and the current `task::deadline` users in Detent and
  Xapiand.
- I reasoned through the claim-word protocol for completion versus cancellation on three loops:
  only the claim's winner posts a resume, the waiter leaves the group's list only on the group's
  loop, and the cancel hop keeps the frame alive until it has unlinked. It gives exactly one
  resume (risk 1, protocol: yes).
- Probe 261110-002555-485959 (ASan+UBSan) and 261110-002557-76c931 (TSan),
  `.scratch/review-async-drivers-design-design/waiter_mutants.sh` (home checkout): three
  ordering mutants of waiter.hh against `explore 2` and `threads 20000`.

  | Mutant | explore | threads ASan | threads TSan |
  |---|---|---|---|
  | original | pass | pass | pass |
  | cancel ignores a lost claim | killed (UAF; `resumes == 1`) | killed | killed |
  | the hop posts the resume, then unlinks | **survives** | **survives** | **survives** |
  | completion posts the resume, then unlinks | **survives** | killed (UAF in `complete`) | survives |

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | no | resolved | Clock (`deadline<S>` on `S::clock`), the thread rule, detached-list `expire` and far deadlines are stated in D4 and are quest 1 criteria; the bench tests each, also under sanitizers. Its one remaining contradiction is F13. | 6240b4, c1d3b9, 2730b5; D4 | - |
| F2 | no | resolved | `expire` detaches due entries before firing; the probe survives both modes. | 2d7f1a; `wheel.hh:48-82` | - |
| F3 | no | resolved | D8 states the claim word, the hop to the group's loop, and unlink before resume. The protocol is right; its test has a blind spot (F14). | D8; `waiter.hh:51-69`; 28d8d8 | - |
| F4 | no | resolved | Machine-owned IO runs under the loop's shutdown token; the staged, read, create/recover and close rows are in the table. The blocking path's residue is F11. | D8 table | - |
| F5 | no | resolved | Driver calls run on the group's loop (D2); the blocking entry commits turns inline (5a484f) and owns its groups. Its termination is F11. | D2, D5; 5a484f | - |
| F6 | no | resolved | Today's `Peers` is measured and costed, and D10 decides the shared transport. Its open points are F12. | 400d3c; D10, Cost | - |
| F7 | no | resolved | The baseline is Detent's runtime per group, 17.5 cores per 1,000; the shared loop is its own row; the core-hours are restated correctly. | f87d0f; Cost | - |
| F8 | no | resolved | "at least 26 cores", with the reason. | Limits | - |
| F9 | no | resolved | The build plan says 3-5 add beside today's APIs and 6 takes the pin bump with `AuthorityPeers`. | Build plan | - |
| F10 | no | resolved | D5 step 2: a manual clock jumps, a steady clock sleeps. | D5 | - |
| F11 | yes | gap | The blocking entry still hangs, and can return before a staged write. (1) Step 3 throws only when the wheel is empty, but a consensus group always holds an entry: a leader re-arms `heartbeat_deadline_` on every drive, even a single voter, and a learner or follower resets `election_deadline_` (`core.h:1054-1066`, `:1003`; quiescence is Q4). So `consensus::run` over a lost wake, or over an operation that needs a peer, sleeps from timer to timer forever on a steady clock, or spins on a manual one, and never reaches the throw that D5, Limits and the Xapiand note promise. (2) The loop commits turns only "whenever the queue empties before the task finishes". A caller cancelled after staging (D8 row 2: "the record is written") lets the task finish, and the entry returns with the record still staged, then gives up ownership of the turn log. | D5 steps 1-3; D8 row 2; `core.h:1003`, `:1054-1066` | (1) Say what bounds it: refuse operations that need a peer at the call (a multi-voter `propose`/`read`), and count group timers as progress only while the awaited operation can still use them (a single voter's first election), otherwise throw; or require a deadline on the entry's task. (2) Before returning or throwing, drain: commit staged turns and finish machine-owned IO. Add both to quests 3 and 4's blocking-entry criteria. |
| F12 | yes | gap | D10 does not say how a frame reaches the loop that owns its group, and its cost and its rejection of per-process sharing depend on that. (1) Routing: each host places its groups on its own loops, so group X can sit on loop 1 of host A and loop 3 of host B. A's loop-1 connection ends on some loop of B, and unless that is loop 3 every frame hops threads, which is the reason D10 gives for rejecting per-process sharing. Avoiding the hop needs either one placement rule on both ends (the same loop count and loop = f(group)) or connections per (local loop, remote loop) pair, which makes H the number of remote loops, not hosts: `41 KB + 2 x H x L_remote x 118 KB` per loop. At 16 loops per host that is about 3.8 MB per peer host per loop instead of 236 KB. (2) Read side: today's reader holds one decoded input per connection and waits for that group's admission before it reads again (`peers.h:368-390`, `:166-205`). On a connection shared by a loop's groups, one group whose admission is full stops every group's frames from that host, heartbeats included, so one slow group can trigger elections in the others. D10 covers only the write side (round-robin, R19). | D10; Cost "41 KB + 2H x 118 KB"; consensus:ab05507 `transport/peers.h:166-205`, `:368-390` | (1) Pick the routing rule (shared placement, connections per loop pair with the cost restated, or an accepted receive hop) and restate the D10 cost and the per-process rejection to match. (2) State that the reader never waits on one group: per-group input slots and budgets, and a frame for a group whose admission is full is dropped (Raft tolerates loss) or parked per group. Add a quest 5 criterion: a group with full admission does not delay another group's heartbeat on the same connection. |
| F13 | yes | defect | D4 contradicts the build plan on `asio_scheduler`. D4 says a wheel-backed deadline is destroyed only on its loop, "narrowing task's 'any thread' ..., which still holds for `asio_scheduler`'s own timers". Quest 1's criteria include "`asio_scheduler::start_timer` on the wheel". If quest 1 does that, every `deadline<asio_scheduler>` is wheel-backed, task's documented and tested contract ("destroyed on any thread", `cancellation.md:65`, `:111`) changes for every user, and Release builds have no check. Users today: Detent `owner_loop.hh:72` (it resumes on the owner through `event`'s `resume_on`, so it looks safe), `https_client.hh:83`, `detent_delivery/client.cc:291`. A frame holding a deadline that hops loops (`schedule_on`) is the case that breaks. | D4 "Threads"; Build plan quest 1; task:bdafa5f `task.hh:1114-1146`, `task_asio.hh:125-136` | Choose one: (a) `asio_scheduler::start_timer` stays an Asio timer (any thread), and only the reactor loop's wheel scheduler is wheel-backed; or (b) it moves onto the wheel, D4 says the narrowing applies to `asio_scheduler`, quest 1 updates `cancellation.md` and the any-thread test, and audits the users above, with a hopped frame as a Debug-asserted case. Either way, make quest 1's criterion match. |
| F14 | yes | gap | `waiter_test` cannot see the ordering D8 rests on. D8's argument is that the claim's winner "posts after the group's last touch". The explorer runs each posted call atomically, so a post followed by a touch of the waiter in the same call looks the same as the correct order. The mutant "the hop posts the resume, then unlinks" survives `explore`, and `threads` under ASan and TSan for 20,000 rounds; "completion posts the resume, then unlinks" survives `explore` and TSan, and only ASan's threaded run catches it. The design calls the exploration "every interleaving", and quest 4's criterion is "as `waiter_test`", so the blind spot carries over. | probes 485959, 76c931; `waiter_test.cc:185-228`; `loops.hh:43-56` | Make every cross-loop post a scheduling point in the explorer (the posted call can run before the poster's next statement), or poison the waiter in the model once its resume is posted and check that nothing touches it. Show both mutants killed by `explore`. |
| N1 | no | gap | Q7/D10's per-frame check, "the node votes in the frame's group", rejects learners and joiners, which Core has (`core.h:1003`, `Membership` in `types.h:80`). The group-open exchange should also carry what today's hello checks (cluster, group, epoch, profile; `hello.h:51`), and Q7(a)'s one pin per node per loop has to reconcile today's per-group `PeerOptions::pins`. | D10, Q7 | For quest 5's security review: a member of the membership in force, with the hello's checks moved to the group-open exchange. |
| N2 | no | nit | The design cites idle bytes to 261110-000912-e9dff1, which was still running during this review. Its finished rows match 1f63c8 and fdfe40. | job state | A7's rerun; cite it once it finishes. |
| N3 | no | nit | The Idle CPU table's "17,523 us (1,000 runtimes)" is one measured runtime multiplied by 1,000, not a 1,000-runtime run (2,000 threads may cost more). | Cost, Idle CPU | Label it as derived. |
| N4 | no | nit | D4 says `inline_scheduler` "declares its manual clock". At task:bdafa5f it has `now()` and `advance()` but no `clock` type (`task.hh:311-316`); quest 1 adds it. | D4 | Say "will declare". |
| N5 | no | nit | `post_call` is "dropped when closed" (`task.hh:167`). D8 covers the group's loop at shutdown, but a caller's loop that closes before a pending resume loses it. | D8 | One sentence on shutdown order: callers' loops outlive their pending resumes. |

Blocking: yes or no. Kind: defect (wrong behavior), gap (unproven claim or missing test), nit. A
finding that would remove or narrow user-visible behavior offers options, including keeping it,
and is never blocking.

## Not run

- No rerun of the cost jobs at 73e96d6 (A7's; the bench is unchanged since 35ed97d), and no Linux
  numbers.
- D10's per-loop transport is not prototyped. F12's 3.8 MB figure is arithmetic on 400d3c's
  per-connection cost, not a measurement.
- No probe of F11 or F13: both are read from the design text and the cited `core.h`, `task.hh`
  and `cancellation.md` lines.
- A8 (the Xapiand lead's review) is outside this lens.
