# consensus-seeds-rate-flake: journal

State: waiting on jobs 261124-074032-b0851b 261124-074033-1fb0e2 261124-074037-244944
Quest: .campaign/detent/quests/consensus-seeds-rate-flake.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Merged consensus main, swapped the local `Lag` copy for `#include "lag.h"` (consensus:803c37e, pushed).
seeds_test passes locally (12/12 ctest). Reruns at 803c37e queued; CI runs on the push. Then ready for review.

## Next

1. When the jobs finish and CI is green: set State "ready for review".

## Blocked on

<What, and who decides. "Nothing" when nothing.>

Nothing.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), name every job you examined, failures included (`campaign brief` lists failed
jobs no journal names), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| A1 | 261124-091322-79245a (rerun of 261124-041545-a9b999 at 803c37e); fails without: 261124-041541-5d4555 (at main 512e3ef, process stopped 0.4 s: log shows "Assertion failed: (gap >= 90ms && gap <= 150ms + 100ms) ... line 570" at the 0.8 s and 1.1 s offsets, not exit 77). Cause: the timer re-arms after each send (unstalled gap 100-150 ms) and the listener timestamps in the same loop, so a stall adds to one gap. |
| A2 | 261124-091322-79245a (rerun at 803c37e; first 261124-041545-a9b999 at a2f6eb3, same stalls: passes); fails without: 261124-041548-778dca (no-interval mutant: assertion at line 598, 141 datagrams in 1.5 s, not exit 77) |
| A3 | 261124-091322-7392b0 (rerun at 803c37e of 261124-041551-dab805; 50 runs beside 4 burners, strict: none failed) |
| A4 | 261124-091325-548d18 (suite at the merge 5341bd3); `ci:ci` (CI run 45428288634 at 803c37e, green, carried across the clean merge) |

## Result

test/transport/seeds_test.cc rate(): a `Lag` sampler (rpc_hosts.h's shape) runs over the 1.5 s window;
each gap is bounded `90 ms <= gap <= 150 ms + lag` (interval + jitter + the host's lag), the burst
bound is `80 ms + lag`, the datagram floor drops one per 150 ms of lag. The old fixed 100 ms slack is
gone: an unstalled host now holds 150 ms plus its ~1-2 ms measured lag, stricter than before, and the
90 ms lower bound (the rate limit) is unchanged. At main, a 0.4 s process stop fails at line 570; at
head the same stops pass; a mutant sending at the burst spacing fails. 50 runs beside 4 burners: 0
failures. No doc change (a test).

## Rulings I made

- The local `Lag` copy is gone: seeds_test.cc includes the shared test/transport/lag.h (consensus:803c37e). The review's mutants (261124-041548-778dca and the review jobs) stand: the bound code is unchanged.
- The burst bound (`heard[2] - start < 80ms`) and the datagram floor take the same lag, one shape.
