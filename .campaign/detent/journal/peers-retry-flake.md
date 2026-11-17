# peers-retry-flake: journal

State: ready for review
Quest: .campaign/detent/quests/peers-retry-flake.md (the lead's; never edit it)

## Doing

Fix committed in consensus quest/detent/peers-retry-flake: a9283e458132 (knobs) and bac4f8162c4a (fix). Jobs queued with --for; waiting.

## Next

1. When the jobs finish, check the mutants failed at the retry (log: "retried fetch: error ...") and fill the Evidence rows; if all pass, set State to ready for review.

## Blocked on

Nothing. No Peers defect: both modes are the test's deadline too small for its reply.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | 261117-122246-95e8bd (passed at bac4f8162c4a; only merges from main since), 261117-122252-5e4bf2 (passed at bac4f8162c4a; only merges from main since); fails without: 261117-122236-68fffc, 261117-122241-9dae77 |
| A2 | 261117-122257-901974 (passed at bac4f8162c4a; only merges from main since) |
| A3 | 261117-122228-c010f0 (passed at bac4f8162c4a; only merges from main since) |

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
