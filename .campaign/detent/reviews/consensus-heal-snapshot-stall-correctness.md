# consensus-heal-snapshot-stall review: correctness

Commit: consensus:5a8685cdcb23cbf206ebfc36a0b1aba88c8d2577
Verdict: APPROVE
Date: 2026-11-20

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

## Evidence I relied on

- 261119-232822-da09d2: `consensus_consensus_test` at this commit, 40015 checks, 0 failures,
  including `heal_after_checkpoint_past_stale_match` in both its holds and lacks cases.
- 261119-231702-89d08c (`--fails-without`, at consensus:910925a, the test before the fix): fails
  exactly the holds check ("a peer holding the entries catches up within two heartbeats of the
  heal, with no install"). The test fails for the stall.
- 261119-232821-ad2f96 / 261119-232822-bff66e: the three Driver-sim seeds (4270, 6197, 8247) at
  consensus:98298fb show one SnapshotNeeded and 10,498-14,881 heal rounds before, and zero
  SnapshotNeeded with the last heal round at 7-40 after, with this commit's core.h.
- 261119-232823-89d14a: the Core sweep, 1000 seeds x 10000 steps at this commit, ok, with 4601
  checkpoints and 1821 installs; the Core simulation drops, delays, duplicates and partitions
  messages and heals at the end (`test/simulation_test.cc:25-36`, `:594`, `:753`, `:775`).
- 261119-232822-71c928: 12,000 Driver-sim seeds (10,000 x 1000, 2000 x 3000) with this core.h, ok.
- 261119-232829-68d538 is the quest's `suite` check, configured as `true`: it proves nothing. The
  whole ctest suite and TSan rest on CI at 5a8685c (queued when I looked), which gates landing.

## What I checked myself

Read `include/consensus/core.h` at 5a8685c: `replicate` (1161-1200), the AppendResponse handler
(975-1029), `release_snapshot` (581-587), the snapshot reply (1043-1057), `drive` (1116-1126),
`become_leader`/`follower`/`fence` (449, 455, 1139-1152), and the follower's AppendRequest check
(895-905).

1. **Safety.** After the fix `replicate` never builds an append with `previous < base_index_`:
   at `next <= base` it either installs or sets `next = base + 1`, so `previous = base` and the
   request carries `log_term(base) == base_term_` (core.h:433), a term the leader always holds. The
   follower refuses `previous < its base` or a base-term mismatch (core.h:900-902) and otherwise
   checks `log_term(previous)` exactly (core.h:903-906). A success at `previous = base` with the
   matching term proves the prefix by log matching; skipping verification of `matched+1..base` is
   sound. The leader never needs a compacted term. No path appends below the base.
2. **Liveness.** A peer that lacks the base refuses the append at the base with
   `hint <= base` (or, after any refusal, `next = max(matched+1, min(hint, previous)) <= base`),
   so `lacks_base` is set, `reset_flights` empties the flights, `retry_after = 0`, and the
   immediate `replicate` starts the install. No loop of appends at the base: every refusal at the
   base installs. A silent peer gets appends at the base every rpc_timeout and no install; an
   install needs the same peer's SnapshotResponse, so it could not have completed either. The
   install retry paths keep the flag: Rejected, Stale and Unavailable-before-Sending release
   without a probe and leave `lacks_base` true (retry install); TimedOut, CaughtUp and
   Unavailable-while-Sending probe at the boundary and clear it (one append, then a refusal
   reinstalls). A peer blocked by an oversized entry still reaches an install once the base passes
   it, one round trip later than before.
3. **The flag's lifetime.** `peers_.clear()` on `fence`, `follower` (any step-down or new term)
   and `become_leader` recreates every Peer with `lacks_base = false`; a member added by a
   membership change starts false (core.h:717). It is cleared by every append sent, by Installed,
   and by a probing release. It is kept across a base move, which is right: a refusal at or below
   the old base means the peer lacks an entry at or below the new one.
4. **Tests.** The unit test fails without the fix (89d08c) and checks values (progress equals the
   leader's last index, no SnapshotNeeded for 2, the awaited boundary equals the checkpoint).
   The Core sweep covers partitions, loss and heals with checkpoints and installs; probes below
   say whether it reaches the new branch.
5. **Docs.** ARCHITECTURE.md:113 and the CHANGELOG line state the rule accurately; see N3 and N4.

Probes, from `.scratch/detent/review-consensus-heal-snapshot-stall-correctness/probe.sh` in the
home (each builds a patched copy under `$JOB_OUT`, never the slot):

- 261120-001557-8a5e8f (`probe.sh count`): instruments both branches of `next <= base` and runs
  the Core sweep 1000 x 10000; passes iff the sweep is ok and the append-at-base branch is reached.
- 261120-001601-be12e8 (`probe.sh no-lacks`): mutant where a refusal never sets `lacks_base`;
  passes iff `consensus_consensus_test` fails; reports whether the sweep also catches it.

Results, both passed:

- 8a5e8f: sweep ok, the append-at-base branch reached 1008 times, installs started 2250 times.
  The sweep exercises the new branch and the install-after-refusal path, under partitions, loss
  and checkpoints, with no safety failure.
- be12e8: the mutant is killed by both. `consensus_consensus_test` fails ("fanout snapshot
  request", exit 1), and the Core sweep fails (exit 1). A peer that lacks the base and never gets
  an install is caught by the suite and the sweep.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| N1 | no | gap | The quest's `suite` check is `true` (state.json), so 68d538 proves nothing; A3's "suite with TSan" rests wholly on CI at 5a8685c, which was queued at review time. | 261119-232829-68d538 (cmd `true`, empty log) | Land only on green CI; no code change. |
| N2 | no | gap | The Driver simulation's sink ignores SnapshotNeeded (`test/driver_simulation.cc:97`), so the 60 s in A1 is that sink plus an unneeded install; the Driver sweeps (71c928, bff66e) cannot exercise a genuine install. The lacks case is covered by the unit test and the Core sweep only. | test/driver_simulation.cc:97 | None for this quest; worth a line in the quest's Decisions answer. |
| N3 | no | nit | CHANGELOG says the peer "catches up within rpc_timeout of a heal instead of waiting out snapshot_timeout (60 s)". The 60 s holds only for a host that leaves SnapshotNeeded unanswered; with the snapshot channel the old cost was an unneeded image capture and transfer ending in CaughtUp (snapshot_channel.h:273). The bound is rpc_timeout plus a round trip when a reply was lost at the heal. | CHANGELOG.md:22; core.h:1121 | Optional wording: "without an install, within about rpc_timeout of a heal". |
| N4 | no | nit | Any refusal of an append at the base installs, including a follower's capacity or limits refusal of a peer that holds the base (hint `replace_from > base`, core.h:929-952, then `min(hint, previous) = base`, core.h:1016). Pre-existing (the old code installed there too) and the docs' wording is literally true; such an install compacts the follower, so it is not harmful. | core.h:1016-1018 | None; a refusal whose hint exceeds `previous` proves the prefix and could advance instead, outside this quest. |
| N5 | no | nit | A refusal that arrives after its flight timed out is ignored as stale (core.h:989-993), so if a peer's round trip always exceeds rpc_timeout it never gets an install. Divergence repair already has the same assumption; the fix extends it to the install trigger. | core.h:989-993, 1121 | None. |

## Not run

- The whole ctest suite and TSan locally (CI's; queued at review time).
- Pipelined (`flow_.pipeline > 1`) heal schedules beyond what the Core sweep draws.
- Xapiand's own host with a real snapshot channel.
