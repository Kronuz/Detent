# system-failover-port-reuse: journal

State: waiting on jobs 261123-110512-a95d5f (A2 suite) 261123-110507-e20eab 261123-110509-1a8eb1 261123-110511-1c5f4a (A1) A3 shards 261123-110520-1cfb2d 261123-110522-238036 261123-110524-4594da 261123-110526-624a20 261123-110527-56826a; diagnostics at main and head 261123-105628-3ecffd 261123-105633-b695c4 261123-105639-065983 261123-105643-9dfc85 261123-105647-143e38 261123-105651-2f7418
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

## Lead's message after the refusal at 590cd741

1. macos-x86_64 CI failure of test_system_restart_rebind (run 45236070277): a test defect, not a
   product defect. The test's stand-in daemon is a Python ThreadingHTTPServer, not detent-authority.
   HTTPServer.server_bind reverse-resolves the bound address (socket.getfqdn) after bind and before
   listen, so a slow lookup leaves the restarted stand-in bound but not listening (lsof: TCP
   127.0.0.1:PORT (CLOSED); macOS times a connect out), alive with empty output past the 20 s wait.
   Reproduced exactly ("alive ... 20 s after restart attempt 2: last TimeoutError('timed out');
   output ''") by making the lookup sleep: the stand-in now poisons socket.getfqdn to sleep 60 s
   (b55839c6, fails: job 261123-110507-e20eab at b55839c6) and Server.server_bind skips the lookup
   (11ead2c3, passes: 261123-110509-1a8eb1; x96 at 24 parallel: 261123-110511-1c5f4a). Not
   reproduced by load alone: x192 at 32 parallel failed once at run 41 ("alive but refused") in job
   261123-105613-fb61f7 and passed in the rerun 261123-105742-1d6aef; the first is the same stall.
   restart_member's failure now includes ps and lsof for the member's pid and port. Whether the
   real detent-authority does a reverse lookup before listening is not exercised here.
2. Suite 261123-100818-4f26f0 failures (test_authority_idle_wake, test_agent_store_crash,
   test_authority_transport_peers): not in my files; each runs at main (slot sfpr-main, 2e34a603)
   and at the head as one job: 3ecffd/b695c4 (idle_wake main/head), 065983/9dfc85 (store_crash),
   143e38/2f7418 (transport_peers). Results below once they finish.
3. Merged origin/main (e7a38ef8), pushed.
4. A2 suite and the five A3 shards resubmitted at 11ead2c3.

## Next

1. When the jobs named in State pass (A1 stress x48 at 8 parallel, 261123-100829-0846b2, passed),
   fill Evidence and Result, set State "ready for review". A shard over 10 min: resplit with fewer
   repeats.

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
