# consensus-fetch-lane: Consensus fetch lane: name-to-bytes reads between peers

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status consensus-fetch-lane`.

## Outcome

consensus's `Peers` carries a fetch lane beside consensus frames and the snapshot channel: a host registers a reader (name to bytes, or not-found), and any member asks a peer for a name and gets its bytes back on the owner executor, over the same mTLS connections, pins, addresses and bulk-lane budgets. Detent's namespace-s5-3 uses it for history files (D91).

## Acceptance criteria

- A1 A fetch for a name the peer's reader holds returns its exact bytes, and not-found for one it lacks, over real TLS peers; replies complete on the owner executor: a transport test
- A2 A large fetch shares the bulk lane's budget and never delays consensus frames past their existing bound; concurrent fetches from several peers complete: a test with frame latency measured during a bulk fetch
- A3 A peer closing or timing out mid-fetch fails that fetch with an error and leaves the connection usable for consensus: a test, plus a mutant that leaks the pending fetch, killed
- A4 TSan clean on the transport tests: the tsan check
- A5 [artifact] `CHANGELOG.md`: "fetch"

## Contracts

Provides `consensus:fetch`: a host-registered reader `(name) -> optional<bytes>` and an async `fetch(peer, name, completion)` on `Peers`, with a new frame kind on the bulk lane. The frame is additive; peers on older consensus need not interoperate (Detent is greenfield, D43). Detent bumps its pin in `cmake/dependencies.json` within namespace-s5-3.

## Decisions

D91 (lead): history fetch rides Peers, not a second voter transport.

## May land early

None: it is a wire change.
