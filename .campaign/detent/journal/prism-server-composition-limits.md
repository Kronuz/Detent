# prism-server-composition-limits: journal

State: waiting on jobs 261120-035135-f42468 (suite) 261120-035136-5bd4e1 (A1) 261120-035138-af5e5b (A2 mutant) and CI run 44731002558 at prism:e0c1ddd
Quest: .campaign/detent/quests/prism-server-composition-limits.md (the lead's; never edit it)

## Doing

Implemented at prism:c1e3ff0, tests fixed at prism:e0c1ddd (pushed; the first suite 261120-035135-f42468 failed on EADDRINUSE: test ports came from the ephemeral range, TIME_WAIT from the load test; now probed from 20000): http aed0314 and reactor 6cd8f73 pinned; one `http::Limits`;
`reactor::tls::server_context` as `http::Tls`; `reactor::raise_open_files(max_clients + 64)`;
`Http::stop()` = `stop_accepting()`, drain = `wait_requests(b.cancel)` on its own thread, then
`cancel_requests()` and http's `stop()`. Tests added to prism_lifecycle: drain, limits, tls.
Mutant branch (local, not pushed) mutant/old-full-stop = prism:6df5be4, the old full stop.

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
| A1 | pending: 261120-035136-5bd4e1 (no local copy of either, at prism:e0c1ddd); lifecycle `limits` and `tls` cases |
| A2 | pending: drain case passes in suite 261120-035135-f42468; fails without: 261120-035138-af5e5b at prism:6df5be4 |
| A3 | [artifact] `ARCHITECTURE.md` "How `main` is put together" at prism:e0c1ddd: the deviation table lists none of the four rows |
| A4 | pending: suite 261120-035135-f42468, CI run 44731002558 |

## Result

pending
