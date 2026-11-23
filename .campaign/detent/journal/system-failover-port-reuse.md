# system-failover-port-reuse: journal

State: blocked on lead: file set (tests/detent_system/authority.py, test_restart_rebind.py, CMakeLists.txt)
Quest: .campaign/detent/quests/system-failover-port-reuse.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Cause found; fix and loop built and proven locally but they sit outside the file set (only
tests/detent_system/test_managed_failover.sh). Nothing of it is committed.

## Next

1. Lead widens the file set to tests/detent_system/{authority.py,test_restart_rebind.py,CMakeLists.txt}.
2. Apply `git apply ~/.scratch/campaign-detent/port-reuse/fix-full.patch`, commit, run the jobs below, push.

## Blocked on

Not the server: detent-authority already sets SO_REUSEADDR on both listeners (reactor.h:530 in
reactor ab69cf2, the HTTPS port; consensus transport/peers.h:117 in a197efb, the peer port). The
bug is in the harness, `Authority.restart_member` (tests/detent_system/authority.py):
1. `Authority.__init__` reserves six ports with bind(0) and `start()` releases them; any other
   process can take a port while it is free. A killed member keeps no listener, so its port is free
   again for the whole outage (here ~1 s of two followers redialling it, beside two other
   ctest --parallel 2 system tests binding and dialling).
2. `restart_member` launches the process and returns without looking at it. The daemon exits at
   once with "bind: Address already in use"; nobody notices until `drained` finds member 0
   "Connection refused" ten seconds later (CI job 137108387708, events.jsonl: member_killed, then
   authority-0 "bind: Address already in use"). The same class is retried at first startup by
   tests/detent_authority/test_multivoter.py (`with_fresh_ports`), but a restart cannot take a fresh
   port (the voters' configuration fixes them).
Which process held the port in that run is not in the artifact (the log names no port); the
mechanism (a squatter on a released port, no check after launch) is proven by the loop, not the
identity of the squatter.

Needs the lead: widen the file set to
- tests/detent_system/authority.py: `restart_member` starts the member, waits until it answers
  health or exits; on exit with "Address already in use" it removes the actor and starts again
  until a 20 s deadline (records member_restart_port_busy), any other exit raises. `start_member`
  takes an `observe` callback to read the output.
- tests/detent_system/test_restart_rebind.py (new): a stand-in daemon that binds with SO_REUSEADDR
  and serves TLS health; kills each member, squats its HTTP port for 0-1 s, requires restart_member
  to return with the member answering.
- tests/detent_system/CMakeLists.txt: registers test_system_restart_rebind.
Patch, ruff and pyrefly clean: ~/.scratch/campaign-detent/port-reuse/fix-full.patch. Locally on
macOS: 5 of 5 loop runs pass with it; 3 of 3 fail (ConnectionRefusedError) without authority.py's
change. TLS fixture used there: ~/.scratch/campaign-detent/port-reuse/tls.
Alternative if the lead prefers no harness retry: hold the reservations with SO_REUSEADDR on Linux
(works there, not on macOS, where SO_REUSEPORT would be needed on both sockets); not chosen, since
it cannot be proven here and splits the shape by platform.

Jobs to submit after widening (one per line):
- `campaign job submit --quest system-failover-port-reuse --for A1 -- sh -c 'i=0; while [ $i -lt 20 ]; do python3 tests/detent_system/test_restart_rebind.py TLS SCRATCH || exit 1; i=$((i+1)); done'` (TLS: a fixture made by tests/detent_authority/tls_certificates.cmake)
- the same with `--fails-without` at the commit without authority.py's change
- `campaign verify system-failover-port-reuse --for A2`, then CI on the push.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | cause above; loop and fix proven locally (not yet as jobs: files outside the set) |
| A2 | not run |

## Result

Not ready.

## Rulings I made

- Fix by a bounded restart retry in the harness, not a server change: the server already sets
  SO_REUSEADDR on both acceptors, so no server change can rebind a port another process holds.
