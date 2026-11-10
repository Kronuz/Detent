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
transport in 261110-000909-400d3c (strict). The threading rules are tested by the bench's
`wheel_test`, `waiter_test` and `blocking_entry` (Rerunnable checks).

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
completion on the calling thread with inline IO, for Xapiand's device chores and the
deterministic simulator.

There is one timer mechanism: an intrusive wheel per reactor loop, serving group timers and
operation deadlines alike, with the loop arming one `asio::steady_timer` at `wheel.next()`. It
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
class TurnDriver {      // one per core
	task::task<void> run(completion_ref io);   // awaits staged records, writes, sync_datas, completes
};
class Store {           // per group; each awaits its operation through the seam
	task::task<Frontier> append(AppendReservation&&, std::string_view, completion_ref, task::cancel_token = {});
	// likewise prepare/write/finish artifact, publish, verify, reclaim_step
};
template <class T> T run(TurnDriver&, task::task<T>);   // the blocking entry (D5)
}
namespace kronuz::consensus {
struct Seams { task::scheduler_ref scheduler; task::timer_wheel& wheel; journal::completion_ref io; Transport& transport; };
template <class Sink> class Driver {   // one per reactor loop
	Driver(Seams, Sink&);
	task::task<void> run();             // the one long-lived frame
	Group& open(GroupOptions);          // state only
	task::task<void> create(Group&, Identity, task::cancel_token = {});
	template <class Restore> task::task<void> recover(Group&, Restore, task::cancel_token = {});
	task::task<Placement> propose(Group&, Propose, task::cancel_token = {});
	task::task<ReadReady> read(Group&, Read, task::cancel_token = {});
	void receive(Group&, Receive);      // marks the group ready
	void applied(Group&, Applied);
	task::task<void> close(Group&);     // retirement until drained
};
template <class Sink, class T> T run(Driver<Sink>&, task::task<T>);   // the blocking entry (D5)
}
```

Every `Driver` call touches its group only on the group's loop. A task call (`create`,
`recover`, `propose`, `read`, `close`) made on another loop first moves there
(`co_await task::schedule_on(group's scheduler)`, one post), links its waiter, and resumes the
caller on the caller's own scheduler through D8's protocol; on the group's loop it neither posts
nor hops. `open`, `receive` and `applied` are plain calls on the group's loop (Debug-asserted
with `scheduler_ref::runs_here()`); another thread reaches them with `post_call`. The waiter
test runs every caller on loop B against a group on loop A (D8).

`Sink` receives what no caller awaits (`Send`, `Committed`, `RoleChanged`, snapshot and repair
notices) with static dispatch and no allocation (Q2). Detent's asio adapter already exists:
`task::asio_scheduler` (`task_asio.hh:58-166`) and `AsioCompletionDriver` behind `completion_ref`.
The transport moves to `task::task` with `task::await_asio` (`task_asio.hh:236-423`) and to one
instance per loop (D10), removing the last `asio::awaitable` from consensus's public headers.
This is the contract `task:driver`.

**D3. Injection through three seams.** The scheduler (`scheduler_ref`, `task.hh:169-199`) says
where frames resume; the wheel (D4) is the clock and the timers; `completion_ref` is the IO:

| `completion_ref` | Who | How a primitive completes |
|---|---|---|
| `AsioCompletionDriver` (exists) | Detent, Xapiand | native queue, notification descriptor on the loop |
| `InlineCompletion` | blocking entry (D5), tests | `execute_primitive` on the caller's thread, the loop of `drive_synchronously` |
| `HeldCompletion` | strata's simulator | queued; the simulator completes, fails or drops each one, at a crash point of its choice |

strata's simulator runs every group on one `task::inline_scheduler` (`task.hh:311-349`) over
journal's `MemoryIO` and repeats: run the scheduler until nothing is queued; then deliver one
message, complete one held IO, or advance its manual clock to `wheel.next()` and expire. No real
time, no threads; every choice comes from its seed.

**D4. One intrusive timer wheel per reactor loop, the one timer mechanism.** An entry is 32
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
  `S` declares a `clock` (`inline_scheduler` declares its manual clock, `asio_scheduler`
  `steady_clock`), and `steady_clock::time_point` otherwise, so today's callers compile
  unchanged. `wheel_test manual` fires a five-second deadline on the manual clock in
  34 us of real time.
- *Threads.* A wheel, and every entry in it, is armed, cancelled, expired and destroyed only on
  its loop's thread (Debug-asserted; the prototype binds the thread on first use). A deadline
  lives on the loop that created it; a child on another loop creates its own deadline on its
  own loop's wheel, linked to the parent's token, and task's linked cancel state is what crosses
  threads (`cancel_state::remove` and `make`, `task.hh:911-975`). Firing and unlinking then
  happen on one thread, so no expiry outlives its deadline and a wheel timer needs no disarm
  bit. A wheel-backed `deadline` is destroyed on its loop, narrowing task's "any thread"
  (`cancellation.md`), which still holds for `asio_scheduler`'s own timers. `wheel_test loops`
  runs a parent on loop A with children created and destroyed on loop B while A fires, under
  TSan and ASan.
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

**D5. The blocking entry runs the same task on the caller's thread, and owns its groups.**
`journal::run(turns, task)` and `consensus::run(driver, task)` build their machine over a
`task::inline_scheduler` and `InlineCompletion` and drain it on the calling thread with no
`io_context`. Whenever the scheduler's queue empties before the task finishes, the loop:

1. Commits the core's pending turn inline (`TurnDriver::commit` over `InlineCompletion`), when
   records are staged: a shared-backend append completes only at its turn's barrier
   (`store.h:328-333`), and `blocking_entry` shows one staged, neither done nor in flight, until
   the inline commit completes it with no thread added.
2. Otherwise advances to `wheel.next()` and expires: a manual clock jumps there (simulator,
   tests); a steady clock sleeps until then, so a single voter's first election waits its
   election timeout (1-2 s at `Timing`'s defaults) in real time.
3. Otherwise throws: the task can never finish (a lost wake), and the entry reports it instead
   of hanging.

`task::sync_wait` is not the blocking entry: it waits for work other threads drive
(`task.hh:744-792`), and over a shared-backend `Store` with no one running its `TurnDriver` it
never returns. The entry owns its groups and their core's `TurnDriver` for its duration: a
group or turn log a reactor `Driver` has open is refused (an owner tag set by `Driver::open` and
by `run`, checked on every step), so a WorkPool chore runs only on cores no loop serves. It
serves what needs no peer: create, recover, checkpoint, close, a single voter's proposals.
`drive_synchronously` becomes `InlineCompletion`'s loop: one implementation, driven two ways.

**D6. One completion queue per reactor loop, shared by its groups.** A queue costs a thread and
descriptors, so it belongs to the loop. A turn write needs a `SyncData` primitive, which
completion lacks (`operation.h:41`; `File::sync_data`, `io.h:84`, is synchronous only):
completion adds it (io_uring `IORING_FSYNC_DATASYNC`, `aio_fsync(O_DSYNC)` on FreeBSD, the
fallback worker's `sync_data`). A multi-slot queue is a later optimization, measured when a loop's
IO queues behind one primitive.

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
| a proposal `Core` accepted, its entry in Core's log or its record staged in a turn | the caller completes with `task::cancelled`; the record is written and may commit, reaching the application through `Committed` (Raft has no withdraw) |
| a read awaiting its read index or the apply that reaches it | the caller completes with `task::cancelled`; the slot is dropped when the index arrives |
| a caller-owned IO primitive (an artifact chunk, a verify read) not yet queued | not issued |
| a caller-owned IO primitive in the kernel | completes into the machine (no torn write); only the caller is released |
| `create` or `recover` in progress | the caller completes with `task::cancelled`; the machine finishes its current durable step under the shutdown token and the group stays open, so a later `create`/`recover` resumes it or `close` retires it |
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
no waiter. A loop that shuts down cancels its groups' waiters before it stops accepting
`post_call`. The prototype is `async-drivers-bench/waiter.hh`; `waiter_test` explores every
interleaving of callers on loop B, the group on loop A, the deadline on loop C and the turn write
on an IO loop: 17, 2,910 and 938,978 schedules for one, two and three callers, each with exactly
one resume per caller, no freed frame reached, and the table's first two rows holding (cancelled
before admission: never staged; staged: written and committed). The same exploration over the
first version of this protocol (the callback posting the resume straight to the caller) reaches a
freed frame in 12 of 19 and 2,308 of 2,860 schedules. *`when_any` against a timer* loses: item 2
rules it out and it needs a frame per race. *A per-group timeout* loses: a fan-out spans groups.

**D9. A snapshot channel holds frame buffers only while a frame arrives.** Each per-peer
`Channel` embeds a `std::array<char, maximum_transfer_bytes>` (`snapshot_channel.h:185`;
128 + 24 + 65,536 bytes, `snapshot_transfer.h:37`), reserved for every peer at construction
(`snapshot_channel.h:44`). The channel keeps the 12-byte length prefix inline and allocates the
body at the frame's declared length (bounded by `maximum_transfer_bytes`) when its first byte
arrives, freeing it after `process()`. strata Track C D17's 8 KiB per idle group is reachable
only with this and D10.

**D10. One transport per reactor loop, shared by its groups.** Today each group constructs its own
`Peers`. Idle and fully connected, a three-voter member's `Peers` holds 511,947 bytes and 5
descriptors, a five-voter member's 982,918 and 9 (job 261110-000909-400d3c): about 117,743 bytes
per connection and 40,976 for its TLS context, listener and object. From the code, it also holds
17 suspended frames (the accept loop, and per connection its dial or accept, `run`, reader and
writer) and 4 armed timers (each writer's wake). The design keeps one transport per loop: one
listener, a Control and a Bulk connection per peer host the loop's groups talk to, and every frame
carrying a group route both ends agree on when the group opens on the connection. A group holds a
16-byte route in the loop's table, its per-group send queues (empty, unallocated while idle) and
its admission state; the per-group queue caps of `PeerOptions` stay per group, and the writer
takes groups' control frames round-robin ahead of any bulk chunk (R19). A loop serving groups
across H peer hosts pays about `41 KB + 2H x 118 KB` and `1 + 2H` descriptors, whatever its group
count. TLS then authenticates a peer host's node identity rather than a group's voter, and each
frame is accepted only for groups whose configuration has that node as a voter (Q7). *Shared
connections per process* lose: a frame for a group on another loop would hop threads on every
receive.

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
(the sum of measured parts, plus the loops' transports: about `41 KB + 2H x 118 KB` and
`1 + 2H` descriptors per loop) and Detent's 1,000 runtimes (from 8); the transport is measured
per member of one group and multiplied; a million multiplies the 1,000 row. Detent's layout
column is a single-voter authority, which starts no `AuthorityPeers` (`service.cc:1706`).

**Idle CPU** per idle group per second (job 261110-000911-f87d0f). Timers alone were measured; the
`run_one` rows multiply the measured wakes by its measured 1,548 ns (leader) or
1,761 ns (follower). The 1 ms timer wakes 786-795 times a second on macOS.

| Per idle group per second | 1 group | 1,000 groups |
|---|---|---|
| Detent today: a runtime per group, its 1 ms timer + one idle `run_one` per wake | 16,306 + 1,217 = 17,523 us | 17,523 us (1,000 runtimes) |
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
  holds no entry, but `Core` does not implement it (Q4).
- **Descriptors.** A shared-layout consensus group holds three descriptors while idle (the
  Store's two, one the Worker's): 3 million at a million groups (Q3). Today's per-group
  transport adds five more per three-voter member (D10 removes them).
- **An idle `run_one` allocates** (1.61 per call for a leader); the driver calls it ten times a
  second instead of a thousand, which hides it but does not remove it.
- **Silent failure: a lost wake.** A group that should be ready and is not, or whose entry was
  not re-armed after a step, stalls with no error. The consensus quest's simulator checks
  liveness under every schedule; a Debug assertion checks after each step that a group with a
  deadline is armed; the blocking entry throws instead of waiting (D5).
- **Far deadlines wake early** once per 4,096 ms round (D4).
- **One primitive per queue** serializes a loop's IO (D6).
- Numbers are from one macOS x86-64 host; allocator rounding differs on Linux, the performance
  gate host (November 2, item 7), where the consensus and Detent quests measure again.

## Build plan

Each quest leaves every repository working; the Worker pump API is deleted last.

| # | Quest (`--repo`) | Files | Criteria | Risk | Needs |
|---|---|---|---|---|---|
| 1 | `task-wheel` (task) | `task.hh`, `task_asio.hh`, tests, bench | `task::timer_wheel` (D4): O(1) arm and cancel, no allocation (job); `expire` tolerates a fire cancelling or re-arming any entry, itself included (the bench's `wheel_test reentry`, `chain` and the review probe as tests) and matches a reference model under random arm, cancel and re-arm from fires; deadlines beyond a round never fire early, `floor(d / round)` early wakes; owner-thread Debug assertions; `deadline<S>` on `S::clock`, `inline_scheduler::advance` expires it with no real time; a parent and children on two loops under TSan and ASan; `asio_scheduler::start_timer` on the wheel; `scheduler_ref` source-compatible; Xapiand lead reviews the diff | medium | Q1 |
| 2 | `completion-seam` (completion) | `operation.h`, `completion.h`, the queues, `asio_completion.h`, tests | `SyncData` on every backend (fault tests); `completion_ref` with a token, `InlineCompletion`, `HeldCompletion`; `drive_synchronously` is `InlineCompletion`'s loop | low | - |
| 3 | `journal-driver` (journal) | `turn_driver.h`, `store.h`, `journal.h`, tests | `TurnDriver::run` and `Store` tasks (D2); crash-point suites through `HeldCompletion` with the sync driver's histories; `journal::run` over a shared backend with no `io_context`, committing turns inline (D5); an append cancelled after staging is written (D8); the owner tag refuses a turn log a `Driver` holds; no Asio in `include/` (grep job) | medium | 1, 2 |
| 4 | `consensus-driver` (consensus) | `core.h` (`next_deadline`), new `driver.h`, `snapshot_channel.h` (D9), tests | `simulation_test` through `Driver` on `inline_scheduler` + `HeldCompletion`; the waiter protocol tested per interleaving (as `waiter_test`) and D8's table row by row, `create`, `recover` and `close` included; every call from another loop under TSan; blocking-entry test and its ownership assertion; idle-cost job: 0 threads, frames, armed timers per group, 3-voter member's consensus state within 8 KiB; Worker suites green; ASan+UBSan and TSan | high | 3 |
| 5 | `consensus-transport` (consensus) | `transport/` on task, tests | one transport per loop (D10) beside today's `Peers`; idle-cost job: per idle group at most 64 bytes, 0 descriptors, frames or timers, per loop and per connection measured; host authentication and the per-group voter check (security review, Q7); no `asio::awaitable` under `include/` once `Peers` leaves | high | 4, Q7 |
| 6 | `detent-on-drivers` (Detent) | `src/detent_authority/` | the owner-loop table; `AuthorityPeers` onto the loop transport; no 1 ms timer in the authority (grep and idle-CPU jobs); the `native` suite; CPU per request and p99 within the November 2 item 7 gate | high | 5 |
| 7 | `worker-pump-removal` (consensus, then a Detent pin) | `worker.h`, `transport/peers.h`, their tests | `run_one`, `take_io_operation`, `take_actions`, `try_submit`, `try_submit_retained`, `storage_operation_failed` and today's `Peers` leave the public API; a grep job over Detent and consensus finds no caller | medium | 6 |

Quests 1 and 2 are additive; 3, 4 and 5 add drivers and the loop transport beside the existing
APIs, so Detent keeps building on its pins throughout; 6 bumps Detent's journal and consensus
pins and migrates the only caller, `AuthorityPeers` included, in one step; 7 deletes. Quiescence
(Q4) is a separate consensus quest after 4.

## Open questions for the owner

- **Q1. Where the wheel lives.** (a) task, beside `inline_scheduler` and `asio_scheduler`, so the
  simulator's clock, the reactor's timers and `task::deadline` are one mechanism; (b) reactor;
  (c) consensus. *Recommendation: (a).*
- **Q2. Output without a waiting caller.** (a) a `Sink` the host supplies, called by the driver;
  (b) a per-loop outbox the host drains with one coroutine, one more copy per action. A per-group
  awaitable is excluded by item 4. *Recommendation: (a).*
- **Q3. Descriptors per idle group.** (a) keep a directory and owner lock per group, 3 million
  descriptors at a million groups; (b) one directory and owner lock per core with artifacts
  named by group, a journal on-disk layout change to settle before Xapiand 7b writes bytes.
  *Recommendation: (b), designed in quest 3.*
- **Q4. Quiescence.** strata Track C D12 needs `Core` changes (`Wake`, `LeaderGone`, quiescent
  followers) outside this split. *Recommendation:* a consensus quest after quest 4; without it
  the million-group row holds for bytes but not for CPU.
- **Q5. The split.** The quest named three repositories; this plan adds task (the wheel) and
  completion (the seam and `SyncData`). *Recommendation: accept both*; the drivers need them.
- **Q6. Kronuz/scheduler.** Xapiand uses it (`src/database/pool.cc`, `src/logger_fwd.h`); with
  the task wheel it is a second timer mechanism. (a) Xapiand's users move onto the wheel and the
  library retires; (b) it stays for work that is not on a reactor loop. *Recommendation: (a)*,
  on the D4 numbers, decided with the Xapiand lead after quest 1.
- **Q7. Transport identity (D10).** A connection shared by a loop's groups authenticates a peer
  host's node, not one group's voter. (a) one certificate per node, pinned once per loop, and a
  per-frame check that the node votes in the frame's group; (b) keep a `Peers` per group and pay
  511,947 bytes and 5 descriptors per idle three-voter member; (c) per-group pins carried in a
  group-open exchange over the shared connection. *Recommendation: (a)*, with quest 5's security
  review.

## Rerunnable checks

| Criterion | Check |
|---|---|
| A1, A2, A3, A5, A6 | this document; the Xapiand lead's review (A8) |
| A4, A7 | `run.sh idle_cost` (261110-000912-e9dff1), `run.sh driver_cost` (261110-000911-f87d0f), `run.sh channel_layout` (261110-000913-e9e87d), `run.sh peers_cost 3 5` (261110-000909-400d3c), pinned with `--ref` to the commits above |
| D4 (review F1, F2) | `run.sh wheel_test` (261110-000902-6240b4), under ASan+UBSan (261110-000905-c1d3b9) and TSan (261110-000906-2730b5); the review's probe (261110-000910-2d7f1a; it crashed at 128df91, 261109-233203-d822ec) |
| D8 (review F3, F4) | `run.sh waiter_test all` and `explore 3` (261110-000904-28d8d8), under ASan+UBSan and TSan (the same two jobs) |
| D5 (review F5) | `run.sh blocking_entry` (261110-000908-5a484f) |

## Status

Designed, not built. The wheel, the waiter protocol and the deadline on a wheel exist as tested
prototypes in the bench; nothing else here has run. From review: whether D1's per-loop driver and
D2's API fit Xapiand's per-index turn (one lease holding the journal append and the apply),
whether D8's cancellation table is the contract Xapiand's fan-out needs, Q2's sink, Q3's layout
change and Q7's transport identity.
