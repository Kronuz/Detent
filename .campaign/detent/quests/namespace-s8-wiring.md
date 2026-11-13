# namespace-s8-wiring: Namespace S8 wiring: relay namespace mirror in the HTTP service

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status namespace-s8-wiring`.

## Outcome

detent-relay serves namespace history to its clients from its `NamespaceRelay` mirror (namespace-s8): it reads layouts, files by name (namespace-s5-3's peer fetch) and W5/W6 frames (namespace-s6-cutover) from the authority or an upstream relay over HTTP, keeps the mirror durable across restarts, and answers W5/W6 the same as the authority would.

## Acceptance criteria

- A1 A relay process fed by a live authority cluster mirrors every block, base and section byte-identically and answers W5/W6 byte-identically to the authority, also behind an upstream relay: an end-to-end test over HTTP
- A2 A relay process restart fetches nothing the mirror holds and answers every R at or above the cut unchanged: the same test with bytes fetched
- A3 The relay never joins consensus and refuses member endpoints (W8): a test
- A4 [artifact] `docs/status/namespace-s8.md`: "service"

## Contracts

Consumes `NamespaceRelay`/`Upstream` (namespace-s8), the peer fetch endpoint (namespace-s5-3) and W5/W6 (namespace-s6-cutover). Provides the relay's namespace HTTP endpoints, identical in bytes to the authority's.

## Decisions

Split from namespace-s8 by the lead (2026-11-13): the authority had no layout export, files by name or W5/W6 to read when S8 landed its mirror.

## May land early

None.
