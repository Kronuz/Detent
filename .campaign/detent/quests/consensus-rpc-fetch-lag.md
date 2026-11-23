# consensus-rpc-fetch-lag: rpc_host fetch: allow the measured host lag in its heartbeat bound

Risk: low. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status consensus-rpc-fetch-lag`.

## Outcome

consensus_transport_rpc_host_fetch judges its Control heartbeats against the election timeout plus the host lag it measures, as group-full does (250 ms plus `lag.worst`), so a stall of the whole runner or of both processes no longer fails it, while head-of-line blocking of Control behind Bulk still does. Reported by xapiand-lead (msg-253): heartbeat gaps of 4,711 ms on macos-x86_64 (run 44925311406), 4,092 ms on linux-tsan at consensus main 5a114dd (run 44930858682) and 1,358-4,592 ms on 9 other TSan runs, with Bulk stalling as long as Control, which marks a whole-host stall; 2 of 65 macos-x86_64 attempts. It reds consensus-request-install's CI (run 45150071779) on Xapiand's critical path.

## Acceptance criteria

- A1 The fetch case's heartbeat bound is 1,000 ms plus the measured host lag, and it prints both: a job running rpc_host fetch 50 times under 4 burners passes at the fix.
- A2 The bound still catches a real heartbeat stall: a mutant that withholds the leader's Control heartbeats for longer than the bound (1,000 ms plus the measured lag) fails the fetch case (`--fails-without`). The fetch case cannot show head-of-line blocking: neither heartbeats sent on Bulk (261123-060906-b60033, gaps 150-161 ms) nor `bulk-ahead-of-control` (261123-090002-e8d6eb, 105-121 ms) moves its gaps, because the Bulk rings serve groups one frame per turn; group-full and call-full cover that.
- A3 The suite passes, and `ci:linux-tsan`, `ci:macos-x86_64` are green at the landed commit.

## Contracts

None: test-only.

## Decisions

Lead, 2026-11-23: a separate quest after consensus-transport-test-flakes, which is one CI round from landing; the fix is the allowance xapiand-lead proposed.

## May land early

None needed: it is one change.

## Tasks

None.
