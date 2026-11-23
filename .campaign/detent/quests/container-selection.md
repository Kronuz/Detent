# container-selection: EX-0: flat-map adoption and container qualification

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status container-selection`. It landed before this
campaign, under the earlier campaign skill; its evidence is its ledger, `docs/status/container-selection.md`.

## Outcome

Detent's model map is the public flat-map library at an immutable pin behind a forwarding header, with no private engine; canonical JSON and mapped payload bytes are unchanged, and the hosted matrix and hardened lifecycle stress pass.

## Acceptance criteria

- A1 [artifact] Contributor tip `19cffc5` reconciled: strong copy assignment, allocation-refusal tests and publication atomicity intact (item 1): `docs/status/container-selection.md`
- A2 [artifact] flat-map `a9a52b0` passes its seven-job matrix; the candidate uses that pin and a forwarding header; canonical JSON and mapped payload bytes match (item 2): `docs/status/container-selection.md`
- A3 [artifact] A clean public-pin native build passes 224/224; Rust and Python pass five binding checks each; the embedded SDK consumer fixture passes (item 3): `docs/status/container-selection.md`
- A4 [artifact] Hosted run 40125876413 at `a686fe1` passes all six enabled jobs (item 4): `docs/status/container-selection.md`
- A5 [artifact] The hardened lifecycle stress run 40125771247 passes 400 repetitions across four concurrent drivers (item 5): `docs/status/container-selection.md`
- A6 [artifact] Two isolated-account fixture corrections pass 1,000 cleanup and 100 controlled-refund repetitions (item 6): `docs/status/container-selection.md`

## Contracts

Consumes `flat-map:a9a52b0`. Canonical JSON and mapped payload bytes unchanged; C++ consumers rebuild.

## Decisions

1. The architect rejects btree `44cb140` as a portable lifetime repair; that branch stays experimental.
2. The 5M exploratory trial (item 7) is capacity evidence, not part of this landing (RC-3).

## May land early

None.
