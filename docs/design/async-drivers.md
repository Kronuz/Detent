# Coroutine drivers for journal and consensus

Event-driven `task::task` drivers over journal's resumable operations and consensus's I/O-free
`Core`, replacing the polling loops and the Worker pump API, so that an idle group costs bytes
and nothing else.

Quest `async-drivers-design` (campaign `detent`). Sources: `docs/SHARED_LIBRARY_DECISIONS.md`,
"Coroutine drivers for journal and consensus" items 1-5, "One coroutine type" and "task
cancellation, inline resume and reactor sessions" (all November 2); PLAN D7; Xapiand triage #18
(one deadline per fan-out, on the cancellation token). Library code is read at journal:e5f6ff1,
consensus:ab05507, task:bdafa5f and completion:560c288 (each `origin/main`; Detent pins
journal:bfad97f and consensus:cef08eb, which differ only in pins and tests for what this design
touches). Every number is a campaign job pinned to those commits, run by
`docs/design/async-drivers-bench/run.sh`: idle bytes, descriptors and threads in
261110-000912-e9dff1 (strict); frames, resumes, wheels, steps and idle CPU in 261110-000911-f87d0f
(exclusive); the snapshot channel's layout in 261110-000913-e9e87d (light); today's per-group
transport in 261110-000909-400d3c (strict); one idle peer connection in 261110-005436-650916
(strict). The threading rules are tested by the bench's `wheel_test`, `waiter_test`,
`blocking_entry`, `reader_test`, `placement_test`, `repartition_lost` and `shrink_dial`, and
`lone_voter` measures a lone voter's first election (Rerunnable checks). Xapiand's review of this design (A8, findings X1-X15) is
`.campaign/xapiand/reviews/detent-async-drivers-design.md` on Xapiand master.

## Abstract

Today a group is pumped. Detent's authority runs one consensus group per process on an
`OwnerLoop` thread and polls it every millisecond (`service.cc:2594-2606`), calling
`Worker::run_one` (`node.cc:557`) whether or not anything happened. Idle, that costs
17.5 ms of CPU per second per authority (16.3 ms for the loop's timer, 1.2 ms for the idle
`run_one` calls), two threads, one `io_context`, one armed timer
and nine descriptors, before its transport.

The design turns the group into state. One driver coroutine per reactor loop steps only the
groups that have work: an input, a completed IO, or a deadline that came due in one shared
timer wheel. An idle group holds a 32-byte wheel entry, a 16-byte ready-list link and a 16-byte
transport route beside the library state it already has, and no thread, `io_context`, armed
timer, coroutine frame or connection of its own. The same step code runs a second way, to
completion on the calling thread with inline IO, for offline tools, tests and the deterministic
simulator.

Journal, consensus and `task::deadline` share one timer mechanism: an intrusive wheel per
reactor loop, serving group timers and operation deadlines alike, with the loop arming one `asio::steady_timer` at `wheel.next()`. It
arms in 14-18 ns with no heap per entry, against 225-538 ns and 120-332 heap
bytes for an `asio::steady_timer` per timer or Kronuz/scheduler's queue. At 1,000 idle groups,
Detent's layout costs 17.5 cores today; on the wheel, heartbeating leaders cost
0.026.

Bytes are the remaining limit. An idle three-voter member costs 683 KB today: 171 KB in
consensus, 96% of it the frame arrays of its `SnapshotChannel`, and 512 KB in the `Peers`
transport it owns. Frame buffers allocated only while a frame arrives (D9) and one transport per
loop instead of per group (D10) bring the member to 7.9 KB.

## What exists today

| Piece | Where | What drives it |
|---|---|---|
| journal operations | `IOOperation` (`completion/operation.h:69-78`): one typed primitive at a time, `complete()` advances | `drive_synchronously` (`operation.h:183-188`) or `AsioCompletionDriver::run` (`asio_completion.h:105-202`) |
| journal turn log | `TurnDriver` (`turn_driver.h:35-128`): `stage` (no unstage), `begin`, `completed`, `failed` | the host by hand, or `commit()`, which writes and `sync_data`s on the caller's thread (`turn_driver.h:92-99`) |
| consensus `Core` | `Core::step(Event) -> Actions` (`core.h:250`), no IO, no clock | `Worker` |
| consensus `Worker` | Core + Store + admission (`worker.h:94`) | the host pumps `run_one(Tick)` (`worker.h:448-548`), `take_io_operation` (`:212`), `take_actions` (`:230`), `try_submit` (`:207`), `applied` (`:239`), `storage_operation_failed` (`:218`) |
| consensus transport | one `Peers` per group (`transport/peers.h:53-91`): its TLS context, a listener, a Control and a Bulk connection per peer, each a reader and a writer coroutine | `asio::awaitable` in public headers (`transport/session.h:111-246`, `transport/peers.h:251-310`) |
| task timers | `task::deadline<S>` (`task.hh:1113-1146`): a `cancel_source` that `S::start_timer(steady_clock::time_point, F)` requests with `cancel_reason::deadline` | `asio_scheduler::start_timer`, one `asio::steady_timer` per deadline (`task_asio.hh:125-136`) |
| Detent's owner loop | `Service::Runtime::run` (`service.cc:1235`, spawned at `:578`) | polls readiness (`:1678-1687`), replay (`:1694-1704`) and the steady state (`:2594-2606`) every 1 ms; `dispatch` (`:1166-1210`) spawns `AsioCompletionDriver::run` per taken operation |

Each completion queue owns a thread and a kernel queue (`linux_completion.h:55-78`,
`bsd_completion.h:46-65`) and accepts one primitive at a time (`linux_completion.h:51`,
`asio_completion.h:120`). Nothing in strata or Xapiand uses journal or consensus today.

## Decisions

**D1. A group is state; the driver is one coroutine per reactor loop.** The group's machine
(journal `Store`; consensus `Worker`'s Core, Store and admission) gains a wheel entry and a
ready-list link, and belongs to one loop for its life. The driver is one `task<void>` per loop
(80-byte frame): it takes the next ready group, runs its machine until it reports no progress
(a bounded number of steps per resume, then `task::yield`), hands any IO operation to the
completion seam, and delivers actions. Frames exist only for work in flight: a caller awaiting
a request (96 bytes) and an IO operation (512 bytes for `AsioCompletionDriver::run`).

- *A coroutine per group* loses: a suspended frame per idle group is what item 4 forbids, at
  least 96 MB at a million groups before any state.
- *Keeping the pump* loses: item 1 deletes it, and an idle `run_one` costs 1,548 ns and
  1.61 allocations (leader) or 1,761 ns and 0.67 (follower), about a thousand times a
  second.

**D2. Public API: `task::task<T>` over `scheduler_ref`, no Asio in journal or consensus headers.**

```cpp
namespace kronuz::journal {
class completion_ref;   // the IO seam (D3): task::task<void> run(std::shared_ptr<IOOperation>, task::cancel_token)
class TurnDriver {      // one per reactor loop
	task::task<void> run(completion_ref io);   // awaits staged records, writes, sync_datas, completes
};
class Store {           // per group; each awaits its operation through the seam
	task::task<Frontier> append(AppendReservation&&, std::string_view, completion_ref, task::cancel_token = {});
	// likewise prepare/write/finish artifact, publish, verify, reclaim_step
};
template <class T> T run(TurnDriver&, task::task<T>);   // the blocking entry (D5)
}
namespace kronuz::consensus {
struct Seams { task::scheduler_ref scheduler; journal::completion_ref io; Transport& transport; };  // the wheel is the scheduler's
template <class Sink> class Driver {   // one per reactor loop
	Driver(Seams, Sink&);
	task::task<void> run();             // the one long-lived frame
	void open(GroupId, GroupOptions);   // state only, entered in the loop's group table
	task::task<void> create(GroupId, Identity, task::cancel_token = {});
	template <class Restore> task::task<void> recover(GroupId, Restore, task::cancel_token = {});
	task::task<Placement> propose(GroupId, Propose, task::cancel_token = {});
	task::task<ReadReady> read(GroupId, Read, task::cancel_token = {});
	void receive(GroupId, Receive);     // marks the group ready
	void applied(GroupId, Applied);
	task::task<void> close(GroupId);    // retirement until drained
};
template <class Sink, class T> T run(Driver<Sink>&, task::task<T>);   // the blocking entry (D5)
}
```

Every `Driver` call touches its group only on the group's loop. A task call (`create`,
`recover`, `propose`, `read`, `close`) made on another loop first moves there
(`co_await task::schedule_on(group's scheduler)`, one post), looks the group up in the loop's
group table by its opaque id, links its waiter, and resumes the caller on the caller's own
scheduler through D8's protocol; on the group's loop it neither posts nor hops. A group closing
or closed completes the call with `consensus::closed`, so a call in flight when its group closes
never reaches freed state (X2). The wheel is the scheduler's (`scheduler_ref::wheel()`, an
optional member like `roots()`), so a Driver cannot pair one loop's scheduler with another's
wheel (X14). `open`, `receive` and `applied` are plain calls on the group's loop (Debug-asserted
with `scheduler_ref::runs_here()`); another thread reaches them with `post_call`. They too name
the group by id through the table, because the transport's `receive` and the application's
`applied` (Sink, below) can land after the group closed: a group closing or closed drops them and
counts the drop per group, as `Worker::applied` drops one while closing today (`worker.h:239-240`;
R4). The waiter
test runs every caller on loop B against a group on loop A (D8).

`Sink` receives what no caller awaits (`Send`, `Committed`, `RoleChanged`, snapshot and repair
notices) with static dispatch and no allocation (Q2). It runs inside a driver step, which D7's
budget does not bound, so it applies nothing inline: it hands a `Committed` range to the
application (Xapiand: a chore under the index's turn), which calls `applied` later through
`post_call`; Core holds one range until then. `close` does not wait for it: an `applied` that
lands after close is dropped (above). The log orders appends and the application's turn
orders applies, superseding November 2's "the per-index turn holds the journal append and the
apply" (X10). Detent's asio adapter already exists:
`task::asio_scheduler` (`task_asio.hh:58-166`) and `AsioCompletionDriver` behind `completion_ref`.
The transport moves to `task::task` with `task::await_asio` (`task_asio.hh:236-423`) and to one
instance per loop (D10), removing the last `asio::awaitable` from consensus's public headers.
This is the contract `task:driver`.

**D3. Injection through three seams.** The scheduler (`scheduler_ref`, `task.hh:169-199`) says
where frames resume and carries the loop's wheel (D4), its clock and timers; `completion_ref` is
the IO; the transport (D10) carries frames. These are `Seams`' three members:

| `completion_ref` | Who | How a primitive completes |
|---|---|---|
| `AsioCompletionDriver` (exists) | Detent, Xapiand | native queue, notification descriptor on the loop |
| `InlineCompletion` | blocking entry (D5), tests | `execute_primitive` on the caller's thread, the loop of `drive_synchronously` |
| `HeldCompletion` | strata's simulator | queued; the simulator completes, fails or drops each one, at a crash point of its choice |

strata's simulator runs every group on one `task::inline_scheduler` (`task.hh:311-349`), which
gains a same-thread `post_call` in quest 1 so D8's cancel hop runs there (X3), over journal's
`MemoryIO` and repeats: run the scheduler until nothing is queued; then deliver one
message, complete one held IO, or advance its manual clock to `wheel.next()` and expire. No real
time, no threads; every choice comes from its seed.

**D4. One intrusive timer wheel per reactor loop, for group timers and operation deadlines.** An entry is 32
bytes inside its owner (next, back-link, deadline, fire function); arm and cancel are O(1)
with no allocation. Consensus gains `Core::next_deadline()`, a `const` accessor over the
deadlines Core already keeps (`election_deadline_`, `heartbeat_deadline_`, `core.h:1196`; each
peer's `retry_after`, `core.h:274`; the transfer deadline, `core.h:686`); the driver re-arms the
group's entry after every step, and a group with no deadline (quiescent, Q4) holds none. The
wheel also implements `start_timer` for `task::deadline` (D8), so operation deadlines are
entries too, and the loop arms one `asio::steady_timer` at `wheel.next()`. A wheel costs 33,312
bytes plus 65,536-67,584 heap bytes per loop, not per group. The prototype is
`async-drivers-bench/wheel.hh`: 4,096 slots of 1 ms and an occupancy bitmap for `next()`.

- *Clock.* The wheel counts nanoseconds on its loop's clock: `steady_clock` on a reactor loop,
  `inline_scheduler::now()` in the simulator. `deadline<S>` takes `S::clock::time_point` when
  `S` declares a `clock` (quest 1 adds them: `inline_scheduler`'s manual clock, which today
  has `now()` and `advance()` but no `clock` type, and `asio_scheduler`'s `steady_clock`), and
  `steady_clock::time_point` otherwise, so today's callers compile
  unchanged. `wheel_test manual` fires a five-second deadline on the manual clock in
  34 us of real time.
- *Threads.* A wheel, and every entry in it, is armed, cancelled and expired only on its loop's
  thread (Debug-asserted; the prototype binds the thread on first use, quest 1 when the loop
  starts). A group's entry is inside
  the group, which lives on that loop. A `task::deadline` keeps task's contract on every
  scheduler, `asio_scheduler` included once its `start_timer` is on the wheel (quest 1): it may
  be created and destroyed on any thread (`docs/mechanisms/cancellation.md:65`; CR1 in
  `docs/status/cancellation.md` removed the on-loop assertion). Its entry lives in its cancel
  state, which the wheel retains while armed. On the loop, creation arms and destruction unlinks
  at once; on another thread each is posted to the loop (`post_call`), and the destructor first
  claims the state against a fire, waiting only for a fire running on the loop, so a destroyed
  deadline requests nothing. Every deadline already allocates that state
  (`cancel_state::make`), so the entry adds 32 bytes to it and no allocation. A child on another
  loop makes its own deadline on its own loop's wheel, linked to the parent's token; task's
  linked cancel state is what crosses threads (`task.hh:911-975`). `wheel_test offloop` creates
  deadlines on and off a running loop and destroys them on another thread or on the loop, 300
  rounds as task's own test (`asio_test.cc:371-390`): none requests after its destructor
  returns and every state is freed, under TSan and ASan. The deadline as first designed, its
  entry inside the deadline and unlinked by whichever thread destroys it, trips the wheel's
  owner assertion off the loop (`offloop-embedded`). `wheel_test loops` runs a parent on loop A
  with children created and destroyed on loop B while A fires.
- *Who is affected.* Every `task::deadline` on `asio_scheduler` at origin/main is created and
  destroyed on its scheduler's own thread, except task's any-thread test, which the contract
  above keeps; none changes:

  | User | Where its deadline is created and destroyed |
  |---|---|
  | Detent `include/detent/owner_loop.hh:72` (`owner_submit`) | the owner loop, after `schedule_on(owner)`, until `co_return` |
  | Detent `include/detent_transport/https_client.hh:83` | `https_health`'s own `io_context`, run on the calling thread |
  | Detent `src/detent_delivery/client.cc:291` (`perform`) | `options.scheduler`'s loop, where the coordinator is spawned (`detent_relay/host.cc:379`, `detent_agent/managed_host.cc:190`) |
  | http `http_asio.h:366`, `:807`; `http_asio_tls.h:97` | the connection's reactor, one thread each (`http_asio.h:1051`) |
  | task `test/asio_test.cc:195-410`, `bench/bench.cc:114` | the test's loop; `:371-390` destroys on other threads |

  Xapiand's `task::deadline<engine::Host>` (`src/database/pool.cc:338`, `src/walk_gate.h:90`)
  uses `engine::Host`'s own `start_timer` and is not affected by quest 1.
- *What moves onto the wheel, and what stays (X7, Q6).* Quest 1 moves group timers and
  `task::deadline` on `asio_scheduler`. After it, Xapiand moves `engine::AsioHost::arm` (turn
  deadlines, `engine::sleep_for`, the control plane's debounce) onto its loop's wheel and the
  catalog's `DB-TIMER` thread onto the control loop's wheel. reactor's event timers and http's
  stay `asio::steady_timer`s until something moves them; reactor's `LagProbe` stays off the
  wheel for good, because `next()` wakes at a slot's end, up to 1 ms late, which the 1 ms p99
  stall row would read as lag. Kronuz/scheduler keeps the off-loop deferred work (logger's
  deferred lines).
- *Fires that cancel.* `expire` moves the due entries to a detached list before running any
  fire, so a fire may cancel or re-arm any entry, itself included; an entry armed during the
  fires for a time already due fires on the next `expire`, and `expire` is not re-entrant. A
  fired parent deadline whose callback destroys a linked child's deadline is this case
  (`wheel_test chain`, `reentry`, and the reviewer's probe, which crashed the first prototype).
- *Deadlines beyond 4,096 ms* wait in their slot and fire at most one tick late, never early;
  `next()` wakes the loop once per round they pass, `floor(d / 4,096 ms)` early wakes in all (7
  for a 30 s request, 73 for 5 minutes; `wheel_test long`). That is one wake per 4.1 s per far
  slot, against ten a second per heartbeating leader; a second level (64 slots of 4,096 ms) is
  quest 1's to add if a loop's wakes are measured to be dominated by far deadlines.

| Per timer, at 1,000,000 entries (1,000 in parentheses) | Bytes | Heap per arm | Arm | Re-arm |
|---|---|---|---|---|
| Intrusive wheel (proposed) | 32 in the owner | 0 | 13.8 ns (18.2) | 15.7 ns (27.8) |
| Kronuz/scheduler `SchedulerQueue` | 64 + 123 heap (332) | 123 (188), the cleared task stays filed | 373 ns (538) | 440 ns (496) |
| One `asio::steady_timer` per timer | 120 + 120 heap (132) | 160 (145) | 227 ns (225) | 282 ns (253) |

Kronuz/scheduler's queue also takes a `shared_ptr` task per arm (`scheduler.h:200`), leaves a
cancelled task filed until a walk (`scheduler.h:181-191`) and fixes its clock to `steady_clock`
(`scheduler.h:203-205`), which the simulator cannot drive. Ticking every group at a fixed
cadence loses on CPU (Cost).

**D5. The blocking entry runs the same task on the caller's thread, owns its groups, and ends.**
`journal::run(turns, task)` and `consensus::run(driver, task)` build their machine over
`InlineCompletion` and drive it on the calling thread with no `io_context`. On a steady clock
the scheduler is `task::blocking_scheduler`: thread-safe `post` and `post_call` that wake its
wait, and timers on a wheel. It is Xapiand's `engine::BlockingScheduler`
(`src/engine/host.h:215-363`) moved into task with the wheel, and Xapiand's becomes it (X3).
`task::inline_scheduler` cannot be it: it declares no `post_call`, so D8's cancel hop is dropped
and the cancelled caller never resumes; its queue is not thread-safe; and its manual clock
cannot wake a sleep. It stays the simulator's scheduler, with a manual clock and no other
thread. The entry runs this loop:

1. Run the scheduler until its queue is empty.
2. If records are staged in the core's turn, commit it inline (`TurnDriver::commit` over
   `InlineCompletion`) and go to 1. A shared-backend append completes only at its turn's barrier
   (`store.h:328-333`); `blocking_entry staged` shows one staged, neither done nor in flight,
   until the inline commit completes it with no thread added.
3. If the task has finished, return its result or rethrow its exception. Steps 1 and 2 ran to a
   fixed point first, so staged records and the IO the machine issued for itself (D8) are written
   before the entry returns, whatever happened to the caller; only then does it give up its
   groups and turn log.
4. Otherwise, if an operation deadline is armed (a `task::deadline` on the entry's scheduler,
   which the entry counts apart from group timers), wait for the earliest, and go to 1. A manual
   clock jumps there; a steady clock sleeps until then or until another thread posts (a cancel's
   hop). Group timers due by then fire as the clock passes them. No group timer counts: a lone
   voter leads at `Start` (X5, quest 4), and a heartbeat or a multi-voter group's election needs a
   peer to move anything, while every group always holds one (`core.h:1054-1066`).
5. Otherwise throw `stalled`: a lost wake.

A cross-thread cancel wakes only step 4's sleep. A task awaiting a token with no operation
deadline armed throws at step 5 even if another thread would request the token later: task
cannot tell a token another thread will request from one whose source sits in the caller's own
frame or links a process-wide shutdown source (`task.hh:995-1058`), and a lost wake never passes
silently (R3). A caller that wants to wait for a cancel arms a deadline.

`stalled` is one type for both entries, `kronuz::journal::stalled`, which `consensus::run`
throws too (consensus builds on journal). It derives from `std::logic_error`, because a lost
wake is a defect in a driver or its machine, not a condition of the run, and it carries
`std::vector<std::uint64_t> groups`, the ids of the entry's groups that held a linked waiter,
which `what()` names. It is not retryable: the same task over the same state stalls again.
Xapiand's WorkPool chores report it as a 5xx naming the index and never retry it.

An operation that needs a peer fails at the call with `consensus::needs_peer` and submits
nothing: `propose` and `read` on a group whose membership has a voting member (voter or
witness, `types.h:70-77`) other than this node. That is the predicate X5 uses for the lone
voter's campaign at `Start`, so a lone voter beside a witness fails `needs_peer` at the call
rather than `stalled` later (R6). `create`, `recover`, checkpoint and `close` need no peer. So
each pass runs work, commits a turn, fires an operation deadline, takes a cross-thread cancel
toward one, or throws.
`blocking_entry rules` runs this loop over a model group whose timer is always armed: a lost
wake throws `stalled` with no clock advance; a three-voter proposal fails `needs_peer` with
nothing staged; a lone voter's first proposal is placed, written and committed with no clock
advance; a proposal staged and then cancelled returns `cancelled` with its record written and
committed; a lost wake under a 30 s deadline returns `cancelled` after 8 advances (7 far-deadline
wakes, D4). The rule this section first stated (throw only on an empty wheel, return as soon as
the task finishes) hangs on the first two, 100,000 heartbeats or elections without returning,
and returns with the cancelled record unwritten. With Core as pinned, the lone voter's proposal
waits out its 1.5 s election; `lone_voter` measures the real one leading 1,499 ms after `Start`.
`blocking_entry threads` runs the steady-clock entry over the prototype scheduler
(`blocking.hh`): a cancel requested on another thread at 50 ms wakes an entry asleep toward a
30 s deadline, which returns `cancelled` 60.2 ms after it started under TSan and 51.2 ms in the
plain build (jobs 261110-031130-32585e, 261110-031128-58310d), and 300 rounds of it all return
`cancelled`; a 200 ms deadline wakes it on the wheel; a lost wake throws at once, with no token
and under a live token another thread requests only after the entry returned
(`lost-wake-live-token`, R3). Over `inline_scheduler` the same cancel's hop is refused and the
entry stalls (`blocking_entry inline`).

`task::sync_wait` is not the blocking entry: it waits for work other threads drive
(`task.hh:744-792`), and over a shared-backend `Store` with no one running its `TurnDriver` it
never returns. The entry owns its groups and their core's `TurnDriver` for its duration: a
group or turn log a reactor `Driver` has open is refused (an owner tag set by `Driver::open` and
by `run`, checked on every step). Xapiand's users are offline tools (recovery tooling), tests and
the simulator; in 7b its WorkPool chores never run the entry on a live group, and journal IO
moves from flush chores to the loops' completion queues. `drive_synchronously` becomes
`InlineCompletion`'s loop: one implementation, driven two ways.

**D6. One completion queue per reactor loop, shared by its groups.** A queue costs a thread and
descriptors, so it belongs to the loop. A turn write needs a `SyncData` primitive, which
completion lacks (`operation.h:41`; `File::sync_data`, `io.h:84`, is synchronous only):
completion adds it (io_uring `IORING_FSYNC_DATASYNC`, `aio_fsync(O_DSYNC)` on FreeBSD, the
fallback worker's `sync_data`). Turn primitives go ahead of artifact and snapshot IO, so a large
index's snapshot (a 10 GB index is about 160,000 chunks of 64 KiB) does not hold every turn of
its loop (X13). A multi-slot queue is a later optimization, measured when a loop's IO queues
behind one primitive.

**D7. Fairness stays where it is.** `Worker`'s admission (foreground burst, maintenance, owed
timer service, `worker.h:484-546`) is unchanged; the driver bounds steps per resume, yields, and
counts an inline `await_asio` resume toward that budget (November 2, item 3).

**D8. A deadline is a `task::deadline` on the loop's wheel, carried by the token (triage #18).**
A fan-out root creates one `task::deadline d(scheduler, at, parent)` and passes `d.token()` to
every child; tokens are explicit arguments (task has no implicit current token). A child needing
a tighter bound makes its own `deadline(scheduler, earlier, token)` on its own loop (D4), linked
to the parent's, so a child can shorten a deadline but never extend it. Firing requests
`cancel_reason::deadline` on the whole tree.

A caller's token covers what the caller owns, never what the group's machine owns. IO the machine
issued for itself (Core's log appends and hard state, snapshot writes, records staged in a turn,
which `TurnDriver` cannot unstage) runs under the loop's shutdown token only, so a caller's
cancellation never stalls Core or leaves a sequence gap:

| Where the caller is | On cancellation |
|---|---|
| `propose`/`read` waiting for admission, not yet given to `Core` | unlinked, never submitted; completes with `task::cancelled` |
| a proposal `Core` accepted, its entry in Core's log or its record staged in a turn | the caller completes with `task::cancelled`; the record is written and may commit, reaching the application through `Committed` (Raft has no withdraw). Rows 1 and 2 look alike to the caller, so a cancelled `propose` has an unknown outcome (Xapiand answers it as one, idempotent by version; X11) |
| a read awaiting its read index or the apply that reaches it | the caller completes with `task::cancelled`; the slot is dropped when the index arrives |
| a caller-owned IO primitive (an artifact chunk, a verify read) not yet queued | not issued |
| a caller-owned IO primitive in the kernel | completes into the machine (no torn write); only the caller is released |
| `create` or `recover` in progress | the caller completes with `task::cancelled`; the machine finishes its current durable step under the shutdown token and the group stays open, so a later `create`/`recover` resumes it or `close` retires it; the host owns opens, single-flight, and requests wait on an open under their own tokens (X15) |
| waiting when its group closes | holding no staged record (a proposal not yet admitted, a read at either stage, a `create`/`recover` once its current durable step ends): unlinked, completes with `consensus::closed`; holding a staged record: completes when its record commits (X2, R4) |
| waiting when its loop shuts down | completes with `task::cancelled` (`cancel_reason::shutdown`); a staged record is still written and may commit, as row 2 (R5) |
| `close` | takes no caller token: retirement drains to the end; only the loop's shutdown stops it |

*The waiter.* A caller awaiting a group holds a waiter in its frame: a claim word, the result
slot, its scheduler, a `cancel_callback` node and an intrusive link in the group's list. The
group's loop links it and registers the callback. Completion, on the group's loop, unlinks the
waiter and then claims it; if the claim wins, it writes the result and posts the caller's resume.
The token's callback, on whatever thread fires it, claims the waiter; if the claim wins, it posts
a hop to the group's loop (`post_call`), which unlinks the waiter if it is still linked and then
posts the caller's resume. Only the claim's winner posts a resume, and it posts after the group's
last touch, so no list or callback reaches a freed frame; the caller's `~cancel_callback` waits
for a callback still running on another thread (`task.hh:911-930`, `:1087`). The callback runs no
user code under the token's lock (November 2, item 1) and allocates nothing; an idle group holds
no waiter. The prototype is `async-drivers-bench/waiter.hh`; `waiter_test` explores every interleaving of
callers on loop B, the group on loop A, the deadline on loop C and the turn write on an IO loop,
each loop's call running whole except that every post to another loop is a scheduling point
(the target may run the posted call, and what was queued before it, before the poster's next
statement). Freed frames are kept and must stay as they were when freed. One and two callers
take 131 and 51,014 schedules, each with exactly one resume per caller, no freed frame reached
or written, and the table's first two rows holding (cancelled before admission: never staged;
staged: written and committed). The review's mutants that post the resume before unlinking,
from the hop or from completion, fail it, as does a cancel that ignores a lost claim. The first
version of this protocol (the callback posting the resume straight to the caller) reaches a freed
frame in 47 of 88 and 18,969 of 27,276 schedules. *`when_any` against a timer* loses: item 2
rules it out and it needs a frame per race. *A per-group timeout* loses: a fan-out spans groups.

*Close, teardown and shutdown (X2).* Loops call each other's groups in every direction (Xapiand
runs a reactor per processor, each with a Driver), so no order between loops can keep a frame
alive, and task's teardown phase 3 destroys suspended frames without resuming them
(`docs/mechanisms/asio-scheduler-teardown.md` in task). Three rules hold instead:

1. A waiter's destructor unlinks it, which is legal on its group's loop and in phase 3, once
   every loop has joined. Groups outlive phase 3.
2. `close` refuses new calls and resolves with `closed` every waiter that holds no staged record:
   proposals not yet admitted, reads awaiting their read index or the apply that reaches it (a
   closing group confirms no read index), and `create`/`recover` once its current durable step
   ends, never before. Each is unlinked and resumed if close wins its claim. Only waiters with a
   staged record drain: they complete at their commit. The group's state is freed once nothing
   of its own is queued; the group table's entry outlives it, so a late `receive` or `applied`
   (D2) finds the group closed, is dropped and is counted. Close does not wait for the
   application's `Committed` range (R4).
3. Graceful shutdown is host-wide, in two steps. While every loop runs, every Driver cancels its
   waiters with `cancel_reason::shutdown` and refuses new calls. Then task's teardown runs.

`waiter_test close` adds a fifth loop that closes the group at any point of the four loops'
interleavings: in 1,796 and 1,014,239 schedules for one and two callers, each caller resumes
once (completed, cancelled or closed) and nothing touches the freed state. `teardown` stops every
loop at any step and destroys the unresumed frames (319 and 133,406 schedules); the group then
names no destroyed frame. Their controls fail: a close that frees the state with waiters linked
breaks 977 of 1,824 schedules, and a destructor that leaves its waiter linked breaks 27 of 319.
The other waiter kinds run the same exploration, caller 0 with no token so only close can end
its wait and the others under the deadline (R4): `close-read`, reads closed at either stage
(310 and 24,028 schedules); `close-open`, one or two callers waiting on one single-flight
`create`/`recover`, closed only after its durable step (657 and 30,896); `close-late`, a
proposal with the transport's `receive` and the application's `applied` arriving at any point
through a sixth loop, 241,710 schedules, 74,247 receives and 180,845 applieds dropped on a
closed group and none touching freed state. Their controls fail: close as rule 2 first read,
resolving only proposals not yet admitted, leaves a read waiting in 32 of 234 schedules and an
open in 110 of 527; `receive` and `applied` through `Group&` reach freed state in 182,104 of
241,710.

**D9. A snapshot channel holds frame buffers only while a frame arrives.** Each per-peer
`Channel` embeds a `std::array<char, maximum_transfer_bytes>` (`snapshot_channel.h:185`;
128 + 24 + 65,536 bytes, `snapshot_transfer.h:37`), reserved for every peer at construction
(`snapshot_channel.h:44`). The channel keeps the 12-byte length prefix inline and allocates the
body at the frame's declared length (bounded by `maximum_transfer_bytes`) when its first byte
arrives, freeing it after `process()`. strata Track C D17's 8 KiB per idle group is reachable
only with this and D10.

**D10. One transport per reactor loop; a connection joins two loops that share groups.** Today
each group constructs its own `Peers`. Idle and fully connected, a three-voter member's `Peers`
holds 511,947 bytes and 5 descriptors, a five-voter member's 982,918 and 9 (job
261110-000909-400d3c): about 117,743 bytes per connection and 40,976 for its TLS context,
listener and object. From the code, it also holds 17 suspended frames (the accept loop, and per
connection its dial or accept, `run`, reader and writer) and 4 armed timers (each writer's wake).

*Placement and routing.* A frame never changes threads, so a connection carries only groups that
live on its loop at both ends. Every host places a group on loop `(slot × L) >> 12`: `slot`
hashes the group's opaque id into 4,096 slots alike on every host, and L, the host's loop count,
is any number from 1 to 4,096 (Xapiand runs a loop per processor, rarely a power of two; X4).
Each loop owns a contiguous range of floor or ceil(4,096 / L) slots, so loop i of a host with
L_a loops shares groups with at most ceil(L_b / L_a) + 1 loops of a peer host with L_b, and with
exactly one when L_a = L_b (`placement_test share`, every pair up to 64 loops; 12 against 16
loops, 2). Each loop listens on its own port (the host's base port plus the loop index), and
a peer's address carries its base port and L, so the dialing loop computes the remote loop of
every group: no lookup, no redirect, no hop at accept or on receive. The lower node id dials, as
today, when the first group the two loops share opens; the group-open exchange fixes the group's
route on the connection, and the connection closes after the last shared group closes. The
Control connection carries every shared group; a Bulk connection opens for one snapshot transfer
and closes after it, so a busy channel stalls only its own transfer (today's bulk reader pauses
its connection, `peers.h:395-412`). *`slot mod L`*, this section's first rule, needs a
power-of-two L: otherwise a loop of a 2-loop host shares groups with all 5 loops of a 5-loop host
(`placement_test mod`, exit 1). *Shared connections per process* lose: a frame for a group on
another loop hops threads on every receive. *A connection per pair of loops under free placement*
loses: up to L_b connections per peer host per loop, 16 at 16 loops.

*L may change at restart (X4).* Nothing durable is keyed by a loop index: a group's id, slot and
artifacts are its own, each loop's turn log is named by (generation, loop), and the host's
layout record names (L, generation). A host opened with another L re-partitions before it
serves:

1. Recover every log of the stored generation, the read every open does, and mark the groups
   recovery calls lost in the host's group table, synced, before anything else.
2. Write each group's live records into its new loop's log of the next generation, and sync each
   new log.
3. Commit by replacing the layout record, then delete the old generation's logs.

A lost group (journal's `TurnRecovery::lost`: a sequence gap, or records in a damaged block)
rejoins under a new id, and `TurnLog::append` refuses its old one (`turn_log.h:61-70`, `:206`).
That verdict exists only as damage in the log, and `lost_` is rebuilt from each open's recovery
(`turn_log.h:176`), so a clean next generation would forget it and let the old id append from
sequence 1: a consensus member with its term and vote forgotten under the same incarnation (R1).
Step 1's mark keeps it in the group table, which every open reads, and journal's `TurnLog`
opens with the table's lost ids (quest 3), so they stay refused whatever log holds the group.
Every open marks, so a later open also keeps the verdict after the damaged segment is gone.
A crash before the commit leaves the old generation authoritative, its damage and the marks
intact, and the next open starts again; after it, the new one is, and leftover old logs are
deleted. The cost is one write of the
live records, bounded by the retained-log budget (16 GiB), and one barrier per new log; a restart
with the same L copies nothing. An rf = 1 node keeps its data through a resize, with no replica to
re-create from. `placement_test remap` crashes after every durable step of 8->6, 6->12, 16->3 and
3->1 re-partitions of 2,000 groups, one in fifty lost to damage, and every group's records come
back exactly once, on its new loop, and every lost group stays lost, through two more restarts;
without the marks (`remap-forget`) lost groups come back. `repartition_lost` runs journal's own
damage scenario over the real RFTW0001 log: after a 1->2 re-partition the new log's recovery no
longer calls group 1 lost, and only the mark refuses its old id. *A fixed partition count P*, with loops serving partitions, loses: P / L barriers per loop
per turn for the host's life. *Turn logs per slot* lose: 4,096 logs, at least one 64 MiB segment
each, 256 GiB. A running host still does not move a hot group (Limits).

The connection hello carries `(base port, L, loop index)`. A dialer whose view of the peer's L
disagrees closes with `l_mismatch` and dials again with the hello's L, and a group-open whose slot
is not the receiving loop's is refused with `misrouted`, never served through a hop (F15). After
a restart that shrinks L, a stale view dials `base port + j` with j >= the new L, where nothing
listens or another process does (R2): 25% of groups for 8->6, 81% for 16->3. A dial that is
refused, fails its handshake or reads no hello therefore falls back to the peer's loop 0 (the
base port, which listens for every L >= 1), whose hello carries the current L; the dialer closes
with `l_mismatch`, updates its view of the peer, and dials the owning loop. A group reaches its
loop in at most three dials (`placement_test hello`, which counts these unbound dials apart from
`misrouted` group-opens; `shrink_dial`). A growing L needs no fallback: every stale target still
listens.

*An idle connection.* The loop transport drives OpenSSL on the non-blocking socket (`SSL_set_fd`,
readiness through `await_asio` on `async_wait`) with `SSL_MODE_RELEASE_BUFFERS`, and allocates
each frame at its length as today's `Session` does. An idle end then holds 40,732 bytes and one
descriptor, against 111,234 for an `asio::ssl::stream` end, today's `Session` (job
261110-005436-650916, 64 connections over loopback mutual TLS with the context `Peers`
configures; the context is 29,408 bytes per loop). Most of the 40 KB is OpenSSL's state after a
handshake; release mode returns the 33,792 bytes of record buffers.

*Cost.* A loop serving groups on H peer hosts with as many loops holds its context, a listener
and one Control connection per peer host: about `29 KB + H x 41 KB` and `1 + H` descriptors,
whatever its group count; a peer host with more loops adds up to ceil(L_b / L_a) per loop. At 16
loops per host that is 41 KB per peer host per loop, against 3.8 MB for a Control and a Bulk
connection per pair of loops at today's 118 KB. Connections grow with loops times peer hosts
(X12): at 16 loops and strata Track D3's bound of 64 peers a host holds 1,024 (42 MB at rest),
against 128 in Track C D12's two lanes per peer, and random placement over 100,000 nodes (1,325
peers) would hold 21,200 (863 MB). D10 therefore depends on the copyset bound. Keepalives run once
per node pair, not per loop pair, and Xapiand 7b's shard RPC uses these connections rather than a
second set. A node pair's keepalive does not cross every loop pair's connection, so a half-open
loop-pair connection under quiescent groups (Q4) is found only when a frame times out on it;
quest 5 says what detects one sooner and tests it (R12). A group holds a 16-byte route in the loop's
table, its per-group send queues (empty, unallocated while idle) and its admission state; the
per-group queue caps of `PeerOptions` stay per group, and the writer takes groups' control frames
round-robin, never behind more than one append frame (R19).

*The reader never waits on one group.* Each group keeps one input slot (today's per-connection
`input`, `peers.h:368-390`, moved to the group) and its share of the loop's incoming byte budget.
A Control frame for a group whose slot is full is dropped and counted per group, and the reader
reads the next frame. Raft tolerates the loss: the leader resends at its next heartbeat or after
`rpc_timeout`, and a vote is asked again at the next election. `reader_test` puts a group whose
admission is full for 3 s beside a follower heartbeating every 100 ms on one connection: today's
rule holds the follower's heartbeats for 3,000 ms, past its 1,000 ms minimum election timeout, so
it would campaign; dropping keeps its longest gap at 100 ms and drops 300 of the busy group's
frames.

*Identity (Q7).* TLS then authenticates a peer host's node rather than a group's voter. A frame
is accepted when its sender is a node the loop's pins authenticate for the cluster and it names
a group and destination incarnation the loop stores (open, or closed and reopened by the frame,
below; R7); `Core` then decides by term, incarnation and
log, as it already admits any authenticated incarnation (`core.h:756-759`; strata Track C D5).
`Core` treats `Receive::authenticated_peer` as authenticated, while node TLS authenticates only
the node, so where the membership names the frame's source incarnation, that incarnation's node
must be the connection's authenticated node; an incarnation the membership does not name (a
joiner's leader) is admitted as now (quest 5's security review).
Membership is not checked (X1): a joiner's membership is empty until the log names it, so it
would refuse its leader's first append and install, and a follower lagging an add-voter would
refuse the new leader. The group-open exchange carries what today's hello checks per group
(cluster, group, epoch, profile; `validate_hello`, `hello.h:48-54`), none of which is
membership.

*A stored group that is not open (X8).* Once Q4 gives groups a closed state, a frame naming a
group stored on this loop but closed opens it (state only) and is delivered; a frame for a group
not stored here is dropped and counted. Until then a loop keeps every stored group open.

## How Detent's owner loop moves

| Today | On the driver |
|---|---|
| readiness poll, 1 ms (`service.cc:1678-1687`) | `co_await driver.create(group)` / `recover` |
| replay poll, 1 ms (`service.cc:1694-1704`) | the application drains `Committed` from the sink until applied reaches committed |
| `node.service` + `dispatch` every 1 ms (`service.cc:1716-2606`, `node.cc:532-600`) | the driver steps the group on input, IO and deadlines; Detent's budgeted work (delivery, reads, health) wakes on a sink notification or a request |
| `Runtime::dispatch` spawning `AsioCompletionDriver::run` (`service.cc:1166-1210`) | the driver's IO path through `completion_ref` |
| `node.propose_prepared`, `take_completions` (`service.cc:2570-2581`) | `co_await driver.propose(group, ..., request_deadline.token())` |
| `Node::retirement_step` (`node.cc:136-145`) | `co_await driver.close(group)` |
| `AuthorityPeers` on `asio::awaitable` (`peers.hh:9`) | the loop's task transport (D10) |

## Cost

macOS x86-64, Apple clang 17, Release; heap is the allocator's usable size (OpenSSL's allocations
included for the transport). Rows at 1,000 groups, per group (job 261110-000912-e9dff1; the
runtime row at 8 runtimes; the transport rows per member of one group, job 261110-000909-400d3c):

| Per idle group | Heap bytes | Descriptors | Threads | `io_context` | Armed timers | Frames |
|---|---|---|---|---|---|---|
| journal, shared Store | 1,913 | 2 | 0 | 0 | 0 | 0 |
| consensus single voter, shared | 6,497 | 3 | 0 | 0 | 0 | 0 |
| consensus 3-voter member, no channel | 6,262 | 3 | 0 | 0 | 0 | 0 |
| the same with its `SnapshotChannel` | 170,902 | 3 | 0 | 0 | 0 | 0 |
| its transport today: own `Peers`, 3 voters | 511,947 | 5 | 0 | 0 | 4 (from code) | 17 (from code) |
| its transport today: own `Peers`, 5 voters | 982,918 | 9 | 0 | 0 | 8 (from code) | 33 (from code) |
| driver additions (designed): wheel entry, ready link | 48 | 0 | 0 | 0 | 0 (an entry) | 0 |
| transport on the driver (designed, D10): route | 16 | 0 | 0 | 0 | 0 | 0 |
| per loop, not per group (designed, D10): context, listener, a Control connection per peer host | 29,408 + H x 40,732 | 1 + H | 0 | 0 | 0 | 0 |
| Detent today: Worker + channel, own journal | 6,832 | 4 | 0 | 0 | 0 | 0 |
| Detent today: its runtime | 10,942 | 5 | 2 | 1 | 1 | 1 (`service.cc:578`, from code) |

**Where a three-voter member's 683 KB goes** (jobs 261110-000912-e9dff1, 261110-000913-e9e87d,
261110-000909-400d3c). 6,262 bytes are the Worker, its Core and its Store, as for a single voter.
164,640 are the `SnapshotChannel`: its 800-byte object, and one block holding two 66,088-byte
per-peer channels, each a 65,688-byte frame array and 400 bytes of state; the 132,176 bytes
requested come back as a 163,840-byte block from macOS's allocator. The reserve follows the voter
count, not the role, so a leader pays the same; Detent's single voter has no peers and pays
nothing. 511,947 are its `Peers`. D9 removes the arrays (the channel keeps its 800-byte object and
800 bytes of per-peer state, 1.6 KB) and D10 replaces the `Peers` with a 16-byte route: 6,262 +
1,600 + 48 + 16 = 7,926 bytes.

| Groups | Single voter + driver | 3-voter member today (+ its `Peers`) | 3-voter member + driver, D9 + D10 | Detent's layout today |
|---|---|---|---|---|
| 1 | 11.8 KB, 5 fds | 688 KB, 10 fds | n/m | 17.9 KB, 9 fds, 2 threads |
| 1,000 | 6.5 MB, 3,002 fds | 683 MB, 8,002 fds | 7.9 MB, 3,002 fds (designed) | 17.8 MB, 9,000 fds, 2,000 threads |
| 1,000,000 | 6.5 GB, 3 M fds | 683 GB, 8 M fds | 7.9 GB, 3 M fds | 17.8 GB, 9 M fds, 2 M threads |

One group includes its core's turn log; 1,000 groups are measured except the D9 + D10 column
(the sum of measured parts, plus the loops' transports: about `29 KB + H x 41 KB` and
`1 + H` descriptors per loop, D10) and Detent's 1,000 runtimes (from 8); the transport is measured
per member of one group and multiplied; a million multiplies the 1,000 row. Detent's layout
column is a single-voter authority, which starts no `AuthorityPeers` (`service.cc:1706`).

**Idle CPU** per idle group per second (job 261110-000911-f87d0f). Timers alone were measured; the
`run_one` rows multiply the measured wakes by its measured 1,548 ns (leader) or
1,761 ns (follower). The 1 ms timer wakes 786-795 times a second on macOS.

| Per idle group per second | 1 group | 1,000 groups |
|---|---|---|
| Detent today: a runtime per group, its 1 ms timer + one idle `run_one` per wake | 16,306 + 1,217 = 17,523 us | 17,523 us (derived: one runtime's, per runtime) |
| a shared 1 ms loop: 1,000 timers on one `io_context` + `run_one` | - | 288 + 1,231 = 1,519 us |
| wheel, 100 ms heartbeat (leader), + `run_one` | 302 + 15 = 317 us | 10.6 + 15.3 = 25.9 us |
| wheel, 1.5 s election timeout (follower), + `run_one` | 41.3 + 1.2 = 42.5 us | 7.8 + 1.2 = 9.0 us |

At 1,000 idle groups Detent's layout costs 17.5 cores today and the wheel
0.026 for leaders: 153,300 core-hours a year saved per 1,000 groups, roughly
$6,100 at an assumed $0.04 per core-hour. Against a shared 1 ms loop (1.52
cores) the saving is 13,100 core-hours, roughly $520. Across three runs of the bench
(261109-230225-963cb8, 261109-232742-f67aa2 and this one) today's rows and `run_one` agree
within 6%; the wheel rows, a few to a few hundred wakes a second, spread up to 27% (followers at
1,000 groups: 7.0-8.9 us), which moves no conclusion.

**One step.** A resume costs 9.0 ns through `inline_scheduler` and 131.0 ns
through `asio_scheduler`; a request frame is created and destroyed in 98.8 ns (pool off,
so malloc's time). Both are small beside the 1,548 ns of the step itself. A `Driver` call
from another loop adds two posts (D2).

## Limits

- **A million heartbeating groups do not fit.** A million leaders at ten heartbeats a second
  cost at least 26 cores of timer and step CPU before any heartbeat is sent: the
  1,000-group run is cache-resident and `run_one` was measured on one hot group, so at a million
  groups (7.9 GB) every step is colder. A million idle groups need quiescence (strata Track C
  D12: the leader stops heartbeating, followers stop their timers); a group with no deadline
  holds no entry, but `Core` does not implement it (Q4). Xapiand's real mix (a third leading,
  two thirds following) comes to about 30 cores. Open idle groups still cost 6.5-7.9 KB and 3
  descriptors each against strata's target of under 1 KB and none, so Xapiand's groups follow
  its catalog: closed when idle, reopened on demand. That needs a lone voter leading at `Start`
  (X5), Q4 with a closed state a frame reopens (X8), and Q3 at zero descriptors (X9).
- **Descriptors.** A shared-layout consensus group holds three descriptors while idle (the
  Store's two, one the Worker's): 3 million at a million groups (Q3). Today's per-group
  transport adds five more per three-voter member (D10 removes them; a loop holds `1 + H`).
- **An idle `run_one` allocates** (1.61 per call for a leader); the driver calls it ten times a
  second instead of a thousand, which hides it but does not remove it.
- **Silent failure: a lost wake.** A group that should be ready and is not, or whose entry was
  not re-armed after a step, stalls with no error. The consensus quest's simulator checks
  liveness under every schedule; a Debug assertion checks after each step that a group with a
  deadline is armed; the blocking entry throws `stalled` instead of waiting, a live token
  included (D5).
- **A group moves between loops only at restart**, when L changes (D10); a running host cannot
  move a hot group to an idle loop.
- **Far deadlines wake early** once per 4,096 ms round, and every wheel timer fires up to 1 ms
  late, at its slot's end (D4).
- **One primitive per queue** serializes a loop's IO (D6).
- Numbers are from one macOS x86-64 host; allocator rounding differs on Linux, the performance
  gate host (November 2, item 7), where the consensus and Detent quests measure again.

## Build plan

Each quest leaves every repository working; the Worker pump API is deleted last.

| # | Quest (`--repo`) | Files | Criteria | Risk | Needs |
|---|---|---|---|---|---|
| 1 | `task-wheel` (task) | `task.hh`, `task_asio.hh`, tests, bench | `task::timer_wheel` (D4): O(1) arm and cancel, no allocation (job); `expire` tolerates a fire cancelling or re-arming any entry, itself included (the bench's `wheel_test reentry`, `chain` and the review probe as tests) and matches a reference model under random arm, cancel and re-arm from fires; deadlines beyond a round never fire early, `floor(d / round)` early wakes; owner-thread Debug assertions; `deadline<S>` on `S::clock`, `inline_scheduler::advance` expires it with no real time; a parent and children on two loops under TSan and ASan; `asio_scheduler::start_timer` on the wheel with task's deadline contract unchanged (created and destroyed on any thread: `asio_test.cc:371-390` passes as is, plus `wheel_test offloop` as a test, under TSan and ASan; the entry in the deadline's cancel state, no added allocation; a wheel destroyed with deadlines armed releases them); `cancellation.md` and the D4 users table unchanged; `scheduler_ref` source-compatible, gaining `wheel()` (X14); the owner check binds when the loop starts; `inline_scheduler` gains a same-thread `post_call`; `task::blocking_scheduler` (D5, X3), Xapiand's `engine::BlockingScheduler` with the wheel: thread-safe `post` and `post_call` that wake its wait, and no wait without an armed timer (the `blocking_entry threads` cases as tests, `lost-wake-live-token` included, under TSan; R3); D4's list of what moves onto the wheel and what stays (X7); Xapiand lead reviews the diff | medium | Q1 |
| 2 | `completion-seam` (completion) | `operation.h`, `completion.h`, the queues, `asio_completion.h`, tests | `SyncData` on every backend (fault tests); `completion_ref` with a token, `InlineCompletion`, `HeldCompletion`; `drive_synchronously` is `InlineCompletion`'s loop; turn primitives ahead of artifact and snapshot IO, measured (X13) | low | - |
| 3 | `journal-driver` (journal) | `turn_driver.h`, `store.h`, `journal.h`, tests | `TurnDriver::run` and `Store` tasks (D2); crash-point suites through `HeldCompletion` with the sync driver's histories; `journal::run` over a shared backend with no `io_context`, committing turns inline (D5): it returns only after staged records are written, an append cancelled after staging included (D8), and throws `journal::stalled` (a `std::logic_error` carrying the waiting groups' ids, D5) on every lost wake, a live token included (the `blocking_entry rules` cases and `lost-wake-live-token` as tests), over `blocking_scheduler` on a steady clock; the owner tag refuses a turn log a `Driver` holds; Q3 (b)'s layout with X9's five conditions: opaque group ids, reclamation and admission per group by name with no directory scan, a per-host group table that reopens a closed group without scanning the turn log, a strata index in its own nested directory that a checkpoint names, zero descriptors per idle group (idle-cost job); turn logs named by (generation, loop), each in its own subdirectory of the host's directory so RFTW0001's scan of one directory for `turns-<n>` (`turn_log.h:92-104`, `:376-378`) stays as is (R13), and the re-partition when L changes at open, crash-tested at every durable step over a damaged old generation: every open marks recovery's lost ids in the group table and `TurnLog` opens with them, so a lost group stays lost after the commit and after a crash at every step (as `placement_test remap` and `repartition_lost`; X4, R1); no Asio in `include/` (grep job) | medium | 1, 2 |
| 4 | `consensus-driver` (consensus) | `core.h` (`next_deadline`, a lone voter's campaign at `Start`), new `driver.h`, `snapshot_channel.h` (D9), tests | `simulation_test` through `Driver` on `inline_scheduler` + `HeldCompletion`; Core campaigns at `Start` when this node is the group's only voter and there is no witness, so a lone voter's first proposal after `create` and after `recover` succeeds with no clock advance (`lone_voter`, which covers `create`, passes, and a test covers `recover`; X5, R9); calls by group id through the loop's group table, `receive` and `applied` included, a closing or closed group dropping and counting them (R4); the waiter protocol tested per interleaving with every cross-loop post a scheduling point and freed frames checked (as `waiter_test`; the review's resume-before-unlink mutants fail it) and D8's table row by row, `create`, `recover` and `close` included; close with linked waiters of every kind, a cancel racing close and teardown with a linked waiter (as `waiter_test close`, `close-read`, `close-open`, `close-late` and `teardown`, their controls failing, under ASan; X2, R4), and close racing a cancel and teardown on real threads under TSan (R11); the host-wide two-step shutdown; every call from another loop under TSan; `consensus::run` per D5's five steps (the `blocking_entry rules` cases: `stalled` on a lost wake with a heartbeat armed, `needs_peer` for a `propose`/`read` on a group with a voting member (voter or witness) other than this node, a lone voter beside a witness included (R6), a lone voter's first proposal, staged-then-cancelled written before return; over `blocking_scheduler`, a cross-thread cancel toward a deadline returns `cancelled` and a lost wake under a live token throws `stalled`, under TSan) and its ownership assertion; idle-cost job: 0 threads, frames, armed timers per group, 3-voter member's consensus state within 8 KiB; Worker suites green; ASan+UBSan and TSan | high | 3 |
| 5 | `consensus-transport` (consensus) | `transport/` on task, tests | one transport per loop (D10) beside today's `Peers`: placement by `(slot × L) >> 12` for any L (as `placement_test map` and `share`), a port per loop, a Control connection per pair of loops sharing groups and a Bulk connection per transfer, no thread hop on receive (TSan job with two loops per host); idle-cost job: per idle group at most 64 bytes, 0 descriptors, frames or timers, an idle connection end within 48 KB, per loop `29 KB + H x 41 KB` within 10%; a group with full admission does not delay another group's heartbeat on the same connection (as `reader_test`); host authentication, the per-frame rule (an authenticated node of the cluster, a group and destination incarnation the loop stores, and, where the membership names the source incarnation, its node the authenticated node; R7) and the hello's checks in the group-open exchange (security review, Q7); a joiner installs and catches up over a shared connection, and a follower lagging an add-voter catches up from the new leader (X1); a dialer with a stale view of a peer restarted with a different L is closed with `l_mismatch` and redials with the hello's L, a dial a shrunk peer refuses falls back to its loop 0, a lower-id dialer with a stale view included, a misrouted group-open is refused with `misrouted`, and a host restarted with another L serves every group from its new loop (`l_mismatch_test`, as `placement_test hello`; R2, R9); keepalives once per node pair, measured (X12), and a half-open loop-pair connection under quiescent groups detected within a stated bound, tested (R12); no `asio::awaitable` under `include/` once `Peers` leaves | high | 4, Q7 |
| 6 | `detent-on-drivers` (Detent) | `src/detent_authority/` | the owner-loop table; `AuthorityPeers` onto the loop transport; no 1 ms timer in the authority (grep and idle-CPU jobs); the `native` suite; CPU per request and p99 within the November 2 item 7 gate | high | 5 |
| 7 | `worker-pump-removal` (consensus, then a Detent pin) | `worker.h`, `transport/peers.h`, their tests | `run_one`, `take_io_operation`, `take_actions`, `try_submit`, `try_submit_retained`, `storage_operation_failed` and today's `Peers` leave the public API; a grep job over Detent and consensus finds no caller | medium | 6 |

Quests 1 and 2 are additive; 3, 4 and 5 add drivers and the loop transport beside the existing
APIs, so Detent keeps building on its pins throughout; 6 bumps Detent's journal and consensus
pins and migrates the only caller, `AuthorityPeers` included, in one step; 7 deletes. Quiescence
(Q4) is a separate consensus quest after 4, with a closed state that a frame or a request
reopens; Xapiand 7b needs it before it writes bytes (X8).

## Open questions for the owner

- **Q1. Where the wheel lives.** (a) task, beside `inline_scheduler` and `asio_scheduler`, so the
  simulator's clock, the reactor's timers and `task::deadline` are one mechanism; (b) reactor;
  (c) consensus. *Recommendation: (a).*
- **Q2. Output without a waiting caller.** (a) a `Sink` the host supplies, called by the driver;
  (b) a per-loop outbox the host drains with one coroutine, one more copy per action. A per-group
  awaitable is excluded by item 4. *Recommendation: (a).*
- **Q3. Descriptors per idle group.** (a) keep a directory and owner lock per group, 3 million
  descriptors at a million groups; (b) one directory and owner lock per host, not per core, so
  L may change (D10), with artifacts named by opaque group id, a journal on-disk layout change to
  settle before Xapiand 7b writes bytes. *Recommendation: (b), designed in quest 3.* Xapiand
  answers (b) on X9's five conditions, all quest 3 criteria.
- **Q4. Quiescence.** strata Track C D12 needs `Core` changes (`Wake`, `LeaderGone`, quiescent
  followers) outside this split. *Recommendation:* a consensus quest after quest 4; without it
  the million-group row holds for bytes but not for CPU. Xapiand needs it before 7b, with a
  closed state a frame or a request reopens, so groups follow its catalog (X8).
- **Q5. The split.** The quest named three repositories; this plan adds task (the wheel) and
  completion (the seam and `SyncData`). *Recommendation: accept both*; the drivers need them.
- **Q6. Kronuz/scheduler.** Xapiand uses it (`src/database/pool.cc`, `src/logger_fwd.h`). (a)
  Xapiand's users move onto the wheel and the library retires; (b) on-loop timers move onto the
  wheel and it stays for work that is not on a reactor loop. *Recommendation: (b)*, Xapiand's
  answer (X6): `AsioHost::arm` and the catalog's `DB-TIMER` move (D4), Kronuz/logger's deferred
  lines stay, armed from any thread and run by its LOG thread with no loop to own a wheel. That is
  one mechanism per thread contract, an owner-thread wheel and an any-thread scheduler. D4's
  `SchedulerQueue` row measures the wheel's workload, not the scheduler's, so retiring the
  library needs the logger's workload measured first, and is the owner's call.
- **Q7. Transport identity (D10).** A connection shared by a loop's groups authenticates a peer
  host's node, not one group's voter. (a) one certificate per node, pinned once per loop, a
  frame accepted from any node the pins authenticate for the cluster when it names a group and
  destination incarnation the loop stores (Core decides the rest, X1; R7), and the hello's per-group
  checks (cluster, group, epoch, profile) in the group-open exchange; today's per-group `PeerOptions::pins` become the loop's
  per-node pins, which every group naming that node must agree on; (b) keep a `Peers` per group
  and pay 511,947 bytes and 5 descriptors per idle three-voter member; (c) per-group pins carried
  in the group-open exchange over the shared connection. *Recommendation: (a)*, with quest 5's
  security review; Xapiand agrees for a trusted cluster with honest faults.

## Rerunnable checks

| Criterion | Check |
|---|---|
| A1, A2, A3, A5, A6 | this document; the Xapiand lead's review (A8) |
| A4, A7 | `run.sh idle_cost` (261110-000912-e9dff1), `run.sh driver_cost` (261110-000911-f87d0f), `run.sh channel_layout` (261110-000913-e9e87d), `run.sh peers_cost 3 5` (261110-000909-400d3c), `run.sh conn_cost 64` (261110-005436-650916), pinned with `--ref` to the commits above |
| D4 (review F1, F2, F13) | `run.sh wheel_test` (261110-005430-687d57), under ASan+UBSan (261110-005428-f43eb1) and TSan (261110-005429-afba6f); `wheel_test offloop-embedded`, which must abort (261110-005431-3ee08b); the review's probe (261110-000910-2d7f1a; it crashed at 128df91, 261109-233203-d822ec) |
| D8 (review F3, F4, F14; Xapiand X2) | `run.sh waiter_test all` (261110-042826-5c2da9), under ASan+UBSan (261110-042827-fb0e1d) and TSan (261110-042829-5f86fb), its `close` and `teardown` controls failing as designed; the review's mutant script at Detent:672e7b7, each mutant killed by `explore` (261110-005423-d753e6, 261110-005426-635ab8; before the fix they survived, 261110-002555-485959, 261110-002557-76c931) |
| D5 (review F5, F11; Xapiand X3, X5) | `run.sh blocking_entry all` (261110-031128-58310d), `blocking_entry threads` under TSan (261110-031130-32585e); `blocking_entry inline`, which must fail (261110-031132-189172); `run.sh lone_voter`, which fails until quest 4 (261110-031138-999c25); `blocking_entry v1`, the first rule alone, which must fail (261110-005433-b5324c) |
| D10 (review F12; Xapiand X4) | `run.sh conn_cost 64` (261110-005436-650916); `run.sh reader_test` (261110-005434-15e1c5) and `reader_test today`, which must fail (261110-005435-c43e84); `run.sh placement_test all` (261110-031134-9f199a) and `placement_test mod`, which must fail (261110-031136-e8b259) |

## Status

Designed, not built. The wheel, the waiter protocol with close and teardown, the deadline on a
wheel, the blocking entry's loop (over a model group), the blocking scheduler, the reader's drop
rule (over a model connection) and placement with its re-partition (over a model of the turn
logs) exist as tested prototypes in the bench, and one idle connection of D10's shape is
measured; nothing else here has run. Xapiand's review (A8) took D2's API with no bridge, D8's
shape, D5's rules, Q3 (b), Q6 (b) and Q7 (a) with the fixes above; it did not review Detent's
owner-loop migration, Q1, Q5, or Q2 beyond the Sink.
