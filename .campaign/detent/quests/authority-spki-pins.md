# authority-spki-pins: Authority voters pin peers by SPKI, the one consensus pin kind

Risk: normal. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status authority-spki-pins`.

## Outcome

Consensus carries one node-pin kind, an SPKI SHA-256 pin (Xapiand cluster-membership F11; one shape everywhere). Detent's voter configuration replaces `certificate_sha256` (a DER certificate pin) with `spki_sha256`; certificate pins are removed, not kept beside (greenfield, no compatibility). A renewed certificate on the same key keeps its pin.

## Acceptance criteria

- A1 Voters authenticate by SPKI pin; a peer with a matching certificate but a different key is refused, and a renewed certificate on the same key is accepted: tests
- A2 `certificate_sha256` is gone from code, config parsing and docs: a grep job
- A3 The suite and CI pass

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
