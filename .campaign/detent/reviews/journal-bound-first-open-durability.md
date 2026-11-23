# journal-bound-first-open review: durability

Commit: journal:095171035dab0964e2e38863c66b06a41ee48c0d
Verdict: APPROVE
Date: 2026-11-23

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

One durability review carries the correctness questions (correctness lens weakened on the record).

## Evidence I relied on

- 261123-110049-af725e (A2, `--fails-without`, test at main's host.h, journal:900a11a): exit 1,
  66 of 138 checks fail, every one for the N1 reason: a cut reopens group 1 or 2 as stored with a
  lower next sequence (`group 1 1 bound 0 next 3`) where the uncut open loses or bounds it. The
  "dropped turns" layout agrees at main, as the journal says (a guard, not the fix's test).
- 261123-111045-7f9830 (open cost A/B, strict, 192 MiB log, 7 runs per round, two rounds): main
  1003 / 915 ms median, fix 1740 / 2698 ms. It ran beside 13 light holds, so the ratio
  (1.7x to 2.9x) is noisy; the extra cost is real (see F1).

## What I checked myself

Probe 261123-112653-c66b0a at journal:0951710 (exit 0), from
`.scratch/detent/review-bound-first-open/run_probe.sh` and `probe.cc`. It includes the branch's
`lost_bound_test.cc` for its layouts and builds against the fix's and main's (e58f977) headers.

1. Risk 1, second crash: a cut at every mutation of the first open, a power loss (3 seeds), then a
   cut at every mutation of the next open, a power loss, then two reopens: 4,876 reopens across the
   four layouts, all equal to the uncut verdict. A single cut with 16 power-loss seeds per cut:
   2,016 checks, 0 failures (the suite's test uses one seed per cut).
2. Risk 1, ordering by reading: `commit` (host.h:726) writes each `.pending`, `sync_files`,
   renames, `sync_directory` before it returns; `settle_damage` (host.h:931) calls `mark_bounds`
   then `mark_lost`, both commit, before any `TurnDriver` is built (host.h:878-886), whose
   `TurnLog` constructor writes the open record. A cut inside `settle_damage` leaves no open
   record, so the damaged epoch is still the newest and the next open recomputes the verdict; with
   the bound stored, recovery supersedes the group's records through it (`superseded`,
   turn_log.h:758) and keeps the ones after it (probe 1 confirms the values).
3. Risk 2: `TurnLog::read` (turn_log.h:344) runs the same `recover(known)` the constructor runs,
   which only scans and opens existing segments; `settle_damage` passes the same `groups` the
   drivers get, and nothing between them touches the log (its commits write shards, its removals
   artifacts). The two verdicts are the same function of the same bytes and inputs.
   `TurnDriver::recovered()` stays truthful because the drivers recover against the pre-step table:
   they report the same bounded and lost sets as at main, and the second `mark_bounds`/`mark_lost`
   pass finds nothing to change.
4. Risk 3: on an undamaged open (the six-group `populate` layout, three successive opens) main and
   fix make the same mutations (6) and syncs (3) and report the same status, bound and sequence of
   all six groups; device calls go from 287 to 545 on the first open (the second read). Moving
   `mark_lost`'s artifact removal ahead of the drivers changes when, not what, it writes.
5. Risk 4: `first_open` cuts at every mutation of the fixed open (its total is measured with the
   fix, 17 or 12, so `settle_damage`'s commits are cut too), reopens twice and compares status,
   bound and next sequence of both groups by value; it fails at main for the right reason (A2).
6. Risk 5: journal-turn-sector-checks (origin, dea3757) does not touch host.h or
   lost_bound_test.cc; its `recover` stays read-only (scan and open only) and its diff changes no
   line that reads `known.bound`, `known.open`, `known.lost` or `known.base`, nor `TurnLog::read`.
   The fix relies only on `read` and the constructor sharing one read-only `recover`, and on a
   stored bound superseding the group's records through it; RFTW0002 keeps both. What it does
   change is the file offsets `gap()` lays its layout on (F4).

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | no | gap | Every open, damaged or not, reads each log twice: open time on a 192 MiB log goes from about 1.0 s to 1.7 to 2.7 s, for a case only rule-5 damage needs. A hook between `TurnLog::recover` and the open record (turn_log.h, turn_driver.h, outside this quest's files) would store the verdict with one read. | 7f9830; probe c66b0a (calls 287 to 545, same mutations) | Lead's call: land as is and open a follow-up quest for the one-read hook, or widen this quest's files. Rerun the A/B under `exclusive` before quoting the ratio. |
| F2 | no | nit | The `dropped()` comment says rule 5 bounds groups 1 and 2 at 2; the open keeps group 2 stored with next sequence 3 (bound 0). Only group 1's expected status is asserted, and the reference is the fixed open's own verdict, so group 2's values have no oracle. The comparison also leaves out record payloads. | probe c66b0a "nested dropped turns" line; lost_bound_test.cc `first_open` | Correct the comment and assert both groups' expected verdict per layout. |
| F3 | no | nit | `settle_damage` stores bounds and losses in two commits; a cut between them leaves a partial table over a still-newest damaged epoch. No probe found a disagreement (both groups' verdicts are per-group), and main's post-driver pass has the same order. | host.h:931-940; probe c66b0a | Optional: one commit of both. |
| F4 | no | nit | `gap()` copies `spans()`'s RFTW0001 offsets; after journal-turn-sector-checks merges, the layout may no longer fit or may lay the damage elsewhere. The test fails loudly then ("a filler lays the gap", the uncut status check), and the journal's Next step 2 names the re-sync. | lost_bound_test.cc `gap()` | Rerun `lost_bound_test first_open` after the merge (the suite gate does). |

## Not run

- The suite and CI at journal:0951710 (A1's evidence is the suite job; CI was running).
- No layout with a parked or deleted group, or with more than one loop; the undamaged comparison
  used only `populate`'s six groups.
- No I/O-error (EIO, ENOSPC) injection inside `settle_damage`; only power cuts.
- The open cost under `exclusive`, or on a log at retention's limit.
- This reviewer ran as a subagent, not a session of its own, so it has no `detent · reviewer`
  title.
