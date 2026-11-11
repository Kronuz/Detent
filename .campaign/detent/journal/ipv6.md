# ipv6: journal

State: waiting on jobs 261111-151643-1b18a0
Quest: .campaign/detent/quests/ipv6.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Full suite 261111-135949-b282c6: all passed except test_system_dual_stack (daemon exited at startup; log now printed on failure). test_system_managed_relay_ipv6 RAN and passed (A1). Rerun of dual_stack+dns_voters: 261111-141907-7e3db2.

## Next

1. Read results; fix bugs the tests find (A3); paste Evidence.

## Blocked on

Nothing.

## Rulings I made

- Config has no host:port or URL fields (address and port are separate JSON fields, address via asio make_address), so IPv6 literals are bare `::1` in JSON; no bracket parsing exists to add.
- local_wildcard no longer probes the OS v6-only default: with consensus-dual-stack `::` is dual-stack everywhere, so `::` whenever a v6 socket opens.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| A1 | suite 261111-151643-1b18a0 (test_system_managed_relay_ipv6 ran and passed in the earlier suite 261111-135949-b282c6, 26.7 s) |
| A2 | 261111-141907-7e3db2; repeat 261111-151449-64a3c0 (test_system_dual_stack, `served IPv6 and IPv4 clients` for authority and relay, host v6_only default off) |
| A3 | none found: the tests passed on the existing code; the only change that was not a test is local_wildcard (A5). The one dual-stack failure (daemon exited at startup, code 1, in the full suite under -j4) is unexplained, passed on rerun (alone, and a repeat job with 4 passes logged); likely a port collision from free_port releasing ports before use. |
| A4 | `CHANGELOG.md`, Unreleased, "IPv6" |
| A5 | suite 261111-151643-1b18a0 (test_authority_dns_voters_names asserts ::1 and 127.0.0.1; passed in 261111-141907-7e3db2) |

## Result

<When ready for review: what changed, its measured effect with numbers and the jobs behind them,
and what it leaves open. Results and how it works go in the docs of the repository it changed
(docs/perf/, docs/mechanisms/, a library's own); name them here. `campaign map` shows this.>

## Rulings I made

<Decisions inside the quest's scope, so the reviewer can check them.>
