# consensus-seeds-rate-flake review: correctness

Commit: consensus:a2f6eb33b65879ff61f7b5f0d96169ba68933e29
Verdict: APPROVE
Date: 2026-11-24

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

## Evidence I relied on

- A1 261124-041541-5d4555 (main 512e3ef, 0.4 s SIGSTOP at 0.8 s and 1.1 s: line 570 asserts) and
  A2 261124-041545-a9b999 (head, same stops: passes, lag 411/412 ms): the logs show the assertion
  and the measured lag, so the reproduction and the fix stand.
- A2 261124-041548-778dca (no-interval mutant a473a7b: asserts at line 598, 141 datagrams). It
  proves only the lower bound, which this commit does not touch, so I added upper-bound mutants.
- A3 261124-041551-dab805 (50 runs beside 4 burners, strict: 0 failed; lag 1-17 ms).

## What I checked myself

Scripts in `.scratch/detent/review-consensus-seeds-rate-flake-correctness/` (home checkout):
`mutants.sh` builds dangling mutant commits of seeds.h on a2f6eb3 (`mutants.txt` lists them),
`probe.sh BUILD fail|pass|report repeat|stall ...` wraps the contributor's repeat.sh/stall.sh.

- Q1, slow (interval 250-400 ms, consensus:0e86fa3): 261124-050629-d43a88 unstalled, 5/5 assert at
  line 598 (lag 1-4 ms); 261124-050631-382d23 beside 4 burners, 5/5 assert at 598 (lag 1-4 ms).
- Q1, blocked (loop thread sleeps 200 ms before each post-burst send, consensus:2f1a5f5):
  261124-050635-4e808e, 5/5 assert at line 598 with lag 1-2 ms. The sampler is its own thread, so
  the seed source blocking its loop does not widen the allowance. Beside 4 burners
  (261124-050637-bd1a98): 5/5 assert.
- Q2, slow under 0.4 s stops at 0.8/1.1 s (261124-050632-19cfe1): the gap bound (150 + 411 ms)
  admits 250-400 ms gaps, but the floor catches it (7 + 2 < 10, line 601). The burst bound and the
  floor keep their meaning: the floor adds floor(lag / 150 ms), at most what one stall of that
  length can remove from the slowest rate, on top of the existing 3 spare; `<= 18` and `>= 90 ms`
  stay exact, so bursting and catch-up are still caught on any host.
- Q3: the local `Lag` (seeds_test.cc:47-69) is token-identical to consensus:e416bb0's
  test/transport/lag.h once `Clock` is read as steady_clock (seeds_test.cc:43) and `1ms` as
  `milliseconds(1)` (a scripted diff printed IDENTICAL). stop() sets `stopped`, joins the sampler,
  then reads `worst`; the destructor's second stop() is a no-op.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| C1 | no | gap | One stall widens every gap's bound and the burst's by the worst lag, not just the gap it hit: under a stall of L the gap check admits any interval up to 150 + L ms, and only the floor still sees a slow source; a stall of about 1 s would hide a 250-400 ms interval entirely. Unstalled and loaded hosts (the normal case) keep full strength. | 261124-050632-19cfe1; seeds_test.cc:595-601 | Optional: have Lag record when the stalls happened and widen only the gap and burst windows that overlap one. Keeping it as is is fine for a test whose quest asks for full strength only on an unstalled host. |
| C2 | no | nit | `lag.stop()` runs before `stop(loop, seeds)`, whose `loop.until` still drains the open raw socket, so a datagram heard there falls outside the sampled window. | seeds_test.cc:586-589, 116-119 | Read the lag after `raw.close()`. |
| C3 | no | nit | The local `Lag` relies on transitive `<algorithm>`/`<cstdint>`; moot once it is replaced by lag.h as the journal plans. | seeds_test.cc:28-36 | Include lag.h after consensus-rpc-host-stream-gap lands. |

## Not run

Still queued when the review was written (confirmatory, not needed for the verdict):
261124-050639-352716 (catchup mutant, unstalled),
261124-050641-eb8792 (catchup mutant under 0.4 s stops, expected to assert at the 90 ms lower
bound), 261124-050643-024d21 (head under 1.0 s stops, expected to pass). No Linux or CI run of the
probes, and no suite run (A4 is CI and `campaign verify` at landing).
