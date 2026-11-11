# ipv6: IPv6 end to end: voters, relays, agents

Risk: low. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status ipv6`.

## Outcome

Every address Detent takes or listens on works over IPv6 as it does over IPv4, and tests prove it:
authority voters and the publisher, the relay's listen and upstream addresses, the agent's and the
delivery client's upstreams, and `detentd`'s readiness probe. The code already uses asio address
types; this quest adds the missing IPv6 tests and fixes whatever they find (bracketed literals in
URLs, loopback choice, dual-stack binds).

## Acceptance criteria

- A1 A three-voter authority on `::1` elects a leader and publishes, and a relay and an agent
  connected over `::1` (URLs with `[::1]:port`) deliver a generation end to end: a system test in
  the suite.
- A2 A relay or authority bound to `[::]` serves both an IPv4 and an IPv6 client on hosts where the
  OS allows dual-stack, and the test says which it ran: a test in the suite.
- A3 Each IPv6 bug found has a test that fails without its fix (`--fails-without` on a revert), or
  the journal states none were found.
- A4 [artifact] `CHANGELOG.md` names IPv6 support; the configuration page is `docs-site-config`'s
  (it documents IPv6 literals and bracketed URLs after this lands).

## Contracts

None changed: configuration fields keep their shapes; IPv6 literals are valid where IPv4 ones are.

## Decisions

- Owner, 2026-11-11: support IPv6 (D63). Names with A and AAAA records are dialed per
  consensus-dns-peers A6.

## May land early

Tests.
