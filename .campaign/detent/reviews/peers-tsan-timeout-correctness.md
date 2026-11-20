# peers-tsan-timeout review: correctness

Commit: consensus:015a7ea36ec69fb25d71d6065808a26995ebf376
Verdict: APPROVE
Date: 2026-11-19

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

The approval covers the code. Landing still needs the quest's own gates, which were queued when
I wrote this: A2 at 015a7ea (3b68ef), A3 (73a0e2, 401d23), the suite (4214f8), ASan groups (b6cc19),
CI 44582638852. It also needs my probe 261119-100317-f6753f to pass (F3). If P1 or P2b fail, this
review reopens as a defect in the picker's premise.

## Evidence I relied on

- 261119-095753-bf2aed (fails-without, mutant consensus:621becf = old bind-0-and-close picker at
  015a7ea): exit 8 with `taken-ports: threw: bind: Address already in use`. It fails for the
  right reason. Its pair at abb7276 (df6d37 passes, 92ae93 fails) matches.
- 261118-135201-486193 / 261118-135207-6362c5: the `throwing` case passes at the ~Cluster fix
  and times out (20 s) at mutant consensus:a1eb72a, which removes the ~Cluster stop. This is the
  hang itself, for the right reason.
- The journal's probe 2 (gdb stacks: the main thread in epoll_wait, lookups parked, no scenario
  frame) and probe 5 (`outage: threw: bind: Address already in use`) are GitHub runs I did not
  rerun. I take them as the link from throw to hang.

## What I checked myself

- Q1, the library: Peers' bind is in the constructor (include/consensus/transport/peers.h:113-119),
  before start() (:121) launches any task. A failed bind is a synchronous throw that leaves nothing
  running, and Peers does not turn it into a hang. The hang was the harness's: Cluster unwound
  voters that were already running without stop(), against the documented contract "retain this
  object until drained()" (peers.h:70-71). Their own shared_from_this tasks kept them redialing.
  ~Cluster (peers_test.cc:281-286) stops only `running[i]`, so a constructor that throws in
  restart() leaves the stopped, retired object alone. That is correct.
- Q2, the helper: the tests bind TCP (asio tcp::acceptor in Peers and groups' Host and Node). The
  claim is UDP, so it does not reserve TCP against a process that does not use the picker.
  Protection rests on three things:
  1. The range lies outside the ephemeral range, so no bind(0) or connect() is handed the port, and
     a redial cannot self-connect onto it.
  2. The UDP claim, bound without SO_REUSEADDR, keeps pickers in separate processes apart.
  3. bindable() rejects a port with a TCP listener at pick time.

  The ranges:
  - Linux: [first-16384, first), [16384, 32768) by default.
  - macOS: [32768, 49152).
  - FreeBSD: [1024, 10000) by default (first=10000).
  - first < 2048 with last > 64511: aborts loudly.

  No hard-coded test port in those ranges exists in consensus' or Detent's tests (grep).
- Probe 261119-100317-f6753f (`.scratch/detent/review-peers-tsan-timeout-correctness/ports_probe.sh`,
  light, 1 CPU, macOS, at 015a7ea). Queued when written.
  - P1: 1500 bind(0) and 500 connect() ports all fall in ephemeral_range(). This checks the oracle
    against the kernel.
  - P2: 4 processes × 150 free_port picks, with no duplicate and no ephemeral port.
  - P2b: 4 processes race claim() over the same 400 ports, and each port is won at most once.
  - P3: once a child has claimed a port, claim() in this process is refused and a plain TCP bind
    succeeds. This documents that the claim is UDP-only.
- Q3, coverage: no pick-then-close remains. These listeners bind 0 and stay open:
  - links_test.cc:83
  - connection_test.cc:122, :703, :923 (Relay)
  - tls_test.cc:57
  - groups_test.cc:135 (Host)
  - groups_test.cc:1588 (Gate)

  Groups' only port reused after a destroy (refill, :1423/:1484) and Cluster's restart ports
  (:1040) now come from free_port.
- Q4, proof: see F4.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | no | nit | No product defect is hidden. A failed listener bind throws synchronously from Peers' constructor, with no task launched. Peers only lives on, redialing, when a running instance is dropped without stop(), which its contract forbids. | peers.h:70-71, :113-121 | None. Optionally, the owner may file "a Peers dropped while running keeps itself alive" as a hardening item. That would narrow the contract, so it is the owner's call. |
| F2 | no | nit | The ports.h header says "a claim keeps tests running side by side apart". That holds only between pickers: the UDP claim does not stop a TCP bind (P3). A later fixed-port TCP binder below the ephemeral range could still collide. None exists today. | ports.h:3-4, :49-66; probe f6753f P3 | Say "tests that pick here" in the comment. |
| F3 | no | gap | taken-ports' oracle is the implementation's own ephemeral_range(). At the fix the contender loop never runs, so `!ephemeral(port)` would pass with a wrong range reading. Probe P1 checks the reading against real bind(0)/connect() on macOS, not on Linux. | peers_test.cc:1336-1356; probe f6753f P1 (pending) | Read f6753f. Optionally, add a bind(0) sample to taken-ports asserting each port is `ephemeral()`, so CI checks Linux and FreeBSD too. |
| F4 | no | gap | Probe 7 (320 runs) and the groups-lagging ×20 loop cannot show the old failure gone. Probe 5's rate was about 1/320, so P(0 of 320 \| unchanged) ≈ 37%. The proof is the mechanism (F3's premise plus bf2aed), not these loops. | journal "What throws, named"; run 44582684934 | Cite probe 7 and b6cc19 as regression runs, not as proof. |
| F5 | no | nit | A scenario that throws with a Mode::Hang FakeDns lookup parked (hung-resolver) prints `threw`, then hangs at exit. LookupThreads' atexit close waits for the parked resolver, and the scenario's FakeDns is never quiesced on unwind. ctest times out with the report already printed. | resolve.h:157-161; peers_test.cc:146-148 | Optional: quiesce the scenario's resolvers in a scope guard. |
| F6 | no | nit | claim() aborts if socket(AF_INET6) fails, so groups_test now needs IPv6 sockets. Each pick holds one UDP fd for the process's life (about 60 per peers run, under macOS's 256). | ports.h:52-56 | None needed. Fall back to AF_INET if a host lacks IPv6. |

Blocking: yes or no. Kind: defect (wrong behavior), gap (unproven claim or missing test), nit. A
finding that would remove or narrow user-visible behavior offers options, including keeping it,
and is never blocking.

## Not run

- Probe f6753f had not run when I wrote this. I did not see its result.
- No Linux or FreeBSD run of the probe. The Linux premise is ip_local_port_range, read from /proc.
- I did not rerun TSan, A3, the suite or CI. Those jobs were queued.
- I did not read the GitHub probe logs (1, 2, 5, 7) myself, beyond the job and run states.
