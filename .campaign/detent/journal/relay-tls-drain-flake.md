# relay-tls-drain-flake: journal

State: ready for review (suite 261120-043325-0ce5e8 and A1 job 261120-043323-963268 passed at 776c7f24; CI pending on the branch)
Quest: .campaign/detent/quests/relay-tls-drain-flake.md (the lead's; never edit it)

## Doing

Waiting on the reproduction and fix jobs, the suite (`campaign verify`) and CI.

## Next

1. Read the jobs' logs: a8d3ef must show UNEXPECTED_MESSAGE counts (not a cert-generation failure),
   0d3927 the fleet pair (UNEXPECTED_MESSAGE, then whether "did not drain" follows); record them.
2. Suite green and CI green at the head; then ready for review.

## Blocked on

The lead: the file set is tests/system/**, which does not exist; `campaign status` flags the
changes in tests/detent_system/** as outside it. Widen it to tests/detent_system/** (recommended).

For the lead (outside this file set, same mechanism, not changed): HTTPSConnection("localhost", port)
to 127.0.0.1-only daemons in tests/detent_authority/test_lost_quorum.py:39,
tests/detent_authority/test_multivoter.py:72 and :297, tests/detent_relay/test_relay_service.py:112 and :345;
tests/docs/run_e2e.py:385 (urllib to https://localhost). Each can fail the same way, about 1 call in 16k
on macOS. Recommend a follow-up quest applying the same dial.

## Mechanism (A1)

- 261119-181942-21336c, test_system_fleet_raw: the harness's own client (authority.py `_exchange`, from
  run_fleet `relay_snapshot` /v1/health) failed in `do_handshake` with `[SSL: UNEXPECTED_MESSAGE]`;
  TaskGroup teardown then stopped the fleet, which reported "did not drain to retained slots".
- `_exchange` connected to "localhost". getaddrinfo("localhost") on macOS returns ::1 first; the daemons
  bind 127.0.0.1 only, so every call first connects to [::1]:PORT, normally refused, then falls back.
- macOS refuses an IPv4 TCP self-connection (EINVAL) but not IPv6: when the ephemeral port the kernel
  picks equals PORT, the ::1 connect succeeds onto its own socket. Probe (scratch, 40k connects to a free
  port each): IPv4 39998 refused + 2 EINVAL; IPv6 39998 refused + 2 self-connected, each failing TLS with
  `[SSL: UNEXPECTED_MESSAGE]` (the client reads its own ClientHello). Rate ~1 per 16-20k calls, load
  independent: a rare, per-call fault, matching 0 of 12 in the earlier A/B.
- A fake server sending a ClientHello-type record reproduces the same string; plain HTTP gives
  WRONG_VERSION_NUMBER, a received alert gives an ALERT reason: only a self-connection explains it here.
- The C++ clients (agent, relay upstream, fleet_load) dial configured addresses (no resolver), and
  test_managed_process.sh uses curl --resolve: not affected.
- Same iteration, separate causes: test_relay_isolated_process failed at test_managed_process.sh:151
  (empty agent never reached the durable revision within its 30 s; no SSL error in its output);
  test_relay_mirror_crash timed out (900 s budget, 812.8 s spent in its two exhaustive phases at load
  ~60). Neither shows UNEXPECTED_MESSAGE; not pursued here.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | fails-without (submitted with --fails-without at 76465578, pre-fix; exit 1 = failed as it must): 261120-031721-a8d3ef (100k natural calls: 2 UNEXPECTED_MESSAGE of 100000), 261120-031733-0d3927 (fleet_raw with [::1] echo: UNEXPECTED_MESSAGE in both runs); 261120-043323-963268 (test_localhost_dial at head, passes) |
| A2 | 261120-031724-7d3e1c (test_localhost_dial fails without fix), 261120-031723-0c5b78 (100k natural calls, fixed, 0 errors), 261120-031734-bed4b1 (fleet_raw with [::1] echo, fixed, 50 runs 4 at once) |
| A3 | 261120-043325-0ce5e8 (suite, exit 0 at 776c7f24; the earlier 0c7ca0 timeout was test_agent_store_crash under load), CI |

## Suite 261120-031802-0c7ca0 (exit 8)

Only test_agent_store_crash failed, by the 900 s ctest timeout (an exhaustive crash model, in the log
"catch-up 898.4 s"); every other test passed. Not this quest (no change under tests/detent_agent or the
store): the same test passes in 38-760 s across earlier jobs and scales with load (-j4 plus other holds).
Rerun at head: 261120-043325-0ce5e8, passed.

## Result

- Fix Detent:5bff29c7, tests/detent_system/authority.py: `_exchange` connects to the fixture's address
  and verifies the certificate as localhost.
- Detent:76465578 adds test_system_localhost_dial (deterministic: [::1] echo beside a 127.0.0.1 TLS
  server; `--natural N` measures the raw rate) and fleet_dial_fault.py/.sh (fleet_raw with the same
  echo on every relay port).

## Rulings I made

- The file set reads tests/system/**, a path that does not exist; I took it to mean tests/detent_system/**
  (the failing harness) and changed nothing elsewhere.
- Dial via http.client's `_create_connection` hook rather than a hand-written HTTP exchange: keeps
  http.client's request and response handling; one line.
- The fleet probe is not a ctest (60+ s per run); test_system_localhost_dial carries the regression.
