# consensus-fetch-oversize-abort review: correctness

Commit: consensus:ea48bc8bd90fd042ecfda4f68f69b3e24c68eb14
Verdict: CHANGES
Date: 2026-11-24

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

## Evidence I relied on

- 261124-123059-eda87c: fetch-dead 20 runs pass at ea48bc8; the test checks values (both 4 MiB
  replies byte-exact, the 48 MiB one `message_size`, `fetches_asked_again == 3`, `dead >= 1`).
- 261124-123054-1bea5f: the fails-without job at the fetch-not-asked-again mutant (536024a, one
  `false &&` on the retry condition) fails for the stated reason: asked again 0, all three fetches
  "Software caused connection abort", which is CI's line.
- 261124-123104-84309d: fetch 200 runs beside 4 burners pass; 261124-134701-6d9c49: the suite,
  171 of 171; CI 45481678940 is green at ea48bc8 (checked with `gh run view`). I did not rerun any
  of these.

## What I checked myself

The scripts are in `.scratch/detent/review-consensus-fetch-oversize-abort-correctness/` (home
checkout). `apply.py` adds `probes.inc` to rpc_host_test.cc, and with `--mutant` or
`--oversize-aborts` it also applies that mutant. `build.sh` builds the test, and `run.sh` and
`probes.sh` run it. All probes are light jobs on fresh checkouts.

- 261124-142029-59ba07, at ea48bc8:
  - probe-partial: a 48 MiB fetch has 2.1-2.6 MiB in when its link dies. In 10 of 10 conclusive
    runs it comes back exact.
  - probe-twice: every connection dies. The fetch ends `connection_aborted` after 2516 ms, and
    the reader ran 2 times, still 2 after 3 s more.
  - probe-drain: the fetch ends `resource_unavailable_try_again` at 503 ms.
  - probe-remove: the fetch ends `not_connected` at 200 ms.
  - probe-gone: the peer process exits in its reader. The fetch ends `timed_out` at 4000 ms,
    which is the whole fetch_deadline.
- 261124-142035-136b08, the same probes at the quest's base consensus:3796626. Every one ends
  `connection_aborted`:
  - twice: at 1010 ms, with one reader call.
  - drain: at 501 ms.
  - remove: at 202 ms.
  - gone: at 1-2 ms.
- 261124-142032-4d3326, mutant no-clear (the retry's `state->bytes.clear()` removed):
  - fetch-dead passes 10 of 10.
  - probe-partial fails 10 of 10: 53.4-55.1 MiB, NOT EXACT, which is the first attempt's prefix
    plus the file.
- 261124-142132-08ecf2 at ea48bc8 and 261124-142136-d9097b at the base, mutant oversize-aborts
  (the oversize `stop` also aborts the Bulk link): `fetch` passes 5 of 5 at both commits. At the
  fix the line shows `failed 2` with links 2+2.

Code read for question 1 (is asking again safe?):

1. `ask` (host_transport.h:1441-1488) reuses the name, the deadline and the `fetching` slot. The
   slot is decremented only on the final end (1463-1464), so nothing is counted twice.
2. `Lane::close` -> `finish` (rpc.h:474-494, 891-925) erases the call. It also releases its
   `in.held` pool bytes, cancels its ticket and drops its chunks before `done`, so the old call
   cannot deliver another body.
3. `close_rpc` moves the lane out (host_transport.h:1494-1499). `rpc_lane` admits only frames of
   the peer's current link (1344-1346), so a late answer from the dead connection never reaches
   the new call.
4. The oversize stop cannot become a retry. `Lane::stop` finishes the call with `cancel` in the
   same call (rpc.h:444-460), so `over` always ends as `message_size`, never as
   `connection_aborted`.
5. A host that is stopping never asks again (1456). A retry already posted before `stop()` runs
   first and ends `not_connected`, so `done` is never lost.

Sweep of the ways a connection ends under a fetch:

| Path | Before (base) | At ea48bc8 |
|---|---|---|
| dead_after, stuck, socket or TLS failure, D3 violation, Bulk replaced by `open_rpc` | `connection_aborted` | asked again. It ends as the second call ends: exact or `message_size` (fetch-dead, probe-partial), or `connection_aborted` when that connection dies too (probe-twice). |
| oversize (fetcher's `message_size`) | Cancel of that call alone | same; never asked again |
| local drain past drain_deadline | `connection_aborted` | `resource_unavailable_try_again` (probe-drain) |
| `remove_node` | `connection_aborted` | `not_connected` (probe-remove) |
| reload, the peer's chain no longer verifies | `connection_aborted` | asked again; the redial fails verification, so it waits to the deadline: `timed_out` (code read, same class as F1) |
| peer gone (process exit, refused port) | `connection_aborted` in 2 ms | `timed_out` at fetch_deadline (probe-gone) |
| `stop()` | `connection_aborted` / `not_connected` | unchanged |

No path asks more than twice.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | yes | gap | A peer that is gone changes what the caller sees. Before the fix the fetch ended `connection_aborted` within 2 ms. Now the second ask waits for a connection that never comes and ends `timed_out` at fetch_deadline: 4000 of 4000 ms in the probe, 30 s by default. The same holds when a reload's redial fails verification. The contract says "connection_aborted: its connection ended twice" (host_transport.h:624; rpc-lane.md fetch bullet; fetch.h) and is silent on this case. A future caller that moves to another source on `connection_aborted` would now wait the whole deadline. | probe-gone at 261124-142029-59ba07 vs 261124-142035-136b08 | Owner's call on the behavior. Options: (a) keep it, and state in the three places that a fetch whose next connection never comes ends `timed_out` at its deadline; (b) end it `connection_aborted` when the retry's first dial fails, which keeps the fast failure; (c) ask again only on a link that is already up or comes up within a short bound. With any option, add a gone-peer case (probe-gone's shape, a child that exits in its reader) that checks the outcome and its time. |
| F2 | yes | gap | The retry's `state->bytes.clear()` is untested. In fetch-dead no fetch has bytes in when its link dies, so removing the clear passes the test. With it removed, the caller gets the first attempt's prefix plus the whole file. | 261124-142032-4d3326: fetch-dead passes 10/10 at the mutant, probe-partial fails 10/10; probe-partial passes 10/10 at ea48bc8 (59ba07) | Add a fetch to fetch-dead that has at least 2 MiB in when the link dies (probe-partial's shape: wait on `bytes_in`, then ask "held"), and check it byte-exact. Tag a `--fails-without` job on the no-clear mutant. |
| F3 | no | gap | Existed before this quest: nothing proves the Contracts line "other calls on the link continue". A mutant whose oversize refusal also aborts the link passes `fetch` at the base and at the fix (1-2 of five 4 MiB run beside the 48 MiB, and with the retry they succeed anyway). | 261124-142136-d9097b, 261124-142132-08ecf2 | When no stall occurs, check that the oversize refusal leaves the link alone: links unchanged and `fetches_asked_again == 0` across a quiet huge + 4 MiB pair. Use mutant oversize-aborts (`apply.py --oversize-aborts`) as its control. |
| F4 | no | nit | A local drain past drain_deadline now ends `resource_unavailable_try_again`, and `remove_node` ends `not_connected`, where both used to end `connection_aborted`. Both codes are documented, but the retry does not mention them, and only `stopping` skips it, while `draining` asks again only to be refused. | probe-drain, probe-remove (59ba07 vs 136b08) | Mention both in F1's text, or skip the retry when `draining` as well, for symmetry with `stopping`. |

## Not run

- TSan, other platforms, and CI's macos-15-intel. Suite probe 45481730112 was still in progress
  when I wrote this.
- Whether the retry ever helps on CI's runner beyond the fetch-dead reproduction.
- A3's doc text beyond the fetch bullet's claims.
- The reload path's `timed_out`, which I read in the code but did not run.
- Heartbeat-gap bounds. A2 and 84309d stand for them.

## Round 2

Commit: consensus:87cb821d1e14
Verdict: CHANGES
Date: 2026-11-24

I re-reviewed the whole retry mechanism, not only the diff. Its states are: the first ask
(waiting or on a lane), the posted retry, the second ask (waiting or on a lane), and the end. I
checked each against every event: the link ends, a dial comes back up, lost or failed, the
deadline passes, `stop()`, `drain()`, `remove_node`, `reload`, the peer drains, and the
oversize refusal.

### Evidence I relied on

- 261124-151246-f0ad2c, 151248-f49548, 151250-cd4e3b, 151252-3b3f74, 151254-e42706 and
  151256-f001a3 at 87cb821: fetch-partial, -twice, -gone, -unverified, -removed and -oversize,
  10 runs each, all pass.
- Each new test fails for its stated reason:
  - 151258-073436 and 151300-44e305 (ea48bc8's host_transport.h, d627e22): fetch-gone and
    fetch-unverified end `timed_out` at 4000 ms against the `< 1000` / `< 2500` bounds, 3 of 3
    runs.
  - 151302-d24b61: remove ends `not_connected` and drain ends `resource_unavailable_try_again`,
    each with asked again 1.
  - 151304-e49b9a (no-clear, af9ba03): 53.6-55.2 MiB, NOT EXACT.
  - 151306-086ff4 (oversize-aborts, 326089a): "ended failed 2: the connection ENDED".
- I did not wait for 151308-fdd296 (fetch-dead x20), 151310-7f0e32 (the suite), 152434-a7cf20
  (A2 x200) or CI 45507969326. They are the lead's gates.

### What I checked myself

`apply.py --round2` adds `probes2.inc`, and `round2.sh` runs it.

- 261124-152648-cd9d13 at 87cb821: round 1's probes and three new waiting probes.
  - probe-gone: `connection_aborted` in 2 ms (round 1: `timed_out` at 4000).
  - probe-partial: exact, 5 of 5.
  - probe-drain: `connection_aborted` at 502-506 ms.
  - probe-remove: `connection_aborted` at 200 ms.
  - probe-twice: `connection_aborted` at 2520 ms, reader 2 calls.
  - The waiting probes: a child host stops itself, host 1's link ends dead, and the second ask
    waits on the redial's TLS handshake. Then `remove_node` gives `connection_aborted` in 0 ms,
    `drain()` gives `connection_aborted` in 0 ms, and doing nothing gives `connection_aborted` at
    the 5 s handshake timeout (4697 ms after the probe acts), within the 10 s deadline.
- 261124-152652-a5ef8c, mutant no-again-map (`fail()` ends a second ask with the caller's outcome):
  the same waiting probes end `not_connected` (remove) and `resource_unavailable_try_again`
  (drain).
- 261124-154350-173472, the same mutant, with the suite's own fetch-removed, -gone, -unverified,
  -twice and -dead, 2 runs each: all pass.

### The table against the code

Every row matches the code at 87cb821:

- The retry's guard (`stopping`, `draining`, `!routed`, then `end` with `connection_aborted`;
  host_transport.h, ask's `done`) gives the drain, `remove_node` and `stop()` rows.
- `Dialed` (`connect` null with no link = failed, which calls `fail_again`; `offer` false = lost,
  which waits and redials) gives "refused, unreachable or handshake fails: `connection_aborted`".
  It also gives "tie-break lost: waits" and "boot or version refused: lost every time, `timed_out`
  at the deadline".
- `fail()` mapping `again` to `connection_aborted` gives "`remove_node`, `drain()` or `stop()`
  while it waits".
- `expire_rpc` gives "`fetch_deadline` passes while it waits: `timed_out`".

What carries over from round 1 and still holds:

1. The `fetching` slot is freed only in `end()`.
2. The first call is erased before its `done` runs (rpc.h `finish`).
3. Late frames from the dead link are dropped (`rpc_lane` admits only the current link).
4. The oversize stop finishes with `cancel` in the same call, so it is never asked again.

### Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| R2-F1 | yes | gap | No test covers the row "`remove_node`, `drain()` or `stop()` while it waits": `fail()` mapping a waiting second ask to `connection_aborted`. With the mapping removed, every fetch case in the suite still passes, and the waiting fetch ends `not_connected` (remove) or `resource_unavailable_try_again` (drain). fetch-removed covers only the first call on a lane, which the retry's guard ends. | 261124-154350-173472 (suite cases pass at the mutant); 261124-152652-a5ef8c vs 152648-cd9d13 (the waiting probes) | Add the waiting shape to fetch-removed: a child that SIGSTOPs in its reader, then `dead_after` 1 s, then wait for `fetches_asked_again == 1`, then `remove_node` and `drain()`. Expect `connection_aborted` at once. Tag a `--fails-without` job on mutant no-again-map. `probes2.inc` has a working version. |
| R2-N1 | no | nit | `fail()` also turns `protocol_not_supported` into `connection_aborted` for a second ask whose new link has the lane off (`open_rpc`). The table does not name this row. | host_transport.h `fail`, `open_rpc` | Name it in the second table, or leave it, since the first ask's link had the lane on. |
| R2-N2 | no | nit | fetch-partial requires `partials == 2`. A run where the 48 MiB reply completes before "held" is served fails instead of reporting itself inconclusive, as fetch-oversize does. That happened in 0 of my 15 runs and 0 of the contributor's 10. | rpc_host_test.cc fetch_partial | Optionally report such a run as inconclusive. |

Round 1's F1-F4 are closed:

- F1: probe-gone and fetch-gone, plus the lead's ruling.
- F2: fetch-partial, with a fails-without job.
- F3: fetch-oversize, with a fails-without job.
- F4: fetch-removed and the table.

### Not run

- The rows "the redial's hellos fail" and "boot or version refused: `timed_out`": I only read the
  code for them.
- A stop() while the second ask waits: I read the code and did not probe it.
- TSan and other platforms.
- The suite, A2 x200 and CI at 87cb821, which were still running.
