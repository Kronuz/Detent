# legacy-wave2: Legacy removal wave 2, steps B to G

Risk: critical. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status legacy-wave2`. It landed before this
campaign, under the earlier campaign skill; its evidence is its ledger, `docs/status/legacy-wave2.md`.

## Outcome

The classic distributor, classic Raft, the `detent_publish` example, the authority admin tool, the `detent_http` library and its polling client, the shared-memory agent and the unmanaged `detent-agent` are gone; `detentd` is the authority, starting with no source configured is a hard error, and docs and website describe only what remains.

## Acceptance criteria

- A1 [artifact] One commit per step B, C, D, E: `docs/status/legacy-wave2.md`
- A2 [artifact] A full configure and build of every remaining target: `docs/status/legacy-wave2.md`
- A3 [artifact] The full native suite: `docs/status/legacy-wave2.md`
- A4 [artifact] Separate ASan/UBSan and TSan builds of the authority and relay suites: `docs/status/legacy-wave2.md`
- A5 [artifact] No reference to a deleted symbol, file, target or option outside historical records: `docs/status/legacy-wave2.md`
- A6 [artifact] A CHANGELOG entry: `docs/status/legacy-wave2.md`
- A7 [artifact] CI green: `docs/status/legacy-wave2.md`
- A8 [artifact] Step F: `detent_http`, its tests and the agent's HTTP source flags are gone and rejected as unknown: `docs/status/legacy-wave2.md`
- A9 [artifact] Step G: the shared-memory agent and unmanaged `detent-agent` are gone; `no source configured` is a hard error (D11); roadmap item 4 dropped (D12): `docs/status/legacy-wave2.md`

## Contracts

Removes `DETENT_BUILD_DISTRIBUTOR`, `--source-address`, `--source-http`, `--source-path`, `--poll-interval-ms` and their config fields; `--authority` stays required.

## Decisions

1. Owner rulings D11, D12, D13; wave decisions 1-5 in the ledger.
2. The leftover-reference grep across docs and website was not done before the developer stopped; the lead made it a follow-up, not a landing condition.

## May land early

None.
