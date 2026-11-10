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
touches). Every number is a campaign job at Detent:87df68b or Detent:646cfb7 pinned to those
commits, run by `docs/design/async-drivers-bench/run.sh`: idle bytes, descriptors and threads in
261109-230223-1f63c8 (strict); frames, resumes, wheels, steps and idle CPU in
261109-230225-963cb8 (exclusive); the snapshot channel's layout in 261109-232231-bced6c (light).

## Abstract

Today a group is pumped. Detent's authority runs one consensus group per process on an
`OwnerLoop` thread and polls it every millisecond (`service.cc:2594-2606`), calling
`Worker::run_one` (`node.cc:557`) whether or not anything happened. Idle, that costs 17.7 ms of
CPU per second per authority (16.4 ms for the loop's timer, 1.3 ms for the idle `run_one` calls),
two threads, one `io_context`, one armed timer and nine descriptors.

The design turns the group into state. One driver coroutine per reactor loop steps only the
groups that have work: an input, a completed IO, or a deadline that came due in one shared
timer wheel. An idle group holds a 24-byte wheel entry and a 16-byte ready-list link beside the
library state it already has, and no thread, `io_context`, armed timer or coroutine frame. The
same step code runs a second way, to completion on the calling thread with inline IO, for
Xapiand's device chores and the deterministic simulator.

There is one timer mechanism: an intrusive wheel per reactor loop, serving group timers and
operation deadlines alike, with the loop arming one `asio::steady_timer` at `wheel.next()`. It
arms in 14-18 ns with no heap per entry, against 219-549 ns and 120-332 heap bytes for an
`asio::steady_timer` per timer or Kronuz/scheduler's queue. At 1,000 idle groups a heartbeating
leader costs 27 us of CPU per second and a follower 8 us, against 1,540 us today: 1.54 cores per
1,000 idle groups become 0.027, about 13,300 core-hours a year saved per 1,000 groups.

Bytes are the remaining limit. An idle three-voter member costs 171 KB today, 96% of it two
65,688-byte frame buffers in its `SnapshotChannel`; the design allocates them only while a frame
arrives (D9), which brings the member to 7.9 KB.

## What exists today

| Piece | Where | What drives it |
|---|---|---|
| journal operations | `IOOperation` (`completion/operation.h:69-78`): one typed primitive at a time, `complete()` advances | `drive_synchronously` (`operation.h:183-188`) or `AsioCompletionDriver::run` (`asio_completion.h:105-202`) |
| journal turn log | `TurnDriver` (`turn_driver.h:35-128`): `stage`, `begin`, `completed`, `failed` | the host by hand, or `commit()`, which writes and `sync_data`s on the caller's thread (`turn_driver.h:92-99`) |
| consensus `Core` | `Core::step(Event) -> Actions` (`core.h:250`), no IO, no clock | `Worker` |
| consensus `Worker` | Core + Store + admission (`worker.h:94`) | the host pumps `run_one(Tick)` (`worker.h:448-548`), `take_io_operation` (`:212`), `take_actions` (`:230`), `try_submit` (`:207`), `applied` (`:239`), `storage_operation_failed` (`:218`) |
| consensus transport | `Session`, `Peers` | `asio::awaitable` in public headers (`transport/session.h:111-246`, `transport/peers.h:251-310`) |
| task timers | `task::deadline<S>` (`task.hh:1113-1146`): a `cancel_source` that `S::start_timer` requests with `cancel_reason::deadline` (`task.hh:821`) | `asio_scheduler::start_timer`, one `asio::steady_timer` per deadline (`task_asio.hh:125-136`) |
| Detent's owner loop | `Service::Runtime::run` (`service.cc:1235`, spawned at `:578`) | polls readiness (`:1678-1687`), replay (`:1694-1704`) and the steady state (`:2594-2606`) every 1 ms; `dispatch` (`:1166-1210`) spawns `AsioCompletionDriver::run` per taken operation |

Each completion queue owns a thread and a kernel queue (`linux_completion.h:55-78`,
`bsd_completion.h:46-65`) and accepts one primitive at a time (`linux_completion.h:51`,
`asio_completion.h:120`). Nothing in strata or Xapiand uses journal or consensus today.

## Decisions

**D1. A group is state; the driver is one coroutine per reactor loop.** The group's machine
(journal `Store`; consensus `Worker`'s Core, Store and admission) gains a wheel entry and a
ready-list link. The driver is one `task<void>` per loop (80-byte frame): it takes the next ready
group, runs its machine until it reports no progress (a bounded number of steps per resume, then
`task::yield`), hands any IO operation to the completion seam, and delivers actions. Frames exist
only for work in flight: a caller awaiting a request (96 bytes) and an IO operation (512 bytes for
`AsioCompletionDriver::run`).

- *A coroutine per group* loses: a suspended frame per idle group is what item 4 forbids, at
  least 96 MB at a million groups before any state.
- *Keeping the pump* loses: item 1 deletes it, and an idle `run_one` costs 1,572 ns and 1.61
  allocations (leader) or 1,750 ns and 0.67 (follower), about a thousand times a second.

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
}
namespace kronuz::consensus {
struct Seams { task::scheduler_ref scheduler; task::timer_wheel& wheel; journal::completion_ref io; };
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
}
```

`Sink` receives what no caller awaits (`Send`, `Committed`, `RoleChanged`, snapshot and repair
notices) with static dispatch and no allocation (Q2). Detent's asio adapter already exists:
`task::asio_scheduler` (`task_asio.hh:58-166`) and `AsioCompletionDriver` behind `completion_ref`.
The transport moves to `task::task` with `task::await_asio` (`task_asio.hh:236-423`), removing the
last `asio::awaitable` from consensus's public headers. This is the contract `task:driver`.

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

**D4. One intrusive timer wheel per reactor loop, the one timer mechanism.** A group's entry is
24 bytes inside the group (next, back-link, deadline); arm and cancel are O(1) with no allocation.
Consensus gains `Core::next_deadline()`, a `const` accessor over the deadlines Core already keeps
(`election_deadline_`, `heartbeat_deadline_`, `core.h:1196`; each peer's `retry_after`,
`core.h:274`; the transfer deadline, `core.h:686`); the driver re-arms the entry after every
step, and a group with no deadline (quiescent, Q4) holds none. The wheel also implements
`start_timer` for `task::deadline` (D8), so operation deadlines are entries too, and the loop
arms one `asio::steady_timer` at `wheel.next()`. A wheel costs 33,296 bytes plus 65,544-68,096
heap bytes per loop, not per group. The prototype is `async-drivers-bench/wheel.hh`: 4,096 slots
of 1 ms and an occupancy bitmap for `next()`.

| Per timer, at 1,000,000 entries (1,000 in parentheses) | Bytes | Heap per arm | Arm | Re-arm |
|---|---|---|---|---|
| Intrusive wheel (proposed) | 24 in the group | 0 | 14.2 ns (18.4) | 15.7 ns (27.2) |
| Kronuz/scheduler `SchedulerQueue` | 64 + 123 heap (332) | 123 (189), the cleared task stays filed | 361 ns (549) | 448 ns (487) |
| One `asio::steady_timer` per timer | 120 + 120 heap (132) | 152 (145) | 223 ns (219) | 301 ns (246) |

Kronuz/scheduler's queue also takes a `shared_ptr` task per arm (`scheduler.h:200`), leaves a
cancelled task filed until a walk (`scheduler.h:181-191`) and fixes its clock to `steady_clock`
(`scheduler.h:203-205`), which the simulator cannot drive. Ticking every group at a fixed
cadence loses on CPU (Cost).

**D5. The blocking entry is the same task, run on the caller's thread.** For journal,
`task::sync_wait(store.append(..., inline_io))` (`task.hh:751-792`) runs with no scheduler; every
await completes inline, so it never suspends and no `io_context` exists. For consensus,
`consensus::run(driver, task)` builds the driver over a `task::inline_scheduler` and
`InlineCompletion` and drains it with `task::run` (`task.hh:726-742`) on the calling thread,
advancing the wheel by the caller's clock when the queue empties first. It serves what needs no
peer: create, recover, checkpoint, close, a single voter's proposals. `drive_synchronously`
becomes `InlineCompletion`'s loop: one implementation, driven two ways.

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
A fan-out root creates one `task::deadline d(wheel, at, parent)` and passes `d.token()` to every
child; tokens are explicit arguments (task has no implicit current token). A child needing a
tighter bound makes its own `deadline(wheel, earlier, token)`, linked to the parent's, so a child
can shorten a deadline but never extend it. Firing requests `cancel_reason::deadline` on the
whole tree. Cancellation detaches a caller; it never undoes a state-machine step:

| Where the caller is | On cancellation |
|---|---|
| waiting for admission, not yet given to `Core` | never submitted; completes with `task::cancelled` |
| a proposal `Core` accepted | completes with `task::cancelled`; the entry may still commit and reaches the application through `Committed` (Raft has no withdraw) |
| a read awaiting its read index | dropped |
| an IO primitive not yet queued | not issued |
| an IO primitive in the kernel | completes into the group's machine (no torn write); only the waiting caller is released |

The awaiter holds a `cancel_callback` node in the caller's frame: no allocation, and an idle group
holds none. The callback runs on the firing loop's thread and only marks the waiter and posts its
resume to the waiter's scheduler, so a fan-out across loops works and no user code runs under the
token's lock (November 2, item 1). *`when_any` against a timer* loses: item 2 rules it out and it
needs a frame per race. *A per-group timeout* loses: a fan-out spans groups.

**D9. A snapshot channel holds frame buffers only while a frame arrives.** Each per-peer
`Channel` embeds a `std::array<char, maximum_transfer_bytes>` (`snapshot_channel.h:185`;
128 + 24 + 65,536 bytes, `snapshot_transfer.h:37`), reserved for every peer at construction
(`snapshot_channel.h:44`). The channel keeps the 12-byte length prefix inline and allocates the
body at the frame's declared length (bounded by `maximum_transfer_bytes`) when its first byte
arrives, freeing it after `process()`. strata Track C D17's 8 KiB per idle group is reachable
only with this.

## How Detent's owner loop moves

| Today | On the driver |
|---|---|
| readiness poll, 1 ms (`service.cc:1678-1687`) | `co_await driver.create(group)` / `recover` |
| replay poll, 1 ms (`service.cc:1694-1704`) | the application drains `Committed` from the sink until applied reaches committed |
| `node.service` + `dispatch` every 1 ms (`service.cc:1716-2606`, `node.cc:532-600`) | the driver steps the group on input, IO and deadlines; Detent's budgeted work (delivery, reads, health) wakes on a sink notification or a request |
| `Runtime::dispatch` spawning `AsioCompletionDriver::run` (`service.cc:1166-1210`) | the driver's IO path through `completion_ref` |
| `node.propose_prepared`, `take_completions` (`service.cc:2570-2581`) | `co_await driver.propose(group, ..., request_deadline.token())` |
| `Node::retirement_step` (`node.cc:136-145`) | `co_await driver.close(group)` |
| `AuthorityPeers` on `asio::awaitable` (`peers.hh:9`) | the task transport (D2) |

## Cost

macOS x86-64, Apple clang 17, Release; heap is the allocator's usable size. Rows at 1,000 groups,
per group (job 1f63c8; the runtime row at 8 runtimes):

| Per idle group | Heap bytes | Descriptors | Threads | `io_context` | Armed timers | Frames |
|---|---|---|---|---|---|---|
| journal, shared Store | 1,913 | 2 | 0 | 0 | 0 | 0 |
| consensus single voter, shared | 6,497 | 3 | 0 | 0 | 0 | 0 |
| consensus 3-voter member, no channel | 6,262 | 3 | 0 | 0 | 0 | 0 |
| the same with its `SnapshotChannel` | 170,902 | 3 | 0 | 0 | 0 | 0 |
| driver additions (designed) | 40 | 0 | 0 | 0 | 0 (an entry) | 0 |
| Detent today: Worker + channel, own journal | 6,832 | 4 | 0 | 0 | 0 | 0 |
| Detent today: its runtime | 10,942 | 5 | 2 | 1 | 1 | 1 (`service.cc:578`, from code) |

**Where a three-voter member's 171 KB goes** (jobs 1f63c8, bced6c). 6,262 bytes are the Worker,
its Core and its Store, as for a single voter. The other 164,640 are the `SnapshotChannel`: its
800-byte object, and one block holding two 66,088-byte per-peer channels, each a 65,688-byte frame
array and 400 bytes of state. The 132,176 bytes requested come back as a 163,840-byte block from
macOS's allocator. The reserve follows the voter count, not the role, so a leader pays the same;
Detent's single voter has no peers and pays nothing. D9 removes the arrays: the channel keeps its
800-byte object and 800 bytes of per-peer state, 1.6 KB.

| Groups | Single voter + driver | 3-voter member + driver, today's channel | 3-voter member + driver, D9 | Detent's layout today |
|---|---|---|---|---|
| 1 | 11.8 KB, 5 fds | 176 KB, 5 fds | n/m | 17.9 KB, 9 fds, 2 threads |
| 1,000 | 6.5 MB, 3,002 fds | 171 MB, 3,002 fds | 7.9 MB (designed) | 17.8 MB, 9,000 fds, 2,000 threads |
| 1,000,000 | 6.5 GB, 3 M fds | 171 GB | 7.9 GB | 17.8 GB, 9 M fds, 2 M threads |

One group includes its core's turn log; 1,000 groups are measured except the D9 column (the sum
of measured parts) and Detent's 1,000 runtimes (from 8); a million multiplies the 1,000 row.

**Idle CPU** per idle group per second (job 963cb8). Timers alone were measured; the `run_one`
rows multiply the measured wakes by its measured 1,572 ns (leader) or 1,750 ns (follower). The
1 ms timer wakes 796-804 times a second on macOS.

| Per idle group per second | 1 group | 1,000 groups |
|---|---|---|
| today: 1 ms timer loop | 16,406 us | 289 us |
| today: + one idle `run_one` per wake | 17,670 us | 1,540 us |
| wheel, 100 ms heartbeat (leader), + `run_one` | 273 + 16 = 289 us | 11.1 + 15.6 = 26.7 us |
| wheel, 1.5 s election timeout (follower), + `run_one` | 41.7 + 1.2 = 42.9 us | 7.0 + 1.2 = 8.2 us |

At 1,000 idle groups that is 1.54 cores today against 0.027 for leaders: about 13,300
core-hours a year per 1,000 groups, roughly $530 at an assumed $0.04 per core-hour.

**One step.** A resume costs 9.2 ns through `inline_scheduler` and 127.8 ns through
`asio_scheduler`; a request frame is created and destroyed in 86.4 ns (pool off, so malloc's
time). Both are small beside the 1,572 ns of the step itself.

## Limits

- **A million heartbeating groups do not fit.** A million leaders at ten heartbeats a second cost
  27 cores of timer and step CPU before any heartbeat is sent. A million idle groups need
  quiescence (strata Track C D12: the leader stops heartbeating, followers stop their timers);
  a group with no deadline holds no entry, but `Core` does not implement it (Q4).
- **Descriptors.** A shared-layout consensus group holds three descriptors while idle (the
  Store's two, one the Worker's): 3 million at a million groups (Q3).
- **An idle `run_one` allocates** (1.61 per call for a leader); the driver calls it ten times a
  second instead of a thousand, which hides it but does not remove it.
- **Silent failure: a lost wake.** A group that should be ready and is not, or whose entry was
  not re-armed after a step, stalls with no error. The consensus quest's simulator checks
  liveness under every schedule; a Debug assertion checks after each step that a group with a
  deadline is armed.
- **One primitive per queue** serializes a loop's IO (D6).
- Numbers are from one macOS x86-64 host; allocator rounding differs on Linux, the performance
  gate host (November 2, item 7), where the consensus and Detent quests measure again.

## Build plan

Each quest leaves every repository working; the Worker pump API is deleted last.

| # | Quest (`--repo`) | Files | Criteria | Risk | Needs |
|---|---|---|---|---|---|
| 1 | `task-wheel` (task) | `task.hh`, `task_asio.hh`, tests, bench | `task::timer_wheel` (D4): O(1) arm and cancel, no allocation (job); `asio_scheduler::start_timer` and `task::deadline` on it (D8); `inline_scheduler::advance` expires it; `scheduler_ref` source-compatible; Xapiand lead reviews the diff | medium | Q1 |
| 2 | `completion-seam` (completion) | `operation.h`, `completion.h`, the queues, `asio_completion.h`, tests | `SyncData` on every backend (fault tests); `completion_ref` with a token, `InlineCompletion`, `HeldCompletion`; `drive_synchronously` is `InlineCompletion`'s loop | low | - |
| 3 | `journal-driver` (journal) | `turn_driver.h`, `store.h`, `journal.h`, tests | `TurnDriver::run` and `Store` tasks (D2); crash-point suites through `HeldCompletion` with the sync driver's histories; `sync_wait` test with no `io_context`; a cancelled append test (D8); no Asio in `include/` (grep job) | medium | 1, 2 |
| 4 | `consensus-driver` (consensus) | `core.h` (`next_deadline`), new `driver.h`, `snapshot_channel.h` (D9), `transport/` on task, tests | `simulation_test` through `Driver` on `inline_scheduler` + `HeldCompletion`; deadline table tests (D8); idle-cost job: 0 threads, frames, armed timers per group, 3-voter member within 8 KiB; blocking-entry test; no `asio::awaitable` under `include/`; Worker suites green; ASan+UBSan and TSan | high | 3 |
| 5 | `detent-on-drivers` (Detent) | `src/detent_authority/` | the owner-loop table; no 1 ms timer in the authority (grep and idle-CPU jobs); the `native` suite; CPU per request and p99 within the November 2 item 7 gate | high | 4 |
| 6 | `worker-pump-removal` (consensus, then a Detent pin) | `worker.h`, its tests | `run_one`, `take_io_operation`, `take_actions`, `try_submit`, `try_submit_retained`, `storage_operation_failed` leave the public API; a grep job over Detent and consensus finds no caller | medium | 5 |

Quests 1 and 2 are additive; 3 and 4 add drivers beside the existing APIs; 5 migrates the only
caller; 6 deletes. Quiescence (Q4) is a separate consensus quest after 4.

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

## Rerunnable checks

| Criterion | Check |
|---|---|
| A1, A2, A3, A5, A6 | this document; the Xapiand lead's review (A8) |
| A4, A7 | `run.sh idle_cost` (261109-230223-1f63c8), `run.sh driver_cost` (261109-230225-963cb8), `run.sh channel_layout` (261109-232231-bced6c), pinned with `--ref` to the commits above |

## Status

Designed, not built. The wheel exists as a measured prototype in the bench; nothing else here
has run. From review: whether D1's per-loop driver and D2's API fit Xapiand's per-index turn (one
lease holding the journal append and the apply), whether D8's cancellation table is the contract
Xapiand's fan-out needs, Q2's sink, and Q3's layout change.
