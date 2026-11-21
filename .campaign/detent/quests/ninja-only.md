# ninja-only: Detent configures with Ninja only: a configure guard refuses any other generator

Risk: low. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status ninja-only`.

## Outcome

Owner ruling relayed from Xapiand (their D168, 2026-11-21): German's projects build with CMake and Ninja only. Commands without -G default to Unix Makefiles, and Makefiles and Ninja trees over one .deps break each other at configure in shared job slots. After this quest Detent's top-level CMakeLists.txt refuses a non-Ninja generator at configure with a message naming -G Ninja, and every script and doc that configures Detent passes -G Ninja. The lead updates quest check commands itself.

## Acceptance criteria

- A1 `cmake -S . -B b -G "Unix Makefiles"` fails at configure with the guard's message; `-G Ninja` configures: a job
- A2 No script or doc in the repository configures without -G Ninja: a job that greps for cmake -S/-B invocations without it and prints none
- A3 CI passes

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
