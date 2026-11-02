> Status: CURRENT (2026-11-02) Ledger of workstream W2 HTTP coroutines (PLAN.md M1).

# http-coroutines

- Design: docs/design/http-coroutines.md (DRAFT, revised for the review); reviews: docs/reviews/http-coroutines-design.md (architect preliminary, REJECT, 2026-11-02; findings folded)
- Branch / worktree: `design/http-coroutines` at `~/code/Kronuz/Detent-wt/http-coroutines`; head: see `git log -1`
- State: ready for implementation (findings folded)
- Done criteria (set by the lead; contributors tick, never edit):
  - [ ] PLAN M1 done criterion 3 (no `workers`, `should_offload`, `DeferredReply`, `user_data`, `BodyReader` or blocking wait on a reactor thread; every handler a coroutine; body cap from Detent's limits)
  - [ ] Cross-thread completion uses Asio's `co_spawn` hop and an owner-local channel, with no Detent completion type; T1's bridge rows pass (added per review L1)
  - [ ] Every submitted authority command receives its reply with at least 1 s to write it, and the receive window stays T; T11 passes on plain TCP and TLS (added per review L2)
  - [ ] A command that may commit is answered `outcome_unknown` (authority) or the retry reply (classic), never `stopping` or `admission_full`; T6 passes (added per review L3)
  - [ ] Classic GET and health never wait on a commit, in both modes; T2 passes deterministically (added per review L4)
  - [ ] `detent::assert_off_loop` guards Detent's blocking points, and moving the classic commit back inline fails T3 (added per review L5)
  - [ ] The lead's 560 KB and 16 MiB commit times and the authority parse times are recorded in the design (added per review C1, C2)
  - [ ] T1, T4 and T6 pass in separate ASan/UBSan and TSan builds (added per review C6)
- Done: design draft D1-D12, T1-T10, Q1-Q4, S1-S6 (`Detent:5c58c31`); revision folding L1-L5, C1-C6 and the answers to Q1-Q4 (this commit)
- Doing: nothing; the design is ready for S1
- Next: S1 (hop helper and `assert_off_loop` on the current pins), then S2 (pins and mechanical conversion), S3 classic, S4 authority, S5 `--workers`, S6 suites and the lead's T9
- Blocked on: W1 for the consensus pin in S2; Q5 (composed-diff GET placement, lead) blocks no step
- Findings folded in: L1-L5, C1-C6, Q1-Q4 answers (this commit)
- Last verified: design reads only; Asio `231cb29` headers (`experimental/channel.hpp`, `awaitable_operators.hpp`, `impl/co_spawn.hpp`) and the http head exports read; no build or test run

## Log
- 2026-11-02: Worktree created from `Detent:aa00b0e`. Read http `679c0f8`, reactor `ade618d`, http-log `431b878`, cluster `428d57d` and UPGRADING-2026-10 (`cpp-libraries:5eedcc7`). Found `Request::user_data` and `defer()` still present at http head (brief premise corrected in the design). Drafted the design.
- 2026-11-02: Architect preliminary review: REJECT (L1-L5, C1-C6). Revised the design per the review and the owner direction (async end to end, Asio hop, Q1 owner coroutine, Q2 removal). Beyond the review: the authority runtime's `io_context` dies with a local `Runtime` while HTTP still serves (D2 lifetime 3); the composed-diff cache moves into the published state to avoid a stale insert (D13); the publisher reply leaves through an owned stream so `request_timeout` stays T (D8). Q5 added for the lead.
