# consensus-dns-peers review: correctness

Commit: consensus:931c42c0df37a64cd8fe378a6fbe15f2d1f97510
Verdict: CHANGES
Date: 2026-11-11

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

## Evidence I relied on

- Suite 261111-064715-c0f32c (Release, macOS): 24/24, `consensus_transport_peers` 8.2 s. It covers
  A1, A2, A3, A4 and A6 as the scenarios are written. I read each scenario's checks and accept
  them as written.
- A2 mutants 261111-064840-a5fa7d (no-deadline) and 261111-064843-5b508f (shared-thread). Both
  logs show a CHECK abort (exit 134), not a hang or a kill at exit. no-deadline fails at
  `peers_test.cc:510` (the hung, uncached peer's redials stop). shared-thread fails at `:503`
  (voter one never resolves "three", so there is no quorum). Each mutant tests the fault it
  names.
- CI run 43042148956 does **not** count as evidence. Every Linux job, including linux-tsan and
  linux-asan-ubsan, failed in the `ccache` packages step (exit 127/100) before configuring.
  freebsd failed on ssh (exit 8). Only macos-x86_64 and macos-arm64 passed. TSan has not run at
  this commit (N2).

## What I checked myself

Scripts are under `.scratch/review-consensus-dns-peers-correctness/` (home checkout). Run them
from the consensus checkout as jobs.

- 261111-065838-38be92 runs `lifetime_probe.sh`. It builds the peers test's fixtures plus the
  probes in `probe_scenarios.inc` under ASan+UBSan (Debug) and runs them. All of these are clean:
  - L1: Peers never stopped, io_context destroyed with two hung lookups, then the resolver
    returns. `~Peers` abandons the lookups, and nothing is posted to the dead executor. There is
    one call per name.
  - L2: stop, drain (0 ms with two hung lookups), destroy, then the lookups answer.
  - L3a and L3b: the answer is posted while the executor is idle, then the queue is run or
    destroyed, after stop() or with no stop.
  - L4: 150 rounds of abandon() racing delivery (answers after 0-3 ms, stop at random points).
  - W: a two-address name whose first endpoint is another voter. This probe found N1.
- 261111-070223-0eaa50 runs `load_probe.sh`. The peers test ran 8 times beside 24 nice-19
  busy loops, with the load average at 44-50. All 8 rounds passed, and every timed check kept a
  wide margin:

  | Check | Measured | Bound |
  |---|---|---|
  | Cached redial through a hung lookup | 344 ms | 1000 ms |
  | stop() drain | 1 ms | 100 ms |
  | Unroutable address first | 108-110 ms | 1000 ms |
  | Cached set | 175 ms | 2000 ms |

  The A3 lookup count was 16-17 lookups in 1.25 s, within its bound of 26. The time-bounded
  tests are not flaky at this load.
- Code reading, `include/consensus/transport/peers.h` and `resolve.h`:
  - Lifetime: the lookup thread holds only the `Lookup` and a resolver copy. Delivery posts with
    a `weak_ptr`, under the same mutex `abandon()` takes. stop() and `~Peers` abandon.
  - At most one lookup per peer: `route.lookup` is cleared only in `resolved()`. The hung test
    checks `count("two") == 1` and `occupancy().lookups == 1`.
  - The race: every attempt sits in `connecting_` and holds `tasks_`. The first success wins and
    the rest are closed. A late success after a winner is closed. A success after the loop exits
    lands on a dead `Race` whose socket is already closed. `dial()` copies the endpoints, so a
    cache replaced mid-dial is harmless.
  - The whole dial is bounded by `deadline`, and stop() closes `connecting_`, which wakes the
    race.
  - Attribution: a dial pins only {local, expected} and still checks `remote.node != expected`.
    A connection cannot be attributed to the wrong NodeId.
  - stop(): the resolve wait timers are cancelled by `abandon_lookups()`. `pause()` returns at
    once after stop(), and no new timer or lookup can start after stop(), because `connect`,
    `dial` and `pause` all check `stopping_` before suspending.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| N1 | yes | defect | A name with several addresses whose earlier endpoint reaches another voter, or any service that accepts TCP on that port, never connects to the right voter while that record stays. The race picks its winner at TCP connect, the pin then refuses it, and the next dial runs the same race in the same order and gets the same winner. A4 says the attempt "retries without wedging", and A6 makes multi-address names the normal case. A stale round-robin record after an IP is reused (for example, pod IPs) wedges this link indefinitely. It also costs the wrong voter a TLS handshake about every 75 ms per lane. | Probe W in 261111-065838-38be92: in 4 s voter one made 53 dials, all refused by the pin, and never reached voter two; voter three rejected 55. The code is at `peers.h:342-344` (dial, then run, with no feedback into the route). | When a dialed connection fails authentication, advance the route so the next dial starts after the endpoint that failed, for example by rotating `route.endpoints` past the winner. A fresh answer resets the order. The test is the W scenario: "two" answers {voter three's address, voter two's address}, and voter one reaches voter two within a few backoffs. |
| N2 | no | gap | TSan, and ASan/UBSan over the suite, have not run at this commit. CI's Linux jobs failed in the packages step (infra), and freebsd failed on ssh. The land check already blocks on this. | CI run 43042148956 | Rerun CI at the fixed commit. |
| N3 | no | gap | Owner's call. With a slow resolver that still answers (slower than `resolve_deadline`), every dial starts a new lookup and waits the full deadline (1 s) even when the cache was refreshed moments ago. Under slow DNS, each reconnect therefore costs up to 1 s more than an IP literal. This meets the quest text ("each dial ... raced against a deadline"). | `peers.h` `resolve()`; doc "The deadline" | Options: keep it; or dial the cache at once and resolve in parallel, waiting only when the cache is empty or the cached dial fails; or reuse an answer younger than a minimum age. |
| N4 | no | nit | Lookups are per dial. A peer that is down but resolvable costs about 13 lookups/s per dialing node (one thread spawn plus one DNS query each), because both lanes back off 100-199 ms. There is no minimum interval between lookups. | load probe, A3: 16-17 lookups in 1.25 s | Optional: a minimum interval between lookups of one name (for example, `resolve_deadline`). |
| N5 | no | nit | "One parked thread per peer name" holds per `Peers` instance. A restarted `Peers` starts its own lookup while the old hung thread is still parked, so parked threads grow with restarts × names until the resolver returns. | `docs/mechanisms/peer-dns.md:90`; R8 | Say "per name per Peers" in the doc. |
| N6 | no | nit | An `addresses` entry for a non-voter is accepted by the constructor. start() still launches its connect, which throws `out_of_range` at `routes_.at(peer)` and counts as one rejection. Before this change it dialed and failed at the pin forever, so the new behavior is quieter but still silent. | `peers.h` start() and resolve() | Reject such entries in the constructor, or skip them in start(). |

## Not run

- TSan of anything (CI's, see N2). My ASan probe instruments only the probe translation unit;
  journal and reactor were built with ASan in the same build directory.
- Linux behavior of the black-hole fallback (a full accept queue) in several-addresses. That is
  CI's.
- The real `getaddrinfo` hanging. Only injected resolvers hang.
- Detent and Xapiand consumers of the changed `PeerOptions::addresses` contract. Those are the
  dns-voters quest and the Xapiand lead.
