# consensus-dns-peers: journal

State: waiting on jobs 261111-064715-c0f32c 261111-064840-a5fa7d 261111-064843-5b508f
Quest: .campaign/detent/quests/consensus-dns-peers.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Waiting on jobs at consensus:931c42c: suite 261111-064715-c0f32c (A1-A4, A6), mutants
261111-064840-a5fa7d (no-deadline) and 261111-064843-5b508f (shared-thread), both `--for A2
--fails-without`; CI (linux-tsan) on the pushed quest branch.

## Next

1. Paste `campaign brief`'s Evidence table once the jobs land; State "ready for review".

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
