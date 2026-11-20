# prism-server-composition-limits: journal

State: waiting on jobs 261120-040331-771e99 (suite) 261120-040332-52f4bf (A1) 261120-040334-0bfcc8 (A2 mutant) 261120-040344-128d37 (F2 mutant) and CI run 44732611068 at prism:5487d43
Quest: .campaign/detent/quests/prism-server-composition-limits.md (the lead's; never edit it)

## Doing

Implemented at prism:c1e3ff0, tests fixed at prism:e0c1ddd (pushed; the first suite 261120-035135-f42468 failed on EADDRINUSE: test ports came from the ephemeral range, TIME_WAIT from the load test; now probed from 20000): http aed0314 and reactor 6cd8f73 pinned; one `http::Limits`;
`reactor::tls::server_context` as `http::Tls`; `reactor::raise_open_files(max_clients + 64)`;
`Http::stop()` = `stop_accepting()`, drain = `wait_requests(b.cancel)` on its own thread, then
`cancel_requests()` and http's `stop()`. Tests added to prism_lifecycle: drain, limits, tls.
Mutant branch (local, not pushed) mutant/old-full-stop = prism:6df5be4, the old full stop.

## Review (correctness, prism:e0c1ddd: CHANGES)

- F1 (blocking): fixed at prism:5487d43. `free_port()` is gone; every instance starts on `--port 0`
  and the test reads the bound port from the banner in its log (`start()`, `banner_port()`).
  Confirming: suite 261120-040331-771e99.
- F2: fixed at prism:5487d43 (the ruling: exit 78). `check_descriptors` and `make_tls` run in
  `main` before the fork (no thread exists; the raised limit and the context are inherited); their
  warnings log after the log opens. Tests check EX_CONFIG for `--daemonize` in `limits` and `tls`.
  Confirming: suite 261120-040331-771e99; fails without: 261120-040344-128d37 at the checks-after-fork
  mutant prism:2be9909.
- F5: fixed at prism:5487d43: the tls case asserts the 14-day warning. F3, F4, F6: not taken (F3 is
  a follow-up in task).
- A2 fails-without rerun at the new head: 261120-040334-0bfcc8 (mutant prism:1e646ea).

## Next

1. When CI run 44732611068 is green, set State ready for review (fix and re-push if red).

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
| A1 | 261120-040332-52f4bf (no local copy, at prism:5487d43); `limits` and `tls` cases in suite 261120-040331-771e99 |
| A2 | drain case in suite 261120-040331-771e99; fails without: 261120-040334-0bfcc8 at prism:1e646ea |
| A3 | [artifact] `ARCHITECTURE.md` "How `main` is put together" at prism:5487d43: the deviation table lists none of the four rows |
| A4 | suite 261120-040331-771e99; CI run 44732611068 pending |

## Result

pending
