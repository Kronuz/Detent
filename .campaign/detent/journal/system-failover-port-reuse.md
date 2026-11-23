# system-failover-port-reuse: journal

State: waiting on jobs 261123-100818-4f26f0 (A2 suite) 261123-100828-0a1fbb 261123-100829-0846b2 (A1) 261123-100831-4a2d3b 261123-100834-000cbb 261123-100836-5b2cd6 (A3)
Quest: .campaign/detent/quests/system-failover-port-reuse.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Widened scope (all ports): every test picks its ports one way, tests/support/ports.py `lease_ports`
(72358eb6, merged main 933a80c6). Earlier jobs 261123-030312-eaf7b9 and -7989cb died on
`xargs -I{}: command line cannot be assembled, too long` (macOS limit), not on the test;
261123-030314-52c787 failed test_system_managed_relay at 59 s under load 330 (run_writers.py
exception group; a load flake of the old reserve-and-release harness, not read further).

## Next

1. When the A2 suite and the A1 and A3 jobs pass, submit the A3 ctest shards (`--repeat
   until-fail`, -j8, strict, <=10 min each) at the head, fill Evidence and Result, push, set State
   "ready for review".

## Blocked on

Nothing. tests/detent_system/managed.py (relay ports) is outside the file set and edited anyway,
one line, because A3 forbids its reserve-and-release; the lead may revert it if unwanted.

## Evidence

(see the table `campaign brief` prints; pasted when every criterion is met)

## Result

Not ready.

## Rulings I made

- Mechanism: a lease, neither of the lead's two options, because both fail here. The daemons do bind
  port 0 and log the port (tests/support/bound_port.sh serves the single-daemon tests), but the
  voters of every cluster test name each other's ports in their configurations before the first
  start, so a daemon cannot choose its port. Retry on EADDRINUSE (the old with_fresh_ports of
  test_multivoter) loses the port first and needs collision detection in each test, and cannot
  move a port a restarted member must keep. The cause is a gap in which another test, or an
  outgoing connection's ephemeral source port, takes the number. `lease_ports` closes it: ports
  come from 20000-30000, below Linux (32768+) and macOS (49152+) ephemeral ranges, and each is
  flock-ed in a directory shared by all test processes until the leasing process exits. No retry
  code remains in the tests (with_fresh_ports, bind_collisions, StartupBindCollision, run_e2e's
  lock files deleted).
- first_read_gate.py and test_localhost_dial.py keep their port-0 sockets: they hold the socket
  while it is used and never hand the number to a daemon. test_managed_case.sh only probes for ::1.
- Authority.release_ports() stays (run_outage, run_partition, test_mixed, outside the file set,
  call it at the end) and now unlocks the leases; its call in test_restart_rebind, which would
  free the ports before the members start, is removed.
- restart_member's bounded retry stays: a process outside the tests can still hold the port while
  a member is down; test_restart_rebind squats on it.
- Fix by a bounded restart retry in the harness, not a server change: the server already sets
  SO_REUSEADDR on both acceptors, so no server change can rebind a port another process holds.
- The CI hang of test_system_restart_rebind (38c47070) is named by mechanism (an unbounded wait).
