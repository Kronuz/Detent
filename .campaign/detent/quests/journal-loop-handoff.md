# journal-loop-handoff: A loop's range table moves to another thread by an explicit owner handoff

Risk: normal. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status journal-loop-handoff`.

## Outcome

A loop's range table keeps one owner thread, checked in debug builds (journal:0fc8c14). A caller that serializes a loop itself, like strata's turn_sealer under its turn mutex, hands the table to the calling thread explicitly (`Host::adopt_loop(loop)` or the like) before using it from there; the debug check then follows the new owner. No lock-held guessing (msg-168, option a).

## Acceptance criteria

- A1 After a handoff, table calls from the new thread pass the debug check and calls from the old thread fail it: tests
- A2 strata's t6w case (close on a thread other than the first) passes with a handoff and aborts without one in a debug build: a test
- A3 The suite and CI pass

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
