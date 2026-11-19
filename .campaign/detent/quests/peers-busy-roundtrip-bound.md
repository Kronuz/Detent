# peers-busy-roundtrip-bound: consensus: peers_test busy round-trip bound (64 ms) fails under load at 256 ms

Risk: low. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status peers-busy-roundtrip-bound`.

## Outcome

peers_test.cc:1155 failed in driver-apply-gap's suite 261119-044522-91dbd6 (consensus:ee03230, loaded host): the slowest round-trip bucket while fetching was 256 ms against a 64 ms bound outside TSan (`round_trip_bound_ns`, peers_test.cc:1068-1071); 8 commits, 204 round trips, 0 timeouts. After this quest the bound is derived from a measured ratio with stated headroom, or the stall is named and fixed in the transport; no retries.

## Acceptance criteria

- A1 The cause is named from samples of a failing run (host stall, loop not running, or a transport wait): jobs.
- A2 The bound is derived with stated headroom from runs on a loaded host, and the test fails at a mutant that adds a real 100 ms transport delay (--fails-without): jobs.
- A3 The suite and CI pass: jobs.
