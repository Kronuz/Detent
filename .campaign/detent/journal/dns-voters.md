# dns-voters: journal

State: waiting on jobs 261111-093342-409257
Quest: .campaign/detent/quests/dns-voters.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Implemented at 142f57cf: pin bump (check_pins: 30 libraries, 0 mismatches), voter_host() validation and
PeerAddress pass-through in service.cc, tests test_voter_hosts.cc (A3), test_dns_voters.py names (A1) and
unresolvable (A2). (Job 261111-092711-ae41c4 cancelled: lead ruling D63 changed the named-local bind.) Named local voter now binds `::` when a v6 socket opens with v6_only off by default, else `0.0.0.0`; test_dns_voters.py names also connects over 127.0.0.1 and ::1.

## Next

1. Read the job; fix failures.
2. Mutant jobs (--fails-without) for A2 (resolve at parse) and A3 (drop validation), then the full suite via `campaign verify`.
3. Paste brief's Evidence table; Result; State ready for review.

## Blocked on

<What, and who decides. "Nothing" when nothing.>

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| <A1> | <from `campaign brief`> |

## Result

<When ready for review: what changed, its measured effect with numbers and the jobs behind them,
and what it leaves open. Results and how it works go in the docs of the repository it changed
(docs/perf/, docs/mechanisms/, a library's own); name them here. `campaign map` shows this.>

## Rulings I made

- Named local voter binds a wildcard without resolving (a name cannot be bound). Lead ruling (D63): `::` dual-stack, `0.0.0.0` only without IPv6; IP-literal local voter binds exactly as before. Consensus's Peers does not set v6_only, so the daemon probes the OS default at parse (a local socket probe, no DNS) and uses `::` only when v6_only defaults off; a host whose default is v6-only gets `0.0.0.0`. Setting v6_only off explicitly needs a PeerOptions knob in consensus.
- Host syntax: names refuse a trailing dot and an all-numeric final label (inet_aton forms like "1.2.3"); duplicates compare host and port as written (case-sensitive).
- Docs (docs/AUTHORITY_TRANSPORT.md 'literal-IP host') left to docs-site-config; outside the file set.
