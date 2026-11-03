> Status: CURRENT (2026-11-02) Ledger of workstream W2 HTTP coroutines (PLAN.md M1).

# http-coroutines

- Design: docs/design/http-coroutines.md (DRAFT, revision 3: retargeted to http's task API, N1-N6 folded); reviews: docs/reviews/http-coroutines-design.md (architect preliminary, REJECT, 2026-11-02; re-check APPROVE WITH CONDITIONS N1-N6, 2026-11-02)
- Branch / worktree: `design/http-coroutines` at `~/code/Kronuz/Detent-wt/http-coroutines`; head: see `git log -1`
- State: building S1 (owner wait, branch `phase/w2-s1`: pin, `OwnerLoop`, T1 (a)-(d)) then S6; design revision 3 stands (designing, ready for preliminary review). Revision 3 adds no new mechanism; the designer recommends a scoped re-check (rechecker) of N1-N6, D2, D10 and D15 rather than a new architect review.
- Done criteria (set by the lead; contributors tick, never edit):
  - [ ] PLAN M1 done criterion 3 (no `workers`, `should_offload`, `DeferredReply`, `user_data`, `BodyReader` or blocking wait on a reactor thread; every handler a coroutine; body cap from Detent's limits)
  - [ ] Owner work is a child task that `schedule_on`s to the owner and waits on a `task::event` in existing per-request state, with no Detent completion type; T1 passes with (b) first (added per review L1; retargeted to task in revision 3 by the lead)
  - [ ] One shape: T12's grep (`asio::awaitable`, `co_spawn`, `use_awaitable`, `awaitable_operators`, `this_coro`, `experimental::channel` in `src include tests benchmarks`) is empty and runs in CI; T13 (no live frames after teardown) passes (added by the lead per the architect's http Q2 ruling)
  - [ ] Every submitted authority command receives its reply with at least 1 s to write it, and the receive window stays T; T11 passes on plain TCP and TLS (added per review L2)
  - [ ] A command that may commit is answered `outcome_unknown` (authority) or the retry reply (classic), never `stopping` or `admission_full`; T6 passes (added per review L3)
  - [ ] Classic GET and health never wait on a commit, in both modes; T2 passes deterministically (added per review L4)
  - [ ] `detent::assert_off_loop` guards Detent's blocking points, and moving the classic commit back inline fails T3 (added per review L5)
  - [ ] The lead's 560 KB and 16 MiB commit times and the authority parse times are recorded in the design (added per review C1, C2)
  - [ ] T1, T4 and T6 pass in separate ASan/UBSan and TSan builds (added per review C6)
- Done: design draft D1-D12, T1-T10, Q1-Q4, S1-S6 (`Detent:5c58c31`); revision 2 folding L1-L5, C1-C6 and the Q1-Q4 answers (`Detent:8bb0e36`); merge of `Detent:6a31280`; revision 3 retargeting to task and folding N1-N6 (this commit)
- Doing: S1 and S6 on `phase/w2-s1` (`~/code/Kronuz/Detent-wt/w2-s1`, from `design/http-coroutines` at `Detent:bf20f94`)
- Review: scoped rechecker on N1-N6, D2, D10 and D15 (lead's decision: no new mechanism, a revision that folds findings in). Next: S1 (task pin, `OwnerLoop`, T1 with (b) first) and S6 (delivery and HTTPS clients) can start now against task C2 (`task:cbfee34`); S2 when http H1, reactor R, task C3 and http-log L land; then S3-S5, S7 (after Q6), S8, S9, S10
- Blocked on:
  - Q6 (settled by the lead under the owner's one-shape rule): completion's `AsioCompletionDriver::run` moves to `task::task` in this wave, Detent owning the change in completion and its consumers (completion's test, consensus's test and bench); unblocks S7 and S8 once it lands.
  - Q5 (settled by the lead): option 1, the composed-diff GET stays on the loop and is measured in M3.
  - The lead: done criterion 2 still names Asio's `co_spawn` hop and channel, which revision 3 replaces (D2); proposed text: "Owner work is a child task that `schedule_on`s to the owner and waits on a `task::event` in existing per-request state, with no Detent completion type; T1 passes with (b) first". The architect's http Q2 ruling also asks for a criterion carrying T12 (no `asio::awaitable`, `co_spawn`, `use_awaitable`, `awaitable_operators`, `this_coro` or `experimental::channel` in `src include tests benchmarks`). PLAN W2 (`PLAN.md:51`) still says `asio::awaitable<void>`.
  - The http designer: `HttpAsioService` must expose stop, close and destroy-roots phases (TH2-R1) for D15.
- Findings folded in: L1-L5, C1-C6, Q1-Q4 answers (`Detent:8bb0e36`); N1-N6 and the re-check's unclosed limit and L4 notes (this commit)
- Last verified: design reads only, 2026-11-02: http `task-handlers.md` rev 2 (`http:209ceef`) and its review (`http:1ae1390`); task `docs/API.md`, `task.hh`, `task_asio.hh`, `docs/design/cancellation.md` (`task:cbfee34`); completion `asio_completion.h` (`completion:e2c6474`); Detent grep inventory at `Detent:6a31280`. No build or test run.

## Evidence

S1, branch `phase/w2-s1` (hosts: leased build box, Ubuntu 24.04, clang 18.1.3, Debug `-O1 -g`, no `NDEBUG`):

- Pin: `task` at `task:cbfee34` in `cmake/dependencies.json`, made available with `reactor`; `detent::owner_loop` (INTERFACE, links `task::asio`) in `src/CMakeLists.txt`.
- Built: `include/detent/owner_loop.hh` (`detent::OwnerLoop`, `detent::owner_submit`), `tests/detent_owner/test_owner_wait.cc` (ctest `test_owner_wait`).
- Failing first (N6): with the `cancel_callback` line removed, `test_owner_wait` fails in the three (b) cases ("the wait ended on the stop"), in (c), in (d) and in the race ("every request got a reply"); restored, it passes.
- Mutants, each in `build-asan`, each dies: no `cancel_callback` (6 CHECK failures, above); backstop deadline not linked to `stop` (5 failures: the (b) cases, (c) and the race; (d) still passes, as it should); no hop home (a copy of `task.hh` whose child final suspend transfers straight to the awaiter: `r.back_home` fails in every case).
- Sanitizers, separate builds, `ctest -R test_owner_wait --repeat until-fail:30`: ASan/UBSan 30 of 30 pass, TSan 30 of 30 pass, native 30 of 30 pass. Each case also checks `task::detail::live_frames()` back at its baseline and `orphaned_waiters() == 0` after the D15 phases.
- T1 (e) is S4's (it needs the authority's runtime failure path).

## Log
- 2026-11-02: Worktree created from `Detent:aa00b0e`. Read http `679c0f8`, reactor `ade618d`, http-log `431b878`, cluster `428d57d` and UPGRADING-2026-10 (`cpp-libraries:5eedcc7`). Found `Request::user_data` and `defer()` still present at http head (brief premise corrected in the design). Drafted the design.
- 2026-11-02: Architect preliminary review: REJECT (L1-L5, C1-C6). Revised the design per the review and the owner direction (async end to end, Asio hop, Q1 owner coroutine, Q2 removal). Beyond the review: the authority runtime's `io_context` dies with a local `Runtime` while HTTP still serves (D2 lifetime 3); the composed-diff cache moves into the published state to avoid a stale insert (D13); the publisher reply leaves through an owned stream so `request_timeout` stays T (D8). Q5 added for the lead.
- 2026-11-02: Merged `Detent:6a31280`. Revision 3: handlers and the owner hop on task (D1, D2: `schedule_on`, `task::event` in existing per-request state, `task::deadline` with a `cancel_callback`); one `OwnerLoop` shape for the three owners; shutdown via a Detent `cancel_source` per service requested by `begin_shutdown()` (D10, closes the re-check's first-reason and classic 10 s limit); four-phase teardown across reactors and owners (D15); D16 and steps S6-S9 move Detent's own `asio::awaitable` code (12 src/include files, 1 benchmark, 15 tests) to task per the architect's http Q2 ruling; T12 (one shape) and T13 (teardown) added. N1 buffered shutdown replies, N2 helper in S2, N3 `request.keep_alive`, N4 guard order, N5 T10 scope, N6 T1(b) first. Found completion's `AsioCompletionDriver` returns `asio::awaitable` (Q6).
- 2026-11-02: S1 on `phase/w2-s1`: task pinned at `task:cbfee34`; `OwnerLoop` and `owner_submit` factored from D2's snippet into one header (the three owners call it with their own `accept` and `finish`); T1 (a)-(d), the stop races and frames-at-baseline pass in ASan/UBSan, TSan and native; three mutants die.
