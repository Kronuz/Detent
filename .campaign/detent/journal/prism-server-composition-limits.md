# prism-server-composition-limits: journal

State: waiting on jobs 261120-034115-0f8431 (suite) 261120-034119-e90fc9 (A1) 261120-034135-ffc330 (A2 mutant), then CI
Quest: .campaign/detent/quests/prism-server-composition-limits.md (the lead's; never edit it)

## Doing

Implemented at prism:c1e3ff0 (pushed): http aed0314 and reactor 6cd8f73 pinned; one `http::Limits`;
`reactor::tls::server_context` as `http::Tls`; `reactor::raise_open_files(max_clients + 64)`;
`Http::stop()` = `stop_accepting()`, drain = `wait_requests(b.cancel)` on its own thread, then
`cancel_requests()` and http's `stop()`. Tests added to prism_lifecycle: drain, limits, tls.
Mutant branch (local, not pushed) mutant/old-full-stop = prism:829072f, the old full stop.

## Next

1. When the jobs pass, check the mutant log fails in `drain` (not at the build); fix what fails.
2. CI on the pushed branch; then Evidence and State ready for review.

## Blocked on

Nothing.

## Rulings I made

- The wait runs on a thread, not the loop: wait_requests blocks, and the wheel timer that fires
  `b.cancel` runs on the loop, so a blocked loop would never cancel at bound - grace.
- Descriptor reserve 64 and the explicit/default rule follow the note (C5); `max_clients_set`
  records whether the value was given.
- Certificate within 14 days of expiry warns from Prism (reactor leaves the warning to the app).
- Timeouts other than idle keep http's defaults (header 30 s, inactivity 60 s): Prism's options
  table has no flags for them.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | pending: 261120-034119-e90fc9 (no local copy of either, at prism:c1e3ff0); lifecycle `limits` and `tls` cases |
| A2 | pending: drain case passes in suite 261120-034115-0f8431; fails without: 261120-034135-ffc330 at prism:829072f |
| A3 | [artifact] `ARCHITECTURE.md` "How `main` is put together" at prism:c1e3ff0: the deviation table lists none of the four rows |
| A4 | pending: suite 261120-034115-0f8431, CI |

## Result

pending
