# peers-retry-flake: journal

State: ready for review
Quest: .campaign/detent/quests/peers-retry-flake.md (the lead's; never edit it)

## Doing

Full peers x60 4-way (1c12ce) failed 3 of 60 at peers_test.cc:1155 (F2 fetch-alongside-consensus: control round trips while 8 MiB fetches run), a case this diff does not touch. Attribution: the same full-test x60 4-way at aa7fa35 (261117-140450-74d009) and at main 361d5d2 (261117-140502-89e916), run under `script` for a pty so stdout is line-buffered and failing runs print their `fetching:` numbers. First attempt at e4e394b was superseded by quest-job replacement. If main fails at a similar rate: pre-existing, message detent:ci-test-flakes (or tell the lead if its file set lacks peers_test.cc). A1 fixed jobs and A2 rerun at aa7fa35; A3 cites 261117-134723-98cca7.

Linux CI at 291dae2 failed deterministically at peers_test.cc:1220: the Close step fetched the F3 table, now 2 MiB, which a fast host finishes before cluster.stop(1) lands, so no connection_aborted. consensus:5e0ab05: the close step fetches its own 8 MiB "history/large" (the table stays 2 MiB for the frozen fetch, retry and reopen); the abort expectation is unchanged. Done at bac4f81 and passing: A1 pair (122236 and 122241 exit 134 as mutants, 122246 and 122252 pass), A2 fetch-failures x200 (122257), suite (122228). Queued at 5e0ab05: A2 full peers test x60 4-way (261117-134718-1c12ce) and the suite (261117-134723-98cca7). Not forced on macOS: the failure needs a host that drains 2 MiB before the stop lands (Linux CI); the lead's CI run is its evidence.

Fix committed in consensus quest/detent/peers-retry-flake: a9283e458132 (knobs) and bac4f8162c4a (fix). Jobs queued with --for; waiting.

## Next

1. When the jobs finish, check the mutants failed at the retry (log: "retried fetch: error ...") and fill the Evidence rows; if all pass, set State to ready for review.

## Blocked on

Nothing. No Peers defect: both modes are the test's deadline too small for its reply.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | 261117-140518-49afa2, 261117-140519-808620; fails without: 261117-122236-68fffc, 261117-122241-9dae77 |
| A2 | 261117-140519-27f287 |
| A3 | 261117-134723-98cca7 (passed at 5e0ab0575ceb; only merges from main since) |

## Result

Test-only (consensus test/transport/peers_test.cc). Before: 11 of 200 runs failed under 4-way load at 500 ms and 8 MiB (probe 261117-121715-f99570): 8 retries timed out, 3 later "reopened" fetches timed out; lanes were intact (4 connections, 4 admitted, 4 reconnects) before every retry. Two modes, both the test's deadline versus its reply:
1. timed_out: the 8 MiB retry runs under the 500 ms fetch_deadline.
2. not_connected at 0 ms: the frozen peer's late reply must finish within a second fetch_deadline of the first failure (peers.h `await_reply`, `late`); otherwise Peers closes the bulk lane. The test's `fetches == 0` wait is met by that close as well as by the drain, then the retry finds no lane, and peers.h documents fetch() "fails at once without a bulk lane". Drains were seen up to 507 ms in probe 261117-121450-50e651, past the 500 ms window.
Fix: reply 2 MiB (32 frames of 64 KiB) and `fetch_deadline` 1 s. `PEERS_RETRY_LAG_MS` (retry's first read) and `PEERS_THAW_LAG_MS` (peer stays hung that long after the failure) force the modes; unset, no effect.

## Rulings I made

- No Peers change: a lane closed by the second deadline and a fetch failing at once without a lane are documented behavior.
- Why the proof is the same: the frozen fetch still times out at the deadline holding its slot with no bytes (`waited >= 1000`); the late reply still drains into the failed fetch over the same lanes (`admitted` and 4 connections unchanged); the retry returns identical bytes. Only the size and the deadline moved, to leave the reply room.
- The earlier 2 s deadline run (probe 2) hit one lane loss with the freeze equal to Cluster's 2 s `options.deadline`; I chose 1 s to stay below it, not measured as the cause.
- Cost: the frozen step waits 1 s instead of 0.5 s.

## Follow-up

Candidate flake, not this quest's: peers_test.cc:1155 (control round trips under 8 MiB fetches) failed 3 of 60 runs under 4-way load in 261117-134718-1c12ce. The attribution reruns passed 60 of 60 both at consensus:aa7fa35 (261117-140450-74d009) and at main consensus:361d5d2 (261117-140502-89e916). The quest's diff does not touch that case.
