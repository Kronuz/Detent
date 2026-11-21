# consensus-full-window-stall: The leader keeps replicating when a full rpc_bytes window meets loss or reorder

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status consensus-full-window-stall`.

## Outcome

With 32 x 64 KiB proposals in flight (rpc_bytes, 2 MiB, types.h:61) and 1% loss or 5% reorder at 20 ms, the leader stops replicating: applied stays fixed from 4 s to 90 s, no election, sockets idle on heartbeats (msg-177; xapiand netlab-consensus, consensus:e12ee46, job 261120-172038-99d4ac). One in flight under the same loss works. After this quest a full window under loss and reorder keeps committing. Candidate sites: follower capacity rejections (core.h:938-952) and their 500 ms retry (core.h:1021), the batch bound (core.h:1183-1187). Root cause first, from a deterministic reproduction.

## Acceptance criteria

- A1 A deterministic simulation (driver_simulation or a core test) with a full rpc_bytes window and dropped or reordered appends reproduces the stall at main and commits at the fix: jobs, one --fails-without
- A2 [artifact] The root cause, named at file and line, with the interleaving: `docs/mechanisms/` note
- A3 Xapiand's netem bench (consensus:e12ee46 bench, 32 in flight, 1% loss and 5% reorder at 20 ms) commits steadily for 90 s: a strict job in the Linux VM
- A4 The suite, TSan and CI pass

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
