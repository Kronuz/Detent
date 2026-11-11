# consensus-dual-stack: Peers listening on :: accept IPv4 too

Risk: low. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status consensus-dual-stack`.

## Outcome

When `PeerOptions::listen` is the IPv6 wildcard `::`, `Peers` opens its acceptor with `v6_only`
off, so one socket serves IPv4 and IPv6 peers on every OS, including those whose default is
v6-only (FreeBSD). Any other listen address binds as today.

## Acceptance criteria

- A1 A `Peers` listening on `::` accepts a peer over 127.0.0.1 and over ::1: a transport test in
  the suite, `--fails-without` on a revert where the OS default is v6-only, or the journal states
  the host's default and that CI's FreeBSD job is the check.
- A2 [artifact] `CHANGELOG.md` names the change.

## Contracts

`consensus:transport`: no API change.

## Decisions

- D63 (owner: IPv6 supported). Found by dns-voters: a named local voter binds `::` only when the
  OS defaults to dual-stack, else `0.0.0.0`.

## May land early

None.
