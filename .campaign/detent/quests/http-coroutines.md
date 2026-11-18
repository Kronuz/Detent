# http-coroutines: W2: Detent's HTTP path on task::task

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status http-coroutines`. It landed before this
campaign, under the earlier campaign skill; its evidence is its ledger, `docs/status/http-coroutines.md`.

## Outcome

Detent's handlers, owners and own coroutine code run on `task::task` over http's task API: no worker offload, `should_offload`, `DeferredReply`, `user_data` or blocking wait on a reactor thread; the relay, classic and authority owners each run an `OwnerLoop`; `--workers` is gone; T10 and T12 run in CI.

## Acceptance criteria

- A1 [artifact] M1 3: no `workers`, `should_offload`, `DeferredReply`, `user_data`, `BodyReader` or blocking wait on a reactor thread; every handler a coroutine; the body cap from Detent's limits (ticked): `docs/status/http-coroutines.md`
- A2 [artifact] Owner work is a child task that `schedule_on`s to the owner and waits on a `task::event`; T1 passes with (b) first (unticked in the ledger): `docs/status/http-coroutines.md`
- A3 [artifact] One shape: T12's grep is empty and runs in CI; T13 passes (unticked in the ledger): `docs/status/http-coroutines.md`
- A4 [artifact] Every authority command gets its reply with at least 1 s to write it; T11 on TCP and TLS (unticked in the ledger): `docs/status/http-coroutines.md`
- A5 [artifact] A command that may commit is answered `outcome_unknown` or the retry reply; T6 (unticked in the ledger): `docs/status/http-coroutines.md`
- A6 [artifact] Classic GET and health never wait on a commit; T2 (ticked): `docs/status/http-coroutines.md`
- A7 [artifact] `detent::assert_off_loop` guards blocking points; T3's revert fails (ticked): `docs/status/http-coroutines.md`
- A8 [artifact] The 560 KB and 16 MiB commit times and authority parse times recorded in the design (unticked in the ledger): `docs/status/http-coroutines.md`
- A9 [artifact] T1, T4 and T6 pass in separate ASan/UBSan and TSan builds (unticked in the ledger): `docs/status/http-coroutines.md`

## Contracts

Provides `detent::OwnerLoop` and `owner_submit` (`include/detent/owner_loop.hh`); consumes http's task handlers and `task::task`.

## Decisions

1. Lead rulings in the ledger: S1, S6, S7 and S8 land together; Q5 option 1; Q6 completion's driver on task in this wave; D16's required `task::asio_scheduler`.
2. M1 was declared done by the lead (2026-11-04) with six of these criteria unticked in the ledger; the evidence section covers parts of them.

## May land early

None.
