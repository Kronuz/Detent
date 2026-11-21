# journal-fuzz-decoder-pin: A build define pins journal_fuzz to one decoder

Risk: low. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status journal-fuzz-decoder-pin`.

## Outcome

Xapiand's track C (strata docs/reviews/track-c-c2.md, C2 condition 2) needs one measured CPU-hour of fuzzing per decoder at the fixed journal, and assigns this pin to Detent. `journal_fuzz` (fuzz/fuzz_turn_log.cc) picks the turn-log segment or the group checkpoint record by `data[0] % 2`, so neither gets a run of its own. After this quest the CMake cache string `JOURNAL_FUZZ_DECODER` (empty, `segment` or `checkpoint`) pins the decoder at build time; empty keeps `% 2`. The input's first byte is still consumed either way, so corpora stay interchangeable.

## Acceptance criteria

- A1 With JOURNAL_FUZZ=ON, each of the three settings builds and runs 60 s of fuzzing clean, and the pinned builds reach only their decoder: a job
- A2 The suite and CI pass

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
