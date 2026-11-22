# split-consensus: Every source file in consensus is split where it improves maintainability (SPLITTING.md)

Risk: low. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status split-consensus`.

## Outcome

Owner rule (2026-11-21), written once in cpp-libraries docs/SPLITTING.md (cpp-libraries:7cab8ec); this quest follows that page and does not restate it. Size only flags a file for a look: "The split must make sense and improve maintainability. Let's not split for the sake of splitting. A 10K file might be ok if it makes complete sense to keep it that way." Cuts follow recognizable boundaries; a coherent unit (one function, state machine or class) stays whole at any size; nothing is cut inside a function, switch or class body (no .inc fragments). Moves only, no behavior change. Files flagged in consensus today (over 1,000 lines): host_transport.h 1,608, core.h 1,516, worker.h 1,235, driver.h 1,142 and 7 tests.

## Acceptance criteria

- A1 [artifact] Each flagged file has a verdict, split along named boundaries or kept whole with the coherent unit that justifies it: `docs/dev/splits.md`
- A2 The split moves code only: cpp-libraries tools/split_check.py (cpp-libraries:920d1b2 or later) with `--base` at the quest's base passes, warnings listed in the journal: a job
- A3 [manual] The lead reads the split and confirms each cut follows a recognizable boundary per SPLITTING.md
- A4 The suite and CI pass
- A5 No class body is over 1,000 lines (owner, 2026-11-21, relayed by xapiand msg-233: "One class body of 5K+ lines is just not right"; "I would still split them with craftmanship and aiming at improving maintainability and parallel work"). Core (core.h, 1,395), HostTransport (host_transport.h, 1,216), Worker (worker.h, 1,142) and Driver (driver.h, 1,050) take step 1: member definitions move out of the class body into one header per mechanism, the class keeps data members and declarations, and the umbrella header keeps its path. Proved by split_check `--members` and a Release fnsizes diff with no change: jobs. Step 2 (subobjects where state stands apart) is a separate reviewed quest if any

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
