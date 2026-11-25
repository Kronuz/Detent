# consensus-dns-peers: Peers dial DNS names without ever blocking consensus

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status consensus-dns-peers`.

## Outcome

A peer address in `PeerOptions` is a host name or an IP literal with a port. Each dial attempt
resolves the name off the transport's executor, raced against a deadline, and dials the result; a
failed, empty or late lookup dials the last address that resolved for that peer, and with none yet
it backs off like a failed connect. A slow, failing or hung resolver never blocks the executor, the
consensus tick, another peer's dial, a dial to a cached address, or shutdown. Identity stays the
pinned certificate, so a name that resolves to the wrong node is refused at the TLS handshake. IP
literals never touch the resolver.

## Acceptance criteria

- A1 A peer named by host name connects and serves consensus traffic, and a peer whose name moves
  to a new address is redialed at the new one: a transport test with an injected resolver, plus one
  over the real system resolver (`localhost`): the suite.
- A2 A resolver that never answers delays nothing else: with one peer's lookup hung forever, the
  other peers connect, a peer with a cached address redials it within the deadline, the node keeps
  quorum and `stop()` returns promptly: a test with time bounds, `--fails-without` on a mutant that
  awaits the lookup with no deadline or shares one blocked resolver thread across peers.
- A3 DNS outage: lookups failing (NXDOMAIN, SERVFAIL, timeout) after the cluster formed keep every
  connection and redial at cached addresses; at startup with no cached address the peer retries
  with backoff while the remaining voters form quorum: a test.
- A6 A name with several addresses (A and AAAA) dials them in turn, IPv6 and IPv4 interleaved, each
  attempt bounded and the next started after a short stagger (RFC 8305 style), so an address with
  no route costs one stagger, not a full connect deadline; the cache keeps the whole set: a test
  with an unroutable first address.
- A4 A name resolving to another node's address is refused by the certificate pin, and the attempt
  retries without crashing or wedging: a test.
- A5 [artifact] `docs/mechanisms/peer-dns.md`: how resolution, the deadline, the cache and the
  resolver threads work, and what a hung `getaddrinfo` costs (one parked thread, nothing else).

## Contracts

Provides `consensus:transport`: `PeerOptions::addresses` maps `NodeId` to a host-and-port value
(host name or IP literal), plus a resolve deadline in `PeerOptions` and an injectable resolver for
tests. Xapiand also uses this transport: the lead tells the Xapiand lead when the quest starts and
before it lands.

## Decisions

- D60 (owner): DNS names are supported; slow DNS or outages must never block, deadlock or slow
  consensus.
- Lead: asio's `async_resolve` runs `getaddrinfo` on one internal thread per `io_context`, and a
  hung `getaddrinfo` cannot be cancelled, so it would stall every later lookup. The contributor
  picks a shape where a hung lookup parks only its own thread (for example a per-lookup thread
  whose result is dropped after the deadline).
- The async-drivers implementation (M1b, D57) also changes consensus's transport connections; if an
  async-drivers quest is active on these files, the lead orders the two with `after`.

## May land early

None.
