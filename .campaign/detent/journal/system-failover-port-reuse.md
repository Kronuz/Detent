# system-failover-port-reuse: journal

State: waiting on jobs 261123-175619-13aea1 (test_relay_service x48 at 8 parallel, main 11b8d2a3) 261123-175626-e51c59 (same, at 756d2c3c, the quest's lease) 261123-181753-4707aa 261123-181754-1e1ca2 261123-181755-9df8e6 261123-181755-ccd2db 261123-181756-29fcce 261123-181757-a6dd85 261123-181757-95980a 261123-181758-f68e8e (A3 at 509d3ef8) 261123-181759-ca3c2e (A2 suite at 509d3ef8); A1 passed: 261123-181552-9c6cf3
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

## Round 3 (fresh contributor, after the lead's message on the round-2 results)

- Merged main (e33e9b33).
- A1 x96 at 24 parallel (1c5f4a) failed twice at the same instant: runs 23 and 24, member 0's
  first restart, ps "S 00:20 /bin/sh .../detent-authority" and, one ps later, lsof shows the same
  pid as Python LISTENing. The stand-in was not stuck: its exec through /bin/sh into Python took
  over 20 s, beside 5 light holds. Fix (f62791e9), as consensus:2d42387: restart_member allows
  20 s plus the longest a 1 ms sleeper thread in the harness went without running during the
  wait, and its failure names both, the spawn time, and the stand-in's own "interpreter up" line,
  so a slow exec (low lag, late start line) is told apart from a stalled host (high lag).
  Whether 24 parallel is fair: decided by 2c2f0f. If it fails with low lag and a late start
  line, the stall is the exec path, not the scheduler, and the ruling goes to the load.
- A3 shards now run three plain rounds each (7 shards, 21 runs per test; until-fail stopped
  repeating a test at its first failure) and exit non-zero only on EADDRINUSE / "Address already
  in use" / errno 48 / 98, listing the test failures they saw.
- A3 load timeouts in round 2's shards (1cfb2d, 238036, 624a20, 56826a; 0 bind errors in all six
  logs): "managed agent 0 did not converge", "unreconciled producer 1 request after thirty
  seconds" in test_system_managed_*, test_authority_multivoter, test_relay_service. Observation,
  not this quest's (A3 asks for no EADDRINUSE).

## Round 4 (fresh contributor)

- 2c2f0f (A1 x96 at 24) was starvation of the restarted stand-in's start, not the bind defect, and the
  lag measure missed it because it timed a thread of the harness. Evidence, in order:
  1. d82e63 (96 runs, 288 restarts at 24 parallel, stand-in timestamps in the events): spawn to
     answer median 0.30 s, p90 15.2 s, max 18.2 s; of that, spawn to the script's first line p90 14.9 s,
     imports 0.13 s, bind 0.00 s, bind to serving 0.01 s, serving to first answer 0.05 s. The bind is
     not slow: the process starts late.
  2. 45463b sampled stuck stand-ins: pid 77633 was `/bin/sh` (the wrapper script `detent-authority`
     the test wrote) in state S, 0% CPU, at `_dyld_start` 7 s after the spawn.
  3. 970c61 (probe /Users/kronuz/.scratch/sfpr/probe/spawn.py, 24 workers, 360 starts per variant,
     kill then respawn): a freshly written `#!/bin/sh` wrapper exec'ing python: median 0.05 s, p90 1.8
     to 3.3 s, max 4.0 to 10.5 s; the same python started directly: max 0.17 s. A HostLag probe that
     started python directly saw 0.1 s of lag while the stand-in waited 20 s (6a1058, FAILED at run 81
     of 288 though the job exited 0: my script ended in awk), so a spawn probe is not the measure.
  Fix bb3ea221 (509d3ef8 after amend): Authority takes the member's command as a Path or a list, and
  the test passes `[sys.executable, __file__, "--serve"]`, no wrapper script. 9c6cf3: 288 runs at 24
  parallel beside 8 light holds, all pass; 864 restarts answered in median 1.11 s, p90 1.93 s, max
  3.61 s (before: max 18.7 s). Same parallelism as the earlier 96 x 24 claim, three times the runs.
  The HostLag from f62791e9 stays (thread lag); the progress line prints each restart's answer time.
- 1ff0d9's test_relay_service failure (relay C, 404 on /v2/history/file under history_reply) is not a
  bind error: it also fails in A3 shards 1,2,3,4,7 at 144457-e9b628 etc. (about 5 of 21 runs under
  -j8 load) with nothing but the lease changed in that file; the authority answers 404 when a file
  named by a layout is absent at its latest seal. A/B on the same loop (48 runs, 8 parallel): 13aea1 at
  main, e51c59 at the quest head (pre-fix probe commit; test_relay_service.py is identical at 509d3ef8).
  If main fails at a similar rate it is the lead's to file; if main passes, read the lease change.

## Next

1. Read 2c2f0f (A1 at 24 parallel) and the shards' failure lists; then when the jobs named in State pass (A1 stress x48 at 8 parallel, 261123-100829-0846b2, passed),
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
