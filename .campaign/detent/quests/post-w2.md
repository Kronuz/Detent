# post-w2: Post-W2 follow-ups: pins, bound ports, CR2 probes

Risk: normal. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status post-w2`. It landed before this
campaign, under the earlier campaign skill; its evidence is its ledger, `docs/status/post-w2.md`.

## Outcome

After W2, the reactor, http, http_log and task pins move to their heads, then cluster and consensus; daemons and fixtures start on port 0 and report the port they bound; the W2 review's CR2 probes become permanent tests.

## Acceptance criteria

- A1 [artifact] Pins: reactor, http_lib, http_log and task to their heads (Done 1); cluster and consensus once their reactor pins publish (Next 1): `docs/status/post-w2.md`
- A2 [artifact] `detentd --port 0` and relay port 0 are accepted and log the bound port; fixtures converted, retry wrapper and probes deleted (Done 2): `docs/status/post-w2.md`
- A3 [artifact] CR2 as `test_authority_service_wire`: keep-alive, pipelining, over-budget `503 outcome_unknown` and the write-deadline margin (Done 3): `docs/status/post-w2.md`
- A4 [artifact] Each new behavior has a mutant that fails its test (Done 4): `docs/status/post-w2.md`
- A5 [artifact] Authority, relay and managed suites pass natively, 83/83 (Done 5): `docs/status/post-w2.md`

## Contracts

`detentd` and the relay accept port 0 and log the bound port.

## Decisions

1. Fixtures that cannot start on port 0 stay as they are (Blocked on 2).

## May land early

None.
