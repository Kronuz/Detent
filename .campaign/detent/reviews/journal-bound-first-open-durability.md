# journal-bound-first-open review: durability

Commit: journal:a2d65da45e72f142695934bd605910e60fd1d3cd
Verdict: APPROVE
Date: 2026-11-24

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

One durability review carries the correctness questions (correctness lens weakened on the record).

## Re-review at journal:a2d65da (round 3: one read through `TurnLogGroups::settle`)

APPROVE. The whole recovery path at the first open was re-checked, not only the diff: each log is
read once, the verdict is durable before that log's open record, and power cuts, nested cuts and
EIO at every mutation of the first open reopen to the uncut verdict for every layout, with one to
three loops and the damage in any loop.

### Evidence I relied on

- 261124-113916-a045ce (A1, `lost_bound_test`, every case, at a2d65da): exit 0, 344 checks, 0
  failures; the six `first_open` layouts print 17, 17, 12, 17, 12, 17 cuts and the asserted uncut
  verdicts.
- 261124-113919-ce832d (A2, `--fails-without`, `first_open` at mutant journal:b7d73c1 = head with
  4eb8165's host.h and turn_log.h; `git diff 4eb8165 b7d73c1 -- include` is empty): exit 1, every
  failure is "gap, later turn, zeroed", cuts 6 to 16 of 17 (11 cuts, both reopens), e.g. cut 9:
  both groups reopen `stored ... next 2` where the uncut open bounds both at 3. That is the silent
  loss the quest closes. The flipped-byte layouts pass at main: RFTW0002's sector checks still
  report a failing sector past an earlier epoch's kept turn; only a lost write needs the fix.

### What I checked myself

Scripts in `.scratch/detent/review-journal-bound-first-open-durability/` (home checkout), run
locally under `quiet light`: `run.sh` (output `out/probe.txt`, `run.log`) and `calls.sh`
(`calls.txt`). `probe.cc` includes the branch's `lost_bound_test.cc`, rewritten by `retarget.py`
so the six `first_open` layouts lie in loop T of an L-loop host, with one more group in every other
loop; it builds against the headers of fix (a2d65da), twopass (4d3df89, the two-read fix after the
RFTW0002 merge) and main (b7d73c1). Every check compares status, bound and next sequence of all
groups.

1. Equivalence with the two passes (risk 1). Structurally: `settle_damage` gets the very
   `TurnRecovery` the driver keeps, computed by the same `recover(known)` from the same `known`
   (`known(loops_)` is taken once before any driver, as the two-pass code took it), and
   `recover` only scans and opens (turn_log.h:941 on: no write, remove, sync or rename). The
   verdict kinds (current, torn, stale, damaged, all-zero, unreadable) are all decided inside
   `recover` and reach `settle_damage` only through `lost` and `bounded`, so the mapping from
   sector verdict to table verdict is unchanged; only its timing moves. By run: `dump` of the
   uncut open (every driver's `damaged`, epoch, last turn, lost, bounded with bound and record
   count, kept groups; the verdict; mutations and syncs; a second open) is byte-identical between
   fix and twopass at loops/target 1/0, 2/0, 2/1 and 3/2 for all six layouts (gap at batch 3,
   both fragments, gap with a later turn, dropped turns, each later-turn layout also zeroed).
2. One read (round-2 F1). `calls.sh`: device calls of the first open, fix = main exactly (292
   for one loop, 284 for dropped turns; 559 and 551 for two loops), twopass 550/542 and
   1,075/1,067. Mutations and syncs equal in all three (17/6, 12/5; 23/9, 18/8). The exclusive
   A/B is the wall-clock check (Not run).
3. Crash ordering (risk 2), states by events below. `cuts`: a power cut at every mutation of the
   first open, 4 crash seeds, two reopens: 0 failures at loops/target 1/0, 2/0, 2/1, 3/1, 3/2
   (736, 1,024, 1,024, 1,312, 1,312 reopens; 17 to 29 cuts per layout). `nested`: a cut at every
   mutation of the first open, a power loss, a cut at every mutation of the next open, a power
   loss, a reopen: 0 failures at 1/0, 2/1, 3/2 (1,144, 2,476, 4,224 reopens). `eio`: EIO before
   and after its effect at every write, sync, sync_directory, create, replace and remove of the
   first open; each open threw (34, 24 per layout at one loop; 46, 36 at two), and a reopen and a
   reopen after a power loss both match: 0 failures (368 and 512 reopens).
4. Teeth: the same `cuts` at main's headers fails only "gap, later turn, zeroed": 11 of 17 cuts
   (1/0), 12 of 23 (2/1), 11 of 29 (3/2), 68 FAIL lines, so the multi-loop probe detects the
   defect wherever the damaged loop sits.
5. Layout honesty (risk 3). Positions are data bytes (504 per 512-byte sector, 1,008 per 1 KiB
   block). `gap()` now puts batch 3 at `z % block >= sector` (W+1's second sector) and Q's first
   fragment past W's first sector, and value-checks the blocks' magic and index, Q's First and
   Last fragments and Z's whole record before damaging; `zero()` and `damage()` touch only those
   second sectors, so W's manifest (W+1's header) survives. The layouts that expect a bound
   ("gap, later turn", "dropped turns", zeroed or not) would fail, not pass, if a manifest were
   lost (unknown damage loses every group), and "batch 3" and "both fragments" are lost because
   W+1 is the newest block, whose manifest no later block carries, not by accident. The
   remaining sites stay off a block's first sector (`populate`: X's second sector; `dropped`,
   `second`, `retention`: `middle()`); `second`'s `at + 2` lands in the first segment's block 0,
   whose header names no earlier block, and its assertion keeps group 1, which a lost manifest
   would fail. The change makes the bound paths tested, not easier.
6. Multi-loop ordering (new in round 3): the verdict is now stored per loop just before that
   loop's open record, not for all loops before any. Loops own disjoint slots, so loop k's
   `settle_damage` commits only loop k's entries; `mark_lost`'s artifact sweep touches other
   loops' lost or deleted entries' marks, which `known` does not read. `mark_bounds`'s clearing
   pass is gated on `drivers_.size() == loops_` (host.h:949); during every settle the driver
   under construction is not yet pushed (host.h:882), so the pass runs only after the open's
   drivers, as before; repartition calls `mark_bounds` with no drivers. `settle` fires once per
   driver: `TurnDriver` passes `known` to its `TurnLog` and keeps no copy (turn_driver.h:126),
   `TurnLog::read` and repartition's targets get no `settle`.

States by events (one log; each loop repeats it after the earlier loops' open records):

| Crash after | Table | Log's newest epoch | Next open |
|---|---|---|---|
| `recover` (reads only) | as before | damaged one | recomputes the same verdict |
| `mark_bounds` commit: `.pending` written or synced, renames partial | each shard old or new (`resolve`) | damaged one | recomputes; a stored bound equals the recomputed one, damage names the group only above it |
| `mark_bounds` done, before `mark_lost` commit (R3) | bounds stored, losses not | damaged one | recomputes; losses stored then |
| `mark_lost` commit or artifact removal partial | lost marked or not; artifacts some removed | damaged one | recomputes; removal is idempotent |
| settle done, open record partial or unsynced | verdict stored | damaged one or new (open record durable or not) | either recomputes, or reads the stored bound and loss from the table |
| open record durable, before `keep` and the post-driver pass | verdict stored | new epoch | the damaged epoch is earlier and a lost write no longer shows; the table's bound supersedes through it and its records after the bound are kept; lost ids stay refused |
| later loops' settle or open record | this loop done | per loop | each loop by its own row |
| post-driver pass (clears bounds a later checkpoint supersedes) | bound kept or cleared | new epoch | a stale bound below a checkpoint is harmless; the next open clears it |

Every row is a mutation the cut sweep enumerates (17 to 29 per open); the nested sweep covers a
crash inside the recovery open of each row; EIO covers a failed step whose process survives.

### Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| R1 | no | gap | The suite's `first_open` runs one loop, so the per-loop interleaving round 3 introduced (loop k's verdict after loop k-1's open record) has no test in the suite; only this probe covers it. | probe `cuts`/`nested` at 2/1, 3/2 (0 failures; main fails 12 of 23, 11 of 29) | Optional: run "gap, later turn, zeroed" in loop 1 of a two-loop host in `first_open`; `retarget.py` shows the id mapping. |
| R2 | no | nit | `mark_lost` now runs once per loop inside `settle_damage` plus once after the drivers, and each run sweeps every slot's entries for artifacts: L+1 in-memory sweeps of the table per open. Device calls are main's exactly. | host.h:934, 962-976; `calls.txt` | Optional: sweep artifacts only in the post-driver call. |
| R3 | no | nit | Round 1's F3 stands: bounds and losses are two commits; no probe found a disagreement. | host.h:930-935; probe `cuts`, `nested` | Optional: one commit. |

### Not run

- 261124-113923-f05db6 (the exclusive open-time A/B) was still queued at 11:53; I read device
  calls instead (fix = main). The lead quotes the wall-clock ratio from that job.
- The suite and CI at a2d65da (`campaign status` showed CI running).
- Damage in two loops at once, a parked or deleted group in the damaged log, a re-partition
  (L changed) over rule-5 damage, and ENOSPC; EIO was injected on the first open only.
- No strict or exclusive runs, by the lead's instruction.
- This reviewer ran as a subagent, not a session of its own, so it has no `detent · reviewer`
  title.

## Round 1 at journal:0951710 (superseded)

Round 1's verdict was APPROVE at journal:0951710. Its F1 (two reads) is fixed by `settle` (calls
equal main's), F2 by `first_open` asserting both groups' verdicts per layout, F4 by `spans()`
calling `gap()`, which value-checks its positions. F3 stands as a nit (R3).

### Evidence I relied on (round 1)

- 261123-110049-af725e (A2, `--fails-without`, test at main's host.h, journal:900a11a): exit 1,
  66 of 138 checks fail, every one for the N1 reason: a cut reopens group 1 or 2 as stored with a
  lower next sequence (`group 1 1 bound 0 next 3`) where the uncut open loses or bounds it. The
  "dropped turns" layout agrees at main, as the journal says (a guard, not the fix's test).
- 261123-111045-7f9830 (open cost A/B, strict, 192 MiB log, 7 runs per round, two rounds): main
  1003 / 915 ms median, fix 1740 / 2698 ms. It ran beside 13 light holds, so the ratio
  (1.7x to 2.9x) is noisy; the extra cost is real (see F1).

### What I checked myself (round 1)

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

### Findings (round 1)

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | no | gap | Every open, damaged or not, reads each log twice: open time on a 192 MiB log goes from about 1.0 s to 1.7 to 2.7 s, for a case only rule-5 damage needs. A hook between `TurnLog::recover` and the open record (turn_log.h, turn_driver.h, outside this quest's files) would store the verdict with one read. | 7f9830; probe c66b0a (calls 287 to 545, same mutations) | Lead's call: land as is and open a follow-up quest for the one-read hook, or widen this quest's files. Rerun the A/B under `exclusive` before quoting the ratio. |
| F2 | no | nit | The `dropped()` comment says rule 5 bounds groups 1 and 2 at 2; the open keeps group 2 stored with next sequence 3 (bound 0). Only group 1's expected status is asserted, and the reference is the fixed open's own verdict, so group 2's values have no oracle. The comparison also leaves out record payloads. | probe c66b0a "nested dropped turns" line; lost_bound_test.cc `first_open` | Correct the comment and assert both groups' expected verdict per layout. |
| F3 | no | nit | `settle_damage` stores bounds and losses in two commits; a cut between them leaves a partial table over a still-newest damaged epoch. No probe found a disagreement (both groups' verdicts are per-group), and main's post-driver pass has the same order. | host.h:931-940; probe c66b0a | Optional: one commit of both. |
| F4 | no | nit | `gap()` copies `spans()`'s RFTW0001 offsets; after journal-turn-sector-checks merges, the layout may no longer fit or may lay the damage elsewhere. The test fails loudly then ("a filler lays the gap", the uncut status check), and the journal's Next step 2 names the re-sync. | lost_bound_test.cc `gap()` | Rerun `lost_bound_test first_open` after the merge (the suite gate does). |

### Not run (round 1)

- The suite and CI at journal:0951710 (A1's evidence is the suite job; CI was running).
- No layout with a parked or deleted group, or with more than one loop; the undamaged comparison
  used only `populate`'s six groups.
- No I/O-error (EIO, ENOSPC) injection inside `settle_damage`; only power cuts.
- The open cost under `exclusive`, or on a log at retention's limit.
- This reviewer ran as a subagent, not a session of its own, so it has no `detent · reviewer`
  title.
