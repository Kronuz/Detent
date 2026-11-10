# async-drivers: M1b: coroutine drivers for journal and consensus, Detent's owner loops on them

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status async-drivers`. Planned from the lead's plan `docs/PLAN.md` at `Detent:2662aef` (M1b row) and `docs/SHARED_LIBRARY_DECISIONS.md` "Coroutine drivers for journal and consensus (November 2)" items 1-5;
the criteria are the plan's own, not new ones.

## Outcome

journal and consensus are driven by `task::task` coroutine drivers over their existing state machines; Detent's owner loops run on them and the callback Worker API is gone.

## Acceptance criteria

- A1 [artifact] One design per library (journal, consensus), each reviewed by the architect and by the Xapiand lead for items 1-5 and 7b fit before code lands (M1b exit): the design and review files
- A2 journal's resumable state machines and consensus's I/O-free `Core` stay the one implementation; a coroutine driver replaces the polling loops and the callback Worker API (item 1): the libraries' suites
- A3 The public type is `task::task<T>`, generic over `scheduler_ref`, with an asio adapter for Detent; no `asio::awaitable` in the public API (item 2): a header check job
- A4 Scheduler, clock and IO are injectable through completion's `IO` seam, so a deterministic simulator drives journal and consensus step by step (item 3): a simulator-driven test
- A5 An idle group costs bytes, not resources: no thread, `io_context`, armed timer or coroutine frame per idle group; each design carries a per-idle-group cost row (item 4): a measured job
- A6 A blocking entry drives the same state machine to completion without an `io_context` on the calling thread (item 5): a test
- A7 Detent's owner loops run on the drivers and the old Worker API is deleted (M1b exit): the `native` suite and a grep job

## Contracts

To be fixed when the lead splits this quest.

## Decisions

1. Agreed between the Detent and Xapiand leads on 2026-11-02; Detent owns both designs (D7).
2. The library halves land in journal and consensus as `--repo` quests ordered before Detent's; the lead splits this quest when M1b starts.

## May land early

None until the lead splits it.
- From async-drivers-design (Xapiand X10): add a `docs/SHARED_LIBRARY_DECISIONS.md` entry that the apply leaves the group's loop.
