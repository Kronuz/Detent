# consensus-transport-test-flakes: rpc_host fetch under TSan and group-full on macOS x86_64 pass every time

Risk: normal. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status consensus-transport-test-flakes`.

## Outcome

Since consensus-transport landed (consensus:9bbd632), two transport tests fail intermittently on main and on unrelated branches: consensus_transport_rpc_host_fetch under linux-tsan ("16 MiB arrived exact NO (Connection timed out) in 30000 ms beside 36 MiB of group uploads on Bulk; links 2+2, dropped 4766"; main CI 44913051788, full-window-stall CI 44924678818) and consensus_transport_rpc_host_group-full on macos-x86_64 ("timing"; main CI 44924240551, transport CI 44906921960). After this quest each is either a test whose budget is wrong for a slow runner (fixed with a bound derived from what it measures, not a bigger timeout) or a transport defect (fixed in transport, with the file set widened by the lead).

## Acceptance criteria

- A1 Each failure's cause, with the log lines or a reproducing job, in the journal's Result: jobs
- A2 A reproducer for each under load (TSan build for fetch; repeat runs under the quiet light load for group-full) failing at the pre-fix commit and passing at the fix: a --fails-without job and a job each
- A3 Ten CI passes in a row of both tests on linux-tsan and macos-x86_64 at the fix: CI run ids
- A4 CI passes

## Contracts

<What this quest provides or consumes (APIs, formats, protocols), stated precisely enough that a
quest depending on it can start before it lands.>

## Decisions

<Decisions made, and by whom; open questions, naming the owner or the lead.>

## May land early

<Internal code behind tests, unless the quest is a format, wire or public API. "None" when
everything waits for the review; `land --upto` refuses then.>

## Tasks

<The steps that build the feature, one agent each: `T1 <what> (files; assignment, tier, effort;
after T0)`. Each merges into the quest branch on its own tests.>

- 2026-11-21, Xapiand lead: rpc_host_fetch failed linux-tsan on quest/xapiand/node-admission at consensus:de51801 (run 44940676266, job 136956946787): heartbeats worst gap 1358 ms vs the 1000 ms bound, 16 MiB fetch took 28,600 ms of 30 s beside 49 MiB of group uploads, dropped 4529, overrun 0. Same shape as this quest's TSan case; decide whether TSan gets its own bound or the fetch starves heartbeats on a slow host.
