# async-drivers-design: Design the journal and consensus coroutine drivers

Risk: low. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status async-drivers-design`.

## Outcome

`docs/design/async-drivers.md` designs the coroutine drivers for journal and consensus that quest
async-drivers builds: each library's existing state machine (journal's resumable machines,
consensus's I/O-free `Core`) driven by `task::task<T>` over `scheduler_ref`, replacing the polling
loops and the callback Worker API, per the November 2 agreement
(`docs/SHARED_LIBRARY_DECISIONS.md`, "Coroutine drivers for journal and consensus", items 1-5) and
PLAN D7. It splits async-drivers into the `--repo` quests (journal, consensus, then Detent) with
criteria, and it carries the measurements that bound an idle group's cost.

## Acceptance criteria

- A1 [artifact] One section per library (journal, consensus): the driver's shape over the existing state machine, which loops and Worker API calls it replaces (file and line), and how Detent's owner loops move onto it: `docs/design/async-drivers.md`
- A2 [artifact] The public API: `task::task<T>` generic over `scheduler_ref`, the asio adapter for Detent, no `asio::awaitable` in public headers (item 2): `docs/design/async-drivers.md`
- A3 [artifact] Injection: scheduler, clock and IO through completion's `IO` seam, and how a deterministic simulator steps journal and consensus with no real time or threads (item 3): `docs/design/async-drivers.md`
- A4 [artifact] Idle cost: a per-idle-group cost row per library (bytes; no thread, `io_context`, armed timer or coroutine frame per idle group; timers in a shared wheel per reactor), at 1, 1,000 and 1,000,000 groups (item 4): `docs/design/async-drivers.md`
- A5 [artifact] The blocking entry: the same state machine driven to completion without an `io_context` on the calling thread (run-to-completion or `sync_wait`), one implementation driven two ways (item 5): `docs/design/async-drivers.md`
- A6 [artifact] Build plan: the `--repo` quests (journal, consensus, Detent) with files, criteria, risk and order, each intermediate state working, and the deletion of the Worker API last: `docs/design/async-drivers.md`
- A7 The idle-cost and resume-cost numbers the design relies on (a task frame, a timer-wheel entry, one driver step against today's loop) are campaign jobs at named commits of task, journal and consensus, `exclusive` for any number that decides, cited by id
- A8 [manual] The Xapiand lead reviewed items 1-5 and 7b fit (a `campaign message xapiand:` answered in the journal)

## Contracts

Provides `task:driver`: the driver API shape journal and consensus will expose. Consumes
`task::task` and `scheduler_ref` (Kronuz/task) and completion's `IO`.

## Decisions

1. D7: Detent owns both designs; Xapiand reviews items 1-5 and 7b fit before code lands.
2. One coroutine type (November 2, "One coroutine type"): task is made cheaper rather than mixed with `asio::awaitable`; a mixed shape needs a case to the owner with before/after numbers.
3. Measurements may build task, journal and consensus at their current `main` in jobs; no library code changes in this quest.

## May land early

None.

## Inputs to weigh at split/start

- Xapiand triage #18 (msg-13, Xapiand D20 names Detent the design owner): one deadline per fan-out operation that every child task inherits and cancels on, carried on the shared task cancellation token. The driver design states how a deadline propagates and cancels.
