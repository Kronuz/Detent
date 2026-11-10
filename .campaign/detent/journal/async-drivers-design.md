# async-drivers-design: journal

State: waiting on jobs 261110-051400-42b328 261110-051401-84305c 261110-051402-039776 261110-051403-98da34 261110-051404-d3763b 261110-051405-fcb3cf 261110-051406-7c0cea 261110-051407-cc2e96 261110-051408-0f19d8 261110-051409-9ec69b 261110-051409-6b0f0a 261110-051410-df6596 261110-051411-f62e3b 261110-051412-164e3c (design re-review R1-R13 at Detent:25f361e, D50)
Quest: .campaign/detent/quests/async-drivers-design.md (the lead's; never edit it)

## Doing

Nothing. Design re-review R1-R13 folded per the lead's D50: bench Detent:940514e, design
Detent:25f361e. Earlier: Xapiand's X1-X15 in Detent:c351fc1 / Detent:786009f; F11-F14 in
Detent:672e7b7 / Detent:29fabf1; F1-F10 in Detent:35ed97d / Detent:3f16916.

## Next

1. The jobs above finish; record exits in the R table and the D5/D8/D10 rows of the design's
   Rerunnable checks (text only; the counts in D8 and D10 are from local runs of the same
   binaries at Detent:25f361e).
2. The design re-review of R1-R4 at head (the lead's).
3. Xapiand's re-review (A8) at head; the lead relays it.
4. The lead, outside this file set: X10 in `docs/SHARED_LIBRARY_DECISIONS.md` (Xapiand's apply
   leaves the group's loop; the turn orders applies, the log orders appends).
5. For the lead, unverified: journal may forget a lost verdict inside one generation too, once
   retention reclaims the segment that holds the damage (`lost_` is rebuilt from each recovery,
   `turn_log.h:176`). D10's group-table mark covers that case as well; no probe was run.

"Task-wheel diff not found" (Xapiand's review, item 5): the diff is quest 1's output
(`task-wheel`, task repository), which starts after this design lands; nothing to review yet.

## Review findings (design re-review, 1ece2ff, D50)

Review: `/Users/kronuz/code/Kronuz/Detent/.campaign/detent/reviews/async-drivers-design-design.md`,
CHANGES, blocking R1-R4. Resolutions fixed by D50.

| ID | Fix | Confirming job (failing before) |
|---|---|---|
| R1 | D10 step 1: every open marks recovery's lost ids in the host's group table, synced; `TurnLog` opens with them (quest 3); quest 3 criterion over a damaged old generation (Detent:25f361e); `placement_test remap` with one group in fifty lost and `remap-forget`, `repartition_lost` over the real RFTW0001 log with `forget` (Detent:940514e) | 261110-051400-42b328 (`repartition_lost`), 261110-051404-d3763b (`placement_test all`) (before: 261110-044744-df2ea9, the reviewer's probe at Detent:1ece2ff, ACCEPTED; controls 261110-051401-84305c `repartition_lost forget` and 261110-051405-fcb3cf `placement_test remap-forget`, must exit 1) |
| R2 | D10: a refused, failed or hello-less dial falls back to the peer's loop 0, whose hello carries L; at most 3 dials; quest 5's `l_mismatch_test` with a stale lower-id dialer (Detent:25f361e); `placement_test hello` counts unbound dials apart from `misrouted`; `shrink_dial` follows the fallback (Detent:940514e) | 261110-051402-039776 (`shrink_dial`), 261110-051404-d3763b (before: 261110-044744-df2ea9, 25.0% / 81.2% / 25.2% / 66.6% unreachable; control 261110-051403-98da34 `shrink_dial stale`, must exit 1) |
| R3 | D5 option (b): step 5 always throws `kronuz::journal::stalled` (`std::logic_error`, the waiting groups' ids, not retryable); a cross-thread cancel wakes only step 4's sleep; Limits matches; quests 1, 3, 4 criteria (Detent:25f361e); `can_wake`/`requestable` removed from blocking.hh and blocking_entry.cc, `lost-wake-live-token` case (Detent:940514e) | 261110-051407-cc2e96 (`blocking_entry all`), 261110-051408-0f19d8 (`threads`, TSan) (before: 261110-044953-75c0a8, no predicate in task; controls 261110-051409-9ec69b `inline` and 261110-051409-6b0f0a `v1`, must exit 1) |
| R4 | D8 close rule 2 and table: every waiter holding no staged record (unadmitted proposal, read at either stage, `create`/`recover` after its durable step) completes `closed`; only staged ones drain; D2: `open`, `receive`, `applied` by `GroupId`, a closed group drops and counts them; close does not wait for the held `Committed` range (Detent:25f361e); `waiter_test close-read`, `close-open`, `close-late` and their `-naive` controls (Detent:940514e) | 261110-051410-df6596 (all), 261110-051411-f62e3b (ASan+UBSan), 261110-051412-164e3c (TSan) (before: the `-naive` controls inside the same runs: 32/234, 110/527, 182,104/241,710 schedules broken) |
| R5 | D8 table: close and shutdown rows split; shutdown completes `cancelled`, the record still written (Detent:25f361e) | n/a (text) |
| R6 | D5: `needs_peer` on a voting member (voter or witness) other than this node, X5's predicate; quest 4 criterion (Detent:25f361e) | n/a (criterion for quest 4) |
| R7 | D10 and Q7: "stores"; a named source incarnation's node must be the authenticated node; quest 5 criterion (Detent:25f361e) | n/a (text; quest 5's security review) |
| R8 | D3: three seams are `Seams`' members, the wheel the scheduler's; D4 heading (Detent:25f361e) | n/a (text) |
| R9 | D2 `TurnDriver` per loop; quest 5 `l_mismatch` redials; quest 4 `create` and `recover` (Detent:25f361e) | n/a (text) |
| R10 | D5 quotes 60.2 ms (TSan, 32585e) and 51.2 ms (plain, 58310d) (Detent:25f361e) | 261110-031130-32585e, 261110-031128-58310d |
| R11 | Quest 4 criterion: close racing a cancel and teardown on real threads under TSan (Detent:25f361e) | n/a (criterion for quest 4) |
| R12 | D10 Cost and quest 5 criterion: a half-open loop-pair connection detected within a stated bound (Detent:25f361e) | n/a (criterion for quest 5) |
| R13 | Quest 3: each (generation, loop) log in its own subdirectory (Detent:25f361e) | n/a (text) |

## Review findings (Xapiand review, A8, f48f7cb)

Review: `/Users/kronuz/code/Kronuz/Xapiand/.campaign/xapiand/reviews/detent-async-drivers-design.md`
(Xapiand:de3c1b572, on Xapiand master). Verdict CHANGES; blocking X1-X5.

| ID | Fix | Confirming job (failing before) |
|---|---|---|
| X1 | D10 identity and Q7 (a): accept a frame from a node the loop's pins authenticate for the cluster naming a group and destination incarnation the loop holds; Core decides; no membership check; quest 5: joiner installs and catches up, lagging follower catches up (Detent:786009f) | n/a (text: consensus:ab05507 `core.h:756-759` already admits any authenticated incarnation; ruling 18) |
| X2 | D8: waiter destructor unlinks (legal on the group's loop and in teardown phase 3); close refuses, resolves unadmitted waiters with `closed`, drains staged, frees state once nothing queued; host-wide two-step shutdown; calls by group id (D2); D8 table row; quest 4 criteria (Detent:786009f); waiter.hh, waiter_test `close`/`teardown` and controls (Detent:c351fc1); the model's group counters kept on the group's loop (Detent:9e7237c, ruling 24) | 261110-042826-5c2da9 (all), 261110-042827-fb0e1d (ASan+UBSan), 261110-042829-5f86fb (TSan), at Detent:9e7237c (at Detent:c351fc1: f53237 and cc8ce9 passed; TSan 76a5a7 exit 134, the model race of ruling 24) (before: the controls inside the same runs, `close-naive` and `teardown-naive`, break 977/1,824 and 27/319 schedules) |
| X3 | D5 on `task::blocking_scheduler` (Xapiand's engine::BlockingScheduler moved into task, with the wheel); `inline_scheduler` gains same-thread `post_call` for the simulator; step 5 waits only while a linked waiter's token can still be requested; quest 1 criterion (Detent:786009f); blocking.hh, `blocking_entry threads` (Detent:c351fc1) | 261110-031128-58310d (all), 261110-031130-32585e (threads, TSan) (before: 261110-031132-189172, `blocking_entry inline`, must exit 1: hop's post_call dropped, stalled) |
| X4 | D10: placement `(slot * L) >> 12` for any L; re-partition at open when L changes (recover the generation, write live records to the next generation's logs, sync, commit by the layout record); hello and `misrouted` kept (F15); Limits; quests 3 and 5 (Detent:786009f); placement_test (Detent:c351fc1) | 261110-031134-9f199a (map, share, remap, hello) (before: 261110-031136-e8b259, `placement_test mod`, the f48f7cb rule, must exit 1) |
| X5 | D5 step 4 counts no election; Core campaigns at Start for a lone voter with no witness; quest 4 criterion (Detent:786009f); blocking_entry rules first-election 0 advances, `core=today` control (Detent:c351fc1) | 261110-031128-58310d (before: 261110-031138-999c25, `lone_voter` on consensus:ab05507, must exit 1: leads 1,499 ms after Start) |
| X6 | Q6 restated as (b): AsioHost::arm and DB-TIMER move, logger keeps Kronuz/scheduler; measure the logger's workload before proposing retirement; retiring is the owner's call (Detent:786009f) | n/a (text) |
| X7 | D4 bullet: what moves onto the wheel and what stays (LagProbe off the wheel for good); owner check binds at loop start; Limits: up to 1 ms late (Detent:786009f) | n/a (text) |
| X8 | Q4 and Build plan: quiescence with a closed state a frame or request reopens, before 7b; D10 rule for a frame naming a stored closed group (Detent:786009f) | n/a (text) |
| X9 | Q3 (b) per host (not per core, for X4) with the five conditions as quest 3 criteria (Detent:786009f) | n/a (text) |
| X10 | D2's Sink applies nothing inline, `applied` later via post_call (Detent:786009f); SHARED_LIBRARY_DECISIONS is outside this file set: listed for the lead (Next 3) | n/a (text) |
| X11 | D8 row 2: a cancelled `propose` has an unknown outcome (Detent:786009f) | n/a (text) |
| X12 | D10 Cost: connections grow with loops x peer hosts, depends on the copyset bound, keepalive per node pair, shard RPC on these connections; quest 5 measures keepalives (Detent:786009f) | n/a (text) |
| X13 | D6: turn primitives ahead of artifact and snapshot IO; quest 2 criterion (Detent:786009f) | n/a (text) |
| X14 | D2: `Seams` takes the wheel from the scheduler (`scheduler_ref::wheel()`); quest 1 criterion (Detent:786009f) | n/a (text) |
| X15 | D8 table: the host owns opens (single flight); requests wait under their own tokens (Detent:786009f); Xapiand's 7b design handles the rest | n/a (text) |

Xapiand's answers to the asks: API (D2) agreed, no bridge; deadlines (D8) shape right, X2/X11
fixed; D5 rules right, X3/X5 fixed; 7b fit arithmetic checks, a million groups fit only once
groups follow the catalog (X5, X8, X9); Q3 (b) with opaque group ids, per-group reclaim by name,
per-core (now per-host) group table, 0 fds per idle group; Q6 (b) (AsioHost timers and DB-TIMER
onto the wheel; Kronuz/logger's deferred lines keep Kronuz/scheduler, retiring it is the owner's
call); Q7 (a) once X1 is fixed; X8: Q4 needs closable quiescent groups.

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

## Review findings (design re-review, 73e96d6)

| ID | Fix | Confirming job (failing before) |
|---|---|---|
| F11 | D5's five steps: drain, commit staged, return only after; advance only to an operation deadline or an owned single voter's election; else `stalled`; `needs_peer` at the call; quests 3/4 criteria (Detent:29fabf1); `blocking_entry rules` (Detent:672e7b7) | 261110-005432-a6d855 (5 cases ok; control fails 3) (before: 261110-005433-b5324c, the rule at 73e96d6 alone, exit 1: lost wake and 3-voter propose hang 100,000 advances, staged-then-cancelled returns unwritten) |
| F12 | D10: placement `slot(group) mod L` (power-of-two L), a port per loop, a Control connection per pair of loops sharing groups, Bulk per transfer, no hop; idle connection end 40.7 KB with `SSL_MODE_RELEASE_BUFFERS` vs 111 KB today; per loop `29 KB + H x 41 KB`, `1 + H` fds; reader drops a full group's frame; quest 5 criteria (Detent:29fabf1); conn_cost.cc, reader_test.cc (Detent:672e7b7) | 261110-005436-650916 (conn_cost, strict: context 29,408; per end asio 111,234, direct 74,524, released 40,732 B; 1 fd), 261110-005434-15e1c5 (reader: longest gap 100 ms) (before: 261110-005435-c43e84, today's reader, gap 3,000 ms, exit 1) |
| F13 | One contract: `asio_scheduler::start_timer` moves onto the wheel and `task::deadline` keeps task's any-thread contract (entry in the cancel state the wheel retains; off-loop create/destroy post to the loop); users audited in D4's table (Detent 3, http 3, task tests; Xapiand's `engine::Host` unaffected); quest 1 criterion (Detent:29fabf1); loops.hh `wheel_deadline`, `wheel_test offloop` (Detent:672e7b7) | 261110-005430-687d57, ASan 261110-005428-f43eb1, TSan 261110-005429-afba6f (300 rounds, states_left=0) (before: 261110-005431-3ee08b, the embedded entry destroyed off its loop, owner assertion, exit 134) |
| F14 | Explorer: every cross-loop post is a scheduling point (target runs its backlog first), freed frames kept and checked unchanged (loops.hh, waiter_test.cc, Detent:672e7b7); D8 counts 131 / 51,014 schedules (Detent:29fabf1) | 261110-005423-d753e6 (reviewer's script, ASan: all three mutants killed by `explore`), 261110-005426-635ab8 (TSan: the same, all three killed by `explore`), 261110-005427-534e76 (all) (before: 261110-002555-485959, 261110-002557-76c931, both mutants survived `explore`) |
| N1 | Q7 (a) and D10: membership member (voter, learner, joiner), hello's checks in group-open, per-node pins (Detent:29fabf1) | n/a (text) |
| N2 | Header and Cost cite 261110-000912-e9dff1, finished, rows match 1f63c8 and fdfe40 | 261110-000912-e9dff1 |
| N3 | Idle CPU row labelled derived (Detent:29fabf1) | n/a (text) |
| N4 | D4: quest 1 adds the `clock` types (Detent:29fabf1) | n/a (text) |
| N5 | D8: callers' loops close after the group loops they wait on (Detent:29fabf1) | n/a (text) |

## Blocked on

Nothing of mine. A8 needs Xapiand's re-review of the fold at head (the lead relays it); X10's
SHARED_LIBRARY_DECISIONS entry is the lead's (Next 3).

## Note for the Xapiand re-review (A8, fold of X1-X15)

Design at Detent:786009f, bench at Detent:c351fc1. Blockers: X1 in D10 "Identity" and Q7; X2 in
D8 "Close, teardown and shutdown", D2 (calls by group id) and the D8 table; X3 and X5 in D5; X4 in
D10 "Placement and routing" and "L may change at restart", Q3 (per host). Non-blocking: X6 Q6,
X7 D4, X8 Q4 and D10, X9 quest 3, X10 D2 Sink (SHARED_LIBRARY_DECISIONS is the lead's), X11 D8,
X12 D10 Cost, X13 D6, X14 D2, X15 D8. Probes and their before-jobs: the X table above.

## Note for the Xapiand lead (A8)

Detent's design for the journal and consensus drivers is `docs/design/async-drivers.md`. What
changes for Xapiand:

1. API: `task::task<T>` over `scheduler_ref`; no `asio::awaitable` in journal or consensus
   headers. One consensus `Driver` per reactor loop; a group is state on one loop (no thread,
   `io_context`, armed timer, frame or connection). Calls from another loop hop to the group's
   loop and resume the caller on its own (D2).
2. Deadlines (#18, D8): one `task::deadline` per fan-out on the loop's timer wheel, on that
   loop's clock (manual in the simulator); a child on another loop makes its own, linked to the
   parent's token, and may only shorten it. A deadline keeps task's contract (created and
   destroyed on any thread) on the wheel; `asio_scheduler` moves onto it; Xapiand's
   `engine::Host` deadlines are unaffected (D4). Every driver call takes a `cancel_token`.
   Cancellation releases the caller exactly once (one claim; the group's loop unlinks before
   the resume); it never withdraws a proposal `Core` accepted (it may still commit, seen
   through `Committed`) and never cancels IO the machine owns (log appends, staged turn
   records), which runs under the loop's shutdown token.
3. Blocking entry (D5): `journal::run` / `consensus::run` over `inline_scheduler` and
   `InlineCompletion`, no `io_context`: it commits turns inline and returns only after staged
   records are written; it advances the clock only to an operation deadline or a single voter's
   first election (a manual clock jumps), throws `stalled` otherwise, and fails a multi-voter
   `propose`/`read` with `needs_peer`. It owns its groups and turn log, so a WorkPool chore runs
   only on cores no reactor loop serves. Not `task::sync_wait`, which hangs over a
   shared-backend Store.
4. 7b fit: a three-voter member costs 683 KB and 8 descriptors idle today: 171 KB of consensus
   state (164 KB of it `SnapshotChannel` frame arrays) and 512 KB, 5 descriptors in its own
   `consensus::Peers`. D9 (lazy frame buffers) and D10 bring the member to 7.9 KB and 3
   descriptors. D10: one transport per reactor loop; every host places a group on loop
   `slot(group) mod L` (power-of-two L) and each loop has its own port, so a connection joins
   the two loops that share groups and no frame hops threads; about `29 KB + H x 41 KB` and
   `1 + H` descriptors per loop for H peer hosts with as many loops (an idle TLS end is 40.7 KB
   with OpenSSL's release mode, 111 KB as today's `asio::ssl::stream`); the reader drops a
   frame for a group whose input slot is full rather than wait. The placement rule constrains
   how Xapiand assigns shards to cores. A million heartbeating groups cost at least 26 cores idle, so 7b's million
   groups need quiescence (Q4).
5. Asks: review items 1-5, 7b fit, D8's cancellation table and D10; Q3 (one directory and owner
   lock per core, a journal layout change before 7b writes bytes); Q6 (Xapiand's
   Kronuz/scheduler users onto the task wheel); Q7 (a shared connection authenticates the peer
   host's node; each frame is checked against the group's membership in force); D10's
   placement rule; review of the task-wheel diff (quest 1).

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | `docs/design/async-drivers.md`: "What exists today" (file:line per loop and Worker call), D1, D5 (journal), D2/D4/D9/D10 (consensus), "How Detent's owner loop moves" |
| A2 | `docs/design/async-drivers.md`: "D2. Public API: `task::task<T>` over `scheduler_ref`, no Asio in journal or consensus headers." |
| A3 | `docs/design/async-drivers.md`: "D3. Injection through three seams." |
| A4 | `docs/design/async-drivers.md`: "Where a three-voter member's 683 KB goes" |
| A5 | `docs/design/async-drivers.md`: "D5. The blocking entry runs the same task on the caller's thread, owns its groups, and ends." |
| A6 | `docs/design/async-drivers.md`: "Build plan", quests 1-7, Worker pump deletion last |
| A7 | 261110-000911-f87d0f (exclusive, Detent:35ed97d): frames 80/96/512 B, resumes 9.0/131.0 ns, wheel arm 13.8 ns vs 227/373 ns, `run_one` 1,548 ns, idle CPU; 261110-000912-e9dff1 (strict, Detent:35ed97d): bytes, fds, threads; 261110-000913-e9e87d (light): channel layout; 261110-000909-400d3c (strict): per-group Peers; 261110-005436-650916 (strict, Detent:672e7b7): one idle peer connection. Refs consensus@ab05507, journal@e5f6ff1, completion@560c288, task@bdafa5f |
| R1-R4 | the R table above (design re-review at Detent:1ece2ff) |
| A8 | Xapiand's review `/Users/kronuz/code/Kronuz/Xapiand/.campaign/xapiand/reviews/detent-async-drivers-design.md` (Xapiand:de3c1b572), verdict CHANGES at Detent:f48f7cb, blocking X1-X5; folded at Detent:c351fc1 (bench) and Detent:786009f (design), jobs in the X table; re-review pending |

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
13. Fix round 2: F13's contract is task's existing one (any-thread create and destroy) kept on
    the wheel by putting the entry in the deadline's cancel state, rather than narrowing it: CR1
    in task's `docs/status/cancellation.md` removed the on-loop assertion on purpose, and the
    audit found no user that needs the narrowing.
14. F12's routing: placement `slot(group) mod L` with power-of-two L and a port per loop, so
    the dialer computes the remote loop; free placement with redirects was rejected on
    connection count (up to L per peer host per loop). Bulk opens per transfer.
15. `waiter_test explore` now covers one and two callers (131 and 51,014 schedules); three
    callers with post scheduling points would take an estimated hour per run, so it is not run.
16. The bench changed in Detent:672e7b7 only by adding targets and tests (conn_cost.cc,
    reader_test.cc, wheel_test/waiter_test/blocking_entry cases, loops.hh); idle_cost.cc,
    driver_cost.cc, channel_layout.cc, peers_cost.cc, probe.hh and wheel.hh are unchanged, so
    the A4/A7 jobs at Detent:35ed97d stand (the lead's ruling for this round).
17. The reviewer's mutant script runs from the home checkout's `.scratch`; it is not copied
    into the file set.
18. X1 gets no probe: the transport it constrains does not exist, and Core's own admission
    (consensus:ab05507 `core.h:756-759`, "any authenticated incarnation") already settles it;
    quest 5 carries the joiner and lagging-follower tests.
19. X4: of Xapiand's options, (a) re-partition at open, with the lead's slot-keyed routing.
    Storage is keyed by group id and (generation, loop), never a bare loop index; a fixed
    partition count costs P/L barriers per turn forever and per-slot logs 256 GiB preallocated.
    Placement switches from `slot mod L` to `(slot * L) >> 12` (division-free, contiguous, any
    L) because `mod` breaks the per-peer sharing bound for non-power-of-two L. Q3 (b)'s directory
    and owner lock move from per core to per host so a resize renames nothing.
20. X2: task calls take a `GroupId` resolved in the loop's group table, so a call whose hop lands
    after close completes with `closed` instead of touching freed state; on-loop plain calls keep
    `Group&`.
21. waiter.hh changed (destructor unlink, `closed`), so waiter_test reruns at Detent:c351fc1
    (f53237, cc8ce9, 76a5a7); the F14 mutant jobs at Detent:672e7b7 stand, since the mutated
    paths (resume before unlink, ignored claim) are unchanged.
22. `lone_voter` exits 1 on consensus as pinned by design: it is X5's before-job and quest 4's
    acceptance check.
23. Step 5 on a steady clock waits while a linked waiter's token can still be requested (a
    SIGINT or request token), rather than throwing: a cross-thread cancel is progress the entry
    can wait for; a lost wake under such a token waits until it is requested or a deadline fires.
24. TSan job 261110-031126-76a5a7 (exit 134, 3+ reports, one location) was a model bug, not a
    design gap: the close model's `posted` counter was decremented by the IO loop's wrapper of
    the turn write and incremented on loop A. The design already keeps group state on its loop
    (D2, D8; completions post back to it, D3). Detent:9e7237c counts a write in flight on A at
    issue and at its barrier; a local TSan build (threads 1000, close 1, teardown 1) is clean.
    Other jobs at Detent:c351fc1 finished as required: 58310d, 32585e, 9f199a pass; 189172,
    e8b259, 999c25 exit 1 (the before-jobs).
25. R1's carrier is the host's group table, not the layout record or a lost marker in the log:
    every open, not only a re-partition, writes recovery's lost ids there before it serves, so
    the verdict outlives any later loss of the damage, and the table already names every group
    (X9). TurnLog at the pin cannot be seeded, so `repartition_lost` models quest 3's seeding by
    the host's refusal; it prints TurnLog's own verdict beside it.
26. Ruling 23 is reversed (D50, option (b)): step 5 throws `stalled` on every lost wake; a cross-
    thread cancel wakes only a deadline's sleep.
27. Ruling 20 is narrowed: `open`, `receive` and `applied` take a `GroupId` too (R4), so every
    Driver call names its group by id; on the group's loop the lookup is the only added cost.
28. `stalled` is one type, `kronuz::journal::stalled` (a `std::logic_error` carrying the waiting
    groups' ids), thrown by both entries, at the coordinator's request for Xapiand: journal is
    the lower of the two libraries; a type per entry would be two shapes for one condition.
29. The reviewer's `run_probes.sh` cannot run at Detent:940514e (its appended targets clash with
    the bench's `repartition_lost` and `shrink_dial`), so the bench carries both probes with the
    fixes and a control each (`forget`, `stale`); `token_predicate` is retired with option (b).
30. The X2/X3/X4 jobs at Detent:c351fc1 and Detent:9e7237c stand only for their numbers quoted
    in the text (60.2/51.2 ms, D8's close and teardown counts); the files changed, so the R
    table's jobs at Detent:25f361e are the evidence for D5, D8 and D10.
