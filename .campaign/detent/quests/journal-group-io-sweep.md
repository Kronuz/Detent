# journal-group-io-sweep: journal: group_io costs O(1) amortized, not O(live ids)

Risk: normal. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status journal-group-io-sweep`.

## Outcome

`Host::group_io` (include/journal/host.h:292) runs `std::erase_if(ranges_, expired)` over every registered range on every call. Job 261118-072911-17102d (exclusive) measured the median call at 5.9 us with 1K live random ids, 147 us at 10K, 4.87 ms at 100K and 135.8 ms at 1M (136 ns per id, superlinear per id as the multimap outgrows cache; ascending ids 23.8 ms at 1M). Xapiand's cluster opens groups through this path. After this quest a call costs O(log live ids) amortized: expired ranges are swept in amortized batches (or unregistered when their Range dies), so the 1M-id call is within 2x of the 1K-id call, with unchanged behavior: a live range is never dropped and every expired range is eventually released.

## Acceptance criteria

Three to five, at outcome level: `- A1 <criterion>: <the test that shows it>`. An untagged one
needs a job tagged `--for A1` passing at the landed code, often the suite; tag one `[artifact]`
(a committed `path`), `[decision]` (a PLAN.md Decisions id) or `[manual]` (checked by hand) when no
job can show it.

- A1 Behavior: a test holds ranges live and lets others expire across many group_io calls and checks by value that live ranges keep publishing and the registry's size stays bounded by live ranges plus the sweep slack: jobs.
- A2 A mutant that never sweeps fails A1's bound; a mutant that sweeps a live range fails A1's publishing check: jobs.
- A3 Cost: group_io_bench at 1K, 10K, 100K and 1M ids, random and ascending, exclusive, before (journal main) and after: the 1M median within 2x of the 1K median: jobs.
- A4 [artifact] docs/perf/group-io.md carries the after table beside the before one.

## Contracts

None: Host's API is unchanged.

## Decisions

Filed 2026-11-18 from journal-group-io-cost's exclusive run; needs that quest's bench and doc, lands after journal-faultfs (CMakeLists.txt, host_test.cc).

## May land early

None.
