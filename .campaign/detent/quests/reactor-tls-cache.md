# reactor-tls-cache: reactor tls::configure: load certificates once, reload on change

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status reactor-tls-cache`.

## Outcome

`reactor::tls::configure` (`reactor_tls.h:56`) reloads certificates from disk on every call (transport-groups-ci journal:72). After this quest loaded certificates are cached and reused, a changed certificate is still picked up, and the per-call cost is measured.

## Acceptance criteria

- A1 Repeated `configure` calls on unchanged files load the certificates once: a test counting loads that fails without the cache (--fails-without): jobs.
- A2 A rotated certificate is served after rotation, never a stale one: a test: jobs.
- A3 `configure` cost before and after at named commits (strict): jobs.
- A4 The suite, ASan and TSan pass: jobs.

## Notes

Filed 2026-11-19 from the untracked-commitment audit (J4).
