# consensus-recovered-over-limit-start: journal

State: ready for review (jobs queued at consensus:d03a71b; pushed)
Quest: .campaign/detent/quests/consensus-recovered-over-limit-start.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Final head consensus:d03a71b; A2 pair, fails-without pair and suite queued; branch pushed for CI.

## Next

1. Read jobs 261124-201345-5a60e2, 261124-201346-931ecd, 261124-201348-90beab, 261124-201349-16891d,
   261124-201351-4ac6e5 and CI; correctness review.

## Table (A1): how recovery leaves a store above its control headroom

Journal Admission (journal:9e11713 admission.h) refills the control pool first, then the replacement
pool, from `free = hard - used - outstanding - pools`. A recovered store's census sets `used`; with
`used > hard - control_pool`, control gets `hard - used` (short of the pool) and replacement gets
nothing. The Driver's pre-Start Tick reserves the campaign pack (3 Control appends) and is refused
when the short pool cannot fund it. Before Start the Driver admits nothing, so `foreground_` stays
0, maintenance (reclaim) never falls due, and the pool never changes: a refusal is permanent.
`used` counts the group's files: the owned store's whole directory; a shared (Host) group's
artifacts only (its batches go to the turn log, reserved but never charged).

| Way recovery lands above `hard - control_pool` | Mode | What frees it | Needs a started group? |
|---|---|---|---|
| Lowered `hard` or raised `control_pool` between runs (GroupOptions are per open) | both | only what reclaim finds; live data needs compaction | reclaim no; compaction yes, and it cannot be funded (below) |
| Control appends drained the pool at the limit before the crash (a Control append settles its bytes into `used`; refill finds `free` 0) | owned only (shared appends charge nothing) | compaction: a checkpoint, then reclaim of the old generation | yes, and unfundable: a checkpoint needs a Replacement permit, and replacement refills only after a whole control pool |
| Crash after a checkpoint's publication, before reclaim of the superseded generation and artifacts | both (shared: only with a lowered limit, since the publication's peak was funded under a whole control pool) | reclaim | no |
| Kill or crash mid-checkpoint or mid-install: staged artifacts and pending manifests (funded by an outstanding Replacement permit, counted as `used` by the census) | both | reclaim (v2 artifact ownership header, written at create, is enough to match) | no |
| Replay growth | both | n/a: replay only reads; the census runs after recovery; a torn tail's truncation only shrinks; the one write recovery queues is the bootstrap's initialization record, a Control append before Start (same rule) | n/a |

| What frees space | Needs a started group? | Today, before Start |
|---|---|---|
| Reclaim (`Store::reclaim_step` / `begin_reclaim`): removes every file unreachable from the frontier's roots, credits `used`, refills | no: Store only | never runs (maintenance is due after `foreground_burst` admitted events; none before Start) |
| Compaction (checkpoint publication, then reclaim) | yes (the Driver admits checkpoint calls once started) and a Replacement permit | unfundable while the control pool is short |
| Snapshot install from a leader | yes, and a Replacement permit | the same |
| Retirement (Core's `retire_log`) | frees Core's charged log, not storage | n/a |
| Close / host retirement | frees nothing in place | n/a |

Rule: recovery frees what it can, then fails loudly. Before Start, a Worker turn refused with Pressure
runs a reclaim step instead, until one whole reclaim pass has completed (roots cannot change before
Start, so any completed pass examined every file). A Pressure turn after that is final: the Driver
fences the group with `over_headroom`, so the open fails with `fenced` naming it. A started group could
not do better: with the control pool short, replacement is empty (no checkpoint, no install), Normal
is refused (`normal_ready` needs whole pools), and Control appends only add.
Every store that opens today still opens: one whose pre-Start turn is funded never reaches the rule.
The ones it changes never started today (the open hung): rows 3-4 now start, rows 1-2 fail by name.
At the Worker level nothing new refuses: a Worker host's pre-Start turn reclaims instead of Pressure.

Adjacent, for the lead (journal's refill order, outside this file set): recovery refills control
before replacement, so a store recovered with `hard - control_pool - replacement_pool < used <=
hard - control_pool` starts with a whole control pool and a short replacement pool: it cannot fund
a checkpoint until reclaim credits space, though before the crash its replacement pool was whole.

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | this journal's table |
| A2 | reclaims 261124-201345-5a60e2; refuses 261124-201348-90beab; fails-without at consensus:0cf87ca (main's worker.h and driver.h, `over_headroom`'s name only): reclaims 261124-201346-931ecd, refuses 261124-201349-16891d. Local Release at d03a71b: reclaims 17 checks, refuses 12, 0 failures; at the mutant both fail "the open completes: never, within 200 ms". |
| A3 | [artifact] docs/mechanisms/deadlines.md, "A recovered store over its control headroom" |
| A4 | suite 261124-201351-4ac6e5; CI on the push |

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
