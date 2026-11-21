# consensus-transport-budgets: HostTransport's window and data queue count wire bytes, and the defaults hold the frames they name

Risk: normal. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status consensus-transport-budgets`.

## Outcome

msg-190 (xapiand track-c-gates): with the 25-byte frame header, HostTransportOptions' 4 MiB window holds one 2 MiB (rpc_bytes) data frame, not two (two exceed it by 50 bytes), and the 8 MiB data_queue holds three, not four (by 100 bytes). flow_bench gap's 0-2 "leader dropped" on 2 MiB rows are that data_queue overflow (host_transport.h:694-695) from top_up's over-posting. Ruling (lead): one rule, budgets count what goes on the wire, headers included; the defaults are sized as N x (rpc_bytes + header), so the window holds two full data frames and the data queue four; top_up posts no more than the queue admits, so a full queue backs off instead of dropping a leader.

## Acceptance criteria

- A1 With default options, two rpc_bytes data frames fit the window and four fit the data queue, checked by value in a test: jobs
- A2 flow_bench gap's 2 MiB rows report no "leader dropped" over its runs (before and after at named commits): jobs
- A3 A mutant that counts payload only fails A1: a --fails-without job
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

- From consensus-transport-security.md (Detent:627e8ac8), non-blocking findings folded here by the lead: F2 fetch replies are held whole outside every budget (up to serving x fetch_bytes, 16 GiB per peer): count them in the bulk budget; F3 the receive bound is per group only; F4 the fuzzer models a peer that never reads; F6 `~Lane` asserts `!depth_`. Criteria for F2 and F4 are added when this quest starts.
