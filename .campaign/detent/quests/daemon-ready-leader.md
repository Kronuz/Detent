# daemon-ready-leader: detentd reports ready only once a leader exists

Risk: normal. The lead writes this quest; contributors read it and never edit it. Its machine
state (status, branch, checks, reviews) is in `state.json`: `campaign status daemon-ready-leader`.

## Outcome

`detentd --daemonize` returns, and `detentd --status` reports `"ready":true`, only once the authority
has a leader that accepts `/v2/open`. Today it returns when the listener is up
(`src/detentd/main.cc:190-193`), about 2 s before a single voter accepts `/v2/open`, and `--status`
already reports ready with role 0, Follower (`src/detent_authority/service.cc:1595-1596`).

## Acceptance criteria

Sources: handoff owner question 7 ("make ready wait for a leader; a small fix with a test") and
`docs/status/quickstart-managed.md`, Blocked on 2.

- A1 A test opens a session immediately after `detentd --daemonize` returns, with no retry, and it succeeds: the `native` gate
- A2 `detentd --status` never reports `"ready":true` while no leader exists: the `native` gate
- A3 Revert check: with the old readiness, the new test fails: a job
- A4 [decision] Readiness waits for a leader: D23

## Contracts

Changes the readiness contract of `detentd --daemonize` and `--status`. The quick-start's retry of
the open with the same nonce (`QUICKSTART.md`, website `quickstart.md`) becomes unnecessary.

## Decisions

1. D23 is the handoff's default; the owner may overrule it.
2. Open (lead): whether the quick-start drops its retry in this quest.

## May land early

None.
