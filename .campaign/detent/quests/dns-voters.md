# dns-voters: Authority voter addresses accept DNS names

Risk: normal. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status dns-voters`.

## Outcome

The authority's `voters[].host` accepts a host name or an IP literal. Parsing the configuration
never resolves a name, so a DNS outage cannot stop the daemon from starting; names reach
consensus's transport, which resolves them per D60. Duplicate voters are refused by host and port
as written, and the voter set stays static and pinned.

## Acceptance criteria

- A1 A three-voter cluster configured by host names (`localhost` aliases) elects a leader and
  serves: an authority test in the suite.
- A2 Config parse does no lookups: with a voter host that never resolves, `detentd` starts, the
  other two voters form quorum and the daemon reports ready: a test.
- A3 Invalid hosts (empty, over 253 bytes, bad labels, unspecified or multicast IP literals) and
  duplicate host-and-port pairs are refused with a clear error: a test.

## Contracts

Consumes `consensus:transport` (host-and-port peer addresses, from `consensus-dns-peers`).
`docs-site-config` documents DNS voter addresses after this lands.

## Decisions

- D60 (owner).

## May land early

None.
