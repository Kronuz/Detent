# async-drivers-design: journal

State: ready for review
Quest: .campaign/detent/quests/async-drivers-design.md (the lead's; never edit it)

## Doing

Nothing. `docs/design/async-drivers.md` is complete (D1-D9, Q1-Q6, build plan of six `--repo`
quests), every number from jobs 261109-230223-1f63c8, 261109-230225-963cb8 and
261109-232231-bced6c.

## Next

1. Design review (lens `design`).
2. A8: the lead relays the note below to the Xapiand lead (`campaign message xapiand:`); record
   the answer here.

## Blocked on

A8 needs the Xapiand lead's review of items 1-5, 7b fit and D8 (deadlines, triage #18); the lead
relays it.

## Note for the Xapiand lead (A8)

Detent's design for the journal and consensus drivers is `docs/design/async-drivers.md`. What
changes for Xapiand:

1. API: `task::task<T>` over `scheduler_ref`; no `asio::awaitable` in journal or consensus
   headers. One consensus `Driver` per reactor loop; a group is state (6.5 KB, 3 fds idle; no
   thread, `io_context`, armed timer or frame).
2. Deadlines (#18, D8): one `task::deadline` per fan-out on the loop's timer wheel; children take
   its token and may only shorten it. Every driver call and IO await takes a `cancel_token`.
   Cancellation releases the caller; it never withdraws a proposal `Core` accepted (it may still
   commit, seen through `Committed`) and never tears an in-flight write.
3. Blocking entry (D5): `task::sync_wait` / `consensus::run` over `InlineCompletion`, no
   `io_context`, for WorkPool device chores.
4. 7b fit: a three-voter shard member costs 171 KB idle today, 164 KB of it `SnapshotChannel`
   frame arrays; the consensus quest makes them lazy (D9), to 7.9 KB. A million heartbeating
   groups cost about 27 cores idle, so 7b's million groups need quiescence (Q4).
5. Asks: review items 1-5, 7b fit and D8's cancellation table; Q3 (one directory and owner lock
   per core, a journal layout change before 7b writes bytes); Q6 (Xapiand's Kronuz/scheduler
   users onto the task wheel); review of the task-wheel diff (quest 1).

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | `docs/design/async-drivers.md`: "What exists today" (file:line per loop and Worker call), D1, D5 (journal), D2/D4/D9 (consensus), "How Detent's owner loop moves" |
| A2 | `docs/design/async-drivers.md`: "D2. Public API: `task::task<T>` over `scheduler_ref`, no Asio in journal or consensus headers." |
| A3 | `docs/design/async-drivers.md`: "D3. Injection through three seams." (`HeldCompletion`, the simulator's loop) |
| A4 | `docs/design/async-drivers.md`: "Cost" per-idle-group rows and the 1 / 1,000 / 1,000,000 table, from jobs 261109-230223-1f63c8 and 261109-232231-bced6c |
| A5 | `docs/design/async-drivers.md`: "D5. The blocking entry is the same task, run on the caller's thread." |
| A6 | `docs/design/async-drivers.md`: "Build plan", quests 1-6, Worker pump deletion last |
| A7 | 261109-230225-963cb8 (exclusive, Detent:87df68b): frames 80/96/512 B, resumes 9.2/127.8 ns, wheel arm 14.2 ns vs 223/361 ns, `run_one` 1,572 ns, idle CPU; 261109-230223-1f63c8 (strict, Detent:87df68b): bytes, fds, threads; 261109-232231-bced6c (light, Detent:646cfb7): channel layout. Refs consensus@ab05507, journal@e5f6ff1, completion@560c288, task@bdafa5f |
| A8 | pending: the Xapiand lead's answer (note above) |

## Result

`docs/design/async-drivers.md`; bench `docs/design/async-drivers-bench/` (`run.sh idle_cost`,
`driver_cost`, `channel_layout`). Designed, not built.

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
