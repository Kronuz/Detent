# system-failover-port-reuse: test_system_managed_authority_failover: the restarted leader's bind fails with Address already in use

Risk: low. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status system-failover-port-reuse`.

## Outcome

CI run 44989878521 (restart-capacity's branch, ASan) failed test_system_managed_authority_failover because the killed leader's restart died with 'bind: Address already in use' (restart-capacity's reviewer, review at Detent:e433d313). This is not D144's isolated-voter defect. After this quest a restarted process in the system tests rebinds its port (SO_REUSEADDR in the server, if it lacks it) or the harness waits for the port, whichever is the real cause.

Widened 2026-11-23 (lead): main's CI at Detent:be98a7ce (linux-x86_64-gcc) and reader-s6's at 1d7f04f2 (linux-x86_64-clang) failed test_relay_service because voter-1's first start died with 'bind: Address already in use'. The harness reserves a port by binding 127.0.0.1:0, closes it, and hands the number to a daemon that binds later; under `ctest -j` another test can take it in between. Ten test files pick ports this way (tests/detent_authority/test_daemon_alone.py, test_dns_voters.py, test_lost_quorum.py, test_multivoter.py, tests/detent_relay/test_relay_service.py, tests/detent_system/first_read_gate.py, test_dual_stack.py, test_localhost_dial.py, test_managed_case.sh, tests/docs/run_e2e.py). After this quest every test gets its ports one way, in one shared helper, that cannot lose the port to another process: the daemon binds port 0 itself and the harness reads the bound port, or the harness retries a start that failed with EADDRINUSE on fresh ports; the contributor picks by what the daemons support and says why.

## Acceptance criteria

- A1 The cause is named from the server's socket options and the harness; a test or loop that hit it fails before and passes after: jobs
- A3 No test reserves a port and releases it before a daemon binds it: a grep job over tests/ finds no such pattern outside the shared helper, and a job runs the ten tests 20 times under `ctest -j8` with no EADDRINUSE
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
