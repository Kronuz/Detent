# consensus-recovered-over-limit-start: journal

State: fixing F1, F3
Quest: .campaign/detent/quests/consensus-recovered-over-limit-start.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Fix round 1 (review .campaign/detent/reviews/consensus-recovered-over-limit-start-correctness.md):
F1 table and deadlines.md rewritten by mode; F3 `owned` case (sync reclaim branch); F2 is
journal-recovery-refill, pointed at from row 2.

## Next

1. Build and run `owned` locally under quiet light; then the mutant commit, jobs, push.

## Table (A1): how recovery leaves a store above its control headroom

Journal Admission (journal:9e11713 admission.h) refills the control pool first, then the replacement
pool, from `free = hard - used - outstanding - pools`. A recovered store's census sets `used`; with
`used > hard - control_pool`, control gets `hard - used` (short of the pool) and replacement gets
nothing. Before Start nothing is admitted, so `foreground_` stays 0, maintenance (reclaim) never
falls due, and the pool never changes on its own.

Two modes, checked against the code. A Driver group is always a Host group (driver.h:202
`open` builds the Host `Worker`; worker.h:117-124 forces async storage). A Host group's appends
reserve `payload + 1` and add nothing (journal:9e11713 journal.h:136 `append_plan` when `shared()`;
settled at store.h:78), so `used` counts its artifacts only and its control pool is whole whenever
no permit is held. An owned store's Control appends settle their bytes into `used` (its whole
directory); no Driver hosts an owned store.

| Way recovery lands above `hard - control_pool` | Driver (Host) group | Owned store (Worker host) | What frees it | Needs a started group? |
|---|---|---|---|---|
| 1. Lowered `hard` or raised `control_pool` between runs (limits are per open) | the only way in: reclaim pass, then the open completes if the pack fits (`reclaims`) or fails with `fenced(over_headroom)` (`refuses`) | reaches it: one reclaim pass, then Pressure, never fenced (`owned`); Start is still admitted | reclaim, of crash leftovers only (rows 3-4); live data needs compaction | reclaim no; compaction yes, and unfundable (replacement empty) |
| 2. Control appends drained the pool at the limit before the crash | never: Host appends add nothing, so its control pool is whole with no permit held | reaches it: one pass, then Pressure, never fenced. Detent's authority turns once (`service`, service_runtime.cc:871) and submits Start (node.cc:208), which plans no appends, so it starts | compaction: a checkpoint, then reclaim of the old generation | yes; and recovery's refill order can leave the replacement pool too short for any checkpoint even when control is whole: journal-recovery-refill (review F2) |
| 3. Crash after a checkpoint's publication, before reclaim of the superseded generation and artifacts | only on top of row 1: the publication's peak was funded under a whole control pool | only on top of rows 1-2, for the same reason | reclaim, before Start | no |
| 4. Kill or crash mid-checkpoint or mid-install: staged artifacts, pending manifests (funded by an outstanding Replacement permit; the census counts them as `used`) | only on top of row 1 (`reclaims` is row 1 plus row 4) | only on top of rows 1-2 | reclaim (the v2 artifact ownership header, written at create, is enough to match) | no |
| 5. Replay growth | none: replay only reads; the census runs after recovery; a torn tail's truncation only shrinks | the same; the one write recovery queues is the bootstrap's initialization record, a Control append before Start | n/a | n/a |

| What frees space | Needs a started group? | Before Start |
|---|---|---|
| Reclaim (`Store::reclaim_step` / `begin_reclaim`): removes every file unreachable from the frontier's roots, credits `used`, refills | no: Store only | runs at a refused turn, until one pass completes (this quest) |
| Compaction (checkpoint publication, then reclaim) | yes (checkpoint calls are admitted once started) and a Replacement permit | unfundable while the control pool is short |
| Snapshot install from a leader | yes, and a Replacement permit | the same |
| Retirement (Core's `retire_log`) | frees Core's charged log, not storage | n/a |
| Close / host retirement | frees nothing in place | n/a |

Rule: recovery frees what it can, then the Driver fails the open by name. Before Start, a Worker
turn refused with Pressure runs a reclaim step instead (`IOPending` async, `Maintenance` sync),
until one whole reclaim pass has completed (roots cannot change before Start, so any completed pass
examined every file). For a Driver group a Pressure turn after that is final: the Driver fences the
group with `over_headroom`, so the open fails with `fenced` naming it. A started group could not do
better: with the control pool short, replacement is empty (no checkpoint, no install), Normal is
refused (`normal_ready` needs whole pools), and Control appends only add. An owned Worker's host
sees Pressure after the pass, never a fence, and decides.
Every store that opens at main still opens: one whose pre-Start turn is funded never reaches the
rule. A Driver group reaches it only through row 1: it starts when reclaiming rows 3-4's leftovers
funds the pack (`reclaims`), and fails by name otherwise (`refuses`); at main both opens hung.

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | this journal's table |
| A2 | reclaims 261124-201345-5a60e2; refuses 261124-201348-90beab; fails-without at consensus:0cf87ca (main's worker.h and driver.h, `over_headroom`'s name only): reclaims 261124-201346-931ecd, refuses 261124-201349-16891d. Local Release at d03a71b: reclaims 17 checks, refuses 12, 0 failures; at the mutant both fail "the open completes: never, within 200 ms". |
| A3 | [artifact] docs/mechanisms/deadlines.md, "A recovered store over its control headroom" |
| A4 | suite 261124-201351-4ac6e5; CI on the push |

Cancelled: `campaign verify` suite 261124-201423-0b9b78 (30 min), the same command as 261124-201351-4ac6e5 at the 45 minutes the lead asked for; the gate's check suite may need `campaign verify` to adopt the 45-minute one.

## Result

Before Start, `Worker::run_one` turns a Pressure turn into a reclaim step (`IOPending` async,
`Maintenance` sync) until a reclaim pass completes (`reclaimed_`, also set by maintenance's passes);
`started_` is set when a Start is admitted. `Driver::step` fences a group whose turn before Start is
Pressure with `over_headroom` ("storage above its control headroom at start"), so the open fails with
`fenced` and the Sink hears `Fenced` with that reason. Measured (test/recovered_over_limit_test.cc,
lone voter, 512 KiB published checkpoint, 114-byte pre-Start pack): `reclaims`, a killed checkpoint's
640 KiB staged image over `hard` = published + 324 KiB: reclaimed, the open completes, the group leads
and commits; `refuses`, `hard` = published + pack - 1: the open fails by name within 200 ms and the
store keeps its two artifacts. At main both opens never complete. Local neighbours: driver_pressure 25
checks, worker 96196 checks, 0 failures.

Files: include/consensus/worker.h, include/consensus/driver.h, test/recovered_over_limit_test.cc
(ctest consensus_recovered_over_limit_{reclaims,refuses}), CMakeLists.txt, docs/mechanisms/deadlines.md.

Left open for the lead: the refill-order note under the table (journal's admission, not this file set).

## Rulings I made

1. Recovery reclaims, then fails by name, rather than planning the pre-Start Tick as nothing: a group
   started with a short pool cannot campaign, take commands, compact or install, so starting it
   only moves the stall past the open.
2. The fence decision is the Driver's (its open hung forever at main); the Worker only reclaims, so
   no Worker host that starts a group today is refused.
3. The named status is `fenced` with the public reason `over_headroom` (driver.h), not a new
   exception type: one mechanism for a group that cannot continue, and every `catch (fenced&)` keeps
   working. A distinct type deriving from `fenced` is a small follow-up if the owner wants one.
4. One completed reclaim pass suffices: no root changes before Start, so any pass that completes
   after recovery examined every file against the same roots.
5. The fails-without commit consensus:0cf87ca (local branch scratch/over-limit-mutant, never pushed)
   keeps `over_headroom`'s declaration so the test compiles against main's behavior.
