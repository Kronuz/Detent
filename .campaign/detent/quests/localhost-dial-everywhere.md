# localhost-dial-everywhere: test clients dial the daemon's bound address, not localhost, in authority, relay and docs tests

Risk: low. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status localhost-dial-everywhere`.

## Outcome

Test clients in tests/detent_authority, tests/detent_relay and tests/docs dial the address the daemon is bound to (127.0.0.1), and check the certificate as localhost. A client that dials "localhost" resolves ::1 first on macOS, where daemons do not listen, and about 1 in 16k such connects lands on its own ephemeral socket (TCP self-connect, which macOS refuses only for IPv4); the TLS client then reads its own ClientHello and fails with UNEXPECTED_MESSAGE. relay-tls-drain-flake proved this and fixed tests/detent_system (Detent:5bff29c7); its journal lists the remaining sites.

## Acceptance criteria

- A1 [artifact] No test client in tests/detent_authority, tests/detent_relay or tests/docs dials "localhost" for a daemon bound to 127.0.0.1: the journal names each site changed, and a grep over those trees is quoted in the journal with no remaining hits.
- A2 The suite passes at head (the gate check).

## Contracts

<What this quest provides or consumes (APIs, formats, protocols), stated precisely enough that a
quest depending on it can start before it lands.>

## Decisions

<Decisions made, and by whom; open questions, naming the owner or the lead.>

## May land early

<Internal code behind tests, unless the quest is a format, wire or public API. "None" when
everything waits for the review; `land --upto` refuses then.>
