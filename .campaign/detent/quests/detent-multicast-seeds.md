# detent-multicast-seeds: Detent nodes find seeds by multicast on one network, with no flags, through consensus's shared seed source

Risk: normal. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status detent-multicast-seeds`.

## Outcome

Detent nodes on one network find each other with no flags, through consensus's shared seed source (`transport/seeds`, consensus:8801b99; docs/mechanisms/seeds.md). Authority voters advertise and seek the voter service; relays advertise the relay service and seek voters and relays; agents seek relays. Discovery only finds candidates: every join still authenticates over TLS with SPKI pins as usual. Configured seeds and DNS seeds keep working beside it, and multicast can be switched off.

## Acceptance criteria

- A1 Three voters, one relay and one agent on loopback start with no seed flags and reach a working cluster: a system test
- A2 A node advertising the wrong service, or one whose pin doesn't match, is never joined: tests
- A3 With multicast off, no datagram is sent or read: a test
- A4 The quickstart docs show the no-flags start
- A5 The suite and CI pass

## Contracts

<What this quest provides or consumes (APIs, formats, protocols), stated precisely enough that a
quest depending on it can start before it lands.>

## Decisions

<Decisions made, and by whom; open questions, naming the owner or the lead.>

## May land early

<Internal code behind tests, unless the quest is a format, wire or public API. "None" when
everything waits for the review; `land --upto` refuses then.>

## Tasks

<The steps that build the feature, one agent each: `T1 <what> (files; assignment, tier, effort;
after T0)`. Each merges into the quest branch on its own tests.>
