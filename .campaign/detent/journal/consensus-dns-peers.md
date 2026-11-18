# consensus-dns-peers: journal

State: ready for review
Quest: .campaign/detent/quests/consensus-dns-peers.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Correctness review findings fixed at consensus:905823d (N1, N3, N4, N5, N6). Suite
261111-073019-6a3cbb green (24/24, peers test 14.4 s). CI run 43049093799 (linux-tsan,
linux-asan-ubsan) in progress; the Linux jobs got past the packages step this time.

Findings, for `campaign accept consensus-dns-peers correctness --confirm ...`:

| Finding | Fix | Evidence |
|---|---|---|
| N1 | `skip_past`: a dial whose connection fails authentication starts the next dial after that endpoint; a changed answer resets the order | suite 261111-073019-6a3cbb (`refused-first`); fails without: 261111-073030-46f6a8 (no-skip, `peers_test.cc:654`); review probe W at the fix 261111-073017-b40c82: "reached voter two: yes" (was NO, 53 refusals, at 931c42c) |
| N2 | CI rerun at the fix | CI run 43049093799 |
| N3 | a cached name dials the cache at once, the lookup refreshes behind it; only an uncached name waits | `slow-resolver`: redial 205 ms with a 1500 ms resolver and 1000 ms deadline; fails without: 261111-073033-9ae876 (wait-cached, redial 1130 ms) |
| N4 | no lookup of a name starts before the last one's deadline | `lookup-spacing`: 16 redials, 5 lookups in 1.2 s; fails without: 261111-073038-4daf76 (lookup-per-dial, 16 lookups) |
| N5 | doc: one parked thread per name per `Peers` | `docs/mechanisms/peer-dns.md` |
| N6 | none needed (R11): already rejected | `membership` scenario in suite 261111-073019-6a3cbb |

## Next

1. If CI 43049093799 linux-tsan is red, fix and rerun; otherwise the lead accepts the review.

## Blocked on

Nothing.

Messages: lead's A6 addition (RFC 8305 staggered dial, whole-set cache) is folded into the
design and proven by the `several-addresses` scenario.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| A1 | 261111-073019-6a3cbb |
| A2 | 261111-073019-6a3cbb; fails without: 261111-064840-a5fa7d, 261111-064843-5b508f, 261111-073033-9ae876 |
| A3 | 261111-073019-6a3cbb; fails without: 261111-073038-4daf76 |
| A6 | 261111-073019-6a3cbb |
| A4 | 261111-073019-6a3cbb; fails without: 261111-073030-46f6a8 |
| A5 | `docs/mechanisms/peer-dns.md` |

## Result

consensus `include/consensus/transport/`:

- `resolve.h` (new): `PeerAddress{host, port}`, `PeerResolver` (blocking, injectable),
  `system_resolver` (getaddrinfo), `interleave` (RFC 8305 section 4 order), `Lookup` (one
  detached thread per lookup; answer delivered under a mutex unless `abandon()`ed).
- `peers.h`: `PeerOptions::addresses` is `map<NodeId, PeerAddress>`; `resolve_deadline` (1 s),
  `stagger` (250 ms), `resolver`. Per peer a route: literal endpoints or the cached set, at most
  one lookup in flight, dials wait on a timer to the lookup's deadline. Dials race every
  endpoint, the next started a stagger later or at once when all started attempts failed,
  bounded by `deadline`. A dial pins only the dialed peer. `PeerStats::lookups,
  lookup_failures, lookup_timeouts`; `PeerOccupancy::lookups`. `pause()` returns at once after
  stop().
- `test/transport/peers_test.cc`: three Workers (MemoryIO) over TLS with in-process EC
  certificates; scenarios names-and-moves, system-localhost (A1), hung-resolver (A2), outage
  (A3), wrong-node (A4), several-addresses (A6). Local run (Debug, macOS): all pass in ~8 s;
  hung cached name redialed in 341 ms (deadline 200 ms), stop() drained in 2 ms with two hung
  lookups, unroutable first address connected in 106 ms (stagger 100, deadline 5000).
- Review fixes (905823d): `Route::first` and `skip_past` (N1), cache-first `resolve()` and lookup
  spacing by `answer_by` (N3, N4); scenarios refused-first, slow-resolver, lookup-spacing,
  membership; mutants no-skip, wait-cached, lookup-per-dial.
- `test/transport/dns_mutants.sh`: both mutants caught locally (no-deadline: redials of the hung
  peer stop; shared-thread: voter one never reaches three, no quorum).
- Docs: `docs/mechanisms/peer-dns.md`; CHANGELOG, README link.

Consumers: Detent's `service.cc` and Xapiand must build `PeerAddress{address.to_string(), port}`
(or the name) when they bump the pin; `dns-voters` covers Detent.

## Rulings I made

- R1 Shape: one `PeerAddress{host, port}` for literals and names (no endpoint overload); a
  literal is detected once at construction and never reaches the resolver.
- R2 One lookup thread per lookup, at most one in flight per peer (both lanes share it), so a
  hung getaddrinfo costs one parked thread per name. Late answers still refresh the cache.
- R3 The deadline runs from the lookup's start; a dial joining an overdue lookup dials the cache
  at once.
- R4 Dial pins only {local, dialed peer}: a wrong-node address fails right after the TLS
  handshake, before hellos; the accepting side still pins all voters.
- R5 Defaults: resolve_deadline 1 s, stagger 250 ms (RFC 8305's Connection Attempt Delay).
- R6 `pause()` skips its wait once stopping (pre-existing: a backoff begun after stop() held
  drained() up to 199 ms).
- R7 The DNS tests live in `peers_test.cc` (CMakeLists.txt is outside the file set) and generate
  their own certificates in-process, since that test has no TLS-fixture dependency.
- R8 A restarted `Peers` starts with an empty cache; during an outage it cannot dial a name it
  never resolved (documented, and asserted in the outage scenario).
- R9 (N1) A dialed connection that never authenticates as the dialed peer (pin refusal, foreign
  service, handshake failure) moves the route's start past that endpoint (`skip_past`, keyed by
  the endpoint, so two lanes refused at once agree). An answer equal to the cache keeps the
  start; only a changed set resets it, so a periodic refresh does not send the next dial back to
  the known-wrong address.
- R10 (N3, N4) One field serves both: a lookup's `answer_by` is its deadline and the earliest
  start of the next lookup of that name (minimum interval = `resolve_deadline`). A cached name
  never waits; `lookup_timeouts` now counts uncached dials whose lookup was late (the A2 test
  checks it in the hung phase, and the cached redial checks a second hung lookup started
  instead). A moved name is followed one dial after the refresh answers.
- R11 (N6) Not reproducible: `validate_peer_identity` makes voters unique and containing local,
  and the constructor requires `addresses.size() + 1 == voters.size()` with every other voter
  present, so the key set already equals the other voters. The `membership` scenario pins this
  (non-voter instead of a voter, beside them, local in place of a voter); it passes at 931c42c
  too, so it has no fails-without. No code change.
- R12 The A6 scenario's restart phase waits for the refresh lookup to answer (idle past the
  spacing first), since a cached redial no longer waits for it.
