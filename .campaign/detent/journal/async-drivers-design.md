# async-drivers-design: journal

State: ready for review (fix round 1 of the design review at 128df91, CHANGES); waiting on job 261110-000912-e9dff1 (idle_cost rerun; bytes matched exactly in 1f63c8 and fdfe40)
Quest: .campaign/detent/quests/async-drivers-design.md (the lead's; never edit it)

## Doing

Nothing. Design review F1-F10 folded in: bench Detent:35ed97d (tests, probes, wheel fix), design
Detent:3f16916.

## Next

1. Design re-review (lens `design`) at head.
2. A8: the lead relays the note below to the Xapiand lead (`campaign message xapiand:`); record
   the answer here.

## Review findings (design review, 128df91)

| ID | Fix | Confirming job |
|---|---|---|
| F1 | D4 clock (`deadline<S>` on `S::clock`), thread rule, detached-list `expire`, far deadlines; quest 1 criteria; wheel.hh, loops.hh, wheel_test.cc (Detent:35ed97d, Detent:3f16916) | 261110-000902-6240b4 (wheel_test), 261110-000905-c1d3b9 (ASan+UBSan), 261110-000906-2730b5 (TSan) |
| F2 | wheel.hh `expire` detaches due entries before firing (Detent:35ed97d) | 261110-000910-2d7f1a (review probe: cancel and rearm exit 0; it crashed at 128df91 in 261109-233203-d822ec); wheel_test reentry in 6240b4 |
| F3 | D8 waiter protocol (claim word, hop to the group's loop, then resume); waiter.hh, waiter_test.cc (Detent:35ed97d, Detent:3f16916) | 261110-000904-28d8d8 (17 / 2,910 / 938,978 schedules clean; naive control reaches a freed frame in 12/19 and 2,308/2,860), c1d3b9 and 2730b5 (threads under ASan, TSan) |
| F4 | D8 machine-owned IO under the shutdown token; staged, read, create/recover, close rows (Detent:3f16916); waiter_test checks staged-then-cancelled is written and committed | 261110-000904-28d8d8 |
| F5 | D2 every Driver call on the group's loop; D5 blocking entry commits turns inline, owns its groups, throws on a lost wake (Detent:3f16916); blocking_entry.cc (Detent:35ed97d) | 261110-000908-5a484f (staged append neither done nor in flight; done after one inline commit, 0 threads added) |
| F6 | D10 one transport per loop; Peers rows in Cost and the groups table; Q7; quest 5 `consensus-transport`; peers_cost.cc (Detent:35ed97d, Detent:3f16916) | 261110-000909-400d3c (3 voters 511,947 B, 5 fds; 5 voters 982,918 B, 9 fds) |
| F7 | Idle CPU baseline is Detent's runtime per group (17,523 us, 17.5 cores per 1,000); shared 1 ms loop a separate row; core-hours restated (Detent:3f16916) | 261110-000911-f87d0f |
| F8 | "at least 26 cores", with why (Detent:3f16916) | 261110-000911-f87d0f |
| F9 | Build plan: 3-5 beside existing APIs, 6 takes the pin bump and AuthorityPeers together (Detent:3f16916) | n/a (text) |
| F10 | D5 step 2: a manual clock jumps, a steady clock sleeps to `wheel.next()` (Detent:3f16916) | n/a (text) |

## Blocked on

A8 needs the Xapiand lead's review of items 1-5, 7b fit, D8 and D10 (Q7); the lead relays it.

## Note for the Xapiand lead (A8)

Detent's design for the journal and consensus drivers is `docs/design/async-drivers.md`. What
changes for Xapiand:

1. API: `task::task<T>` over `scheduler_ref`; no `asio::awaitable` in journal or consensus
   headers. One consensus `Driver` per reactor loop; a group is state on one loop (no thread,
   `io_context`, armed timer, frame or connection). Calls from another loop hop to the group's
   loop and resume the caller on its own (D2).
2. Deadlines (#18, D8): one `task::deadline` per fan-out on the loop's timer wheel, on that
   loop's clock (manual in the simulator); a child on another loop makes its own, linked to the
   parent's token, and may only shorten it. Every driver call takes a `cancel_token`.
   Cancellation releases the caller exactly once (one claim; the group's loop unlinks before
   the resume); it never withdraws a proposal `Core` accepted (it may still commit, seen
   through `Committed`) and never cancels IO the machine owns (log appends, staged turn
   records), which runs under the loop's shutdown token.
3. Blocking entry (D5): `journal::run` / `consensus::run` over `inline_scheduler` and
   `InlineCompletion`, no `io_context`: it commits turns inline, sleeps to the wheel's next
   deadline (a manual clock jumps), throws on a lost wake, and owns its groups and turn log, so
   a WorkPool chore runs only on cores no reactor loop serves. Not `task::sync_wait`, which hangs
   over a shared-backend Store.
4. 7b fit: a three-voter member costs 683 KB and 8 descriptors idle today: 171 KB of consensus
   state (164 KB of it `SnapshotChannel` frame arrays) and 512 KB, 5 descriptors in its own
   `consensus::Peers`. D9 (lazy frame buffers) and D10 (one transport per reactor loop, about
   41 KB + 118 KB per peer-host connection per loop) bring the member to 7.9 KB and 3
   descriptors. A million heartbeating groups cost at least 26 cores idle, so 7b's million
   groups need quiescence (Q4).
5. Asks: review items 1-5, 7b fit, D8's cancellation table and D10; Q3 (one directory and owner
   lock per core, a journal layout change before 7b writes bytes); Q6 (Xapiand's
   Kronuz/scheduler users onto the task wheel); Q7 (a shared connection authenticates the peer
   host's node; each frame is checked against the group's voters); review of the task-wheel
   diff (quest 1).

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | `docs/design/async-drivers.md`: "What exists today" (file:line per loop and Worker call), D1, D5 (journal), D2/D4/D9/D10 (consensus), "How Detent's owner loop moves" |
| A2 | `docs/design/async-drivers.md`: "D2. Public API: `task::task<T>` over `scheduler_ref`, no Asio in journal or consensus headers." |
| A3 | `docs/design/async-drivers.md`: "D3. Injection through three seams." |
| A4 | `docs/design/async-drivers.md`: "Where a three-voter member's 683 KB goes" |
| A5 | `docs/design/async-drivers.md`: "D5. The blocking entry runs the same task on the caller's thread, and owns its groups." |
| A6 | `docs/design/async-drivers.md`: "Build plan", quests 1-7, Worker pump deletion last |
| A7 | 261110-000911-f87d0f (exclusive, Detent:35ed97d): frames 80/96/512 B, resumes 9.0/131.0 ns, wheel arm 13.8 ns vs 227/373 ns, `run_one` 1,548 ns, idle CPU; 261110-000912-e9dff1 (strict, Detent:35ed97d): bytes, fds, threads; 261110-000913-e9e87d (light): channel layout; 261110-000909-400d3c (strict): per-group Peers. Refs consensus@ab05507, journal@e5f6ff1, completion@560c288, task@bdafa5f |
| A8 | pending: the Xapiand lead's answer (note above) |

## Result

`docs/design/async-drivers.md`; bench `docs/design/async-drivers-bench/` (`run.sh idle_cost`,
`driver_cost`, `channel_layout`, `peers_cost`, `wheel_test`, `waiter_test`, `blocking_entry`;
`SANITIZE=address,undefined` or `thread`). Designed, not built.

## Rulings I made

1. Library heads read and measured: origin/main of journal (e5f6ff1), consensus (ab05507), task
   (bdafa5f), completion (560c288); local `main` of journal and consensus is behind origin, so
   the design cites origin/main. Detent pins older journal (bfad97f) and consensus (cef08eb).
2. Bench lives in docs/design/async-drivers-bench/ (file set) and pins the libraries through
   `--ref`, passed to CMake as FETCHCONTENT_SOURCE_DIR_<NAME>.
3. Task frames are counted with TASK_FRAME_POOL=0 (task.hh:438) so the pool hides nothing; the
   create+destroy time is therefore malloc's, an upper bound on the pooled path.
4. Jobs 261109-145202-956fb1, 261109-145203-34a395, 261109-222607-679f33 and
   261109-222608-33c941 failed or were skipped on submission quoting (the `--ref` options became
   the command, exit 127); 261109-230223-1f63c8 and 261109-230225-963cb8 are the same runs and
   stand.
5. Added `channel_layout` (Detent:646cfb7) to attribute the follower's 171 KB with a job rather
   than a local probe; it adds a target and does not change `idle_cost` or `driver_cost`, so
   their jobs at Detent:87df68b stand.
6. Deadlines (triage #18) reuse task's existing `task::deadline` with the wheel as its
   `start_timer`, so group timers and operation deadlines are one mechanism (D4, D8).
7. Kronuz/scheduler's future (Q6) is the owner's and the Xapiand lead's, not this quest's: the
   design does not use it and names the retirement as a question.
8. Fix round 1: the wheel entry grows to 32 bytes (a fire pointer per entry, so group timers and
   deadlines share one wheel); driver_cost reran at it (f87d0f) and the design cites that run.
9. The transport decision (D10, per loop) is proposed in the design with its identity change as
   Q7 for the owner, rather than blocking: the quest designs, and quest 5 carries the security
   review.
10. peers_cost builds against Homebrew OpenSSL 4.1 (`/usr/local/opt/openssl@4`), whose headers
    `/usr/local/include` holds; mixing them with openssl@3's library is undefined.
11. A7's numbers at 87df68b and 128df91 (963cb8, f67aa2) stay cited only for run-to-run spread.
12. The bench is unchanged since Detent:35ed97d (`git diff 35ed97d..HEAD -- docs/design/async-drivers-bench` is empty), so its jobs there stand for the doc and journal commits after it.
