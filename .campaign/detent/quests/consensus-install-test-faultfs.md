# consensus-install-test-faultfs: driver_install_test builds its journal on faultfs, not MemoryIO

Risk: low. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status consensus-install-test-faultfs`.

## Outcome

consensus's test/driver_install_test.cc (added by consensus-driver-install, consensus:8780e44) still includes journal/memory_io.h, the last consensus user. It builds its journal on faultlab's faultfs, as consensus-faultfs moved the other ten tests (consensus:813d95f..bb961b3), so journal-drop-memory-io can delete the header.

## Acceptance criteria

- A1 `git grep memory_io` in consensus finds nothing, and the five consensus_driver_install_* tests pass: a job
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
