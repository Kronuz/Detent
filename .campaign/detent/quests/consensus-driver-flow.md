# consensus-driver-flow: Worker and Driver options carry Flow, so Driver hosts can pipeline appends

Risk: normal. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status consensus-driver-flow`.

## Outcome

msg-192 (Xapiand's architect, their D161): worker.h:150,178-179 builds every Core with Flow{} (one flight, frame_bytes 0, so appends go up to rpc_bytes, 2 MiB) and no Worker or Driver option carries Flow, so no Driver host can use D15's pipelining (at 10 ms RTT one flight of 256 KiB moves 16 MB/s). After this quest Worker and Driver options carry a Flow that reaches every Core they build; the default stays Flow{}; Xapiand's strata::group then sets Flow{4, 4 MiB, 256 KiB}. The per-peer append pacing from consensus-full-window-stall applies within whatever Flow the host sets.

## Acceptance criteria

- A1 A Driver built with Flow{4, 4 MiB, 256 KiB} keeps up to four appends in flight of at most 256 KiB each, checked by value; the default Driver is unchanged: jobs
- A2 A mutant that drops the option on the way to Core fails A1: a --fails-without job
- A3 [artifact] docs/DRIVER.md names the option and its default
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
