# quickstart-managed: Quick-start on the loopback authority and managed agent

Risk: low. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status quickstart-managed`. It landed before this
campaign, under the earlier campaign skill; its evidence is its ledger, `docs/status/quickstart-managed.md`.

## Outcome

`QUICKSTART.md` and the website tutorial teach one loopback recipe: a single-voter `detentd --authority`, knobs published through its transactional API, a managed `detent-agent` over verified TLS, and `detent get` reading the generation.

## Acceptance criteria

- A1 [artifact] `QUICKSTART.md` and the website tutorial pages teach one loopback recipe: single-voter authority, transactional publish, managed agent over verified TLS, `detent get`: `docs/status/quickstart-managed.md`
- A2 [artifact] Every command runs on this host exactly as written: `docs/status/quickstart-managed.md`
- A3 [artifact] The Client's `source_path` and `source_poll` docs stay as they are (step H): `docs/status/quickstart-managed.md`
- A4 [artifact] The website build job is green: `docs/status/quickstart-managed.md`

## Contracts

None.

## Decisions

1. The pages drop Homebrew until the formula is bumped (D22).
2. Findings for the lead: `detentd --daemonize` reports ready about 2 s early (quest `daemon-ready-leader`); stale `/v1/snapshot` lines (quest `legacy-h`).

## May land early

None.
